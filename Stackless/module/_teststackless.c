/*
 *
 * Copyright 2018 Anselm Kruis <anselm.kruis@atos.net>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "Python.h"
#include "frameobject.h"
#include "stackless_api.h"
#ifdef MS_WINDOWS
#include "malloc.h" /* for alloca */
#endif

/*
 * Demo and test code for soft switchable C-functions
 */

typedef struct {
    PyObject_HEAD
} SoftSwitchableDemoObject;

static PyTypeObject SoftSwitchableDemo_Type;

#define SoftSwitchableDemoObject_Check(v)      (Py_TYPE(v) == &SoftSwitchableDemo_Type)

/* The documentation includes the following code as an example and needs
 * the next comment as marker.
 */
/*DO-NOT-REMOVE-OR-MODIFY-THIS-MARKER:ssf-example-start*/

/*
 * SoftSwitchableDemo methods
 *
 * The purpose of this demo is to test soft switchable extension methods and
 * give an example of their usage. Otherwise the demo code is very simplistic.
 */

static
PyObject *
demo_soft_switchable(PyObject *retval, long *step, PyObject **ob1,
        PyObject **ob2, PyObject **ob3, long *n, void **any)
{
    /*
     *  Boiler plate code for soft switchable functions
     */

    /* Every function, that supports the stackless-protocol starts with this
     * macro. It defines the local variable "int stackless" and moves the value
     * of the global variable "_PyStackless_TRY_STACKLESS" to "stackless".
     */
    STACKLESS_GETARG();

    /* This function returns a new reference.
     * If retval is NULL, then PyErr_Occurred() is true.
     */
    Py_XINCREF(retval);

    /*
     * Optional: define a struct for additional state
     */
    struct {
        /*
         * If *ob1, *ob2, *ob3, *n and *any are insufficient for the state of this method,
         * you can define state variables here and store this structure in *any.
         */
        int var1;  // Minimal example
    } *state = *any;

    /*
     * Specific vars for this example.
     */
    int do_schedule = *n;
    if (*step == 0 && *n >= 100)
        *step = *n; // n==100: demo for calling back to Python

    /*
     * Always present: the switch, that is used to jump to the next step.
     *
     * Of course you are not limited to a linear sequence of steps, but
     * this is simplest use case. If you think of a state machine, the variable
     * (*step) is the number of the next state to enter.
     * The initial value of (*step) is 0.
     */
    switch(*step) {
    case 0:
        (*step)++;  // set to the next step
        /*
         * Optional: initialize the state structure.
         */
        *any = state = PyMem_Calloc(1, sizeof(*state));
        if (state == NULL) {
            Py_CLEAR(retval);
            goto exit_func;
        }

        /*
         * Code specific for this example
         */
        state->var1++;  /* This example is a bit simplistic */

        /*
         * Now eventually schedule.
         */
        if (do_schedule) {
            /*
             * The function PyStackless_Schedule() supports the stackless-protocol.
             * Therefore we may call TACKLESS_PROMOTE_ALL(). This macro copies the
             * local variable "stackless" into "_PyStackless_TRY_STACKLESS".
             */
            STACKLESS_PROMOTE_ALL();  // enable a stackless call
            Py_SETREF(retval, PyStackless_Schedule(retval, do_schedule > 1));
            /*
             * In debug builds STACKLESS_ASSERT asserts, that the previously called
             * function did reset "_PyStackless_TRY_STACKLESS". If you call
             * STACKLESS_PROMOTE_ALL() (or one of its variants) prior to calling
             * a function that does not support the stackless protocol, the
             * assertion fails.
             */
            STACKLESS_ASSERT();       // sanity check in debug builds
            if (STACKLESS_UNWINDING(retval))
                return retval;
            else if (retval == NULL)
                goto exit_func;
        }
        /* no break */
    case 1:
        (*step)++;
        /*
         * Add more business logic here
         */

        state->var1++;  /* This example is a bit simplistic */
        /* now get rid of the state in *any, because it can't be pickled. */
        *n = state->var1;
        PyMem_Free(*any);
        *any = NULL;    /* only if *any is NULL, the state can be pickled. */

        /*
         * Now eventually schedule.
         */
        if (do_schedule) {
            STACKLESS_PROMOTE_ALL();  // enable a stackless call
            Py_SETREF(retval, PyStackless_Schedule(retval, do_schedule > 1));
            STACKLESS_ASSERT();       // sanity check in debug builds
            if (STACKLESS_UNWINDING(retval))
                return retval;
            else if (retval == NULL)
                goto exit_func;
        }

        /*
         * And so on ...
         */
        /* no break */
    case 2:
        /*
         * Prepare the result
         */
        Py_SETREF(retval, PyLong_FromLong(*n));
        break;

    /*
     * Demo code for calling back to Python.
     */
    case 100:
        (*step)++;  // set to the next step
        /*
         * Here we demonstrate a stackless callback into Python code.
         * This test assumes, that result is a callable object.
         *
         * The API function PyObject_Call supports the stackless protocoll.
         * Therefore we may call STACKLESS_PROMOTE_ALL();
         */
        PyObject * args = PyTuple_New(0);
        STACKLESS_PROMOTE_ALL();
        Py_SETREF(retval, PyObject_Call(retval, args, NULL));
        STACKLESS_ASSERT();
        if (STACKLESS_UNWINDING(retval))
            return retval;
        /* no break */

    case 101:
        (*step)++;  // set to the next step

        /*
         * Error handling for the callback.
         *
         * This needs a step of its own, if the callback supports the
         * stackless-protocol and fails.
         */
        if (retval == NULL) {
            assert(PyErr_Occurred());

            if (! PyErr_ExceptionMatches(PyExc_Exception))
                // it is a BaseException, don't handle it
                return NULL;

            /*
             * A very simple error handling: fetch the exception set it as
             * context of a new RuntimeError.
             */
            PyObject *et, *ev, *tb;
            PyErr_Fetch(&et, &ev, &tb);
            PyErr_SetString(PyExc_RuntimeError, "demo_soft_switchable callback failed");
            _PyErr_ChainExceptions(et, ev, tb);
        }

        break;

    default:
        /*
         * Boiler plate code: error handling
         */
        PyErr_SetString(PyExc_SystemError, "invalid state");
        Py_CLEAR(retval);
    }

exit_func:
    PyMem_Free(*any);
    *any = NULL;
    return retval;
}


static PyStacklessFunctionDeclarationObject demo_soft_switchable_declaration = {
        PyObject_HEAD_INIT(NULL)
        demo_soft_switchable,
        "demo_soft_switchable"
};

/*DO-NOT-REMOVE-OR-MODIFY-THIS-MARKER:ssf-example-end*/

static PyObject *
SoftSwitchableDemo_demo(SoftSwitchableDemoObject *self, PyObject *args)
{
    PyObject *result;
    int action;
    if (!PyArg_ParseTuple(args, "iO:demo", &action, &result))
        return NULL;

    return PyStackless_CallFunction(&demo_soft_switchable_declaration, result, NULL, NULL, NULL, action, NULL);
}


static PyMethodDef SoftSwitchableDemo_methods[] = {
    {"demo",            (PyCFunction)SoftSwitchableDemo_demo,  METH_VARARGS | METH_STACKLESS,
        PyDoc_STR("demo(action, result) -> result")},
    {NULL,              NULL}           /* sentinel */
};


static PyTypeObject SoftSwitchableDemo_Type = {
    /* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
    PyVarObject_HEAD_INIT(NULL, 0)
    "_teststackless.SoftSwitchableDemo",             /*tp_name*/
    sizeof(SoftSwitchableDemoObject),          /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    /* methods */
    0,                          /*tp_dealloc*/
    0,                          /*tp_print*/
    0,                          /*tp_getattr*/
    0,                          /*tp_setattr*/
    0,                          /*tp_reserved*/
    0,                          /*tp_repr*/
    0,                          /*tp_as_number*/
    0,                          /*tp_as_sequence*/
    0,                          /*tp_as_mapping*/
    0,                          /*tp_hash*/
    0,                          /*tp_call*/
    0,                          /*tp_str*/
    0,                          /*tp_getattro*/
    0,                          /*tp_setattro*/
    0,                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,         /*tp_flags*/
    0,                          /*tp_doc*/
    0,                          /*tp_traverse*/
    0,                          /*tp_clear*/
    0,                          /*tp_richcompare*/
    0,                          /*tp_weaklistoffset*/
    0,                          /*tp_iter*/
    0,                          /*tp_iternext*/
    SoftSwitchableDemo_methods,                /*tp_methods*/
    0,                          /*tp_members*/
    0,                          /*tp_getset*/
    0,                          /*tp_base*/
    0,                          /*tp_dict*/
    0,                          /*tp_descr_get*/
    0,                          /*tp_descr_set*/
    0,                          /*tp_dictoffset*/
    0,                          /*tp_init*/
    0,                          /*tp_alloc*/
    PyType_GenericNew,          /*tp_new*/
    0,                          /*tp_free*/
    0,                          /*tp_is_gc*/
};
/* --------------------------------------------------------------------- */

/* Function of two integers returning integer */

PyDoc_STRVAR(_teststackless_softswitchabledemo_doc, "demo(action, result) -> result");

static PyObject *
_teststackless_softswitchabledemo(PyObject *self, PyObject *args)
{
    PyObject *result;
    int action;
    if (!PyArg_ParseTuple(args, "iO:demo", &action, &result))
        return NULL;

    return PyStackless_CallFunction(&demo_soft_switchable_declaration, result, NULL, NULL, NULL, action, NULL);
}


/******************************************************

 some test functions

 ******************************************************/


PyDoc_STRVAR(test_cframe__doc__,
    "test_cframe(switches, words=0) -- a builtin testing function that does nothing\n\
but tasklet switching. The function will call PyStackless_Schedule() for switches\n\
times and then finish.\n\
If words is given, as many words will be allocated on the C stack.\n\
Usage: Create two tasklets for test_cframe and run them by run().\n\
\n\
    t1 = tasklet(test_cframe)(500000)\n\
    t2 = tasklet(test_cframe)(500000)\n\
    run()\n\
This can be used to measure the execution time of 1.000.000 switches.");

/* we define the stack max as the typical recursion limit
* times the typical number of words per recursion.
* That is 1000 * 64
*/

#define STACK_MAX_USEFUL 64000
#define STACK_MAX_USESTR "64000"

#define VALUE_ERROR(str, ret) return (PyErr_SetString(PyExc_ValueError, (str)), ret)

static
PyObject *
test_cframe(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *argnames[] = { "switches", "words", NULL };
    long switches, extra = 0;
    long i;
    PyObject *ret = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "l|l:test_cframe",
        argnames, &switches, &extra))
        return NULL;
    if (extra < 0 || extra > STACK_MAX_USEFUL)
        VALUE_ERROR("test_cframe: words are limited by 0 and " \
            STACK_MAX_USESTR, NULL);
    if (extra > 0)
        alloca(extra * sizeof(PyObject*));
    Py_INCREF(ret);
    for (i = 0; i<switches; i++) {
        Py_DECREF(ret);
        ret = PyStackless_Schedule(Py_None, 0);
        if (ret == NULL) return NULL;
    }
    return ret;
}


PyDoc_STRVAR(test_cstate__doc__,
    "test_cstate(callable) -- a builtin testing function that simply returns the\n\
result of calling its single argument.  Used for testing to force stackless\n\
into using hard switching.");

static
PyObject *
test_cstate(PyObject *f, PyObject *callable)
{
    return PyObject_CallObject(callable, NULL);
}


PyDoc_STRVAR(test_PyEval_EvalFrameEx__doc__,
    "test_PyEval_EvalFrameEx(code, globals, args=()) -- a builtin testing function.\n\
This function tests the C-API function PyEval_EvalFrameEx(), which is not used\n\
by Stackless Python.\n\
The function creates a frame from code, globals and args and executes the frame.");

static PyObject* test_PyEval_EvalFrameEx(PyObject *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = { "code", "globals", "args", "alloca", "throw", "oldcython",
        "code2", NULL };
    PyThreadState *tstate = PyThreadState_GET();
    PyCodeObject *co, *code2 = NULL;
    PyObject *globals, *co_args = NULL;
    Py_ssize_t alloca_size = 0;
    PyObject *exc = NULL;
    PyObject *oldcython = NULL;
    PyFrameObject *f;
    PyObject *result = NULL;
    void *p;
    Py_ssize_t na;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!O!|O!nOO!O!:test_PyEval_EvalFrameEx", kwlist,
        &PyCode_Type, &co, &PyDict_Type, &globals, &PyTuple_Type, &co_args, &alloca_size,
        &exc, &PyBool_Type, &oldcython, &PyCode_Type, &code2))
        return NULL;
    if (exc && !PyExceptionInstance_Check(exc)) {
        PyErr_SetString(PyExc_TypeError, "exc must be an exception instance");
        return NULL;
    }
    p = alloca(alloca_size);
    assert(globals != NULL);
    assert(tstate != NULL);
    na = PyTuple_Size(co->co_freevars);
    if (na == -1)
        return NULL;
    if (na > 0) {
        PyErr_Format(PyExc_ValueError, "code requires cell variables");
        return NULL;
    }
    f = PyFrame_New(tstate, co, globals, NULL);
    if (f == NULL) {
        return NULL;
    }
    if (co_args) {
        PyObject **fastlocals;
        Py_ssize_t i;
        na = PyTuple_Size(co_args);
        if (na == -1)
            goto exit;
        if (na > co->co_argcount) {
            PyErr_Format(PyExc_ValueError, "too many items in tuple 'args'");
            goto exit;
        }
        fastlocals = f->f_localsplus;
        if (oldcython == Py_True) {
            /* Use the f_localsplus offset from regular C-Python. Old versions of cython used to
            * access f_localplus directly. Current versions compute the field offset for
            * f_localsplus at run-time.
            */
            fastlocals--;
        }
        for (i = 0; i < na; i++) {
            PyObject *arg = PyTuple_GetItem(co_args, i);
            if (arg == NULL) {
                goto exit;
            }
            Py_INCREF(arg);
            fastlocals[i] = arg;
        }
        if (alloca_size > 0 && na > 0) {
            Py_SETREF(fastlocals[0], PyLong_FromVoidPtr(p));
        }
    }
    if (exc) {
        PyErr_SetObject(PyExceptionInstance_Class(exc), exc);
    }
    if (code2) {
        Py_INCREF(code2);
        Py_SETREF(f->f_code, code2);
    }
    result = PyEval_EvalFrameEx(f, exc != NULL);
    /* result = Py_None; Py_INCREF(Py_None); */
exit:
    ++tstate->recursion_depth;
    Py_DECREF(f);
    --tstate->recursion_depth;
    return result;
}


/* ---------- */


/* List of functions defined in the module */

static PyMethodDef _teststackless_methods[] = {
    {"softswitchablefunc",             _teststackless_softswitchabledemo,         METH_VARARGS | METH_STACKLESS,
    _teststackless_softswitchabledemo_doc},
    { "test_cframe", (PyCFunction) test_cframe, METH_VARARGS | METH_KEYWORDS,
    test_cframe__doc__ },
    { "test_cstate", (PyCFunction) test_cstate, METH_O,
    test_cstate__doc__ },
    { "test_PyEval_EvalFrameEx", (PyCFunction) test_PyEval_EvalFrameEx, METH_VARARGS | METH_KEYWORDS,
    test_PyEval_EvalFrameEx__doc__ },
    {NULL,              NULL}           /* sentinel */
};

PyDoc_STRVAR(module_doc,
"This is a test module for Stackless.");

static PyModuleDef _teststacklessmodule;
static int
_teststackless_exec(PyObject *m)
{
    /* Finalize the type object including setting type of the new type
     * object; doing it here is required for portability, too. */
    if (PyType_Ready(&SoftSwitchableDemo_Type) < 0 ||
            PyModule_AddObject(m, "SoftSwitchableDemo", (PyObject *)&SoftSwitchableDemo_Type) < 0 ||
            PyStackless_InitFunctionDeclaration(&demo_soft_switchable_declaration, m, &_teststacklessmodule) < 0 )
        goto fail;

    return 0;
 fail:
    Py_XDECREF(m);
    return -1;
}

static struct PyModuleDef_Slot _teststackless_slots[] = {
    {Py_mod_exec, _teststackless_exec},
    {0, NULL},
};

static PyModuleDef _teststacklessmodule = {
    PyModuleDef_HEAD_INIT,
    "_teststackless",
    module_doc,
    0,
    _teststackless_methods,
    _teststackless_slots,
    NULL,
    NULL,
    NULL
};

/* Export function for the module (*must* be called PyInit__teststackless) */

PyMODINIT_FUNC
PyInit__teststackless(void)
{
    return PyModuleDef_Init(&_teststacklessmodule);
}
