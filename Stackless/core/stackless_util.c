#include "Python.h"

#ifdef STACKLESS
#include "stackless_impl.h"

int * const _PyStackless__TryStacklessPtr = &_PyStackless_TRY_STACKLESS;

/* Initialize the Stackless runtime state */
void
slp_initialize(struct _stackless_runtime_state * state) {
    /* initialize all fields to zero */
    memset(state, 0, sizeof(*state));
}

/* Shorthands to return certain errors */

PyObject *
slp_type_error(const char *msg)
{
    PyErr_SetString(PyExc_TypeError, msg);
    return NULL;
}

PyObject *
slp_runtime_error(const char *msg)
{
    PyErr_SetString(PyExc_RuntimeError, msg);
    return NULL;
}

PyObject *
slp_value_error(const char *msg)
{
    PyErr_SetString(PyExc_ValueError, msg);
    return NULL;
}

PyObject *
slp_null_error(void)
{
    if (!PyErr_Occurred())
        PyErr_SetString(PyExc_SystemError,
            "null argument to internal routine");
    return NULL;
}


/* CAUTION: This function returns a borrowed reference */
PyFrameObject *
slp_get_frame(PyTaskletObject *task)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return ts ? (ts->st.current == task ? ts->frame : task->f.frame) : NULL;
}

void slp_check_pending_irq()
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *current = ts->st.current;

    /* act only if hard irq is in effect */
    if (current->flags.pending_irq && !(ts->st.runflags & PY_WATCHDOG_SOFT)) {
        if (current->flags.atomic)
            return;
        if (!TASKLET_NESTING_OK(current))
            return;
        /* trigger interrupt */
        ts->st.tick_watermark = ts->st.tick_counter + 1;
        current->flags.pending_irq = 0;
    }
}

int
slp_return_wrapper(PyObject *retval)
{
    STACKLESS_ASSERT();
    if (retval == NULL)
        return -1;
    if (STACKLESS_UNWINDING(retval)) {
        STACKLESS_UNPACK(PyThreadState_GET(), retval);
        Py_XDECREF(retval);
        return 1;
    }
    Py_DECREF(retval);
    return 0;
}

int
slp_return_wrapper_hard(PyObject *retval)
{
    STACKLESS_ASSERT();
    if (retval == NULL)
        return -1;
    assert(!STACKLESS_UNWINDING(retval));
    Py_DECREF(retval);
    return 0;
}

int
slp_int_wrapper(PyObject *retval)
{
    int ret = -909090;

    STACKLESS_ASSERT();
    if (retval != NULL) {
        ret = PyLong_AsLong(retval);
        Py_DECREF(retval);
    }
    return ret;
}

int
slp_current_wrapper( int(*func)(PyTaskletObject*), PyTaskletObject *task )
{
    PyThreadState *ts = PyThreadState_GET();

    int ret;
    ts->st.main = (PyTaskletObject*)Py_None;
    ret = func(task);
    ts->st.main = NULL;
    return ret;
}

int
slp_resurrect_and_kill(PyObject *self, void(*killer)(PyObject *))
{
    /* modelled after typeobject.c's slot_tp_del */

    PyObject *error_type, *error_value, *error_traceback;

    /* this is different from typeobject.c's slot_tp_del: our callers already 
       called PyObject_GC_UnTrack(self) */
    assert(PyObject_IS_GC(self) && ! _PyObject_GC_IS_TRACKED(self));

    /* Temporarily resurrect the object. */
    assert(Py_REFCNT(self) == 0);
    Py_REFCNT(self) = 1;

    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    killer(self);

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);

    /* Undo the temporary resurrection; can't use DECREF here, it would
     * cause a recursive call.
     */
    assert(Py_REFCNT(self) > 0);
    if (--Py_REFCNT(self) == 0)
        return 0;    /* this is the normal path out */

    /* __del__ resurrected it!  Make it look like the original Py_DECREF
     * never happened.
     */
    {
        Py_ssize_t refcnt = Py_REFCNT(self);
        _Py_NewReference(self);
        Py_REFCNT(self) = refcnt;
    }
    /* If Py_REF_DEBUG, _Py_NewReference bumped _Py_RefTotal, so
     * we need to undo that. */
    _Py_DEC_REFTOTAL;
    /* If Py_TRACE_REFS, _Py_NewReference re-added self to the object
     * chain, so no more to do there.
     * If COUNT_ALLOCS, the original decref bumped tp_frees, and
     * _Py_NewReference bumped tp_allocs:  both of those need to be
     * undone.
     */
#ifdef COUNT_ALLOCS
    --Py_TYPE(self)->tp_frees;
    --Py_TYPE(self)->tp_allocs;
#endif
    /* This code copied from iobase_dealloc() (in 3.0) */
    /* When called from a heap type's dealloc, the type will be
       decref'ed on return (see e.g. subtype_dealloc in typeobject.c).
       This will undo that, thus preventing a crash.  But such a type
       _will_ have had its dict and slots cleared. */
    if (PyType_HasFeature(Py_TYPE(self), Py_TPFLAGS_HEAPTYPE))
        Py_INCREF(Py_TYPE(self));

    return -1;
}

/*
 * A thread id is either an unsigned long or the special value -1.
 */
long
slp_parse_thread_id(PyObject *thread_id, unsigned long *id)
{
    int overflow;
    long result1;
    unsigned long result2;

    assert(id != NULL);
    if (thread_id == NULL)
        return 1;
    /* thread_id is either an unsigned long or -1. We distinguish these values */
    result1 = PyLong_AsLongAndOverflow(thread_id, &overflow);
    if (overflow == 0 && result1 == -1) {
        /* a special negative id */
        *id = (unsigned long)result1;
        return result1;
    }
    result2 = PyLong_AsUnsignedLong(thread_id);
    if (PyErr_Occurred())
        return 0;
    *id = result2;
    return 1;
}

#endif
