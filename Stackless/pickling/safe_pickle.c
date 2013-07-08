#include "Python.h"
#ifdef STACKLESS

#include "compile.h"

#include "core/stackless_impl.h"
#include "platf/slp_platformselect.h"

/* safe pickling */

static int(*cPickle_save)(PyObject *, PyObject *, int) = NULL;
#if 0 /* add stack spilling later */
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
    ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &f;
    Py_DECREF(retval);
    cf->i = cPickle_save(cf->ob1, cf->ob2, cf->n);
    ts->st.cstack_root = saved_base;

    /* jump back. No decref, frame contains result. */
    Py_DECREF(cur->cstate);
    cur->cstate = cst;
    ts->frame = cf->f_back;
    slp_transfer_return(cst);
    /* never come here */
    return NULL;
}

#endif
static int pickle_M(PyObject *self, PyObject *args, int pers_save);

int
slp_safe_pickling(int(*save)(PyObject *, PyObject *, int),
                  PyObject *self, PyObject *args, int pers_save)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    int ret = -1;
#if 0
    PyCFrameObject *cf = NULL;
    PyCStackObject *cst;

    if (ts->st.cstack_root == NULL) {
        /* mark the stack spilling base */
        ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &save;
        ret = (*save)(self, args, pers_save);
        ts->st.cstack_root = NULL;
        return ret;
    }

    cPickle_save = save;

    if (ts->st.main == NULL)
#endif
        return pickle_M(self, args, pers_save);
#if 0

    cf = slp_cframe_new(pickle_callback, 1);
    if (cf == NULL)
        goto finally;
    Py_INCREF(self);
    cf->ob1 = self;
    Py_INCREF(args);
    cf->ob2 = args;
    cf->n = pers_save;
    ts->frame = (PyFrameObject *) cf;
    cst = cur->cstate;
    cur->cstate = NULL;
    if (slp_transfer(&cur->cstate, NULL, cur) < 0)
        return -1; /* fatal */
    Py_XDECREF(cur->cstate);
    cur->cstate = cst;
    ret = cf->i;
finally:
    Py_XDECREF(cf);
    return ret;
#endif
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
    return PyInt_FromLong(cPickle_save(_self, _args, _pers_save));
}

static int
pickle_M(PyObject *self, PyObject *args, int pers_save)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = slp_cframe_new(pickle_runmain, 0);
    int ret;
    intptr_t *old_root;

    if (cf == NULL) return -1;
    _self = self;
    _args = args;
    _pers_save = pers_save;
#if 0
    old_root = ts->st.cstack_root;
    ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &self;
#endif
    ret = slp_int_wrapper(slp_eval_frame((PyFrameObject *)cf));
#if 0
    ts->st.cstack_root = old_root;
#endif
    return ret;
}

#endif
