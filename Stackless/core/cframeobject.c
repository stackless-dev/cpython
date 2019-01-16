/******************************************************

  The CFrame

 ******************************************************/

/*
 * The purpose of a CFrame is to allow any callable to be run as
 * a tasklet.
 * A CFrame does not appear in tracebacks, but it does
 * play a role in frame chains.
 *
 * For simplicity, it mimicks the fields which slp_transfer needs
 * to do a proper switch, and the standard frame fields have
 * been slightly rearranged, to keep the size of CFrame small.
 * I have looked through all reachable C extensions to find out
 * which fields need to be present.
 *
 * The tasklet holds either a frame or a cframe and knows
 * how to handle them.
 *
 * XXX in a later, thunk-based implementation, CFrames should
 * vanish and be replaced by special thunks, which don't mess
 * with the frame chain.
 */

#include "Python.h"
#include "structmember.h"

#ifdef STACKLESS
#include "internal/stackless_impl.h"
#include "internal/slp_prickelpit.h"

static PyCFrameObject *free_list = NULL;
static int numfree = 0;         /* number of cframes currently in free_list */
#define MAXFREELIST 200         /* max value for numfree */

static void
cframe_dealloc(PyCFrameObject *cf)
{
    PyObject_GC_UnTrack(cf);
    Py_XDECREF(cf->f_back);
    Py_XDECREF(cf->ob1);
    Py_XDECREF(cf->ob2);
    Py_XDECREF(cf->ob3);
    if (numfree < MAXFREELIST) {
        ++numfree;
        cf->f_back = (PyFrameObject *) free_list;
        free_list = cf;
    }
    else
        Py_TYPE(cf)->tp_free((PyObject*)cf);
}

static int
cframe_traverse(PyCFrameObject *cf, visitproc visit, void *arg)
{
    Py_VISIT(cf->f_back);
    Py_VISIT(cf->ob1);
    Py_VISIT(cf->ob2);
    Py_VISIT(cf->ob3);
    return 0;
}

/* clearing a cframe while the object still exists */

static void
cframe_clear(PyCFrameObject *cf)
{
    /* The Python C-API documentation recomends to use Py_CLEAR() to release
     * references held by container objects. The following code is an unrolled
     * version of four Py_CLEAR() macros. It is more robust at no additional
     * costs.
     */
    PyFrameObject *tmp_f_back;
    PyObject *tmp_ob1, *tmp_ob2, *tmp_ob3;
    tmp_f_back = cf->f_back;
    tmp_ob1 = cf->ob1;
    tmp_ob2 = cf->ob2;
    tmp_ob3 = cf->ob3;
    cf->f_back = NULL;
    cf->ob1 = cf->ob2 = cf->ob3 = NULL;
    Py_XDECREF(tmp_f_back);
    Py_XDECREF(tmp_ob1);
    Py_XDECREF(tmp_ob2);
    Py_XDECREF(tmp_ob3);
}


PyCFrameObject *
slp_cframe_new(PyFrame_ExecFunc *exec, unsigned int linked)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf;
    PyFrameObject *back;

    if (free_list == NULL) {
        cf = PyObject_GC_NewVar(PyCFrameObject, &PyCFrame_Type, 0);
        if (cf == NULL)
            return NULL;
    }
    else {
        assert(numfree > 0);
        --numfree;
        cf = free_list;
        free_list = (PyCFrameObject *) free_list->f_back;
        _Py_NewReference((PyObject *) cf);
    }

    if (linked) {
        back = SLP_CURRENT_FRAME(ts);
    }
    else
        back = NULL;
    Py_XINCREF(back);
    cf->f_execute = exec;
    cf->f_back = back;
    cf->ob1 = cf->ob2 = cf->ob3 = NULL;
    cf->i = cf->n = 0;
    cf->any1 = cf->any2 = NULL;
    _PyObject_GC_TRACK(cf);
    return cf;
}

/* pickling support for cframes */

#define cframetuplefmt "iOOll"
#define cframetuplenewfmt "iOO!ll:cframe"

static PyObject * execute_soft_switchable_func(PyFrameObject *, int, PyObject *);
SLP_DEF_INVALID_EXEC(execute_soft_switchable_func)

static PyObject *
cframe_reduce(PyCFrameObject *cf)
{
    PyObject *res = NULL, *exec_name = NULL;
    PyObject *params = NULL;
    int valid = 1;

    if (cf->f_execute == execute_soft_switchable_func) {
        exec_name = (PyObject *) cf->any2;
        assert(cf->any2);
        assert(PyStacklessFunctionDeclarationType_CheckExact(exec_name));
        Py_INCREF(exec_name);
        valid = cf->any1 == NULL;
    } else if ((exec_name = slp_find_execname((PyFrameObject *) cf, &valid)) == NULL)
        return NULL;

    params = slp_into_tuple_with_nulls(&cf->ob1, 3);
    if (params == NULL) goto err_exit;

    res = Py_BuildValue ("(O()(" cframetuplefmt "))",
                         Py_TYPE(cf),
                         valid,
                         exec_name,
                         params,
                         cf->i,
                         cf->n);

err_exit:
    Py_XDECREF(exec_name);
    Py_XDECREF(params);
    return res;
}

static PyObject *
cframe_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, ":cframe", kwlist))
        return NULL;
    return (PyObject *) slp_cframe_new(NULL, 0);
}

/* note that args is a tuple, although we use METH_O */

static PyObject *
cframe_setstate(PyObject *self, PyObject *args)
{
    PyCFrameObject *cf = (PyCFrameObject *) self;
    int valid;
    PyObject *exec_name = NULL;
    PyFrame_ExecFunc *good_func, *bad_func;
    PyObject *params;
    long i, n;
    void *any2=NULL;

    if (!PyArg_ParseTuple (args, cframetuplenewfmt,
                           &valid,
                           &exec_name,
                           &PyTuple_Type, &params,
                           &i,
                           &n))
        return NULL;

    if (PyStacklessFunctionDeclarationType_CheckExact(exec_name)) {
        good_func = execute_soft_switchable_func;
        bad_func = cannot_execute_soft_switchable_func;
        any2 = exec_name; /* not ref counted */
    } else if (slp_find_execfuncs(Py_TYPE(cf), exec_name, &good_func, &bad_func))
        return NULL;

    if (PyTuple_GET_SIZE(params)-1 != 3)
        VALUE_ERROR("bad argument for cframe unpickling", NULL);

    /* mark this frame as coming from unpickling */
    Py_INCREF(Py_None);
    cf->f_back = (PyFrameObject *) Py_None;
    cf->f_execute = valid ? good_func : bad_func;
    slp_from_tuple_with_nulls(&cf->ob1, params);
    cf->i = i;
    cf->n = n;
    cf->any1 = NULL;
    cf->any2 = any2;
    Py_INCREF(cf);
    return (PyObject *) cf;
}

static PyMethodDef cframe_methods[] = {
    {"__reduce__",    (PyCFunction)cframe_reduce, METH_NOARGS, NULL},
    {"__reduce_ex__", (PyCFunction)cframe_reduce, METH_VARARGS, NULL},
    {"__setstate__",  (PyCFunction)cframe_setstate, METH_O, NULL},
    {NULL, NULL}
};


static PyObject * run_cframe(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = (PyCFrameObject*) f;
    PyTaskletObject *task = ts->st.current;
    int done = cf->i;

    SLP_SET_CURRENT_FRAME(ts, f);

    if (retval == NULL || done)
        goto exit_run_cframe;

    if (cf->ob2 == NULL)
        cf->ob2 = PyTuple_New(0);
    Py_DECREF(retval);
    STACKLESS_PROPOSE_ALL(ts);
    retval = PyObject_Call(cf->ob1, cf->ob2, cf->ob3);
    STACKLESS_ASSERT();
    cf->i = 1; /* mark ourself as done */

    if (STACKLESS_UNWINDING(retval)) {
        /* try to shortcut */
        PyFrameObject *f;  /* a borrowed ref */
        if (ts->st.current == task && (f = SLP_PEEK_NEXT_FRAME(ts)) != NULL &&
            f->f_back == (PyFrameObject *) cf) {
                Py_XINCREF(cf->f_back);
                Py_SETREF(f->f_back, cf->f_back);
        }
        return retval;
    }
    /* pop frame */
exit_run_cframe:
    SLP_STORE_NEXT_FRAME(ts, cf->f_back);
    return retval;
}

SLP_DEF_INVALID_EXEC(run_cframe)

PyCFrameObject *
slp_cframe_newfunc(PyObject *func, PyObject *args, PyObject *kwds, unsigned int linked)
{
    PyCFrameObject *cf;

    if (func == NULL || !PyCallable_Check(func))
        TYPE_ERROR("cframe function must be a callable", NULL);
    cf = slp_cframe_new(run_cframe, linked);
    if (cf == NULL)
        return NULL;
    Py_INCREF(func);
    cf->ob1 = func;
    Py_INCREF(args);
    cf->ob2 = args;
    Py_XINCREF(kwds);
    cf->ob3 = kwds;
    return cf;
}

static PyMemberDef cframe_memberlist[] = {
    {"f_back",    T_OBJECT,     offsetof(PyCFrameObject, f_back), READONLY},
    {"ob1",        T_OBJECT,    offsetof(PyCFrameObject, ob1),    READONLY},
    {"ob2",        T_OBJECT,    offsetof(PyCFrameObject, ob2),    READONLY},
    {"ob3",        T_OBJECT,    offsetof(PyCFrameObject, ob3),    READONLY},
    {"i",        T_LONG,        offsetof(PyCFrameObject, i),      READONLY},
    {"n",        T_LONG,        offsetof(PyCFrameObject, n),      READONLY},
    {NULL}  /* Sentinel */
};

PyTypeObject PyCFrame_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "_stackless.cframe",
    sizeof(PyCFrameObject),
    0,
    (destructor)cframe_dealloc,        /* tp_dealloc */
    0,                                 /* tp_print */
    0,                                 /* tp_getattr */
    0,                                 /* tp_setattr */
    0,                                 /* tp_compare */
    0,                                 /* tp_repr */
    0,                                 /* tp_as_number */
    0,                                 /* tp_as_sequence */
    0,                                 /* tp_as_mapping */
    0,                                 /* tp_hash */
    0,                                 /* tp_call */
    0,                                 /* tp_str */
    PyObject_GenericGetAttr,           /* tp_getattro */
    PyObject_GenericSetAttr,           /* tp_setattro */
    0,                                 /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, /* tp_flags */
    0,                                 /* tp_doc */
    (traverseproc)cframe_traverse,        /* tp_traverse */
    (inquiry) cframe_clear,            /* tp_clear */
    0,                                 /* tp_richcompare */
    0,                                 /* tp_weaklistoffset */
    0,                                 /* tp_iter */
    0,                                 /* tp_iternext */
    cframe_methods,                    /* tp_methods */
    cframe_memberlist,                 /* tp_members */
    0,                                 /* tp_getset */
    0,                                 /* tp_base */
    0,                                 /* tp_dict */
    0,                                 /* tp_descr_get */
    0,                                 /* tp_descr_set */
    0,                                 /* tp_dictoffset */
    0,                                 /* tp_init */
    0,                                 /* tp_alloc */
    cframe_new,                        /* tp_new */
    PyObject_GC_Del,                   /* tp_free */
};

int slp_init_cframetype(void)
{
    if (PyType_Ready(&PyCFrame_Type))
        return -1;
    if (PyType_Ready(&PyStacklessFunctionDeclaration_Type))
        return -1;
    /* register the cframe exec func */
    return slp_register_execute(&PyCFrame_Type, "run_cframe",
                                run_cframe, SLP_REF_INVALID_EXEC(run_cframe));
}

/* Clear out the free list */

void
slp_cframe_fini(void)
{
    while (free_list != NULL) {
        PyCFrameObject *cf = free_list;
        free_list = (PyCFrameObject *) free_list->f_back;
        PyObject_GC_Del(cf);
        --numfree;
    }
    assert(numfree == 0);
}


/*
 *  API for soft switchable extension functions / methods
 */

static PyObject *
function_declaration_reduce(PyStacklessFunctionDeclarationObject *self)
{
    if (self->name == NULL || *self->name == '\0') {
        PyErr_SetString(PyExc_SystemError, "no function name");
        return NULL;
    }
    return PyUnicode_FromString(self->name);
}

static PyMethodDef function_declaration_methods[] = {
    {"__reduce__",    (PyCFunction)function_declaration_reduce, METH_NOARGS, NULL},
    {"__reduce_ex__", (PyCFunction)function_declaration_reduce, METH_VARARGS, NULL},
    {NULL, NULL}
};

static PyMemberDef function_declaration_memberlist[] = {
    {"__module__", T_STRING, offsetof(PyStacklessFunctionDeclarationObject, module_name), READONLY},
    {"name",       T_STRING,    offsetof(PyStacklessFunctionDeclarationObject, name),    READONLY},
    {NULL}  /* Sentinel */
};

PyDoc_STRVAR(PyStacklessFunctionDeclaration_Type__doc__,
"SoftSwitchableFunctionDeclaration objects represent a soft switchable\n\
extension function written in C in a pickle.\n\
");

PyTypeObject PyStacklessFunctionDeclaration_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "_stackless.FunctionDeclaration",             /*tp_name*/
    sizeof(PyStacklessFunctionDeclarationObject),          /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    0,                                 /* tp_dealloc */
    0,                                 /* tp_print */
    0,                                 /* tp_getattr */
    0,                                 /* tp_setattr */
    0,                                 /* tp_compare */
    0,                                 /* tp_repr */
    0,                                 /* tp_as_number */
    0,                                 /* tp_as_sequence */
    0,                                 /* tp_as_mapping */
    0,                                 /* tp_hash */
    0,                                 /* tp_call */
    0,                                 /* tp_str */
    0,                                 /* tp_getattro */
    0,                                 /* tp_setattro */
    0,                                 /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                /* tp_flags */
    PyStacklessFunctionDeclaration_Type__doc__, /* tp_doc */
    0,                                 /* tp_traverse */
    0,                                 /* tp_clear */
    0,                                 /* tp_richcompare */
    0,                                 /* tp_weaklistoffset */
    0,                                 /* tp_iter */
    0,                                 /* tp_iternext */
    function_declaration_methods,    /* tp_methods */
    function_declaration_memberlist, /* tp_members */
};

static
PyObject *
execute_soft_switchable_func(PyFrameObject *f, int exc, PyObject *retval)
{
    /*
     * Special rule for frame execution functions: we now own a reference to retval!
     */
    PyCFrameObject *cf = (PyCFrameObject *)f;
    PyThreadState *ts = PyThreadState_GET();
    PyObject *ob1, *ob2, *ob3;
    PyStacklessFunctionDeclarationObject *ssfd =
            (PyStacklessFunctionDeclarationObject*)cf->any2;
    if (retval == NULL && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_SystemError, "retval is NULL, but no error occurred");
        SLP_STORE_NEXT_FRAME(ts, cf->f_back);
        return NULL;
    }
    assert(ssfd);
    assert(ssfd->sfunc);

    /* make sure we own refs to the arguments during the function call */
    ob1 = cf->ob1;
    ob2 = cf->ob2;
    ob3 = cf->ob3;
    Py_XINCREF(ob1);
    Py_XINCREF(ob2);
    Py_XINCREF(ob3);
    STACKLESS_PROPOSE_ALL(ts);
    /* use Py_SETREF, because we own a ref to retval. */
    Py_XSETREF(retval, ssfd->sfunc(retval, &cf->i, &cf->ob1, &cf->ob2, &cf->ob3, &cf->n, &cf->any1));
    STACKLESS_RETRACT();
    STACKLESS_ASSERT();
    Py_XDECREF(ob1);
    Py_XDECREF(ob2);
    Py_XDECREF(ob3);
    if (STACKLESS_UNWINDING(retval))
        return retval;
    SLP_STORE_NEXT_FRAME(ts, cf->f_back);
    return retval;
}

PyAPI_FUNC(int)
PyStackless_InitFunctionDeclaration(PyStacklessFunctionDeclarationObject *sfd, PyObject *module, PyModuleDef *module_def) {
    assert(sfd != NULL);
    Py_TYPE(sfd) = &PyStacklessFunctionDeclaration_Type;
    if (module_def != NULL) {
        assert(module_def->m_name);
        sfd->module_name = module_def->m_name;
    }
    if (module == NULL)
        return 0;
    Py_INCREF(sfd); /* PyModule_AddObject steals a reference. */
    return PyModule_AddObject(module, sfd->name, (PyObject *)sfd);
}

PyObject *
PyStackless_CallFunction(PyStacklessFunctionDeclarationObject *ssfd, PyObject *arg,
        PyObject *ob1, PyObject *ob2, PyObject *ob3, long n, void *any)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *et=NULL, *ev=NULL, *tb=NULL;

    assert(ssfd);
    assert(ts->st.main != NULL);

    if (arg == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "No error occurred, but arg is NULL");
            return NULL;
        }
        PyErr_Fetch(&et, &ev, &tb);
    }

    if (stackless) {
        /* Only soft-switch, if the caller can handle Py_UnwindToken.
         * If called from Python-code, this is always the case,
         * but if you call this method from a C-function, you don't know. */
        PyCFrameObject *cf;
        cf = slp_cframe_new(execute_soft_switchable_func, 1);
        if (cf == NULL) {
            _PyErr_ChainExceptions(et, ev, tb);
            return NULL;
        }
        cf->any2 = ssfd;

        Py_XINCREF(ob1);
        Py_XINCREF(ob2);
        Py_XINCREF(ob3);
        cf->ob1 = ob1;
        cf->ob2 = ob2;
        cf->ob3 = ob3;
        /* already set cf->i = 0 */
        assert(cf->i == 0);
        cf->n = n;
        cf->any1 = any;
        SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *) cf);
        Py_DECREF(cf);
        Py_XINCREF(arg);
        if (arg == NULL)
            PyErr_Restore(et, ev, tb);
        return STACKLESS_PACK(ts, arg);
    } else {
        /*
         * Call the PySoftSwitchableFunc direct
         */
        PyObject *retval, *saved_ob1, *saved_ob2, *saved_ob3;
        long step = 0;

        Py_XINCREF(arg);

        saved_ob1 = ob1;
        saved_ob2 = ob2;
        saved_ob3 = ob3;
        Py_XINCREF(saved_ob1);
        Py_XINCREF(saved_ob2);
        Py_XINCREF(saved_ob3);
        if (arg == NULL)
            PyErr_Restore(et, ev, tb);
        retval = ssfd->sfunc(arg, &step, &ob1, &ob2, &ob3, &n, &any);
        STACKLESS_ASSERT();
        assert(!STACKLESS_UNWINDING(retval));
        Py_XDECREF(saved_ob1);
        Py_XDECREF(saved_ob2);
        Py_XDECREF(saved_ob3);
        Py_XDECREF(arg);
        return retval;
    }
}

#endif
