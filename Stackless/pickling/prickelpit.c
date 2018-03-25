#include "Python.h"
#ifdef STACKLESS

#include "compile.h"

#include "core/stackless_impl.h"
#include "pickling/prickelpit.h"
#include "module/channelobject.h"

/* platform specific constants */
#include "platf/slp_platformselect.h"

/******************************************************

  type template and support for pickle helper types

 ******************************************************/

#if PY_VERSION_HEX >= 0x030502C1
/* issue25718 got fixed in 3.4.4rc1 */
#define NO_STATE_FORMAT "()"
#define NO_STATE_ARG    /* nothing */
#else
/* Bug http://bugs.python.org/issue25718 requires, that the state object for
 * mutable types has a boolean value of True. Immutable types use a different
 * copy.copy() mechanism.
 */
#define NO_STATE_FORMAT "(O)"
#define NO_STATE_ARG    ,Py_None
#endif

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
        Py_TYPE(inst) = type;
    return inst;
}

int
slp_generic_init(PyObject *ob, PyObject *args, PyObject *kwds)
{

    initproc init = Py_TYPE(ob)->tp_base->tp_init;

    if (init)
        return init(ob, args, kwds);
    return 0;
}

static PyObject *
generic_setstate(PyObject *self, PyObject *args)
{
    if (is_wrong_type(Py_TYPE(self))) return NULL;
    Py_TYPE(self) = Py_TYPE(self)->tp_base;
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
    Py_TYPE(ob) = Py_TYPE(ob)->tp_base;
    if (Py_TYPE(ob)->tp_dealloc != NULL)
        Py_TYPE(ob)->tp_dealloc(ob);
}

static int
_wrap_traverse(PyObject *ob, visitproc visit, void *arg)
{
    PyTypeObject *type = Py_TYPE(ob);
    int ret = 0;
    Py_TYPE(ob) = type->tp_base;
    if (Py_TYPE(ob)->tp_traverse != NULL)
        ret = Py_TYPE(ob)->tp_traverse(ob, visit, arg);
    Py_TYPE(ob) = type;
    return ret;
}

static void
_wrap_clear(PyObject *ob)
{
    Py_TYPE(ob) = Py_TYPE(ob)->tp_base;
    if (Py_TYPE(ob)->tp_clear != NULL)
        Py_TYPE(ob)->tp_clear(ob);
}


#define MAKE_WRAPPERTYPE(type, prefix, name, reduce, newfunc, setstate) \
 \
static PyMethodDef prefix##_methods[] = { \
    {"__reduce__",     (PyCFunction)reduce,             METH_NOARGS,    NULL}, \
    {"__setstate__",   (PyCFunction)setstate,           METH_O,         NULL}, \
    {"__new__",            (PyCFunction)_new_wrapper,   METH_VARARGS | METH_KEYWORDS, \
     PyDoc_STR("wwwwwaaaaaT.__new__(S, ...) -> " \
               "a new object with type S, a subtype of T")}, \
    {NULL, NULL} \
}; \
 \
static struct _typeobject wrap_##type = { \
    PyVarObject_HEAD_INIT(&PyType_Type, 0) \
    "_stackless._wrap." name, \
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
    slp_generic_init,                           /* tp_init */ \
    0,                                          /* tp_alloc */ \
    newfunc,                                    /* tp_new */ \
    0,                                          /* tp_free */ \
};

static PyObject *types_mod = NULL;
static PyObject *reduce_frame_func = NULL;

PyDoc_STRVAR(set_reduce_frame__doc__,
"set_reduce_frame(func) -- set the function used to reduce frames during pickling.\n"
"The function takes a frame as its sole argument and must return a pickleable object.\n");

static PyObject *
set_reduce_frame(PyObject *self, PyObject *func)
{
    if (func == Py_None) {
        Py_CLEAR(reduce_frame_func);
    } else {
        if (!PyCallable_Check(func)) {
            TYPE_ERROR("func must be callable", NULL);
        }
        Py_INCREF(func);
        Py_XSETREF(reduce_frame_func, func);
    }
    Py_RETURN_NONE;
}

PyObject *
slp_reduce_frame(PyFrameObject * frame) {
    if (!PyFrame_Check(frame) || reduce_frame_func == NULL) {
        Py_INCREF(frame);
        return (PyObject *)frame;
    }
    return PyObject_CallFunctionObjArgs(reduce_frame_func, (PyObject *)frame, NULL);
}

/* Helper function for gen_setstate and tb_setstate.
 * It unwraps the first argument of the args tuple, if it is a _Frame_Wrapper.
 * Returns a new reference to an argument tuple.
 *
 * This functionality is required, to adhere to the __reduce__/__setstate__ protocol.
 * It requires, that __setstate__ accepts the state returned by __reduce__. (copy.copy()
 * depends on it.)
 */
static PyObject *
unwrap_frame_arg(PyObject * args) {
    PyObject *wrapper_type, *arg0, *result;
    int is_instance;
    Py_ssize_t len, i;

    if (!PyTuple_Check(args)) {
        Py_INCREF(args);
        return args;
    }
    if ((len = PyTuple_Size(args)) < 1) {
        if (len < 0)
            return NULL;
        Py_INCREF(args);
        return args;
    }
    if ((arg0 = PyTuple_GetItem(args, 0)) == NULL) /* arg0 is a borrowed reference */
        return NULL;
    if ((wrapper_type = PyObject_GetAttrString(reduce_frame_func, "__self__")) == NULL)
        return NULL;
    is_instance = PyObject_IsInstance(arg0, wrapper_type);
    Py_DECREF(wrapper_type);
    if (is_instance == 0) {
        Py_INCREF(args);
        return args;
    } else if (is_instance == -1) {
        return NULL;
    }
    if ((arg0 = PyObject_GetAttrString(arg0, "frame")) == NULL)
        return NULL;
    if ((result = PyTuple_New(len)) == NULL) {
        Py_DECREF(arg0);
        return NULL;
    }
    if (PyTuple_SetItem(result, 0, arg0)) { /* steals ref to arg0 */
        Py_DECREF(arg0);
        Py_DECREF(result);
        return NULL;
    }
    for (i=1; i<len; i++) {
        if ((arg0 = PyTuple_GetItem(args, i)) == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        /* arg0 is a borrowed reference */
        Py_INCREF(arg0);
        if (PyTuple_SetItem(result, i, arg0)) { /* steals ref to arg0 */
            Py_DECREF(arg0);
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

static struct PyMethodDef _new_methoddef[] = {
    {"__new__", (PyCFunction)_new_wrapper, METH_VARARGS | METH_KEYWORDS,
     PyDoc_STR("T.__new__(S, ...) -> "
               "a new object with type S, a subtype of T.__base__")},
    {0}
};

static int init_type(PyTypeObject *t, int (*initchain)(void))
{
    PyMethodDescrObject *reduce;
    PyWrapperDescrObject *init;
    PyObject *func;
    const char *name = strrchr(t->tp_name, '.')+1;

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
    PyDescr_TYPE(init) = t->tp_base;
    reduce = (PyMethodDescrObject *) PyDict_GetItemString(t->tp_dict,
                                "__reduce__");
    PyDescr_TYPE(reduce) = t->tp_base;
    /* insert the __new__ replacement which is special */
    func = PyCFunction_New(_new_methoddef, (PyObject *)t);
    if (func == NULL || PyDict_SetItemString(t->tp_dict, "__new__", func))
        return -1;
    if (initchain != NULL)
        return initchain();
    return 0;
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

    message = PyUnicode_FromFormat("cannot execute invalid frame with "
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
    SLP_STORE_NEXT_FRAME(tstate, f->f_back);
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
        || (nameobj = PyUnicode_FromString(name)) == NULL
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
            _PyUnicode_AsString(exec_name), type->tp_name);
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
                      PyDict_GetItemString(Py_TYPE(f)->tp_dict, "_exec_map");
    PyObject *dic = dp ? dp->dict : NULL;
    PyObject *exec_addr = PyLong_FromVoidPtr(f->f_execute);

    assert(valid != NULL);

    if (exec_addr == NULL) return NULL;
    exec_name = dic ? PyDict_GetItem(dic, exec_addr) : NULL;
    if (exec_name == NULL) {
        char msg[500];
        PyErr_Clear();
        sprintf(msg, "frame exec function at %p is not registered!",
            (void *)f->f_execute);
        PyErr_SetString(PyExc_ValueError, msg);
        *valid = 0;
    }
    else {
        PyFrame_ExecFunc *good, *bad;
        if (slp_find_execfuncs(Py_TYPE(f), exec_name, &good, &bad)) {
            exec_name = NULL;
            goto err_exit;
        }
        if (f->f_execute == bad)
            *valid = 0;
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
            PyObject *pos = PyLong_FromSsize_t(i);
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
        if (PyLong_CheckExact(pos)) {
            int p = PyLong_AS_LONG(pos);
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

#define codetuplefmt "iiiiiSOOOSSiSOO"

static struct _typeobject wrap_PyCode_Type;

static PyObject *
code_reduce(PyCodeObject * co)
{
    PyObject *tup = Py_BuildValue(
        "(O(" codetuplefmt ")())",
        &wrap_PyCode_Type,
        co->co_argcount,
        co->co_kwonlyargcount,
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
    return init_type(&wrap_PyCode_Type, initchain);
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
        Py_TYPE(ob) = type;
    return ob;
}

/* note that args is a tuple, although we use METH_O */

static PyObject *
cell_setstate(PyObject *self, PyObject *args)
{
    PyCellObject *cell = (PyCellObject *) self;
    PyObject *ob = NULL;

    if (is_wrong_type(Py_TYPE(self))) return NULL;
    if (!PyArg_ParseTuple (args, "|O", &ob))
        return NULL;
    Py_XINCREF(ob);
    Py_CLEAR(cell->ob_ref);
    cell->ob_ref = ob;
    Py_INCREF(self);
    Py_TYPE(self) = Py_TYPE(self)->tp_base;
    return self;
}

MAKE_WRAPPERTYPE(PyCell_Type, cell, "cell", cell_reduce, cell_new, cell_setstate)

static int init_celltype(void)
{
    return init_type(&wrap_PyCell_Type, initchain);
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
                Py_TYPE(ob) = type;
    Py_XDECREF(co);
    Py_XDECREF(globals);
    return ob;
}

#define COPY(src, dest, attr) Py_XINCREF(src->attr); Py_CLEAR(dest->attr); \
                              dest->attr = src->attr

static PyObject *
func_setstate(PyObject *self, PyObject *args)
{
    PyFunctionObject *fu;
    PyObject *args2;

    if (is_wrong_type(Py_TYPE(self))) return NULL;
    Py_TYPE(self) = Py_TYPE(self)->tp_base;
    args2 = PyTuple_GetSlice(args, 0, 5);
    if (args2 == NULL)
        return NULL;
    fu = (PyFunctionObject *)
        Py_TYPE(self)->tp_new(Py_TYPE(self), args2, NULL);
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
    return init_type(&wrap_PyFunction_Type, initchain);
}
#undef initchain
#define initchain init_functype


/******************************************************

  pickling addition to frame objects

 ******************************************************/

#define frametuplefmt "O)(OiSOiOOiiOO"

DEF_INVALID_EXEC(eval_frame)
DEF_INVALID_EXEC(eval_frame_value)
DEF_INVALID_EXEC(eval_frame_noval)
DEF_INVALID_EXEC(eval_frame_iter)
DEF_INVALID_EXEC(eval_frame_setup_with)
DEF_INVALID_EXEC(eval_frame_with_cleanup)
DEF_INVALID_EXEC(channel_seq_callback)
DEF_INVALID_EXEC(slp_restore_tracing)
DEF_INVALID_EXEC(slp_tp_init_callback)

static PyTypeObject wrap_PyFrame_Type;

static PyObject *
frameobject_reduce(PyFrameObject *f)
{
    int i;
    PyObject **f_stacktop;
    PyObject *blockstack_as_tuple = NULL, *localsplus_as_tuple = NULL,
    *res = NULL, *exec_name = NULL;
    int valid = 1;
    int have_locals = f->f_locals != NULL;
    PyObject * dummy_locals = NULL;
    PyObject * f_trace = NULL;

    if (!have_locals)
        if ((dummy_locals = PyDict_New()) == NULL)
            return NULL;

    if ((exec_name = slp_find_execname(f, &valid)) == NULL)
        return NULL;

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
        localsplus_as_tuple = slp_into_tuple_with_nulls(
            f->f_localsplus, f->f_valuestack - f->f_localsplus);
        if (localsplus_as_tuple == NULL) goto err_exit;

        /* frames without a stacktop cannot be run */
        valid = 0;
    }

    f_trace = f->f_trace;
    if (NULL == f_trace)
        f_trace = Py_None;
    Py_INCREF(f_trace);
    if (f_trace != Py_None) {
        int with_trace_func = slp_pickle_with_tracing_state();
        if (-1 == with_trace_func) goto err_exit;
        if (!with_trace_func) {
            Py_DECREF(f_trace);
            f_trace = Py_None;
            Py_INCREF(f_trace);
        }
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
                         f_trace,
                 f->f_lasti,
                 f->f_lineno,
                 blockstack_as_tuple,
                 localsplus_as_tuple
                 );

err_exit:
    Py_XDECREF(exec_name);
    Py_XDECREF(blockstack_as_tuple);
    Py_XDECREF(localsplus_as_tuple);
    Py_XDECREF(dummy_locals);
    Py_XDECREF(f_trace);
    return res;
}

#define frametuplenewfmt "O!"
#define frametuplesetstatefmt "O!iUO!iO!OiiO!O:frame_new"

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
    if (SLP_CURRENT_FRAME(ts) != NULL && PyFrame_Check(SLP_CURRENT_FRAME(ts))) {
        globals = SLP_CURRENT_FRAME(ts)->f_globals;
        Py_INCREF(globals);
    }
    else
        globals = Py_BuildValue("{sO}", "__builtins__",
                                PyEval_GetBuiltins());
    if (globals == NULL)
        return NULL;
    f = PyFrame_New(ts, (PyCodeObject *) f_code, globals, globals);
    assert(f->f_execute == NULL); /* frame is not executable */
    if (f != NULL)
        Py_TYPE(f) = &wrap_PyFrame_Type;
    Py_DECREF(globals);
    return (PyObject *) f;
}


static PyObject *
frame_setstate(PyFrameObject *f, PyObject *args)
{
    int f_lasti, f_lineno;
    Py_ssize_t i;
    PyObject *f_globals, *f_locals, *blockstack_as_tuple;
    PyObject *localsplus_as_tuple, *trace, *f_code;
    PyObject *exec_name = NULL;
    PyFrame_ExecFunc *good_func, *bad_func;
    int valid, have_locals;
    Py_ssize_t tmp;

    if (is_wrong_type(Py_TYPE(f))) return NULL;

    Py_CLEAR(f->f_globals);
    Py_CLEAR(f->f_locals);

    if (!PyArg_ParseTuple (args, frametuplesetstatefmt,
                           &PyCode_Type, &f_code,
                           &valid,
                           &exec_name,
                           &PyDict_Type, &f_globals,
                           &have_locals,
                           &PyDict_Type, &f_locals,
                           &trace,
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
    if (slp_find_execfuncs(Py_TYPE(f)->tp_base, exec_name, &good_func,
                           &bad_func))
        return NULL;

    Py_CLEAR(f->f_locals);
    Py_CLEAR(f->f_globals);

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
        assert(f->f_trace == NULL);
        f->f_trace = trace;
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
        Py_ssize_t ncells, nfreevars;

        f->f_stacktop = NULL;
        valid = 0;  /* cannot run frame without stack */

        /* Now handle pickles without localsplus in case of cell variables.
           Python requires that localsplus[index_of_a_cellvar] is a cell object.
           Otherwise PyFrame_FastToLocals() crashes in frameobject.c: map_to_dict */
        ncells = PyTuple_GET_SIZE(f->f_code->co_cellvars);
        nfreevars = PyTuple_GET_SIZE(f->f_code->co_freevars);
        if (ncells || nfreevars) {
            PyObject **values;

            assert(f->f_code->co_nlocals + ncells + nfreevars == f->f_valuestack - f->f_localsplus);

            values = f->f_localsplus + f->f_code->co_nlocals;
            for(i = ncells + nfreevars; --i >= 0; ) {
                if (values[i] == NULL) {
                    values[i] = PyCell_New(NULL);  /* uninitialised variable */
                } else {
                    assert(PyCell_Check(values[i]));
                }
            }
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "stack must be tuple or None for frame");
        goto err_exit;
    }

    /* mark this frame as coming from unpickling */
    Py_INCREF(Py_None);
    Py_CLEAR(f->f_back);
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

    /* See if this frame is valid to be run. */
    f->f_execute = valid ? good_func : bad_func;

    Py_TYPE(f) = &PyFrame_Type;
    Py_INCREF(f);
    return (PyObject *) f;
err_exit:
    /* Make sure that the frame is not executable. */
    f->f_execute = NULL;
    /* Clear members that could leak. */
    PyFrame_Type.tp_clear((PyObject*)f);

    return NULL;
}

PyFrameObject *
slp_clone_frame(PyFrameObject *f)
{
    PyObject *tup, *func, *args;
    PyFrameObject *fnew;

    if (PyFrame_Check(f))
        tup = frameobject_reduce(f);
    else
        tup = PyObject_CallMethod((PyObject *) f, "__reduce__", "");
    if (tup == NULL)
        return NULL;
    if (!PyTuple_Check(tup)) {
        PyErr_SetNone(PyExc_TypeError);
        goto error;
    }
    if (PyTuple_GET_SIZE(tup) < 2) {
        PyErr_SetNone(PyExc_ValueError);
        goto error;
    }
    func = PyTuple_GET_ITEM(tup, 0);
    args = PyTuple_GET_ITEM(tup, 1);
    fnew = (PyFrameObject *) PyObject_CallObject(func, args);
    if (fnew == NULL) {
        goto error;
    }
    if (PyTuple_GET_SIZE(tup) >= 3) {
        PyObject *tmp;
        args = PyTuple_GET_ITEM(tup, 2);
        tmp = PyObject_CallMethod((PyObject *) fnew, "__setstate__", "(O)", args);
        Py_XDECREF(tmp);
        if (tmp == NULL) {
            Py_DECREF(fnew);
            fnew = NULL;
        }
    }
    Py_DECREF(tup);
    return fnew;
error:
    Py_DECREF(tup);
    return NULL;
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
    Py_CLEAR(f->f_back);
    return f;
}

MAKE_WRAPPERTYPE(PyFrame_Type, frame, "frame", frameobject_reduce, frame_new, frame_setstate)

static int init_frametype(void)
{
    return slp_register_execute(&PyFrame_Type, "eval_frame",
                             PyEval_EvalFrameEx_slp, REF_INVALID_EXEC(eval_frame))
        || slp_register_execute(&PyFrame_Type, "eval_frame_value",
                             slp_eval_frame_value, REF_INVALID_EXEC(eval_frame_value))
        || slp_register_execute(&PyFrame_Type, "eval_frame_noval",
                             slp_eval_frame_noval, REF_INVALID_EXEC(eval_frame_noval))
        || slp_register_execute(&PyFrame_Type, "eval_frame_iter",
                             slp_eval_frame_iter, REF_INVALID_EXEC(eval_frame_iter))
        || slp_register_execute(&PyFrame_Type, "eval_frame_setup_with",
                             slp_eval_frame_setup_with, REF_INVALID_EXEC(eval_frame_setup_with))
        || slp_register_execute(&PyFrame_Type, "eval_frame_with_cleanup",
                             slp_eval_frame_with_cleanup, REF_INVALID_EXEC(eval_frame_with_cleanup))
        || slp_register_execute(&PyCFrame_Type, "channel_seq_callback",
                             channel_seq_callback, REF_INVALID_EXEC(channel_seq_callback))
        || slp_register_execute(&PyCFrame_Type, "slp_restore_tracing",
                             slp_restore_tracing, REF_INVALID_EXEC(slp_restore_tracing))
        || slp_register_execute(&PyCFrame_Type, "slp_tp_init_callback",
                             slp_tp_init_callback, REF_INVALID_EXEC(slp_tp_init_callback))
        || init_type(&wrap_PyFrame_Type, initchain);
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

typedef PyTracebackObject tracebackobject;

static PyTypeObject wrap_PyTraceBack_Type;

static PyObject *
tb_reduce(tracebackobject * tb)
{
    PyObject *tup = NULL;
    PyObject *frame_reducer;
    const char *fmt = "(O()(OiiO))";

    if (tb->tb_next == NULL)
        fmt = "(O()(Oii))";
    frame_reducer = slp_reduce_frame(tb->tb_frame);
    if (frame_reducer == NULL)
        return NULL;

    tup = Py_BuildValue(fmt,
                        &wrap_PyTraceBack_Type,
                        frame_reducer, tb->tb_lasti, tb->tb_lineno, tb->tb_next);
    Py_DECREF(frame_reducer);
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
        Py_TYPE(tb) = type;
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

    if (is_wrong_type(Py_TYPE(tb))) return NULL;

    if ((args = unwrap_frame_arg(args)) == NULL)  /* now args is a counted ref! */
        return NULL;

    if (!PyArg_ParseTuple(args,
                          "O!ii|O!:traceback",
                          &PyFrame_Type, &frame,
                          &lasti, &lineno,&PyTraceBack_Type, &next)) {
        Py_DECREF(args);
        return NULL;
    }
    Py_DECREF(args);

    Py_XINCREF(next);
    tb->tb_next = next;
    Py_XINCREF(frame);
    tb->tb_frame = frame;
    tb->tb_lasti = lasti;
    tb->tb_lineno = lineno;
    Py_TYPE(tb) = Py_TYPE(tb)->tp_base;

    if (frame != NULL && next != NULL && next->tb_frame != NULL &&
        (PyObject *)(next->tb_frame->f_back) == Py_None) {
        /* Reconstruct the f_back chain as far as possible. */
        next->tb_frame->f_back = frame;
        Py_INCREF(frame);
        Py_DECREF(Py_None);
    }

    Py_INCREF(self);
    return self;
}

MAKE_WRAPPERTYPE(PyTraceBack_Type, tb, "traceback", tb_reduce, tb_new, tb_setstate)

static int init_tracebacktype(void)
{
    return init_type(&wrap_PyTraceBack_Type, initchain);
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
    const char *name = PyModule_GetName(m);

    if (name == NULL) return NULL;

    /* is this module maybe not imported? */
    if (PyDict_GetItemString(modules, name) == NULL)
        return Py_BuildValue("(O(s)O)",
                             &wrap_PyModule_Type,
                             PyModule_GetName(m),
                             PyModule_GetDict(m));

    if (import == NULL) {
        import = run_script("import importlib\n" "ret = importlib.import_module\n", "ret");
        if (import == NULL)
            return NULL;
    }
    return Py_BuildValue("O(s)", import, name);
    /* would be shorter, but the search result is quite arbitrary:
        tup = PyObject_GetAttrString(m, "__name__");
     */
}

typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
    struct PyModuleDef *md_def;
    void *md_state;
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
    return init_type(&wrap_PyModule_Type, initchain);
}
#undef initchain
#define initchain init_moduletype



/******************************************************

  pickling of dictviews
  TODO: Is this really necessary?  Native 3.3 seems to get by without it.

 ******************************************************/


static PyTypeObject wrap_PyDictKeys_Type;

static PyObject *
dictview_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *inst;
    PyObject *dict;
    Py_ssize_t size = PyTuple_Size(args);

    if (size == -1) {
        return NULL;
    }
    if (size != 1) {
        PyErr_SetString(PyExc_ValueError, "args must contain exactly one argument");
        return NULL;
    }
    dict = PyTuple_GetItem(args, 0);
    if (dict == NULL)
        return NULL;
    inst = _PyDictView_New(dict, type->tp_base);
    if (inst != NULL)
        Py_TYPE(inst) = type;
    return (PyObject *)inst;
}

static PyObject *
dictkeysview_reduce(_PyDictViewObject *di)
{
    PyObject *tup;

    assert(di != NULL);
    tup = Py_BuildValue("(O(O)" NO_STATE_FORMAT ")",
                &wrap_PyDictKeys_Type,
                di->dv_dict
                NO_STATE_ARG);
    return tup;
}

static PyTypeObject wrap_PyDictValues_Type;

static PyObject *
dictvaluesview_reduce(_PyDictViewObject *di)
{
    PyObject *tup;

    assert(di != NULL);
    tup = Py_BuildValue("(O(O)" NO_STATE_FORMAT ")",
                &wrap_PyDictValues_Type,
                di->dv_dict
                NO_STATE_ARG);
    return tup;
}

static PyTypeObject wrap_PyDictItems_Type;

static PyObject *
dictitemsview_reduce(_PyDictViewObject *di)
{
    PyObject *tup;

    assert(di != NULL);
    tup = Py_BuildValue("(O(O)" NO_STATE_FORMAT ")",
                &wrap_PyDictItems_Type,
                di->dv_dict
                NO_STATE_ARG);
    return tup;
}

MAKE_WRAPPERTYPE(PyDictKeys_Type, dictkeysview, "dict_keys",
         dictkeysview_reduce, dictview_new, generic_setstate)

static int init_dictkeysviewtype(void)
{
    return init_type(&wrap_PyDictKeys_Type, initchain);
}
#undef initchain
#define initchain init_dictkeysviewtype

MAKE_WRAPPERTYPE(PyDictValues_Type, dictvaluesview, "dict_values",
         dictvaluesview_reduce, dictview_new, generic_setstate)

static int init_dictvaluesviewtype(void)
{
    return init_type(&wrap_PyDictValues_Type, initchain);
}
#undef initchain
#define initchain init_dictvaluesviewtype

MAKE_WRAPPERTYPE(PyDictItems_Type, dictitemsview, "dict_items",
         dictitemsview_reduce, dictview_new, generic_setstate)

static int init_dictitemsviewtype(void)
{
    return init_type(&wrap_PyDictItems_Type, initchain);
}
#undef initchain
#define initchain init_dictitemsviewtype



/******************************************************

  pickling of generators and coroutines

 ******************************************************/

static PyTypeObject wrap_PyGen_Type;
static PyTypeObject wrap_PyCoro_Type;

/* Used to initialize a generator created by gen_new. */
static PyFrameObject *gen_exhausted_frame = NULL;

/* A helper for pickling the _PyErr_StackItem* members of generator-like and tasklet
 * objects. This method returns a pointer to the object, that contains the
 * that contains *exc_info.
 */
PyObject *
slp_get_obj_for_exc_state(_PyErr_StackItem *exc_info) {
    PyObject *result;
    assert(exc_info);
    Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyGenObject, gi_exc_state));
    Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyCoroObject, cr_exc_state));
    Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyAsyncGenObject, ag_exc_state));

    result = (PyObject *)(((char *)exc_info) - offsetof(PyTaskletObject, exc_state));
    assert(PyTasklet_Check(result) ||
            PyGen_Check(result) ||
            PyObject_TypeCheck(result, &PyCoro_Type) ||
            PyObject_TypeCheck(result, &PyAsyncGen_Type));
    Py_INCREF(result);
    return result;
}

/* The usual reduce method.
 * Also assert at compile time, that the size of generator and coroutines is
 * equal.
 */
static PyObject *
_gen_reduce(PyTypeObject *type, PyGenObject *gen)
{
    PyObject *tup;
    PyObject *frame_reducer = (PyObject *)gen->gi_frame;
    PyObject *exc_type, *exc_value, *exc_traceback, *exc_info_obj;
    Py_BUILD_ASSERT(sizeof(PyGenObject) == sizeof(PyCoroObject));

    if (frame_reducer == NULL) {
        /* Pickle NULL as None. See gen_setstate() for the corresponding
         * unpickling code. */
        Py_INCREF(Py_None);
        frame_reducer = Py_None;
    } else {
        frame_reducer = slp_reduce_frame(gen->gi_frame);
    }
    if (frame_reducer == NULL)
        return NULL;

    if (gen->gi_exc_state.previous_item != NULL) {
        assert(gen->gi_exc_state.previous_item != &(PyThreadState_GET()->exc_state));
        exc_info_obj = slp_get_obj_for_exc_state(gen->gi_exc_state.previous_item);
        if (exc_info_obj == NULL)
            return NULL;
        assert(exc_info_obj != Py_None);
    } else {
        Py_INCREF(Py_None);
        exc_info_obj = Py_None;
    }

    exc_type = gen->gi_exc_state.exc_type;
    exc_value = gen->gi_exc_state.exc_value;
    exc_traceback = gen->gi_exc_state.exc_traceback;
    if (exc_type == NULL) exc_type = Py_None;
    if (exc_value == NULL) exc_value = Py_None;
    if (exc_traceback == NULL) exc_traceback = Py_None;
    Py_INCREF(exc_type);
    Py_INCREF(exc_value);
    Py_INCREF(exc_traceback);

    tup = Py_BuildValue("(O()(OiOOOOOO))",
                        type,
                        frame_reducer,
                        gen->gi_running,
                        gen->gi_name,
                        gen->gi_qualname,
                        exc_type,
                        exc_value,
                        exc_traceback,
                        exc_info_obj
                        );
    Py_DECREF(frame_reducer);
    Py_DECREF(exc_info_obj);
    Py_DECREF(exc_type);
    Py_DECREF(exc_value);
    Py_DECREF(exc_traceback);
    return tup;
}

static PyObject *
gen_reduce(PyGenObject *gen)
{
    return _gen_reduce(&wrap_PyGen_Type, gen);
}

static PyObject *
gen_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyGenObject *gen;
    if (is_wrong_type(type)) return NULL;

    /* A reference to frame is stolen by PyGen_New. */
    assert(gen_exhausted_frame != NULL);
    assert(PyFrame_Check(gen_exhausted_frame));
    if (type == &wrap_PyGen_Type) {
        gen = (PyGenObject *)PyGen_NewWithQualName(slp_ensure_new_frame(gen_exhausted_frame), NULL, NULL);
    }
    else {
        assert(type == &wrap_PyCoro_Type);
        gen = (PyGenObject *)PyCoro_New(slp_ensure_new_frame(gen_exhausted_frame), NULL, NULL);
    }
    if (gen == NULL)
        return NULL;
    Py_TYPE(gen) = type;
    return (PyObject *) gen;
}

static PyObject *
gen_setstate(PyObject *self, PyObject *args)
{
    PyGenObject *gen = (PyGenObject *) self;
    PyFrameObject *f;
    PyObject *obj;
    int gi_running;
    PyObject *name = NULL;
    PyObject *qualname = NULL;
    PyObject *exc_type, *exc_value, *exc_traceback;
    PyObject *old_type, *old_value, *old_traceback;
    PyObject *exc_info_obj;

    if (is_wrong_type(Py_TYPE(self))) return NULL;

    if ((args = unwrap_frame_arg(args)) == NULL)  /* now args is a counted ref! */
        return NULL;

    if (!PyArg_ParseTuple(args, "OiOOOOOO:generator",
                          &obj,
                          &gi_running,
                          &name,
                          &qualname,
                          &exc_type,
                          &exc_value,
                          &exc_traceback,
                          &exc_info_obj)) {
        Py_DECREF(args);
        return NULL;
    }

    if (obj == Py_None) {
        /* No frame, generator is exhausted */
        Py_CLEAR(gen->gi_frame);

        /* Even if gi_frame is NULL, gi_code is still valid. Therefore
         * I set it to the code of the exhausted frame singleton. 
         */
        assert(gen_exhausted_frame != NULL);
        assert(PyFrame_Check(gen_exhausted_frame));
        obj = (PyObject *)gen_exhausted_frame->f_code;
        Py_INCREF(obj);
        Py_SETREF(gen->gi_code, obj);

        gen->gi_running = gi_running;
        if (name == NULL) {
            name = gen_exhausted_frame->f_code->co_name;
            assert(name != NULL);
        }
        if (qualname == NULL) {
            qualname = name;
        }
        Py_INCREF(name);
        Py_SETREF(gen->gi_name, name);
        Py_INCREF(qualname);
        Py_SETREF(gen->gi_qualname, qualname);
        Py_TYPE(gen) = Py_TYPE(gen)->tp_base;
        Py_INCREF(gen);
        Py_DECREF(args); /* holds the obj ref */
        return (PyObject *)gen;
    }
    if (!PyFrame_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,
                        "invalid frame object for gen_setstate");
        Py_DECREF(args); /* holds the obj ref */
        return NULL;
    }
    f = (PyFrameObject *)obj;
    obj = NULL;

    old_type = gen->gi_exc_state.exc_type;
    old_value = gen->gi_exc_state.exc_value;
    old_traceback = gen->gi_exc_state.exc_traceback;
    if (exc_type != Py_None) {
        Py_INCREF(exc_type);
        gen->gi_exc_state.exc_type = exc_type;
    } else
        gen->gi_exc_state.exc_type = NULL;
    if (exc_value != Py_None) {
        Py_INCREF(exc_value);
        gen->gi_exc_state.exc_value = exc_value;
    } else
        gen->gi_exc_state.exc_value = NULL;
    if (exc_traceback != Py_None) {
        Py_INCREF(exc_traceback);
        gen->gi_exc_state.exc_traceback = exc_traceback;
    } else
        gen->gi_exc_state.exc_value = NULL;
    assert(gen->gi_exc_state.previous_item == NULL);
    if (exc_info_obj == Py_None) {
        gen->gi_exc_state.previous_item = NULL;
    } else {
        /* Check the preconditions for the next assinment.
         *
         * The cast in the assignment is OK, because all possible concrete types of exc_info_obj
         * have the exec_state member at the same offset.
         */
        assert(PyTasklet_Check(exc_info_obj) ||
                PyGen_Check(exc_info_obj) ||
                PyObject_TypeCheck(exc_info_obj, &PyCoro_Type) ||
                PyObject_TypeCheck(exc_info_obj, &PyAsyncGen_Type));
        Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyGenObject, gi_exc_state));
        Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyCoroObject, cr_exc_state));
        Py_BUILD_ASSERT(offsetof(PyTaskletObject, exc_state) == offsetof(PyAsyncGenObject, ag_exc_state));
        /* Make sure, that *exc_info_obj stays alive after Py_DECREF(args) at the end of this function.
         */
        assert(Py_REFCNT(exc_info_obj) > 1);
        gen->gi_exc_state.previous_item = &(((PyGenObject *)exc_info_obj)->gi_exc_state);
    }

    if (!gi_running) {
        if ((f = slp_ensure_new_frame(f)) != NULL) {
            /* use a second one for late initialization */
            PyGenObject *tmpgen;
            /* PyGenerator_New eats an existing reference */
            if ((tmpgen = (PyGenObject *)
                    PyGen_NewWithQualName(f, NULL, NULL)) == NULL) {
                Py_DECREF(f);
                Py_DECREF(args); /* holds the frame and name refs */
                return NULL;
            }
            Py_INCREF(f);
            Py_SETREF(gen->gi_frame, f);
            Py_INCREF(f->f_code);
            Py_SETREF(gen->gi_code, (PyObject *)f->f_code);
            Py_INCREF(name);
            Py_SETREF(gen->gi_name, name);
            Py_INCREF(qualname);
            Py_SETREF(gen->gi_qualname, qualname);
            /* The frame the temporary generator references
               will have GeneratorExit raised on it, when the
               temporary generator is torn down.  So clearing
               the frame from the temporary generator before
               discarding it, will save the frame for later. */
            Py_CLEAR(((PyGenObject *)tmpgen)->gi_frame);
            Py_DECREF(tmpgen);
            Py_INCREF(gen);
            Py_TYPE(gen) = Py_TYPE(gen)->tp_base;
        }
        else
            gen = NULL;
        Py_DECREF(args); /* holds the frame and name refs */
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
    Py_SETREF(gen->gi_frame, f);
    Py_INCREF(f->f_code);
    Py_SETREF(gen->gi_code, (PyObject *)f->f_code);
    gen->gi_running = gi_running;
    Py_INCREF(name);
    Py_SETREF(gen->gi_name, name);
    Py_INCREF(qualname);
    Py_SETREF(gen->gi_qualname, qualname);
    Py_TYPE(gen) = Py_TYPE(gen)->tp_base;
    Py_INCREF(gen);
    Py_DECREF(args); /* holds the frame and name refs */
    Py_XDECREF(old_type);
    Py_XDECREF(old_value);
    Py_XDECREF(old_traceback);
    return (PyObject *)gen;
}

MAKE_WRAPPERTYPE(PyGen_Type, gen, "generator", gen_reduce,
                 gen_new, gen_setstate)

DEF_INVALID_EXEC(gen_iternext_callback)

static int init_generatortype(void)
{
    int res;
    PyGenObject *gen = (PyGenObject *) run_script(
        "def exhausted_generator(): yield 42\n"   /* define a generator */
        "g = exhausted_generator()\n"             /* instanciate it */
        "g.__next__()\n", "g");                       /* force callback frame creation */
    PyFrameObject *cbframe;

    if (gen == NULL || gen->gi_frame->f_back == NULL)
        return -1;
    cbframe = gen->gi_frame->f_back;
    res = slp_register_execute(Py_TYPE(cbframe), "gen_iternext_callback",
              gen->gi_frame->f_back->f_execute,
              REF_INVALID_EXEC(gen_iternext_callback))
          || init_type(&wrap_PyGen_Type, initchain);

    assert(gen_exhausted_frame == NULL);
    gen_exhausted_frame = slp_ensure_new_frame(gen->gi_frame);
    if (gen_exhausted_frame == NULL) {
        return -1;
    }

    Py_DECREF(gen);
    return res;
}
#undef initchain
#define initchain init_generatortype

static PyObject *
coro_reduce(PyGenObject *gen)
{
    return _gen_reduce(&wrap_PyCoro_Type, gen);
}

MAKE_WRAPPERTYPE(PyCoro_Type, coro, "coroutine", coro_reduce,
    gen_new, gen_setstate)

static int init_coroutinetype(void)
{
    return init_type(&wrap_PyCoro_Type, initchain);
}
#undef initchain
#define initchain init_coroutinetype


/******************************************************

  support code for module dict pickling

 ******************************************************/

PyObject *
PyStackless_Pickle_ModuleDict(PyObject *pickler, PyObject *self)
{
    PyObject *modict, *retval = NULL, *valuesview = NULL, *values = NULL;
    PyObject *mod = NULL, *id = NULL;

    modict = PyObject_GetAttrString(pickler, "module_dict_ids");
    if (modict == NULL) {
        PyObject *modules, *dict;

        PyErr_Clear();
        if ((modict = PyDict_New()) == NULL) goto finally;
        modules = PyImport_GetModuleDict();
        valuesview = PyDict_Values(modules);
        values = PyObject_CallMethod(valuesview, "__iter__", "()");
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
    Py_XDECREF(valuesview);
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
static int
_wrapmodule_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(gen_exhausted_frame);
    Py_VISIT(reduce_frame_func);
    return 0;
}

static int
_wrapmodule_clear(PyObject *self)
{
    Py_CLEAR(gen_exhausted_frame);
    Py_CLEAR(reduce_frame_func);
    return 0;
}

static PyMethodDef _wrapmodule_methods[] = {
    {"set_reduce_frame", set_reduce_frame, METH_O, set_reduce_frame__doc__},
    {NULL,                          NULL}       /* sentinel */
};

static struct PyModuleDef _wrapmodule = {
    PyModuleDef_HEAD_INIT,
    "_stackless._wrap",
    NULL,
    -1,
    _wrapmodule_methods,
    NULL,
    _wrapmodule_traverse,
    _wrapmodule_clear,
    NULL
};

PyObject*
init_prickelpit(void)
{
    PyObject *tmp;

    types_mod = PyModule_Create(&_wrapmodule);
    if (types_mod == NULL)
        return NULL;
    if (initchain()) {
        Py_CLEAR(types_mod);
        return NULL;
    }
    tmp = types_mod;
    types_mod = NULL;
    return tmp;
}

#endif
