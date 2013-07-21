#include "Python.h"
#ifdef STACKLESS

#include "compile.h"

#include "core/stackless_impl.h"
#include "pickling/prickelpit.h"
#include "module/channelobject.h"

/******************************************************

  type template and support for pickle helper types

 ******************************************************/

/* check that we really have the right wrapper type */

static int is_wrong_type(PyTypeObject *type)
{
    /* this works because the tp_base's name was modified to
     * point into the wrapper's name
     */
    if (type->tp_base == NULL ||
        strrchr(type->tp_name, '.')+1 != type->tp_base->tp_name) {
        PyErr_SetString(PyExc_TypeError, "incorrect wrapper type");
        return -1;
    }
    return 0;
}

/* supporting __setstate__ for the wrapper type */

static PyObject *
generic_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *inst;
    /* we don't want to support derived types here. */
    if (is_wrong_type(type))
        return NULL;
    assert(type->tp_base->tp_new != NULL);
    inst = type->tp_base->tp_new(type->tp_base, args, kwds);
    if (inst != NULL)
        inst->ob_type = type;
    return inst;
}

int
generic_init(PyObject *ob, PyObject *args, PyObject *kwds)
{

    initproc init = ob->ob_type->tp_base->tp_init;

    if (init)
        return init(ob, args, kwds);
    return 0;
}

static PyObject *
generic_setstate(PyObject *self, PyObject *args)
{
    if (is_wrong_type(self->ob_type)) return NULL;
    self->ob_type = self->ob_type->tp_base;
    Py_INCREF(self);
    return self;
}

/* redirecting cls.__new__ */

static PyObject *
_new_wrapper(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *type;
    PyObject *newfunc, *res = NULL;

    if (self == NULL || !PyType_Check(self))
        Py_FatalError("__new__() called with non-type 'self'");
    type = (PyTypeObject *)self;
    if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
        PyErr_Format(PyExc_TypeError,
                     "%s.__new__(): not enough arguments",
                     type->tp_name);
        return NULL;
    }
    if (is_wrong_type(type)) return NULL;

    newfunc = PyObject_GetAttrString((PyObject *) type->tp_base, "__new__");
    if (newfunc != NULL) {
        res = PyObject_Call(newfunc, args, kwds);
        Py_DECREF(newfunc);
    }
    return res;
}

/* just in case that setstate gets not called, we need to protect* */

static void
_wrap_dealloc(PyObject *ob)
{
    ob->ob_type = ob->ob_type->tp_base;
    if (ob->ob_type->tp_dealloc != NULL)
        ob->ob_type->tp_dealloc(ob);
}

static int
_wrap_traverse(PyObject *ob, visitproc visit, void *arg)
{
    PyTypeObject *type = ob->ob_type;
    int ret = 0;
    ob->ob_type = ob->ob_type->tp_base;
    if (ob->ob_type->tp_traverse != NULL)
        ret = ob->ob_type->tp_traverse(ob, visit, arg);
    ob->ob_type = type;
    return ret;
}

static void
_wrap_clear(PyObject *ob)
{
    ob->ob_type = ob->ob_type->tp_base;
    if (ob->ob_type->tp_clear != NULL)
        ob->ob_type->tp_clear(ob);
}


#define MAKE_WRAPPERTYPE(type, prefix, name, reduce, newfunc, setstate) \
 \
static PyMethodDef prefix##_methods[] = { \
    {"__reduce__",     (PyCFunction)reduce,             METH_NOARGS,    NULL}, \
    {"__setstate__",   (PyCFunction)setstate,           METH_O,         NULL}, \
    {"__new__",            (PyCFunction)_new_wrapper,   METH_KEYWORDS, \
     PyDoc_STR("wwwwwaaaaaT.__new__(S, ...) -> " \
               "a new object with type S, a subtype of T")}, \
    {NULL, NULL} \
}; \
 \
static struct _typeobject wrap_##type = { \
    PyObject_HEAD_INIT(&PyType_Type) \
    0, \
    "stackless._wrap." name, \
    0, \
    0, \
    (destructor)_wrap_dealloc,              /* tp_dealloc */ \
    0,                                          /* tp_print */ \
    0,                                          /* tp_getattr */ \
    0,                                          /* tp_setattr */ \
    0,                                          /* tp_compare */ \
    0,                                          /* tp_repr */ \
    0,                                          /* tp_as_number */ \
    0,                                          /* tp_as_sequence */ \
    0,                                          /* tp_as_mapping */ \
    0,                                          /* tp_hash */ \
    0,                                          /* tp_call */ \
    0,                                          /* tp_str */ \
    PyObject_GenericGetAttr,                    /* tp_getattro */ \
    PyObject_GenericSetAttr,                    /* tp_setattro */ \
    0,                                          /* tp_as_buffer */ \
    0,                                          /* tp_flags */ \
    0,                                          /* tp_doc */ \
    (traverseproc) _wrap_traverse,              /* tp_traverse */ \
    (inquiry) _wrap_clear,                  /* tp_clear */ \
    0,                                          /* tp_richcompare */ \
    0,                                          /* tp_weaklistoffset */ \
    0,                                          /* tp_iter */ \
    0,                                          /* tp_iternext */ \
    prefix##_methods,                           /* tp_methods */ \
    0,                                          /* tp_members */ \
    0,                                          /* tp_getset */ \
    &type,                                      /* tp_base */ \
    0,                                          /* tp_dict */ \
    0,                                          /* tp_descr_get */ \
    0,                                          /* tp_descr_set */ \
    0,                                          /* tp_dictoffset */ \
    generic_init,                               /* tp_init */ \
    0,                                          /* tp_alloc */ \
    newfunc,                                    /* tp_new */ \
    0,                                          /* tp_free */ \
};

static PyObject *types_mod = NULL;
static PyObject *pickle_reg = NULL;

static struct PyMethodDef _new_methoddef[] = {
    {"__new__", (PyCFunction)_new_wrapper, METH_KEYWORDS,
     PyDoc_STR("T.__new__(S, ...) -> "
               "a new object with type S, a subtype of T.__base__")},
    {0}
};

static int init_type(PyTypeObject *t, int register_reduce, int (*initchain)(void))
{
    PyMethodDescrObject *reduce;
    PyWrapperDescrObject *init;
    PyObject *args, *retval = NULL, *func;
    int ret = 0;
    char *name = strrchr(t->tp_name, '.')+1;

    /* we patch the type to use *our* name, which makes no difference */
    assert (strcmp(name, t->tp_base->tp_name) == 0);
    t->tp_base->tp_name = name;
    t->tp_basicsize = t->tp_base->tp_basicsize;
    t->tp_itemsize  = t->tp_base->tp_itemsize;
    t->tp_flags     = t->tp_base->tp_flags & ~Py_TPFLAGS_READY;
    if (PyObject_SetAttrString(types_mod, name, (PyObject *) t))
        return -1;
    /* patch the method descriptors to require the base type */
    if (PyType_Ready(t)) return -1;
    init = (PyWrapperDescrObject *) PyDict_GetItemString(t->tp_dict,
                                                        "__init__");
    init->d_type = t->tp_base;
    reduce = (PyMethodDescrObject *) PyDict_GetItemString(t->tp_dict,
                                                        "__reduce__");
    reduce->d_type = t->tp_base;
    /* insert the __new__ replacement which is special */
    func = PyCFunction_New(_new_methoddef, (PyObject *)t);
    if (func == NULL || PyDict_SetItemString(t->tp_dict, "__new__", func))
        return -1;
    if (register_reduce) {
	    /* register with copy_reg */
        args = Py_BuildValue("(OO)", t->tp_base, reduce);
        if (pickle_reg != NULL &&
            (retval = PyObject_Call(pickle_reg, args, NULL)) == NULL)
            ret = -1;
        Py_XDECREF(retval);
        Py_XDECREF(args);
    }
    if (ret == 0 && initchain != NULL)
        ret = initchain();
    return ret;
}

/* root of init function chain */

#define initchain NULL

/* helper to execute a bit of code which simplifies things */

static PyObject *
run_script(char *src, char *retname)
{
    PyObject *globals = PyDict_New();
    PyObject *retval;

    if (globals == NULL)
        return NULL;
    if (PyDict_SetItemString(globals, "__builtins__",
                             PyEval_GetBuiltins()) != 0)
        return NULL;
    retval = PyRun_String(src, Py_file_input, globals, globals);
    if (retval != NULL) {
        Py_DECREF(retval);
        retval = PyMapping_GetItemString(globals, retname);
    }
    PyDict_Clear(globals);
    Py_DECREF(globals);
    return retval;
}

/******************************************************

  default execute function for invalid frames

 ******************************************************/

/*
 * note that every new execute function should also create
 * a different call of this function.
 */

PyObject *
slp_cannot_execute(PyFrameObject *f, char *exec_name, PyObject *retval)
{
    /*
     * show an error message and raise exception.
     */
    PyObject *message;
    PyThreadState *tstate = PyThreadState_GET();

    /* if we already have an exception, we keep it */
    if (retval == NULL)
        goto err_exit;

    message = PyString_FromFormat("cannot execute invalid frame with "
                                  "'%.100s': frame had a C state that"
                                  " can't be restored.",
                                  exec_name);
    if (message == NULL) {
        /* try at least something */
        PyErr_SetString(PyExc_RuntimeError,
                        "invalid frame, cannot build error message.");
        goto err_exit;
    }
    PyErr_SetObject(PyExc_RuntimeError, message);
    Py_DECREF(message);
err_exit:
    tstate->frame = f->f_back;
    Py_DECREF(f);
    --tstate->recursion_depth;
    Py_XDECREF(retval);
    return NULL;
}

/* registering and retrieval of frame exec functions */

/* unfortunately, this object is not public,
 * so we need to repeat it here:
 */

typedef struct {
    PyObject_HEAD
    PyObject *dict;
} proxyobject;

int
slp_register_execute(PyTypeObject *t, char *name, PyFrame_ExecFunc *good,
                     PyFrame_ExecFunc *bad)
{
    PyObject *g = NULL, *b = NULL, *nameobj = NULL;
    PyObject *tup = NULL, *dic = NULL;
    proxyobject *dp = NULL;
    int ret = -1;

/*
    WE CANNOT BE DOING THIS HERE, AS THE EXCEPTION CLASSES ARE NOT INITIALISED.
    assert(PyObject_IsSubclass((PyObject *)t, (PyObject *)&PyFrame_Type) ||
           PyObject_IsSubclass((PyObject *)t,
                               (PyObject *)&PyCFrame_Type));
*/
    if (0
        || PyType_Ready(t) || name == NULL
        || (nameobj = PyString_FromString(name)) == NULL
        || (g = PyLong_FromVoidPtr(good)) == NULL
        || (b = PyLong_FromVoidPtr(bad)) == NULL
        || (tup = Py_BuildValue("OO", g, b)) == NULL
        )
        goto err_exit;
    dp = (proxyobject*) PyDict_GetItemString(t->tp_dict, "_exec_map");
    if ((dic = dp ? dp->dict : NULL) == NULL) {
        if (0
            || (dic = PyDict_New()) == NULL
            || (dp = (proxyobject *) PyDictProxy_New(dic)) == NULL
            || PyDict_SetItemString(t->tp_dict, "_exec_map",
                                    (PyObject *) dp)
            )
            goto err_exit;
    }
    else {
        Py_INCREF(dic);
        Py_INCREF(dp);
    }
    if (0
        || PyDict_GetItem(dp->dict, nameobj) != NULL
        || PyDict_GetItem(dp->dict, g) != NULL
        || PyDict_GetItem(dp->dict, b) != NULL
        ) {
        PyErr_SetString(PyExc_SystemError,
                        "duplicate/ambiguous exec func");
        goto err_exit;
    }
    if (0
        || PyDict_SetItem(dp->dict, nameobj, tup)
        || PyDict_SetItem(dp->dict, g, nameobj)
        || PyDict_SetItem(dp->dict, b, nameobj)
        )
        goto err_exit;
    PyErr_Clear();
    ret = 0;
err_exit:
    Py_XDECREF(nameobj);
    Py_XDECREF(g);
    Py_XDECREF(b);
    Py_XDECREF(tup);
    Py_XDECREF(dic);
    Py_XDECREF(dp);
    return ret;
}

int
slp_find_execfuncs(PyTypeObject *type, PyObject *exec_name,
                   PyFrame_ExecFunc **good, PyFrame_ExecFunc **bad)
{
    PyObject *g, *b;
    proxyobject *dp = (proxyobject *)
                      PyDict_GetItemString(type->tp_dict, "_exec_map");
    PyObject *dic = dp ? dp->dict : NULL;
    PyObject *exec_tup = dic ? PyDict_GetItem(dic, exec_name) : NULL;

    if (0
        || exec_tup == NULL
        || !PyArg_ParseTuple(exec_tup, "OO", &g, &b)
        || (*good = (PyFrame_ExecFunc*)PyLong_AsVoidPtr(g)) == NULL
        || (*bad =  (PyFrame_ExecFunc*)PyLong_AsVoidPtr(b)) == NULL) {
        char msg[500];

        PyErr_Clear();
        sprintf(msg, "Frame exec function '%.20s' not defined for %s",
            PyString_AS_STRING(exec_name), type->tp_name);
        PyErr_SetString(PyExc_ValueError, msg);
        return -1;
    }
    return 0;
}

PyObject *
slp_find_execname(PyFrameObject *f, int *valid)
{
    PyObject *exec_name = NULL;
    proxyobject *dp = (proxyobject *)
                      PyDict_GetItemString(f->ob_type->tp_dict, "_exec_map");
    PyObject *dic = dp ? dp->dict : NULL;
    PyObject *exec_addr = PyLong_FromVoidPtr(f->f_execute);

    if (exec_addr == NULL) return NULL;
    exec_name = dic ? PyDict_GetItem(dic, exec_addr) : NULL;
    if (exec_name == NULL) {
        char msg[500];
        PyErr_Clear();
        sprintf(msg, "frame exec function at %lx is not registered!",
            (unsigned long)(void *)f->f_execute);
        PyErr_SetString(PyExc_ValueError, msg);
        valid = 0;
    }
    else {
        PyFrame_ExecFunc *good, *bad;
        if (slp_find_execfuncs(f->ob_type, exec_name, &good, &bad)) {
            exec_name = NULL;
            goto err_exit;
        }
        if (f->f_execute == bad)
            valid = 0;
        else if (f->f_execute != good) {
            PyErr_SetString(PyExc_SystemError,
                "inconsistent c?frame function registration");
            goto err_exit;
        }
    }
err_exit:
    Py_XDECREF(exec_addr);
    Py_XINCREF(exec_name);
    return exec_name;
}

/******************************************************

  pickling of objects that may contain NULLs

 ******************************************************/

/*
 * To restore arrays which can contain NULLs, we add an extra
 * tuple at the beginning, which contains the positions of
 * all objects which are meant to be a real NULL.
 */

PyObject *
slp_into_tuple_with_nulls(PyObject **start, Py_ssize_t length)
{
    PyObject *res = PyTuple_New(length+1);
    PyObject *nulls = PyTuple_New(0);
    Py_ssize_t i, nullcount = 0;
    if (res == NULL)
        return NULL;
    for (i=0; i<length; ++i) {
        PyObject *ob = start[i];
        if (ob == NULL) {
            /* store None, and add the position to nulls */
            PyObject *pos = PyInt_FromSsize_t(i);
            if (pos == NULL)
                return NULL;
            ob = Py_None;
            if (_PyTuple_Resize(&nulls, ++nullcount))
                return NULL;
            PyTuple_SET_ITEM(nulls, nullcount-1, pos);
        }
        Py_INCREF(ob);
        PyTuple_SET_ITEM(res, i+1, ob);
    }
    /* save NULL positions as first element */
    PyTuple_SET_ITEM(res, 0, nulls);
    return res;
}

Py_ssize_t
slp_from_tuple_with_nulls(PyObject **start, PyObject *tup)
{
    Py_ssize_t i, length = PyTuple_GET_SIZE(tup)-1;
    PyObject *nulls;
    if (length < 0) return 0;

    /* put the values into the array */
    for (i=0; i<length; ++i) {
        PyObject *ob = PyTuple_GET_ITEM(tup, i+1);
        Py_INCREF(ob);
        start[i] = ob;
    }
    nulls = PyTuple_GET_ITEM(tup, 0);
    if (!PyTuple_Check(nulls)) {
        /* XXX we should report this error */
        return length;
    }
    /* wipe the NULL positions */
    for (i=0; i<PyTuple_GET_SIZE(nulls); ++i) {
        PyObject *pos = PyTuple_GET_ITEM(nulls, i);
        if (PyInt_CheckExact(pos)) {
            int p = PyInt_AS_LONG(pos);
            if (p >= 0 && p < length) {
                PyObject *hold = start[p];
                start[p] = NULL;
                Py_XDECREF(hold);
            }
        }
    }
    return length;
}

/******************************************************

  pickling of code objects

 ******************************************************/

#define codetuplefmt "iiiiSOOOSSiSOO"

static struct _typeobject wrap_PyCode_Type;

static PyObject *
code_reduce(PyCodeObject * co)
{
    PyObject *tup = Py_BuildValue(
        "(O(" codetuplefmt ")())",
        &wrap_PyCode_Type,
        co->co_argcount,
        co->co_nlocals,
        co->co_stacksize,
        co->co_flags,
        co->co_code,
        co->co_consts,
        co->co_names,
        co->co_varnames,
        co->co_filename,
        co->co_name,
        co->co_firstlineno,
        co->co_lnotab,
        co->co_freevars,
        co->co_cellvars
    );

    return tup;
}

MAKE_WRAPPERTYPE(PyCode_Type, code, "code", code_reduce, generic_new,
                 generic_setstate)

static int init_codetype(void)
{
    return init_type(&wrap_PyCode_Type, 1, initchain);
}
#undef initchain
#define initchain init_codetype


/******************************************************

  pickling addition to cell objects

 ******************************************************/

/*
 * cells create cycles via function closures.
 * We therefore need to use the 3-element protocol
 * of __reduce__
 * We must also export this type to funcobject where
 * a typecheck of the function_closure member is done,
 * since a function may get a __setstate__ call with
 * a partially initialized cell object.
 */

static PyTypeObject wrap_PyCell_Type;
PyTypeObject *_Pywrap_PyCell_Type = &wrap_PyCell_Type;

static PyObject *
cell_reduce(PyCellObject *cell)
{
    PyObject *tup = NULL;

    if (cell->ob_ref == NULL) {
        tup = Py_BuildValue("(O()())", &wrap_PyCell_Type);
    }
    else {
        tup = Py_BuildValue("(O()(O))", &wrap_PyCell_Type, cell->ob_ref);
    }
    return tup;
}

static PyObject *
cell_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *ob;

    if (is_wrong_type(type)) return NULL;
    if (!PyArg_ParseTuple (args, "", &ob))
        return NULL;
    ob = PyCell_New(NULL);
    if (ob != NULL)
        ob->ob_type = type;
    return ob;
}

/* note that args is a tuple, although we use METH_O */

static PyObject *
cell_setstate(PyObject *self, PyObject *args)
{
    PyCellObject *cell = (PyCellObject *) self;
    PyObject *ob = NULL;

    if (is_wrong_type(self->ob_type)) return NULL;
    if (!PyArg_ParseTuple (args, "|O", &ob))
        return NULL;
    Py_XDECREF(cell->ob_ref);
    cell->ob_ref = ob;
    Py_XINCREF(cell->ob_ref);
    Py_INCREF(self);
    self->ob_type = self->ob_type->tp_base;
    return self;
}

MAKE_WRAPPERTYPE(PyCell_Type, cell, "cell", cell_reduce, cell_new, cell_setstate)

static int init_celltype(void)
{
    return init_type(&wrap_PyCell_Type, 1, initchain);
}
#undef initchain
#define initchain init_celltype


/******************************************************

  pickling addition to function objects

 ******************************************************/

#define functuplefmt "OOOOOO"

static PyTypeObject wrap_PyFunction_Type;

static PyObject *
func_reduce(PyFunctionObject * func)
{
    PyObject *tup = Py_BuildValue(
        "(O()(" functuplefmt "))",
        &wrap_PyFunction_Type,
        /* Standard function constructor arguments. */
        func->func_code != NULL ? func->func_code : Py_None,
        func->func_globals != NULL ? func->func_globals : Py_None,
        func->func_name != NULL ? func->func_name : Py_None,
        func->func_defaults != NULL ? func->func_defaults : Py_None,
        func->func_closure != NULL ? func->func_closure : Py_None,
        /* Additional data we need to preserve. */
        func->func_module != NULL ? func->func_module : Py_None
    );
    return tup;
}

static PyObject *
func_new(PyTypeObject *type, PyObject *args, PyObject *kewd)
{
    PyObject *ob = NULL, *co = NULL, *globals = NULL;

    /* create a fake function for later initialization */
    if (is_wrong_type(type)) return NULL;
    if ((co = Py_CompileString("", "", Py_file_input)) != NULL)
        if ((globals = PyDict_New()) != NULL)
            if ((ob = PyFunction_New(co, globals)) != NULL)
                ob->ob_type = type;
    Py_XDECREF(co);
    Py_XDECREF(globals);
    return ob;
}

#define COPY(src, dest, attr) Py_XINCREF(src->attr); Py_XDECREF(dest->attr); \
                              dest->attr = src->attr

static PyObject *
func_setstate(PyObject *self, PyObject *args)
{
    PyFunctionObject *fu;
    PyObject *args2;

    if (is_wrong_type(self->ob_type)) return NULL;
    self->ob_type = self->ob_type->tp_base;
    args2 = PyTuple_GetSlice(args, 0, 5);
    if (args2 == NULL)
        return NULL;
    fu = (PyFunctionObject *)
         self->ob_type->tp_new(self->ob_type, args2, NULL);
    Py_DECREF(args2);
    if (fu != NULL) {
        PyFunctionObject *target = (PyFunctionObject *) self;
        COPY(fu, target, func_code);
        COPY(fu, target, func_globals);
        COPY(fu, target, func_name);
        COPY(fu, target, func_defaults);
        COPY(fu, target, func_closure);

        Py_XINCREF(PyTuple_GetItem(args, 5));
    target->func_module = PyTuple_GetItem(args, 5);

        Py_DECREF(fu);
        Py_INCREF(self);
        return self;
    }
    return NULL;
}

#undef COPY

MAKE_WRAPPERTYPE(PyFunction_Type, func, "function", func_reduce, func_new,
                 func_setstate)

static int init_functype(void)
{
    return init_type(&wrap_PyFunction_Type, 1, initchain);
}
#undef initchain
#define initchain init_functype


/******************************************************

  pickling addition to frame objects

 ******************************************************/

#define frametuplefmt "O)(OiSOiOOOiiOO"
/* #define frametuplefmt "O)(OiSOiOOiOiiOO" */

DEF_INVALID_EXEC(eval_frame)
DEF_INVALID_EXEC(eval_frame_value)
DEF_INVALID_EXEC(eval_frame_noval)
DEF_INVALID_EXEC(eval_frame_iter)
DEF_INVALID_EXEC(channel_seq_callback)

static PyTypeObject wrap_PyFrame_Type;

static PyObject *
frameobject_reduce(PyFrameObject *f)
{
    int i;
    PyObject **f_stacktop;
    PyObject *blockstack_as_tuple = NULL, *localsplus_as_tuple = NULL,
    *res = NULL, *exec_name = NULL, *exc_as_tuple = NULL;
    int valid = 1;
    int have_locals = f->f_locals != NULL;
    PyObject * dummy_locals = NULL;

    if (!have_locals)
        if ((dummy_locals = PyDict_New()) == NULL)
            return NULL;

    if ((exec_name = slp_find_execname(f, &valid)) == NULL)
        return NULL;

    if (f->f_exc_type != NULL && f->f_exc_type != Py_None) {
        exc_as_tuple = slp_into_tuple_with_nulls(&f->f_exc_type, 3);
        if (exc_as_tuple == NULL) goto err_exit;
    }
    else {
        Py_INCREF(Py_None);
        exc_as_tuple = Py_None;
    }

    blockstack_as_tuple = PyTuple_New (f->f_iblock);
    if (blockstack_as_tuple == NULL) goto err_exit;

    for (i = 0; i < f->f_iblock; i++) {
        PyObject *tripel = Py_BuildValue("iii",
                        f->f_blockstack[i].b_type,
                        f->f_blockstack[i].b_handler,
                        f->f_blockstack[i].b_level);
        if (!tripel) goto err_exit;
        PyTuple_SET_ITEM(blockstack_as_tuple, i, tripel);
    }

    f_stacktop = f->f_stacktop;
    if (f_stacktop != NULL) {
        if (f_stacktop < f->f_valuestack) {
            PyErr_SetString(PyExc_ValueError, "stack underflow");
        goto err_exit;
        }
        localsplus_as_tuple = slp_into_tuple_with_nulls(
            f->f_localsplus, f_stacktop - f->f_localsplus);
        if (localsplus_as_tuple == NULL) goto err_exit;
    }
    else {
        localsplus_as_tuple = Py_None;
        Py_INCREF(Py_None);
        /* frames without a stacktop cannot be run */
        valid = 0;
    }

    res = Py_BuildValue ("(O(" frametuplefmt "))",
                         &wrap_PyFrame_Type,
                         f->f_code,
                         f->f_code,
                         valid,
                         exec_name,
                         f->f_globals,
                         have_locals,
                         have_locals ? f->f_locals : dummy_locals,
                         f->f_trace != NULL ? f->f_trace : Py_None,
/*                           f->f_restricted, */
                 exc_as_tuple,
                 f->f_lasti,
                 f->f_lineno,
                 blockstack_as_tuple,
                 localsplus_as_tuple
                 );

err_exit:
    Py_XDECREF(exec_name);
    Py_XDECREF(exc_as_tuple);
    Py_XDECREF(blockstack_as_tuple);
    Py_XDECREF(localsplus_as_tuple);
    Py_XDECREF(dummy_locals);
    return res;
}

#define frametuplenewfmt "O!"
#define frametuplesetstatefmt "O!iSO!iO!OOiiO!O:frame_new"
/*#define frametuplesetstatefmt "O!iSO!iO!OiOiiO!O:frame_new"*/

static PyObject *
frame_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *f;
    PyCodeObject *f_code;
    PyObject *globals;

    if (is_wrong_type(type)) return NULL;
    if (!PyArg_ParseTuple(args, frametuplenewfmt, &PyCode_Type, &f_code))
        return NULL;
    if (ts->frame != NULL && PyFrame_Check(ts->frame)) {
        globals = ts->frame->f_globals;
        Py_INCREF(globals);
    }
    else
        globals = Py_BuildValue("{sO}", "__builtins__",
                                PyEval_GetBuiltins());
    if (globals == NULL)
        return NULL;
    f = PyFrame_New(ts, (PyCodeObject *) f_code, globals, globals);
    if (f != NULL)
        f->ob_type = &wrap_PyFrame_Type;
    Py_DECREF(globals);
    return (PyObject *) f;
}


static PyObject *
frame_setstate(PyFrameObject *f, PyObject *args)
{
    int /*f_restricted, */ f_lasti, f_lineno, i;
    PyObject *f_globals, *f_locals, *blockstack_as_tuple;
    PyObject *localsplus_as_tuple, *exc_as_tuple, *trace, *f_code;
    PyObject *exec_name = NULL;
    PyFrame_ExecFunc *good_func, *bad_func;
    int valid, have_locals;
    Py_ssize_t tmp;

    if (is_wrong_type(f->ob_type)) return NULL;

    if (f->f_globals != NULL) {
        Py_DECREF(f->f_globals);
        f->f_globals = NULL;
    }
    if (f->f_locals != NULL) {
        Py_DECREF(f->f_locals);
        f->f_locals = NULL;
    }

    if (!PyArg_ParseTuple (args, frametuplesetstatefmt,
                           &PyCode_Type, &f_code,
                           &valid,
                           &exec_name,
                           &PyDict_Type, &f_globals,
                           &have_locals,
                           &PyDict_Type, &f_locals,
                           &trace,
/*                             &f_restricted, */
                   &exc_as_tuple,
                   &f_lasti,
                   &f_lineno,
                   &PyTuple_Type, &blockstack_as_tuple,
                   &localsplus_as_tuple
                   ))
        return NULL;

    if (f->f_code != (PyCodeObject *) f_code) {
        PyErr_SetString(PyExc_TypeError,
                        "invalid code object for frame_setstate");
        return NULL;
    }
    if (slp_find_execfuncs(f->ob_type->tp_base, exec_name, &good_func,
                           &bad_func))
        return NULL;

    if (have_locals) {
        Py_INCREF(f_locals);
        f->f_locals = f_locals;
    }
    Py_INCREF(f_globals);
    f->f_globals = f_globals;

    if (trace != Py_None) {
        if (!PyCallable_Check(trace)) {
            PyErr_SetString(PyExc_TypeError,
                            "trace must be a function for frame");
            goto err_exit;
        }
        Py_INCREF(trace);
        f->f_trace = trace;
    }

/*      f->f_restricted = f_restricted; */

    if (exc_as_tuple != Py_None) {
        if (PyTuple_GET_SIZE(exc_as_tuple) != 4) {
            PyErr_SetString(PyExc_ValueError,
                            "bad exception tuple for frame");
            goto err_exit;
        }
        slp_from_tuple_with_nulls(&f->f_exc_type, exc_as_tuple);
    }

    if (PyTuple_Check(localsplus_as_tuple)) {
        Py_ssize_t space =  f->f_code->co_stacksize + (f->f_valuestack - f->f_localsplus);

        if (PyTuple_GET_SIZE(localsplus_as_tuple)-1 > space) {
            PyErr_SetString(PyExc_ValueError, "invalid localsplus for frame");
            goto err_exit;
        }
        f->f_stacktop = f->f_localsplus;
        f->f_stacktop += slp_from_tuple_with_nulls(f->f_localsplus,
                                                   localsplus_as_tuple);
    }
    else if (localsplus_as_tuple == Py_None) {
        f->f_stacktop = NULL;
        valid = 0;  /* cannot run frame without stack */
    }
    else {
        PyErr_SetString(PyExc_TypeError, "stack must be tuple or None for frame");
        goto err_exit;
    }

    Py_XDECREF(f->f_back);
    /* mark this frame as coming from unpickling */
    Py_INCREF(Py_None);
    f->f_back = (PyFrameObject *) Py_None;

    f->f_lasti = f_lasti;
    f->f_lineno = f_lineno;
    tmp = PyTuple_GET_SIZE(blockstack_as_tuple);
    f->f_iblock = Py_SAFE_DOWNCAST(tmp, Py_ssize_t, int);
    if (f->f_iblock < 0 || f->f_iblock > CO_MAXBLOCKS) {
        PyErr_SetString(PyExc_ValueError, "invalid blockstack for frame");
        goto err_exit;
    }
    for (i = 0; i < CO_MAXBLOCKS; i++) {
        if (i < f->f_iblock) {
            if (!PyArg_ParseTuple(
                PyTuple_GET_ITEM(blockstack_as_tuple, i),
                "iii",
                &f->f_blockstack[i].b_type,
                &f->f_blockstack[i].b_handler,
                &f->f_blockstack[i].b_level
                ))
                goto err_exit;
        } else {
            f->f_blockstack[i].b_type =
            f->f_blockstack[i].b_handler =
            f->f_blockstack[i].b_level = 0;
        }
    }

    /* see if this frame is valid to be run */
    f->f_execute = valid ? good_func : bad_func;

    f->ob_type = &PyFrame_Type;
    Py_INCREF(f);
    return (PyObject *) f;
err_exit:
    Py_XDECREF(f);
    return NULL;
}

PyFrameObject *
slp_clone_frame(PyFrameObject *f)
{
    PyObject *tup, *func, *args;
    PyFrameObject *fnew, *retval;

    if (PyFrame_Check(f))
        tup = frameobject_reduce(f);
    else
        tup = PyObject_CallMethod((PyObject *) f, "__reduce__", "");
    if (tup == NULL)
        return NULL;
    func = PyTuple_GET_ITEM(tup, 0);
    args = PyTuple_GET_ITEM(tup, 1);
    fnew = (PyFrameObject *) PyObject_CallObject(func, args);
    retval = fnew;
    if (PyTuple_GET_SIZE(tup) == 3 && fnew != NULL) {
        args = PyTuple_GET_ITEM(tup, 2);
        if (PyObject_CallMethod((PyObject *) f, "__setstate__", "(O)", args) == NULL)
            retval = NULL;
        Py_DECREF(fnew);
    }
    Py_DECREF(tup);
    return retval;
}

/*
 * return a usable reference to the frame.
 * If the frame doesn't come from unpickling,
 * a clone is created.
 * Otherwise, the frame is incref'd.
 */

PyFrameObject *
slp_ensure_new_frame(PyFrameObject *f)
{
    /* the type check for tasklets is included here for brevity */
    if (! (PyCFrame_Check(f) || PyFrame_Check(f)) ) {
        PyErr_SetString(PyExc_TypeError,
            "tasklet unpickle needs list of frames last parameter.");
        return NULL;
    }
    if ((PyObject *) f->f_back != Py_None) {
        f = slp_clone_frame(f);
        if (f==NULL) {
            return NULL;
        }
    }
    else {
        Py_INCREF(f);
    }
    Py_XDECREF(f->f_back);
    f->f_back = NULL;
    return f;
}

MAKE_WRAPPERTYPE(PyFrame_Type, frame, "frame", frameobject_reduce, frame_new, frame_setstate)

static int init_frametype(void)
{
    return slp_register_execute(&PyFrame_Type, "eval_frame",
                             PyEval_EvalFrameEx_slp, REF_INVALID_EXEC(eval_frame))
        || slp_register_execute(&PyFrame_Type, "eval_frame_value",
                             PyEval_EvalFrame_value, REF_INVALID_EXEC(eval_frame_value))
        || slp_register_execute(&PyFrame_Type, "eval_frame_noval",
                             PyEval_EvalFrame_noval, REF_INVALID_EXEC(eval_frame_noval))
        || slp_register_execute(&PyFrame_Type, "eval_frame_iter",
                             PyEval_EvalFrame_iter, REF_INVALID_EXEC(eval_frame_iter))
        || slp_register_execute(&PyCFrame_Type, "channel_seq_callback",
                             channel_seq_callback, REF_INVALID_EXEC(channel_seq_callback))
        || init_type(&wrap_PyFrame_Type, 1, initchain);
}
#undef initchain
#define initchain init_frametype


/******************************************************

  pickling of tracebacks

 ******************************************************/

/*
 * Simplified version with full recursion.
 * This is fine, since we are making cPickle stackless.
 */

/* XXX automate checking these double definitions: */

typedef struct _tracebackobject {
    PyObject_HEAD
    struct _tracebackobject *tb_next;
    PyFrameObject *tb_frame;
    int tb_lasti;
    int tb_lineno;
} tracebackobject;

static PyTypeObject wrap_PyTraceBack_Type;

static PyObject *
tb_reduce(tracebackobject * tb)
{
    PyObject *tup = NULL;
    char *fmt = "(O()(OiiO))";

    if (tb->tb_next == NULL)
        fmt = "(O()(Oii))";
    tup = Py_BuildValue(fmt,
                        &wrap_PyTraceBack_Type,
                        tb->tb_frame, tb->tb_lasti, tb->tb_lineno, tb->tb_next);
    return tup;
}

static PyObject *
tb_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    tracebackobject *tb;

    if (is_wrong_type(type)) return NULL;
    if (!PyArg_ParseTuple(args, ":traceback")) return NULL;

    tb = PyObject_GC_New(tracebackobject, &PyTraceBack_Type);
    if (tb != NULL) {
        tb->tb_next = NULL;
        tb->tb_frame = NULL;
        PyObject_GC_Track(tb);
        tb->ob_type = type;
    }
    return (PyObject *) tb;
}

static
PyObject *
tb_setstate(PyObject *self, PyObject *args)
{
    tracebackobject *tb = (tracebackobject*) self, *next = NULL;
    PyFrameObject *frame;
    int lasti, lineno;

    if (is_wrong_type(tb->ob_type)) return NULL;

    if (!PyArg_ParseTuple(args,
                          "O!ii|O!:traceback",
                          &PyFrame_Type, &frame,
                          &lasti, &lineno,&PyTraceBack_Type, &next))
        return NULL;

    Py_XINCREF(next);
    tb->tb_next = next;
    Py_XINCREF(frame);
    tb->tb_frame = frame;
    tb->tb_lasti = lasti;
    tb->tb_lineno = lineno;
    tb->ob_type = tb->ob_type->tp_base;

    Py_INCREF(self);
    return self;
}

MAKE_WRAPPERTYPE(PyTraceBack_Type, tb, "traceback", tb_reduce, tb_new, tb_setstate)

static int init_tracebacktype(void)
{
    return init_type(&wrap_PyTraceBack_Type, 1, initchain);
}
#undef initchain
#define initchain init_tracebacktype


/******************************************************

  pickling of modules

 ******************************************************/

static PyTypeObject wrap_PyModule_Type;


static PyObject *
module_reduce(PyObject * m)
{
    static PyObject *import = NULL;
    PyObject *modules = PyImport_GetModuleDict();
    char *name = PyModule_GetName(m);

    if (name == NULL) return NULL;

    /* is this module maybe not imported? */
    if (PyDict_GetItemString(modules, name) == NULL)
        return Py_BuildValue("(O(s)O)",
                             &wrap_PyModule_Type,
                             PyModule_GetName(m),
                             PyModule_GetDict(m));

    if (import == NULL) {
        import = run_script("ret = __import__", "ret");
        if (import == NULL)
            return NULL;
    }
    return Py_BuildValue("(O(s()()(s)))", import, name, "");
    /* would be shorter, but the search result is quite arbitrary:
        tup = PyObject_GetAttrString(m, "__name__");
     */
}

typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
} PyModuleObject;

static PyObject *
module_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *mod, *mod_dict = NULL, *dict = NULL;
    char *name;

    if (is_wrong_type(type)) return NULL;

    /*
     * This is a funny case, like "int(derivednumber)". Instead of creating
     * a module, we have to produce its name string, instead, since this is
     * the way to create a fresh basic module:
     */
    if (PyTuple_GET_SIZE(args) == 1 &&
        PyModule_Check(PyTuple_GET_ITEM(args, 0)) ) {
        mod = PyTuple_GET_ITEM(args, 0);
        return PyObject_GetAttrString(mod, "__name__");
    }
    else if (!PyArg_ParseTuple(args, "s|O!:module", &name,
                               &PyDict_Type, &dict))
        return NULL;
    mod = PyModule_New(name);
    if (mod != NULL)
        mod_dict = PyModule_GetDict(mod);
    if (mod_dict && dict && PyDict_Update(mod_dict, dict)) {
        Py_DECREF(mod);
        mod = NULL;
    }
    return mod;
}

MAKE_WRAPPERTYPE(PyModule_Type, module, "module", module_reduce, module_new, generic_setstate)

static int init_moduletype(void)
{
    return init_type(&wrap_PyModule_Type, 1, initchain);
}
#undef initchain
#define initchain init_moduletype


/******************************************************

  pickling of iterators

 ******************************************************/

/* XXX make sure this copy is always up to date */
typedef struct {
    PyObject_HEAD
    long      it_index;
    PyObject *it_seq;
} seqiterobject;

static PyTypeObject wrap_PySeqIter_Type;

static PyObject *
iter_reduce(seqiterobject *iterator)
{
    PyObject *tup;
    tup = Py_BuildValue("(O(Ol)())",
                        &wrap_PySeqIter_Type,
                        iterator->it_seq,
                        iterator->it_index);
    return tup;
}

static PyObject *
iter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *iter, *seq;
    long index;

    if (is_wrong_type(type)) return NULL;
    if (!PyArg_ParseTuple(args, "Ol:iter", &seq, &index)) return NULL;
    iter = PySeqIter_New(seq);
    if (iter != NULL) {
        ((seqiterobject *) iter)->it_index = index;
        iter->ob_type = type;
    }
    return iter;
}

MAKE_WRAPPERTYPE(PySeqIter_Type, iter, "iterator", iter_reduce, iter_new,
                 generic_setstate)

/* XXX make sure this copy is always up to date */
typedef struct {
    PyObject_HEAD
    PyObject *it_callable;
    PyObject *it_sentinel;
} calliterobject;

static PyTypeObject wrap_PyCallIter_Type;

static PyObject *
calliter_reduce(calliterobject *iterator)
{
    PyObject *tup;

    tup = Py_BuildValue("(O(OO))",
                        &wrap_PyCallIter_Type,
                        iterator->it_callable,
                        iterator->it_sentinel);
    return tup;
}

static
PyObject *
calliter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    calliterobject *it;
    PyObject *it_callable;
    PyObject *it_sentinel;

    if (is_wrong_type(type)) return NULL;
    if (!PyArg_ParseTuple(args, "OO:calliter", &it_callable,
                          &it_sentinel))
        return NULL;

    it = (calliterobject *) PyCallIter_New(it_callable, it_sentinel);
    if (it != NULL)
        it->ob_type = type;
    return (PyObject *) it;
}

MAKE_WRAPPERTYPE(PyCallIter_Type, calliter, "callable-iterator",
                 calliter_reduce, calliter_new, generic_setstate)

static int init_itertype(void)
{
    return init_type(&wrap_PySeqIter_Type, 1, NULL)
        || init_type(&wrap_PyCallIter_Type, 1, initchain);
}
#undef initchain
#define initchain init_itertype


/******************************************************

  pickling of class/instance methods (PyMethod)

 ******************************************************/

static PyTypeObject wrap_PyMethod_Type;

static PyObject *
method_reduce(PyObject *m)
{
    PyObject *tup, *func, *self, *clas;
    char *fmt = "(O(OOO)())";

    func = PyMethod_GET_FUNCTION(m);
    self = PyMethod_GET_SELF(m);
    clas = PyMethod_GET_CLASS(m);
    if (self == NULL)
        self = Py_None;
    if (clas == NULL)
        fmt = "(O(OO)())";
    tup = Py_BuildValue(fmt, &wrap_PyMethod_Type, func, self, clas);
    return tup;
}

MAKE_WRAPPERTYPE(PyMethod_Type, method, "instancemethod", method_reduce,
                 generic_new, generic_setstate)

static int init_methodtype(void)
{
    return init_type(&wrap_PyMethod_Type, 1, initchain);
}
#undef initchain
#define initchain init_methodtype


/******************************************************

  pickling of dictiter

 ******************************************************/

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict; /* Set to NULL when iterator is exhausted */
    Py_ssize_t di_used;
    Py_ssize_t di_pos;
    PyObject* di_result; /* reusable result tuple for iteritems */
    Py_ssize_t len;
} dictiterobject;

/*
    binaryfunc is a big problem.  we don't have access to the
    function pointers, so what we have to do is exhaust the
    dictionary iterator by copying it.

    when we unpickle, we return an entirely different kind of object.
*/

static PyObject *
dictiterkey_reduce(dictiterobject *di)
{
    PyObject *tup, *list, *key;
    Py_ssize_t i;

    list = PyList_New(0);
    if (list == NULL)
        return PyErr_NoMemory();

    /* is this dictiter is already exhausted? */
    if (di->di_dict != NULL) {
        if (di->di_used != di->di_dict->ma_used) {
            PyErr_SetString(PyExc_RuntimeError,
                "dictionary changed size during iteration");
            di->di_used = -1; /* Make this state sticky */
            return NULL;
        }
        i = di->di_pos;
        while (PyDict_Next((PyObject *)di->di_dict, &i, &key, NULL)) {
            assert(key != NULL);
            if (PyList_Append(list, key) == -1) {
                Py_DECREF(list);
                return NULL;
            }
        }
    }
    /* masquerade as a PySeqIter */
    tup = Py_BuildValue("(O(Nl)())",
        &wrap_PySeqIter_Type,
        list,
        0
        );
    return tup;
}

static PyObject *
dictitervalue_reduce(dictiterobject *di)
{
    PyObject *tup, *list, *value;
    Py_ssize_t i;

    list = PyList_New(0);
    if (list == NULL)
        return PyErr_NoMemory();

    /* is this dictiter is already exhausted? */
    if (di->di_dict != NULL) {
        if (di->di_used != di->di_dict->ma_used) {
            PyErr_SetString(PyExc_RuntimeError,
                "dictionary changed size during iteration");
            di->di_used = -1; /* Make this state sticky */
            return NULL;
        }
        i = di->di_pos;
        while (PyDict_Next((PyObject *)di->di_dict, &i, NULL, &value)) {
            assert(value != NULL);
            if (PyList_Append(list, value) == -1) {
                Py_DECREF(list);
                return NULL;
            }
        }
    }
    /* masquerade as a PySeqIter */
    tup = Py_BuildValue("(O(Nl)())",
        &wrap_PySeqIter_Type,
        list,
        0
        );
    return tup;
}

static PyObject *
dictiteritem_reduce(dictiterobject *di)
{
    PyObject *tup, *list, *key, *value, *res;
    Py_ssize_t i;

    list = PyList_New(0);
    if (list == NULL)
        return PyErr_NoMemory();

    /* is this dictiter is already exhausted? */
    if (di->di_dict != NULL) {
        if (di->di_used != di->di_dict->ma_used) {
            PyErr_SetString(PyExc_RuntimeError,
                "dictionary changed size during iteration");
            di->di_used = -1; /* Make this state sticky */
            return NULL;
        }
        i = di->di_pos;
        while (PyDict_Next((PyObject *)di->di_dict, &i, &key, &value)) {
            assert(key != NULL && value != NULL);
            res = Py_BuildValue("OO", key, value);
            if (res == NULL) {
                Py_DECREF(list);
                return NULL;
            }
            if (PyList_Append(list, res) == -1) {
                Py_DECREF(list);
                Py_DECREF(res);
                return NULL;
            }
            Py_DECREF(res);
        }
    }
    /* masquerade as a PySeqIter */
    tup = Py_BuildValue("(O(Nl)())",
        &wrap_PySeqIter_Type,
        list,
        0
        );
    return tup;
}

static PyTypeObject wrap_PyDictIterKey_Type;

MAKE_WRAPPERTYPE(PyDictIterKey_Type, dictiterkey, "dictionary-keyiterator",
                 dictiterkey_reduce, generic_new, generic_setstate)

static int init_dictiterkeytype(void)
{
    return init_type(&wrap_PyDictIterKey_Type, 1, initchain);
}
#undef initchain
#define initchain init_dictiterkeytype

static PyTypeObject wrap_PyDictIterValue_Type;

MAKE_WRAPPERTYPE(PyDictIterValue_Type, dictitervalue, "dictionary-valueiterator",
                 dictitervalue_reduce, generic_new, generic_setstate)

static int init_dictitervaluetype(void)
{
    return init_type(&wrap_PyDictIterValue_Type, 1, initchain);
}
#undef initchain
#define initchain init_dictitervaluetype

static PyTypeObject wrap_PyDictIterItem_Type;

MAKE_WRAPPERTYPE(PyDictIterItem_Type, dictiteritem, "dictionary-itemiterator",
                 dictiteritem_reduce, generic_new, generic_setstate)

static int init_dictiteritemtype(void)
{
    return init_type(&wrap_PyDictIterItem_Type, 1, initchain);
}
#undef initchain
#define initchain init_dictiteritemtype


/******************************************************

  pickling of setiter

 ******************************************************/

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    PySetObject *si_set; /* Set to NULL when iterator is exhausted */
    Py_ssize_t si_used;
    Py_ssize_t si_pos;
    Py_ssize_t len;
} setiterobject;

extern PyTypeObject PySetIter_Type;
static PyTypeObject wrap_PySetIter_Type;

static PyObject *
setiter_reduce(setiterobject *it)
{
    PyObject *list, *set, *elem;
    Py_ssize_t i;

    list = PyList_New(0);
    if (list == NULL)
    return PyErr_NoMemory();

    /* is this setiter is already exhausted? */
    if (it->si_set != NULL) {
    if (it->si_used != it->si_set->used) {
        PyErr_SetString(PyExc_RuntimeError,
        "set changed size during iteration");
        it->si_used = -1; /* Make this state sticky */
        return NULL;
    }
    i = it->si_pos;
    while (_PySet_Next((PyObject *)it->si_set, &i, &elem)) {
        if (PyList_Append(list, elem) == -1) {
        return NULL;
        }
    }
    }
    /* masquerade as a PySeqIter */
    set = Py_BuildValue("(O(Ol)())",
    &wrap_PySeqIter_Type,
    list,
    0
    );
    Py_DECREF(list);
    return set;
}

MAKE_WRAPPERTYPE(PySetIter_Type, setiter, "setiterator", setiter_reduce, generic_new, generic_setstate)

static int init_setitertype(void)
{
    return init_type(&wrap_PySetIter_Type, 1, initchain);
}
#undef initchain
#define initchain init_setitertype

/******************************************************

  pickling of enumerate

 ******************************************************/

#if PY_VERSION_HEX >= 0x02030000

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    long      en_index;        /* current index of enumeration */
    PyObject* en_sit;          /* secondary iterator of enumeration */
    PyObject* en_result;       /* result tuple  */
} enumobject;

static PyTypeObject wrap_PyEnum_Type;

static PyObject *
enum_reduce(enumobject *en)
{
    PyObject *tup;

    tup = Py_BuildValue("(O(O)(l))",
                        &wrap_PyEnum_Type,
                        en->en_sit,
                        en->en_index
                        );
    return tup;
}

static PyObject *
enum_setstate(PyObject *self, PyObject *args)
{
    if (is_wrong_type(self->ob_type)) return NULL;
    if (!PyArg_ParseTuple (args, "l:enumerate",
                           &((enumobject *)self)->en_index))
        return NULL;
    self->ob_type = self->ob_type->tp_base;
    Py_INCREF(self);
    return self;
}

MAKE_WRAPPERTYPE(PyEnum_Type, en, "enumerate", enum_reduce, generic_new,
                 enum_setstate)

static int init_enumtype(void)
{
    return init_type(&wrap_PyEnum_Type, 1, initchain);
}
#undef initchain
#define initchain init_enumtype

#endif /* PY_VERSION_HEX >= 0x02030000 */

/******************************************************

  pickling of listiter

 ******************************************************/

#if PY_VERSION_HEX >= 0x02030000

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    long it_index;
    PyListObject *it_seq; /* Set to NULL when iterator is exhausted */
} listiterobject;

static PyTypeObject wrap_PyListIter_Type;

static PyObject *
listiter_reduce(listiterobject *it)
{
    PyObject *tup, *list;

    /* it's finished or exhausted */
    if (it->it_seq == NULL ||
        it->it_index >= PyList_GET_SIZE(it->it_seq)) {
        list = PyList_New(0);
        if (list == NULL)
            return PyErr_NoMemory();
    } else {
        list = PyList_GetSlice((PyObject *)it->it_seq,
                               it->it_index,
                               PyList_Size((PyObject *)it->it_seq));
    }
    /* masquerade as a PySeqIter */
    tup = Py_BuildValue("(O(Ol)())",
                        &wrap_PySeqIter_Type,
                        list,
                        0
                        );
    Py_DECREF(list);
    return tup;
}

MAKE_WRAPPERTYPE(PyListIter_Type, listiter, "listiterator", listiter_reduce, generic_new, generic_setstate)

static int init_listitertype(void)
{
    return init_type(&wrap_PyListIter_Type, 1, initchain);
}
#undef initchain
#define initchain init_listitertype

#endif /* PY_VERSION_HEX >= 0x02030000 */


/******************************************************

  pickling of rangeiter

 ******************************************************/

#if PY_VERSION_HEX >= 0x02030000

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    long index;
    long start;
    long step;
    long len;
} rangeiterobject;

static PyTypeObject wrap_PyRangeIter_Type;

static PyObject *
rangeiter_reduce(rangeiterobject *it)
{
    PyObject *tup;

    tup = Py_BuildValue("(O(llll)())",
                        &wrap_PyRangeIter_Type,
                        it->index,
                        it->start,
                        it->step,
                        it->len
                        );
    return tup;
}

static
PyObject *
rangeiter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    rangeiterobject *it;

    if (is_wrong_type(type)) return NULL;
    it = PyObject_New(rangeiterobject, type);
    if (it == NULL)
        return NULL;
    if (!PyArg_ParseTuple(args, "llll:rangeiterator",
                                &it->index,
                                &it->start,
                                &it->step,
                                &it->len))
        return NULL;
    return (PyObject *)it;
}

MAKE_WRAPPERTYPE(PyRangeIter_Type, rangeiter, "rangeiterator",
                 rangeiter_reduce, rangeiter_new, generic_setstate)

static int init_rangeitertype(void)
{
    return init_type(&wrap_PyRangeIter_Type, 1, initchain);
}
#undef initchain
#define initchain init_rangeitertype

#endif /* PY_VERSION_HEX >= 0x02030000 */


/******************************************************

  pickling of tupleiter

 ******************************************************/

#if PY_VERSION_HEX >= 0x02030000

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    long it_index;
    PyListObject *it_seq; /* Set to NULL when iterator is exhausted */
} tupleiterobject;

static PyTypeObject wrap_PyTupleIter_Type;

static PyObject *
tupleiter_reduce(tupleiterobject *it)
{
    PyObject *tup, *tuple;

    /* it's finished or exhausted */
    if (it->it_seq == NULL || it->it_index >= PyTuple_GET_SIZE(it->it_seq)) {
        tuple = PyTuple_New(0);
        if (tuple == NULL)
            return PyErr_NoMemory();
    }
    else {
        tuple = PyTuple_GetSlice(
            (PyObject *)it->it_seq,
            it->it_index,
            PyTuple_Size((PyObject *)it->it_seq));
    }
    /* masquerade as a PySeqIter */
    tup = Py_BuildValue("(O(Ol)())",
                        &wrap_PySeqIter_Type,
                        tuple,
                        0
                        );
    Py_DECREF(tuple);
    return tup;
}

MAKE_WRAPPERTYPE(PyTupleIter_Type, tupleiter, "tupleiterator", tupleiter_reduce, generic_new, generic_setstate)

static int init_tupleitertype(void)
{
    return init_type(&wrap_PyTupleIter_Type, 1, initchain);
}
#undef initchain
#define initchain init_tupleitertype

#endif /* PY_VERSION_HEX >= 0x02030000 */

/******************************************************

  pickling of xrange

 ******************************************************/


/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    long        start;
    long        step;
    long        len;
} rangeobject;

static PyTypeObject wrap_PyRange_Type;

static PyObject *
range_reduce(rangeobject *r)
{
    PyObject *tup;

    assert(r != NULL);
    tup = Py_BuildValue("(O(lll)())",
                        &wrap_PyRange_Type,
                        r->start,
                        r->start + r->len * r->step,
                        r->step
                        );
    return tup;
}

MAKE_WRAPPERTYPE(PyRange_Type, range, "xrange", range_reduce,
                 generic_new, generic_setstate)

static int init_rangetype(void)
{
    return init_type(&wrap_PyRange_Type, 0, initchain);
}
#undef initchain
#define initchain init_rangetype


/******************************************************

  pickling of method wrappers

 ******************************************************/

/*
 * unfortunately we have to copy here.
 * XXX automate checking such situations.
 */

typedef struct {
    PyObject_HEAD
    PyWrapperDescrObject *descr;
    PyObject *self;
} wrapperobject;

static PyTypeObject wrap_PyMethodWrapper_Type;

static PyObject *
methw_reduce(wrapperobject *w)
{
    PyObject *name = PyObject_GetAttrString((PyObject *) w->descr,
                                            "__name__");
    PyObject *tup = NULL;

    if (name != NULL) {
        tup = Py_BuildValue("(O()(OO))",
                            &wrap_PyMethodWrapper_Type,
                            name,
                            w->self
                            );
    }
    Py_XDECREF(name);
    return tup;
}

static PyObject *
methw_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    /* this is a bit hairy since we need to build a dummy one */
    wrapperobject *w = NULL, *neww = NULL;
    PyObject *it = NULL, *tup = PyTuple_New(0);

    it = PyObject_GetIter(tup);
    if (it != NULL)
        w = (wrapperobject *) PyObject_GetAttrString(it, "next");
    if (w != NULL && PyMethodWrapper_Check(w)) {
        neww = (wrapperobject *) PyWrapper_New((PyObject *) w->descr,
                                               w->self);
    }
    else
        PyErr_SetString(PyExc_SystemError,
                        "problem with wrapper pickling");
    Py_DECREF(tup);
    Py_XDECREF(w);
    Py_XDECREF(it);
    if (neww != NULL)
        neww->ob_type = type;
    return (PyObject *) neww;
}

static PyObject *
methw_setstate(PyObject *self, PyObject *args)
{
    PyObject *name, *inst;
    PyObject *w;

    if (is_wrong_type(self->ob_type)) return NULL;
    if (!PyArg_ParseTuple(args, "O!O:method-wrapper",
                          &PyString_Type, &name,
                          &inst))
        return NULL;
    /* now let's see if we can retrieve a wrapper, again */
    w = PyObject_GetAttr(inst, name);
    if (w != NULL && !PyMethodWrapper_Check(w)) {
        PyErr_SetString(PyExc_ValueError,
                        "bad unpickle data for method-wrapper");
        return NULL;
    }
    if (w == NULL) return NULL;
    /* now fill our object with the right data */
    {
        wrapperobject *neww = (wrapperobject *) self;
        wrapperobject *oldw = (wrapperobject *) w;
        Py_DECREF(neww->descr);
        Py_INCREF(oldw->descr);
        neww->descr = oldw->descr;
        Py_DECREF(neww->self);
        Py_INCREF(oldw->self);
        neww->self = oldw->self;
    }
    Py_DECREF(w);
    self->ob_type = self->ob_type->tp_base;
    Py_INCREF(self);
    return self;
}


MAKE_WRAPPERTYPE(PyMethodWrapper_Type, methw, "method-wrapper", methw_reduce,
                 methw_new, methw_setstate)

static int init_methodwrappertype(void)
{
    return init_type(&wrap_PyMethodWrapper_Type, 1, initchain);
}
#undef initchain
#define initchain init_methodwrappertype


/******************************************************

  pickling of generators

 ******************************************************/

/* Note: this definition is not compatible to 2.2.3, since
   the gi_weakreflist does not exist. Therefore, we don't
   create the object ourselves, but use the provided function
   with a fake frame. The gi_weakreflist is not touched.
 */

typedef struct {
    PyObject_HEAD
    /* The gi_ prefix is intended to remind of generator-iterator. */

    PyFrameObject *gi_frame;

    /* True if generator is being executed. */
    int gi_running;

#if PY_VERSION_HEX >= 0x02060000   /* 2.6 */
    /* The code object backing the generator */
    PyObject *gi_code;
#endif
    /* List of weak reference. */
    PyObject *gi_weakreflist;
} genobject;

static PyTypeObject wrap_PyGen_Type;

static PyObject *
gen_reduce(genobject *gen)
{
    PyObject *tup;
    tup = Py_BuildValue("(O()(Oi))",
                        &wrap_PyGen_Type,
                        gen->gi_frame,
                        gen->gi_running
                        );
    return tup;
}

static PyObject *
gen_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    genobject *gen;

    if (is_wrong_type(type)) return NULL;
    Py_INCREF(Py_None);
    gen = (genobject *) PyGenerator_New((PyFrameObject *) Py_None);
    if (gen == NULL)
        return NULL;
    gen->ob_type = type;
    return (PyObject *) gen;
}

static PyObject *
gen_setstate(PyObject *self, PyObject *args)
{
    genobject *gen = (genobject *) self;
    PyFrameObject *f;
    int gi_running;

    if (is_wrong_type(self->ob_type)) return NULL;
    if (!PyArg_ParseTuple(args, "O!i:generator",
                          &PyFrame_Type, &f, &gi_running))
        return NULL;

    if (!gi_running) {
        if ((f = slp_ensure_new_frame(f)) != NULL) {
            /* use a second one for late initialization */
            genobject *tmpgen;
            /* PyGenerator_New eats an existing reference */
            if ((tmpgen = (genobject *)
                       PyGenerator_New(f)) == NULL) {
                Py_DECREF(f);
                return NULL;
            }
            Py_INCREF(f);
            Py_DECREF(gen->gi_frame);
            gen->gi_frame = f;
#if PY_VERSION_HEX >= 0x02060000   /* 2.6 */
            Py_INCREF(f->f_code);
            Py_DECREF(gen->gi_code);
            gen->gi_code = (PyObject *)f->f_code;
#endif
            /* The frame the temporary generator references
               will have GeneratorExit raised on it, when the
               temporary generator is torn down.  So clearing
               the frame from the temporary generator before
               discarding it, will save the frame for later. */
            Py_CLEAR(((PyGenObject *)tmpgen)->gi_frame);
            Py_DECREF(tmpgen);
            Py_INCREF(gen);
            gen->ob_type = gen->ob_type->tp_base;
        }
        else
            gen = NULL;
        return (PyObject *) gen;
    }

    /*
     * The frame might now be initially unpickled (with PyNone as f_back),
     * or it is already chained into a tasklet.
     * Fortunately, we can simply leave it this way:
     * since gi_running is set, there is no way to continue the
     * generator without the corresponding tasklet.
     */
    Py_INCREF(f);
    Py_DECREF(gen->gi_frame);
    gen->gi_frame = f;
#if PY_VERSION_HEX >= 0x02060000   /* 2.6 */
    Py_INCREF(f->f_code);
    Py_DECREF(gen->gi_code);
    gen->gi_code = (PyObject *)f->f_code;
#endif
    gen->gi_running = gi_running;
    gen->ob_type = gen->ob_type->tp_base;
    Py_INCREF(gen);
    return (PyObject *)gen;
}

MAKE_WRAPPERTYPE(PyGen_Type, gen, "generator", gen_reduce,
                 gen_new, gen_setstate)

DEF_INVALID_EXEC(gen_iternext_callback)

static int init_generatortype(void)
{
    int res;
    genobject *gen = (genobject *) run_script(
        "def f(): yield 42\n"           /* define a generator */
        "g = f()\n"                     /* instanciate it */
        "g.next()\n", "g");             /* force callback frame creation */
    PyFrameObject *cbframe;

    if (gen == NULL || gen->gi_frame->f_back == NULL)
        return -1;
    cbframe = gen->gi_frame->f_back;
    res = slp_register_execute(cbframe->ob_type, "gen_iternext_callback",
              gen->gi_frame->f_back->f_execute,
              REF_INVALID_EXEC(gen_iternext_callback))
          || init_type(&wrap_PyGen_Type, 1, initchain);
    Py_DECREF(gen);
    return res;
}
#undef initchain
#define initchain init_generatortype


/******************************************************

  support code for module dict pickling

 ******************************************************/

PyObject *
PyStackless_Pickle_ModuleDict(PyObject *pickler, PyObject *self)
{
    PyObject *modict, *retval = NULL, *values = NULL;
    PyObject *mod = NULL, *id = NULL;

    modict = PyObject_GetAttrString(pickler, "module_dict_ids");
    if (modict == NULL) {
        PyObject *modules, *dict;

        PyErr_Clear();
        if ((modict = PyDict_New()) == NULL) goto finally;
        modules = PyImport_GetModuleDict();
        values = PyObject_CallMethod(modules, "itervalues", "()");
        for (;;) {
            mod = PyIter_Next(values);
            if (mod == NULL) {
                if (PyErr_Occurred()) goto finally;
                break;
            }
            if (!PyModule_Check(mod)) {
                Py_DECREF(mod);
                continue;
            }
            dict = PyModule_GetDict(mod);
            id = PyLong_FromVoidPtr(dict);
            if (id == NULL) goto finally;
            if (PyDict_SetItem(modict, id, mod)) goto finally;
            Py_DECREF(id);
            Py_DECREF(mod);
            id = mod = NULL;
        }
        if (PyObject_SetAttrString(pickler, "module_dict_ids", modict))
            goto finally;
    }
    if (!PyDict_Check(modict)) {
        PyErr_SetString(PyExc_TypeError,
                        "pickler: module_dict_ids is not a dictionary");
        goto finally;
    }
    {
        PyObject *thisid = PyLong_FromVoidPtr(self);
        PyObject *themodule;

        if (thisid == NULL) goto finally;
        if ((themodule = PyDict_GetItem(modict, thisid)) == NULL) {
            Py_INCREF(Py_None);
            retval = Py_None;
        }
        else {
            PyObject *builtins = PyEval_GetBuiltins();
            PyObject *getattr = PyDict_GetItemString(builtins, "getattr");
            if (getattr == NULL) goto finally;
            retval = Py_BuildValue("(O(Os))",
                getattr, themodule, "__dict__");
        }
        Py_DECREF(thisid);
    }
finally:
    Py_XDECREF(modict);
    Py_XDECREF(values);
    return retval;
}

char slp_pickle_moduledict__doc__[] = PyDoc_STR(
    "pickle_moduledict(pickler, dict) -- see if a dict is a global module\n"
    "dictionary. If so, the dict is not pickled, but a\n"
    "getattr(module, \"__dict__\") call isgenerated. As a side effect,\n"
    "the pickler gets a property 'module_dict_ids'\n"
    "which holds all id's of the current global module dictionaries.");

PyObject *
slp_pickle_moduledict(PyObject *self, PyObject *args)
{
    PyObject *pickler, *dict;
    if (!PyArg_ParseTuple(args, "OO!:moduledict_pickler",
                          &pickler,
                          &PyDict_Type, &dict))
        return NULL;
    return PyStackless_Pickle_ModuleDict(pickler, dict);
}

/******************************************************

  source module initialization

 ******************************************************/

int init_prickelpit(void)
{
    PyObject *copy_reg;
    int ret = 0;

    types_mod = Py_InitModule("stackless._wrap", NULL);
    if (types_mod == NULL) return -1;
    if (PyObject_SetAttrString(slp_module, "_wrap", types_mod)) return -1;
    copy_reg = PyImport_ImportModule("copy_reg");
    if (copy_reg != NULL) {
        pickle_reg = PyObject_GetAttrString(copy_reg, "pickle");
    }
    PyErr_Clear();
    if (initchain())
        ret = -1;

    Py_XDECREF(pickle_reg);
    Py_XDECREF(copy_reg);
    return 0;
}

#endif
