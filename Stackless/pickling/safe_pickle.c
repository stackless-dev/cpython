#include "Python.h"
#ifdef STACKLESS

#include "compile.h"

#define SLP_BUILD_CORE
#include "internal/stackless_impl.h"
#include "internal/slp_platformselect.h"

/* safe pickling */

static int(*cPickle_save)(PyObject *, PyObject *, int) = NULL;

static PyObject *
pickle_callback(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    PyCStackObject *cst;
    PyCFrameObject *cf = (PyCFrameObject *) f;
    intptr_t *saved_base;

    /* store and update thread state */
    ts->st.nesting_level = 1; /* always hard-switch from this one now */
    /* swap the cstates, hold on to the return one */
    cst = cur->cstate;
    cur->cstate = ts->st.initial_stub;
    Py_INCREF(cur->cstate);

    /* We must base our new stack from here, because oterwise we might find
     * ourselves in an infinite loop of stack spilling.
     */
    saved_base = ts->st.cstack_root;
    ts->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *) &f;
    if (retval) {
        Py_DECREF(retval);
        cf->i = cPickle_save(cf->ob1, cf->ob2, cf->n);
    } else {
        cf->i = -1;
    }
    ts->st.cstack_root = saved_base;

    /* jump back. No decref, frame contains result. */
    Py_DECREF(cur->cstate);
    cur->cstate = cst;
    SLP_STORE_NEXT_FRAME(ts, cf->f_back);
    SLP_FRAME_EXECFUNC_DECREF(f);
    slp_transfer_return(cst);
    /* never come here */
    assert(0);
    return NULL;
}

static int pickle_M(PyObject *self, PyObject *args, int pers_save);

int
slp_safe_pickling(int(*save)(PyObject *, PyObject *, int),
                  PyObject *self, PyObject *args, int pers_save)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    int ret = -1;
    PyCFrameObject *cf = NULL;
    PyCStackObject *cst;

    cPickle_save = save;

    if (ts->st.main == NULL)
        return pickle_M(self, args, pers_save);

    cf = slp_cframe_new(pickle_callback, 1);
    if (cf == NULL)
        goto finally;
    Py_INCREF(self);
    cf->ob1 = self;
    Py_INCREF(args);
    cf->ob2 = args;
    cf->n = pers_save;
    SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *) cf);
    cst = cur->cstate;
    cur->cstate = NULL;
    if (slp_transfer(&cur->cstate, NULL, cur) < 0)
        return -1; /* fatal */
    {
        /* Clear the frame reference held by the transfer mechanism. */
        PyFrameObject *f;
        f = SLP_CLAIM_NEXT_FRAME(ts);
        assert(f == cf->f_back);
        Py_XDECREF(f);
    }
    Py_XSETREF(cur->cstate, cst);
    ret = cf->i;
finally:
    Py_XDECREF(cf);
    return ret;
}

/* safe unpickling is not needed */


/*
 * the following stuff is only needed in the rare case that we are
 * run without any initialisation. In this case, we don't save stack
 * but use slp_eval_frame, which initializes everything.
 */

static PyObject *_self, *_args;
static int _pers_save;

static PyObject *
pickle_runmain(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    Py_XDECREF(retval);
    ts->frame = f->f_back;
    Py_DECREF(f);
    return PyLong_FromLong(cPickle_save(_self, _args, _pers_save));
}

static int
pickle_M(PyObject *self, PyObject *args, int pers_save)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = slp_cframe_new(pickle_runmain, 0);
    int ret;

    if (cf == NULL) return -1;
    _self = self;
    _args = args;
    _pers_save = pers_save;
    ts->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *) &self;
    ret = slp_int_wrapper(slp_eval_frame((PyFrameObject *)cf));
    return ret;
}

#endif
