/* Stackless module implementation */

#include "Python.h"
#include "pythread.h"

#ifdef STACKLESS
#include "core/stackless_impl.h"

#define IMPLEMENT_STACKLESSMODULE
#include "platf/slp_platformselect.h"
#include "core/cframeobject.h"
#include "pickling/prickelpit.h"
#include "core/stackless_methods.h"


/******************************************************

  The atomic context manager
  This is provided here for perfomance, because setting
  the atomic state is an important part of writing higher
  level operations.  It is equivalent to the following,
  but much faster:
  @contextlib.contextmanager
  def atomic():
      old = stackless.getcurrent().set_atomic(True)
      try:
          yield
      finally:
          stackless.getcurrent().set_atomic(old)

 ******************************************************/
typedef struct PyAtomicObject
{
    PyObject_HEAD
    int old;
} PyAtomicObject;

static PyObject *
atomic_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static void
atomic_dealloc(PyObject *self)
{
    PyObject_DEL(self);
}

static PyObject *
atomic_enter(PyObject *self)
{
    PyAtomicObject *a = (PyAtomicObject*)self;
    PyObject *c = PyStackless_GetCurrent();
    if (c) {
        a->old = PyTasklet_GetAtomic((PyTaskletObject*)c);
        PyTasklet_SetAtomic((PyTaskletObject*)c, 1);
        Py_DECREF(c);
    }
    Py_RETURN_NONE;
}

static PyObject *
atomic_exit(PyObject *self, PyObject *args)
{
    PyAtomicObject *a = (PyAtomicObject*)self;
    PyObject *c = PyStackless_GetCurrent();
    if (c) {
        PyTasklet_SetAtomic((PyTaskletObject*)c, a->old);
        Py_DECREF(c);
    }
    Py_RETURN_NONE;
}

static PyMethodDef atomic_methods[] = {
    {"__enter__", (PyCFunction)atomic_enter, METH_NOARGS, NULL},
    {"__exit__",  (PyCFunction)atomic_exit, METH_VARARGS, NULL},
    {NULL, NULL}
};

PyDoc_STRVAR(PyAtomic_Type__doc__,
"A context manager for setting the 'atomic' flag of the \n\
current tasklet to true for the duration.\n");

PyTypeObject PyAtomic_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_stackless.atomic",
    sizeof(PyAtomicObject),
    0,
    atomic_dealloc,                             /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_compare */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    PyObject_GenericSetAttr,                    /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                         /* tp_flags */
    PyAtomic_Type__doc__,                       /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    atomic_methods,                             /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    atomic_new,                                 /* tp_new */
};

static PyObject *
atomic_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyAtomicObject *a = PyObject_NEW(PyAtomicObject, &PyAtomic_Type);
    return (PyObject *)a;
}

/******************************************************

  The Stackless Module

 ******************************************************/

PyThreadState *slp_initial_tstate = NULL;

static void *slp_error_handler = NULL;

int
slp_pickle_with_tracing_state()
{
    PyObject *flag, *slp_module;
    int result = -1;

    slp_module = PyImport_ImportModule("_stackless");
    if (slp_module == NULL)
        return -1;
    flag = PyObject_GetAttrString(slp_module, "pickle_with_tracing_state");
    Py_DECREF(slp_module);
    if (NULL != flag) {
        result = PyObject_IsTrue(flag);
        Py_DECREF(flag);
    }
    return result;
}

PyDoc_STRVAR(schedule__doc__,
"schedule(retval=stackless.current) -- switch to the next runnable tasklet.\n\
The return value for this call is retval, with the current\n\
tasklet as default.\n\
schedule_remove(retval=stackless.current) -- ditto, and remove self.");

static PyObject *
schedule(PyObject *self, PyObject *args, PyObject *kwds);
static PyObject *
schedule_remove(PyObject *self, PyObject *args, PyObject *kwds);

static PyObject *
PyStackless_Schedule_M(PyObject *retval, int remove)
{
    PyMethodDef sched = {"schedule", (PyCFunction)schedule, METH_VARARGS|METH_KEYWORDS};
    PyMethodDef s_rem = {"schedule", (PyCFunction)schedule_remove, METH_VARARGS|METH_KEYWORDS};
    if (remove)
        return PyStackless_CallCMethod_Main(&s_rem, NULL, "O", retval);
    else
        return PyStackless_CallCMethod_Main(&sched, NULL, "O", retval);
}

PyObject *
PyStackless_Schedule(PyObject *retval, int remove)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *prev = ts->st.current, *next = prev->next;
    PyObject *tmpval, *ret = NULL;
    int switched, fail;

    if (ts->st.main == NULL) return PyStackless_Schedule_M(retval, remove);

    /* make sure we hold a reference to the previous tasklet.
     * this will be decrefed after the switch is complete
     */
    Py_INCREF(prev);
    assert(ts->st.del_post_switch == NULL);
    ts->st.del_post_switch = (PyObject*)prev;

    TASKLET_CLAIMVAL(prev, &tmpval);
    TASKLET_SETVAL(prev, retval);
    if (remove) {
        slp_current_remove();
        Py_DECREF(prev);
        if (next == prev)
            next = 0; /* we were the last runnable tasklet */
    }

    fail = slp_schedule_task(&ret, prev, next, stackless, &switched);

    if (fail) {
        TASKLET_SETVAL_OWN(prev, tmpval);
        if (remove) {
            slp_current_unremove(prev);
            Py_INCREF(prev);
        }
    } else
        Py_DECREF(tmpval);
    if (!switched || fail)
        /* we must do this ourselves */
        Py_CLEAR(ts->st.del_post_switch);
    return ret;
}

PyObject *
PyStackless_Schedule_nr(PyObject *retval, int remove)
{
    STACKLESS_PROPOSE_ALL();
    return PyStackless_Schedule(retval, remove);
}

static PyObject *
schedule(PyObject *self, PyObject *args, PyObject *kwds)
{
    STACKLESS_GETARG();
    PyObject *retval = (PyObject *) PyThreadState_GET()->st.current;
    static char *argnames[] = {"retval", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:schedule",
        argnames, &retval))
    {
        return NULL;
    }
    STACKLESS_PROMOTE_ALL();
    return PyStackless_Schedule(retval, 0);
}

static PyObject *
schedule_remove(PyObject *self, PyObject *args, PyObject *kwds)
{
    STACKLESS_GETARG();
    PyObject *retval = (PyObject *) PyThreadState_GET()->st.current;
    static char *argnames[] = {"retval", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:schedule_remove",
        argnames, &retval))
    {
        return NULL;
    }
    STACKLESS_PROMOTE_ALL();
    return PyStackless_Schedule(retval, 1);
}


PyDoc_STRVAR(getruncount__doc__,
"getruncount() -- return the number of runnable tasklets.");

int
PyStackless_GetRunCount(void)
{
    PyThreadState *ts = PyThreadState_GET();
    return ts->st.runcount;
}

static PyObject *
getruncount(PyObject *self)
{
    PyThreadState *ts = PyThreadState_GET();
    return PyLong_FromLong(ts->st.runcount);
}


PyDoc_STRVAR(getcurrent__doc__,
"getcurrent() -- return the currently executing tasklet.");

PyObject *
PyStackless_GetCurrent(void)
{
    PyThreadState *ts = PyThreadState_GET();
    /* only if there is a main tasklet, is the current one
     * the active one.  Otherwise, it is merely a runnable
     * tasklet
     */
    PyObject *t = NULL;
    if (ts->st.main)
        t = (PyObject*)ts->st.current;

    Py_XINCREF(t); /* CCP change, allow to work if stackless isn't init */
    return t;
}

static PyObject *
getcurrent(PyObject *self)
{
    return PyStackless_GetCurrent();
}

PyDoc_STRVAR(getcurrentid__doc__,
"getcurrentid() -- return the id of the currently executing tasklet.");

long
PyStackless_GetCurrentId(void)
{
    #ifdef WITH_THREAD
    PyThreadState *ts = PyGILState_GetThisThreadState();
#else
    PyThreadState *ts = PyThreadState_GET();
#endif
    PyTaskletObject *t = NULL;
    /* if there is threadstate, and there is a main tasklet, then the
     * "current" is the actively running tasklet.
     * If there isn't a "main", then the tasklet in "current" is merely a
     * runnable one
     */
    if (ts != NULL) {
        t = ts->st.current;
        if (t && t != ts->st.main && ts->st.main != NULL) {
#if SIZEOF_VOID_P > SIZEOF_LONG
            return (long)((Py_intptr_t)t) ^ (long)((Py_intptr_t)t >> 32);
#else
            return (long)t;
#endif
        }
    }
    /* We want the ID to be constant, before and after a main tasklet
     * is initialized on a thread.Therefore, for a main tasklet, we use its
     * thread ID.
     */
    if (ts)
        return ts->thread_id;
#ifdef WITH_THREAD
    return PyThread_get_thread_ident();
#else
    return 0;
#endif
}

static PyObject *
getcurrentid(PyObject *self)
{
    return PyLong_FromLong(PyStackless_GetCurrentId());
}

PyDoc_STRVAR(getmain__doc__,
"getmain() -- return the main tasklet of this thread.");

static PyObject *
getmain(PyObject *self)
{
    PyThreadState *ts = PyThreadState_GET();
    PyObject * t = (PyObject*) ts->st.main;
    Py_INCREF(t);
    return t;
}

PyDoc_STRVAR(enable_soft__doc__,
"enable_softswitch(flag) -- control the switching behavior.\n"
"Tasklets can be either switched by moving C stack slices around\n"
"or by avoiding stack changes at all. The latter is only possible\n"
"in the top interpreter level. Switching it off is for timing and\n"
"debugging purposes. This flag exists once for the whole process.\n"
"For inquiry only, use 'None' as the flag.\n"
"By default, soft switching is enabled.");

static PyObject *
enable_softswitch(PyObject *self, PyObject *flag)
{
    PyObject *ret;
    int newflag;
    if (!flag || flag == Py_None)
        return PyBool_FromLong(slp_enable_softswitch);
    newflag = PyObject_IsTrue(flag);
    if (newflag == -1 && PyErr_Occurred())
        return NULL;
    ret = PyBool_FromLong(slp_enable_softswitch);
    slp_enable_softswitch = newflag;
    return ret;
}


PyDoc_STRVAR(run_watchdog__doc__,
"run_watchdog(timeout=0, threadblock=False, soft=False,\n\
              ignore_nesting=False, totaltimeout=False) -- \n\
run tasklets until they are all\n\
done, or timeout instructions have passed, if timeout is not 0.\n\
Tasklets must provide cooperative schedule() calls.\n\
If the timeout is met, the function returns.\n\
This function must be called from the main tasklet only.\n\
If a tasklet is interrupted, it is returned as the return value of the\n\
function.\n\
If an exception occours, it will be passed to the main tasklet.\n\
The other optional flags paremeter work as follows:\n\
threadblock:  When set, ensures, that the thread\n\
will block when it runs out of tasklets to await channel activity from\n\
other Python(r) threads to wake it up.\n\
soft:  When set, tasklets won't be interrupted but\n\
run will return (with a None) at the next convenient scheduling moment\n\
when the timeout has occurred.\n\
ignore_nesting: Allow interrupts at any interpreter nesting level,\n\
ignoring the tasklets' own ignore_nesting attribute.\n\
totaltimeout: The 'timeout' argument is the total timeout for run(),\n\
rather than a maximum timeslice for a single tasklet.  This for run()\n\
to return after a certain time.");

static PyObject *
interrupt_timeout_return(void)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *current = ts->st.current;
    PyTaskletObject *watchdog;
    PyObject *ret;

    /*
     * we mark the IRQ as pending if
     * a) we are in soft interrupt mode
     * b) we are in hard interrupt mode, but aren't allowed to break here which happens
     * when "atomic" is set, or the "nesting level" is relevant and not ignored.
     * Also, when switch trapped, we don't perform an involountary switch.
     */
    if ((ts->st.runflags & PY_WATCHDOG_SOFT) ||
        current->flags.atomic || ts->st.schedlock ||
        !TASKLET_NESTING_OK(current) ||
        ts->st.switch_trap)
    {
        ts->st.tick_watermark = ts->st.tick_counter + ts->st.interval;
        current->flags.pending_irq = 1;
        Py_INCREF(Py_None);
        return Py_None;
    }
    else
        current->flags.pending_irq = 0;

    /* interrupt!  Signal that an interrupt has indeed taken place */
    assert(ts->st.interrupted == NULL);
    ts->st.interrupted = (PyObject*)ts->st.current;
    Py_INCREF(ts->st.interrupted);

    /* switch to the watchdog tasklet */
    watchdog = slp_get_watchdog(ts, 1);
    slp_schedule_task(&ret, ts->st.current, watchdog, 1, 0);
    return ret;
}

static PyObject *
run_watchdog(PyObject *self, PyObject *args, PyObject *kwds);

static PyObject *
PyStackless_RunWatchdog_M(long timeout, long flags)
{
    PyMethodDef def = {"run", (PyCFunction)run_watchdog, METH_VARARGS | METH_KEYWORDS};
    int threadblock, soft, ignore_nesting, totaltimeout;
    threadblock = (flags & Py_WATCHDOG_THREADBLOCK) ? 1 : 0;
    soft =        (flags & PY_WATCHDOG_SOFT) ? 1 : 0;
    ignore_nesting=(flags & PY_WATCHDOG_IGNORE_NESTING) ? 1 : 0;
    totaltimeout =(flags & PY_WATCHDOG_TOTALTIMEOUT) ? 1 : 0;

    return PyStackless_CallCMethod_Main(&def, NULL, "liii",
        timeout, threadblock, soft, ignore_nesting, totaltimeout);
}


PyObject *
PyStackless_RunWatchdog(long timeout)
{
    return PyStackless_RunWatchdogEx(timeout, 0);
}

static int
push_watchdog(PyThreadState *ts, PyTaskletObject *t, int *interrupt);
static PyTaskletObject *
pop_watchdog(PyThreadState *ts);
static int
check_watchdog(PyThreadState *ts, PyTaskletObject *task);


PyObject *
PyStackless_RunWatchdogEx(long timeout, int flags)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *old_current, *victim;
    PyObject *retval;
    int fail;
    PyObject* (*old_interrupt)(void) = NULL;
    int old_runflags = 0;
    long old_watermark = 0, old_interval = 0;
    int interrupt;

    if (ts->st.main == NULL)
        return PyStackless_RunWatchdog_M(timeout, flags);

    /* is this an interrupt watchdog?  Treat it differently. */
    interrupt = timeout > 0;

    /* push the current tasklet onto the watchdog stack */
    if (push_watchdog(ts, ts->st.current, &interrupt))
        return NULL;

    old_current = ts->st.current;
    /* store watchdog state and set up a new one, if we are the active interrupt watchdog */
    if (interrupt) {
        old_interrupt = ts->st.interrupt;
        old_runflags = ts->st.runflags;
        old_watermark = ts->st.tick_watermark;
        old_interval = ts->st.interval;

        if (timeout <= 0)
            ts->st.interrupt = NULL;
        else
            ts->st.interrupt = interrupt_timeout_return;
        ts->st.interval = timeout;
        ts->st.tick_watermark = ts->st.tick_counter + timeout;
        ts->st.runflags = flags;
    }

    /* run the watchdog */
    Py_DECREF(ts->st.current);
    slp_current_remove(); /* it still exists in the watchdog stack */
    fail = slp_schedule_task(&retval, old_current, ts->st.current, 0, 0);

    if (fail) {
        /* we failed to switch */
        PyTaskletObject *undo = pop_watchdog(ts);
        assert(undo == old_current);
        slp_current_unremove(undo);
        return NULL;
    }

    /* we are back in the original tasklet */
    assert(old_current == ts->st.current);
    if (check_watchdog(ts, old_current)) {
        /* we were awoken.  But if there are deeper tasklets on the stack, we pop them here
         * and make them runnable.  Those that are popped here by us, will know this because
         * when they wake up, they won't be on the stack anymore
         */
        for(;;) {
            PyTaskletObject *popped = pop_watchdog(ts);
            if (!PyTasklet_Scheduled(popped))
                /* popped a deeper tasklet, that hasn't been scheduled meanwhile */
                slp_current_insert(popped); /* steals reference */
            else {
                /* Usually only the current tasklet.
                 * But also a deeper tasklet, that got scheduled (e.g. by taskler.insert())
                 */
                Py_DECREF(popped);
            }
            if (popped == old_current)
                break;
        }
        if (interrupt) {
            ts->st.interrupt = old_interrupt;
            ts->st.runflags = old_runflags;
            ts->st.tick_watermark = old_watermark;
            ts->st.interval = old_interval;
        }
    }

    /* retval really should be PyNone here (or NULL).  Technically, it is the
     * tempval of some tasklet that has quit.  Even so, it is quite
     * useless to use.  run() must return None, or a tasklet
     */
    if (retval == NULL)
        return NULL;
    Py_DECREF(retval);

    /*
     * If we were interrupted and using hard interrupts (bit 1 in flags not set)
     * we need to return the interrupted tasklet)
     */
    if (ts->st.interrupted) {
        victim = (PyTaskletObject*)ts->st.interrupted;
        ts->st.interrupted = NULL;
        if (!(flags & PY_WATCHDOG_SOFT)) {
            /* remove victim from runnable queue */
            slp_current_remove_tasklet(victim);
            Py_DECREF(victim); /* the ref from the runqueue */
            return (PyObject*) victim;
        } else
            /* soft interrupt just leave the victim in place */
            Py_DECREF(victim);
    }
    Py_RETURN_NONE;
}

static int
push_watchdog(PyThreadState *ts, PyTaskletObject *t, int *interrupt)
{
    /* push ourselves on the stack of watchdogs.
     * The zeroeth member is special, it is the tasklet that should be
     * interrupted if the watchdog timer times out.  All the others
     * are awoken in case the run-queue empties in a top-down fashion
     */
    if (ts->st.watchdogs == NULL) {
        /* initialize it with a PyNone in theinterrupt slot */
        ts->st.watchdogs = PyList_New(1);
        if (ts->st.watchdogs == NULL)
            return -1;
        Py_INCREF(Py_None);
        PyList_SET_ITEM(ts->st.watchdogs, 0, Py_None);
    }
    if (*interrupt) {
        /* the caller is requesting to be the interrupt watchdog.  Let's see: */
        if (PyList_GET_ITEM(ts->st.watchdogs, 0) != Py_None) {
            /* no, someone else already is the interrupt watchdog */
            *interrupt = 0;
        } else {
            Py_INCREF(t);
            if (PyList_SetItem(ts->st.watchdogs, 0, (PyObject *)t))
                return -1;
        }
    }
    if (PyList_Append(ts->st.watchdogs, (PyObject*) t))
        return -1;
    return 0;
}

static PyTaskletObject *
pop_watchdog(PyThreadState *ts)
{
    PyTaskletObject *t;
    Py_ssize_t s;
    if (ts->st.watchdogs == NULL || (s = PyList_GET_SIZE(ts->st.watchdogs)) == 1) {
        PyErr_SetNone(PyExc_ValueError);
        return NULL;
    }
    t = (PyTaskletObject*) PyList_GET_ITEM(ts->st.watchdogs, s - 1);
    Py_INCREF(t);
    if (PyList_SetSlice(ts->st.watchdogs, s - 1, s, NULL)) {
        Py_DECREF(t);
        return NULL;
    }
    if ((PyObject *)t == PyList_GET_ITEM(ts->st.watchdogs, 0)) {
        /* clear it from the interrupt slot too */
        Py_INCREF(Py_None);
        if(PyList_SetItem(ts->st.watchdogs, 0, Py_None)) {
            Py_DECREF(t);
            return NULL;
        }
    }
    return t;
}

/* check if a tasklet is on the watchdog stack */
static int
check_watchdog(PyThreadState *ts, PyTaskletObject *task)
{
    Py_ssize_t i = ts->st.watchdogs ? PyList_GET_SIZE(ts->st.watchdogs) : 0;
    /* we don't check the "intterupt" slot, number 0 */
    while (i > 1) {
        if (PyList_GET_ITEM(ts->st.watchdogs, i-1) == (PyObject*)task)
            return 1;
        --i;
    }
    return 0;
}

/* find the correct target to wake up when we block.  The innermost watchdog
 * or the main tasklet.  Returns a borrowed reference
 * if "interrupt" is set, then we want the tasklet running the outermost
 * true watchdog, i.e. the one with interrupt conditions set.
 */
PyTaskletObject *
slp_get_watchdog(PyThreadState *ts, int interrupt)
{
    PyTaskletObject *t;
    /* TODO: treat interrupt differently */
    if (ts->st.watchdogs == NULL || PyList_GET_SIZE(ts->st.watchdogs) == 1)
        return ts->st.main;
    if (interrupt) {
        t = (PyTaskletObject*)PyList_GET_ITEM(ts->st.watchdogs, 0);
        if ((PyObject*)t == Py_None)
            t = ts->st.main;  /* strange, there should be no interrupt in this case! */
        return t;
    }
    return (PyTaskletObject*)PyList_GET_ITEM(
        ts->st.watchdogs, PyList_GET_SIZE(ts->st.watchdogs) - 1);
}

static PyObject *
run_watchdog(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *argnames[] = {"timeout", "threadblock", "soft",
                                                            "ignore_nesting", "totaltimeout",
                                                            NULL};
    long timeout = 0;
    int threadblock = 0;
    int soft = 0;
    int ignore_nesting = 0;
    int totaltimeout = 0;
    int flags;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|liiii:run_watchdog",
                                     argnames, &timeout, &threadblock, &soft,
                                     &ignore_nesting, &totaltimeout))
        return NULL;
    flags = threadblock ? Py_WATCHDOG_THREADBLOCK : 0;
    flags |= soft ? PY_WATCHDOG_SOFT : 0;
    flags |= ignore_nesting ? PY_WATCHDOG_IGNORE_NESTING : 0;
    flags |= totaltimeout ? PY_WATCHDOG_TOTALTIMEOUT : 0;
    return PyStackless_RunWatchdogEx(timeout, flags);
}

PyDoc_STRVAR(get_thread_info__doc__,
"get_thread_info(thread_id) -- return a 3-tuple of the thread's\n\
main tasklet, current tasklet and runcount.\n\
To obtain a list of all thread infos, use\n\
\n\
map (stackless.get_thread_info, stackless.threads)");

static PyObject *
get_thread_info(PyObject *self, PyObject *args)
{
    PyThreadState *ts = PyThreadState_GET();
    PyInterpreterState *interp = ts->interp;
    long id = 0;
    /* The additional optional argument flags is currently intentionally
     * undocumented. The lower order flag bits are reserved for future public
     * applications. If the flag bit 31 is set, the returned tuple contains
     * the watchdog list as an additional item. The Stackless test suite uses
     * this feature to ensure that the list is empty at start and end of each
     * test.
     */
    unsigned long flags=0;

    if (!PyArg_ParseTuple(args, "|lk:get_thread_info", &id, &flags))
        return NULL;
    if (id != -1) {
        SLP_HEAD_LOCK();
        for (ts = interp->tstate_head; id && ts != NULL; ts = ts->next) {
            if (ts->thread_id == id)
                break;
        }
        SLP_HEAD_UNLOCK();
        if (ts == NULL)
            RUNTIME_ERROR("Thread id not found", NULL);
    }

    return Py_BuildValue((flags & (1ul<<31)) ? "(OOiO)": "(OOi)",
        ts->st.main ? (PyObject *) ts->st.main : Py_None,
        ts->st.runcount ? (PyObject *) ts->st.current : Py_None,
        ts->st.runcount,
        ts->st.watchdogs ? ts->st.watchdogs : Py_None);
}

static PyObject *
slpmodule_reduce(PyObject *self)
{
    return PyUnicode_FromString("_stackless");
}

int
PyStackless_AdjustSwitchTrap(int change)
{
    PyThreadState *ts = PyThreadState_GET();
    int old = ts->st.switch_trap;
    ts->st.switch_trap += change;
    return old;
}

PyDoc_STRVAR(slpmodule_switch_trap__doc__,
"switch_trap(change) -- Change the switch trap level of the thread. When non-zero, \n\
tasklet switches won't take place. Instead, an exception is raised.\n\
Returns the old trap value.  Defaults to 0.");

static PyObject *
slpmodule_switch_trap(PyObject *self, PyObject *args)
{
    int change = 0;
    if (!PyArg_ParseTuple(args, "|i", &change))
        return NULL;
    return PyLong_FromLong(PyStackless_AdjustSwitchTrap(change));
}

PyDoc_STRVAR(set_error_handler__doc__,
"set_error_handler(handler) -- Set or delete the error handler function \n\
used for uncaught exceptions on tasklets.  \"handler\" should take three \n\
arguments: \"def handler(type, value, tb):\".  Its return value is ignored.\n\
If \"handler\" is None, the default handler is used, which raises the error \n\
on the main tasklet.  Similarly if the handler itself raises an error.\n\
Returns the previous handler or None.");

static PyObject *
set_error_handler(PyObject *self, PyObject *args)
{
    PyObject *old, *handler = NULL;
    if (!PyArg_ParseTuple(args, "|O:set_error_handler", &handler))
        return NULL;
    old = slp_error_handler ? slp_error_handler : Py_None;
    Py_INCREF(old);
    if (handler == Py_None)
        handler = NULL;
    Py_XINCREF(handler);
    Py_CLEAR(slp_error_handler);
    slp_error_handler = handler;
    return old;
}

int
PyStackless_CallErrorHandler(void)
{
    assert(PyErr_Occurred());
    if (slp_error_handler != NULL) {
        PyObject *exc, *val, *tb;
        PyObject *result;
        PyErr_Fetch(&exc, &val, &tb); /* we own the refs in exc, val and tb */
        assert(exc != NULL);
        if (!val) {
            Py_INCREF(Py_None);
            val = Py_None;
        }
        if (!tb) {
            Py_INCREF(Py_None);
            tb = Py_None;
        }
        result = PyObject_CallFunction(slp_error_handler, "OOO", exc, val, tb);
        Py_DECREF(exc);
        Py_DECREF(val);
        Py_DECREF(tb);
        Py_XDECREF(result);
        if (!result)
            return -1; /* an error in the handler! */
        return 0;
    } else
        return -1; /* just raise the current exception */

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

static
PyObject *
test_cframe(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *argnames[] = {"switches", "words", NULL};
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
        alloca(extra*sizeof(PyObject*));
    Py_INCREF(ret);
    for (i=0; i<switches; i++) {
        Py_DECREF(ret);
        ret = PyStackless_Schedule(Py_None, 0);
        if (ret == NULL) return NULL;
    }
    return ret;
}

PyDoc_STRVAR(test_outside__doc__,
"test_outside() -- a builtin testing function.\n\
This function simulates an application that does not run \"inside\"\n\
Stackless, with active, running frames, but always needs to initialize\n\
the main tasklet to get \"inside\".\n\
The function will terminate when no other tasklets are runnable.\n\
\n\
Typical usage: Create a tasklet for test_cframe and run by test_outside().\n\
\n\
    t1 = tasklet(test_cframe)(1000000)\n\
    test_outside()\n\
This can be used to measure the execution time of 1.000.000 switches.");

static
PyObject *
test_outside(PyObject *self)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *stmain = ts->st.main;
    PyCStackObject *cst = ts->st.initial_stub;
    PyFrameObject *f = SLP_CURRENT_FRAME(ts);
    int recursion_depth = ts->recursion_depth;
    int nesting_level = ts->st.nesting_level;
    PyObject *ret = Py_None;
    int jump = ts->st.serial_last_jump;

    Py_INCREF(ret);
    ts->st.main = NULL;
    ts->st.initial_stub = NULL;
    ts->st.nesting_level = 0;
    SLP_SET_CURRENT_FRAME(ts, NULL);
    ts->recursion_depth = 0;
    slp_current_remove();
    while (ts->st.runcount > 0) {
        Py_DECREF(ret);
        ret = PyStackless_Schedule(Py_None, 0);
        if (ret == NULL) {
            break;
        }
    }
    ts->st.main = stmain;
    Py_CLEAR(ts->st.initial_stub);
    ts->st.initial_stub = cst;
    SLP_SET_CURRENT_FRAME(ts, f);
    slp_current_insert(stmain);
    ts->recursion_depth = recursion_depth;
    ts->st.nesting_level = nesting_level;
    ts->st.serial_last_jump = jump;
    return ret;
}


PyDoc_STRVAR(test_cframe_nr__doc__,
"test_cframe_nr(switches) -- a builtin testing function that does nothing\n\
but soft tasklet switching. The function will call PyStackless_Schedule_nr() for switches\n\
times and then finish.\n\
Usage: Cf. test_cframe().");

static
PyObject *
test_cframe_nr_loop(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = (PyCFrameObject *) f;

    if (retval == NULL)
        goto exit_test_cframe_nr_loop;
    while (cf->n-- > 0) {
        Py_DECREF(retval);
        retval = PyStackless_Schedule_nr(Py_None, 0);
        if (retval == NULL) {
            SLP_STORE_NEXT_FRAME(ts, cf->f_back);
            return NULL;
        }
        if (STACKLESS_UNWINDING(retval)) {
            return retval;
        }
        /* was a hard switch */
    }
exit_test_cframe_nr_loop:
    SLP_STORE_NEXT_FRAME(ts, cf->f_back);
    Py_DECREF(cf);
    return retval;
}

static
PyObject *
test_cframe_nr(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *argnames[] = {"switches", NULL};
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf;
    long switches;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "l:test_cframe_nr",
                                     argnames, &switches))
        return NULL;
    cf = slp_cframe_new(test_cframe_nr_loop, 1);
    if (cf == NULL)
        return NULL;
    cf->n = switches;
    SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *) cf);
    Py_INCREF(Py_None);
    return STACKLESS_PACK(ts, Py_None);
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


PyDoc_STRVAR(test_nostacklesscalltype__doc__,
"A callable extension type that does not support stackless calls\n"
"It calls arg[0](*arg[1:], **kw).\n"
"There are two special keyword arguments\n"
"  post_callback: callback(result, stackless, slp_try_stackless, **kw) that runs after arg[0].\n"
"  set_try_stackless: integer value, assigned to set spl_try_stackless."
);

typedef struct {
    PyObject_HEAD
} test_nostacklesscallObject;

static
PyObject *
test_nostacklesscall_call(PyObject *f, PyObject *arg, PyObject *kw)
{
    PyObject * result;
    PyObject * callback1;
    PyObject * callback2 = NULL;
    PyObject * rest;
    int stackless = slp_try_stackless;

    callback1 = PyTuple_GetItem(arg, 0);
    if (callback1 == NULL)
        return NULL;
    Py_INCREF(callback1);

    rest = PyTuple_GetSlice(arg, 1, PyTuple_GET_SIZE(arg));
    if (rest == NULL) {
        Py_DECREF(callback1);
        return NULL;
    }

    if (kw && PyDict_Check(kw)) {
        PyObject * setTryStackless;

        setTryStackless = PyDict_GetItemString(kw, "set_try_stackless");
        if (setTryStackless) {
            int l;
            Py_INCREF(setTryStackless);
            PyDict_DelItemString(kw, "set_try_stackless");
            l = PyObject_IsTrue(setTryStackless);
            Py_DECREF(setTryStackless);
            if (-1 != l)
                slp_try_stackless = l;
            else
                return NULL;
        }

        callback2 = PyDict_GetItemString(kw, "post_callback");
        if (callback2) {
            Py_INCREF(callback2);
            PyDict_DelItemString(kw, "post_callback");
        }
    }

    if (callback1 == Py_None) {
        result = rest;
        Py_INCREF(result);
    }
    else
        result = PyObject_Call(callback1, rest, kw);

    Py_DECREF(callback1);
    Py_DECREF(rest);

    if (NULL == callback2)
        return result;

    if (NULL == result) {
        Py_DECREF(callback2);
        return NULL;
    }

    rest = Py_BuildValue("Oii", result, stackless, slp_try_stackless);
    Py_DECREF(result);
    if (rest == NULL) {
        Py_DECREF(callback2);
        return NULL;
    }

    slp_try_stackless = 0;
    result = PyObject_Call(callback2, rest, kw);
    Py_DECREF(callback2);
    Py_DECREF(rest);

    return result;
}

static PyObject *get_test_nostacklesscallobj(void)
{
    static PyTypeObject test_nostacklesscallType = {
            PyVarObject_HEAD_INIT(NULL, 0)
            "_stackless.test_nostacklesscall_type",   /*tp_name*/
            sizeof(test_nostacklesscallObject), /*tp_basicsize*/
            0,                         /*tp_itemsize*/
            0,                         /*tp_dealloc*/
            0,                         /*tp_print*/
            0,                         /*tp_getattr*/
            0,                         /*tp_setattr*/
            0,                         /*tp_reserved*/
            0,                         /*tp_repr*/
            0,                         /*tp_as_number*/
            0,                         /*tp_as_sequence*/
            0,                         /*tp_as_mapping*/
            0,                         /*tp_hash */
            test_nostacklesscall_call, /*tp_call*/
            0,                         /*tp_str*/
            0,                         /*tp_getattro*/
            0,                         /*tp_setattro*/
            0,                         /*tp_as_buffer*/
            Py_TPFLAGS_DEFAULT & ~(Py_TPFLAGS_HAVE_STACKLESS_EXTENSION | Py_TPFLAGS_HAVE_STACKLESS_CALL) ,        /*tp_flags*/
            test_nostacklesscalltype__doc__, /* tp_doc */
    };

    test_nostacklesscallType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&test_nostacklesscallType) < 0)
        return NULL;
    return (PyObject*)PyObject_New(test_nostacklesscallObject, &test_nostacklesscallType);
}


PyDoc_STRVAR(test_PyEval_EvalFrameEx__doc__,
"test_PyEval_EvalFrameEx(code, globals, args=()) -- a builtin testing function.\n\
This function tests the C-API function PyEval_EvalFrameEx(), which is not used\n\
by Stackless Python.\n\
The function creates a frame from code, globals and args and executes the frame.");

static PyObject* test_PyEval_EvalFrameEx(PyObject *self, PyObject *args, PyObject *kwds) {
    static char *kwlist[] = {"code", "globals", "args", "alloca", "throw", "oldcython",
                             "code2", NULL};
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


/******************************************************

  The Stackless External Interface

 ******************************************************/

PyObject *
PyStackless_Call_Main(PyObject *func, PyObject *args, PyObject *kwds)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf;
    PyObject *retval;

    if (ts->st.main != NULL)
        RUNTIME_ERROR(
            "Call_Main cannot run within a main tasklet", NULL);
    cf = slp_cframe_newfunc(func, args, kwds, 0);
    if (cf == NULL)
        return NULL;
    retval = slp_eval_frame((PyFrameObject *) cf);
    Py_DECREF((PyObject *)cf);
    return retval;
}

/* this one is shamelessly copied from PyObject_CallMethod */

static PyObject *
build_args(char *format, va_list va)
{
    PyObject *args;
    if (format && *format)
        args = Py_VaBuildValue(format, va);
    else
        return PyTuple_New(0);
    if (args == NULL)
        return NULL;

    if (!PyTuple_Check(args)) {
        PyObject *a;
        a = PyTuple_New(1);
        if (a == NULL)
            goto fail;
        if (PyTuple_SetItem(a, 0, args) < 0) {
            Py_DECREF(a);
            return NULL;
        }
        args = a;
    }
    return args;
fail:
    Py_DECREF(args);
    return NULL;
}

PyObject *
PyStackless_CallMethod_Main(PyObject *o, char *name, char *format, ...)
{
    va_list va;
    PyObject *args, *func = NULL, *retval;

    if (o == NULL)
        return slp_null_error();

    if (name != NULL) {
        func = PyObject_GetAttrString(o, name);
        if (func == NULL) {
            PyErr_SetString(PyExc_AttributeError, name);
            return NULL;
        }
    } else {
        /* direct call, no method lookup */
        func = o;
        Py_INCREF(func);
    }

    if (!PyCallable_Check(func))
        return slp_type_error("call of non-callable attribute");

    va_start(va, format);
    args = build_args(format, va);
    va_end(va);
    if (!args) {
        Py_DECREF(func);
        return NULL;
    }

    /* retval = PyObject_CallObject(func, args); */
    retval = PyStackless_Call_Main(func, args, NULL);
    Py_DECREF(args);
    Py_DECREF(func);
    return retval;
}

PyObject *
PyStackless_CallCMethod_Main(PyMethodDef *meth, PyObject *self, char *format, ...)
{
    va_list va;
    PyObject *func = PyCFunction_New(meth, self);
    PyObject *retval, *args;
    if (!func)
        return NULL;

    va_start(va, format);
    args = build_args(format, va);
    va_end(va);
    if (!args) {
        Py_DECREF(func);
        return NULL;
    }
    retval = PyStackless_Call_Main(func, args, NULL);
    Py_DECREF(args);
    Py_DECREF(func);
    return retval;
}

void PyStackless_SetScheduleFastcallback(slp_schedule_hook_func func)
{
    _slp_schedule_fasthook = func;
}

int PyStackless_SetScheduleCallback(PyObject *callable)
{
    PyObject * temp = _slp_schedule_hook;
    if(callable != NULL && !PyCallable_Check(callable))
        TYPE_ERROR("schedule callback must be callable", -1);
    Py_XINCREF(callable);
    _slp_schedule_hook = callable;
    if (callable!=NULL)
        PyStackless_SetScheduleFastcallback(slp_schedule_callback);
    else
        PyStackless_SetScheduleFastcallback(NULL);
    Py_XDECREF(temp);
    return 0;
}

PyDoc_STRVAR(set_schedule_callback__doc__,
"set_schedule_callback(callable) -- install a callback for scheduling.\n\
Every explicit or implicit schedule will call the callback function.\n\
Returns the previous callback or None, if none was installed.\n\
Example:\n\
  def schedule_cb(prev, next):\n\
      ...\n\
When a tasklet is dying, next is None.\n\
When main starts up or after death, prev is None.\n\
Pass None to switch monitoring off again.");

static PyObject *
set_schedule_callback(PyObject *self, PyObject *arg)
{
    PyObject * old = _slp_schedule_hook;
    if (NULL == old)
        old = Py_None;
    Py_INCREF(old);
    if (arg == Py_None)
        arg = NULL;
    if (PyStackless_SetScheduleCallback(arg)) {
        Py_DECREF(old);
        return NULL;
    }
    return old;
}

PyDoc_STRVAR(get_schedule_callback__doc__,
"get_schedule_callback() -- get the current callback for scheduling.\n\
This function returns None, if no callback is installed.");

static PyObject *
get_schedule_callback(PyObject *self)
{
    PyObject *temp = _slp_schedule_hook;
    if (NULL == temp)
        temp = Py_None;
    Py_INCREF(temp);
    return temp;
}

PyDoc_STRVAR(set_channel_callback__doc__,
"set_channel_callback(callable) -- install a callback for channels.\n\
Every send/receive action will call the callback function.\n\
Example:\n\
  def channel_cb(channel, tasklet, sending, willblock):\n\
      ...\n\
sending and willblock are integers.\n\
Pass None to switch monitoring off again.");

static PyObject *
set_channel_callback(PyObject *self, PyObject *arg)
{
    PyObject * old = slp_get_channel_callback();
    if (arg == Py_None)
        arg = NULL;
    if (PyStackless_SetChannelCallback(arg)) {
        Py_XDECREF(old);
        return NULL;
    }
    if (NULL == old) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    return old;
}

PyDoc_STRVAR(get_channel_callback__doc__,
"get_channel_callback() -- get the current callback for channels.\n\
This function returns None, if no callback is installed.");

static PyObject *
get_channel_callback(PyObject *self)
{
    PyObject *temp = slp_get_channel_callback();
    if (NULL == temp) {
        temp = Py_None;
        Py_INCREF(temp);
    }
    return temp;
}



/******************************************************

  The Introspection Stuff

 ******************************************************/

#ifdef STACKLESS_SPY

static PyObject *
_peek(PyObject *self, PyObject *v)
{
    static PyObject *o;
    PyTypeObject *t;
    int i;

    if (v == Py_None) {
        return PyLong_FromVoidPtr((void *)_peek);
    }
    if (PyCode_Check(v)) {
        return PyLong_FromVoidPtr((void *)(((PyCodeObject*)v)->co_code));
    }
    if (PyLong_Check(v) && PyLong_AsLong(v) == 0) {
        return PyLong_FromVoidPtr((void *)(&PyEval_EvalFrameEx_slp));
    }
    if (!PyLong_Check(v)) goto noobject;
    o = (PyObject*) PyLong_AsVoidPtr(v);
    /* this is plain heuristics, use for now */
    if (CANNOT_READ_MEM(o, sizeof(PyObject))) goto noobject;
    if (IS_ON_STACK(o)) goto noobject;
    if (Py_REFCNT(o) < 1 || Py_REFCNT(o) > 10000) goto noobject;
    t = Py_TYPE(o);
    for (i=0; i<100; i++) {
        if (t == &PyType_Type)
            break;
        if (CANNOT_READ_MEM(t, sizeof(PyTypeObject))) goto noobject;
        if (IS_ON_STACK(o)) goto noobject;
        if (Py_REFCNT(t) < 1 || Py_REFCNT(t) > 10000) goto noobject;
        if (!(isalpha(t->tp_name[0])||t->tp_name[0]=='_'))
            goto noobject;
        t = Py_TYPE(t);
    }
    Py_INCREF(o);
    return o;
noobject:
    Py_INCREF(v);
    return v;
}

PyDoc_STRVAR(_peek__doc__,
"_peek(adr) -- try to find an object at adr.\n\
When no object is found, the address is returned.\n\
_peek(None)  _peek's address.\n\
_peek(0)     PyEval_EvalFrame's address.\n\
_peek(frame) frame's tstate address.\n\
Internal, considered dangerous!");

#endif

/* finding refcount problems */

#if defined(Py_TRACE_REFS)

PyDoc_STRVAR(_get_refinfo__doc__,
"_get_refinfo -- return (maximum referenced object,\n"
"refcount, ref_total, computed total)");

static PyObject *
_get_refinfo(PyObject *self)
{
    PyObject *op, *max = Py_None;
    PyObject *refchain;
    Py_ssize_t ref_total = _Py_RefTotal;
    Py_ssize_t computed_total = 0;

    refchain = PyTuple_New(0)->_ob_next; /* None doesn't work in 2.2 */
    Py_DECREF(refchain->_ob_prev);
    /* find real refchain */
    while (Py_TYPE(refchain) != NULL)
        refchain = refchain->_ob_next;

    for (op = refchain->_ob_next; op != refchain; op = op->_ob_next) {
        if (Py_REFCNT(op) > Py_REFCNT(max))
            max = op;
        computed_total += Py_REFCNT(op);
    }
    return Py_BuildValue("(Onnn)", max, Py_REFCNT(max), ref_total,
                         computed_total);
}

PyDoc_STRVAR(_get_all_objects__doc__,
"_get_all_objects -- return a list with all objects but the list.");

static PyObject *
_get_all_objects(PyObject *self)
{
    PyObject *lis, *ob;
    lis = PyList_New(0);
    if (lis) {
        ob = lis->_ob_next;
        while (ob != lis && Py_TYPE(ob) != NULL) {
            if (PyList_Append(lis, ob))
                return NULL;
            ob = ob->_ob_next;
        }
    }
    return lis;
}

#endif

/******************************************************

  Special support for RPython

 ******************************************************/

/*
  In RPython, we are not allowed to use cyclic references
  without explicitly breaking them, or we leak memory.
  I wrote a tool to find out which code line causes
  unreachable objects. It works by running a full GC run
  after every line. To make this reasonably fast, it makes
  sense to remove the existing GC objects temporarily.
 */

static PyObject *
_gc_untrack(PyObject *self, PyObject *ob)
{
    PyObject_GC_UnTrack(ob);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
_gc_track(PyObject *self, PyObject *ob)
{
    PyObject_GC_Track(ob);
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(_gc_untrack__doc__,
"_gc_untrack, gc_track -- remove or add an object from the gc list.");

static PyObject *
slpmodule_getdebug(PyObject *self)
{
#ifdef _DEBUG
    PyObject *ret = Py_True;
#else
    PyObject *ret = Py_False;
#endif
    Py_INCREF(ret);
    return ret;
}

PyDoc_STRVAR(slpmodule_getdebug__doc__,
"Returns True if this is a DEBUG build");

static PyObject *
slpmodule_getuncollectables(PyObject *self)
{
    PyObject *lis = PyList_New(0);
    PyCStackObject *cst = slp_cstack_chain;

    if (lis == NULL)
        return NULL;
    do {
        if (cst->task != NULL) {
            if (PyList_Append(lis, (PyObject *) cst->task)) {
                Py_DECREF(lis);
                return NULL;
            }
        }
        cst = cst->next;
    } while (cst != slp_cstack_chain);
    return lis;
}

PyDoc_STRVAR(slpmodule_getuncollectables__doc__,
"Get a list of all tasklets which have a non-trivial C stack.\n\
These might need to be killed manually in order to free memory,\n\
since their C stack might prevent garbage collection.\n\
Note that a tasklet is reported for every C stacks it has.");

PyObject *
slp_getthreads(PyObject *self)
{
    PyObject *lis = PyList_New(0);
    PyThreadState *ts = PyThreadState_GET();
    PyInterpreterState *interp = ts->interp;

    if (lis == NULL)
        return NULL;

    SLP_HEAD_LOCK();
    for (ts = interp->tstate_head; ts != NULL; ts = ts->next) {
        PyObject *id = PyLong_FromLong(ts->thread_id);
        if (id == NULL) {
            SLP_HEAD_UNLOCK();
            Py_DECREF(lis);
            return NULL;
        }
        if (PyList_Append(lis, id)) {
            SLP_HEAD_UNLOCK();
            Py_DECREF(lis);
            Py_DECREF(id);
            return NULL;
        }
        Py_DECREF(id);
    }
    SLP_HEAD_UNLOCK();
    if(PyList_Reverse(lis)) {
        Py_DECREF(lis);
        return NULL;
    }
    return lis;
}

PyDoc_STRVAR(slpmodule_getthreads__doc__,
"Return a list of all thread ids, starting with main.");

/* List of functions defined in the module */

#define PCF PyCFunction
#define METH_KS METH_VARARGS | METH_KEYWORDS | METH_STACKLESS

static PyMethodDef stackless_methods[] = {
    {"schedule",                    (PCF)schedule,              METH_KS,
     schedule__doc__},
    {"schedule_remove",             (PCF)schedule_remove,       METH_KS,
     schedule__doc__},
    {"run",                         (PCF)run_watchdog,          METH_VARARGS | METH_KEYWORDS,
     run_watchdog__doc__},
    {"getruncount",                 (PCF)getruncount,           METH_NOARGS,
     getruncount__doc__},
    {"getcurrent",                  (PCF)getcurrent,            METH_NOARGS,
     getcurrent__doc__},
    {"getcurrentid",                (PCF)getcurrentid,          METH_NOARGS,
    getcurrentid__doc__},
    {"getmain",                     (PCF)getmain,               METH_NOARGS,
     getmain__doc__},
    {"enable_softswitch",           (PCF)enable_softswitch,     METH_O,
     enable_soft__doc__},
    {"test_cframe",                 (PCF)test_cframe,           METH_VARARGS | METH_KEYWORDS,
     test_cframe__doc__},
    {"test_cframe_nr",              (PCF)test_cframe_nr,        METH_VARARGS | METH_KEYWORDS,
    test_cframe_nr__doc__},
    {"test_outside",                (PCF)test_outside,          METH_NOARGS,
    test_outside__doc__},
    {"test_cstate",                 (PCF)test_cstate,           METH_O,
    test_cstate__doc__},
    {"test_PyEval_EvalFrameEx",     (PCF)test_PyEval_EvalFrameEx, METH_VARARGS | METH_KEYWORDS,
    test_PyEval_EvalFrameEx__doc__},
    {"set_channel_callback",        (PCF)set_channel_callback,  METH_O,
     set_channel_callback__doc__},
    {"get_channel_callback",        (PCF)get_channel_callback,  METH_NOARGS,
     get_channel_callback__doc__},
    {"set_schedule_callback",       (PCF)set_schedule_callback, METH_O,
     set_schedule_callback__doc__},
    {"get_schedule_callback",       (PCF)get_schedule_callback, METH_NOARGS,
     get_schedule_callback__doc__},
    {"_pickle_moduledict",          (PCF)slp_pickle_moduledict, METH_VARARGS,
     slp_pickle_moduledict__doc__},
    {"get_thread_info",             (PCF)get_thread_info,       METH_VARARGS,
     get_thread_info__doc__},
    {"switch_trap",                 (PCF)slpmodule_switch_trap, METH_VARARGS,
     slpmodule_switch_trap__doc__},
    {"set_error_handler",           (PCF)set_error_handler,     METH_VARARGS,
     set_error_handler__doc__},
    {"_gc_untrack",                 (PCF)_gc_untrack,           METH_O,
    _gc_untrack__doc__},
    {"_gc_track",                   (PCF)_gc_track,             METH_O,
    _gc_untrack__doc__},
#ifdef STACKLESS_SPY
    {"_peek",                       (PCF)_peek,                 METH_O,
     _peek__doc__},
#endif
#if defined(Py_TRACE_REFS)
    {"_get_refinfo",                (PCF)_get_refinfo,          METH_NOARGS,
     _get_refinfo__doc__},
    {"_get_all_objects",            (PCF)_get_all_objects,      METH_NOARGS,
     _get_all_objects__doc__},
#endif
    {"__reduce__",                  (PCF)slpmodule_reduce,      METH_NOARGS, NULL},
    {"__reduce_ex__",               (PCF)slpmodule_reduce,      METH_VARARGS, NULL},
    {"getdebug",                   (PCF)slpmodule_getdebug,    METH_NOARGS,
    slpmodule_getdebug__doc__},
    {"getuncollectables",          (PCF)slpmodule_getuncollectables,    METH_NOARGS,
    slpmodule_getuncollectables__doc__},
    {"getthreads",                 (PCF)slp_getthreads,    METH_NOARGS,
    slpmodule_getthreads__doc__},
    {NULL,                          NULL}       /* sentinel */
};


PyDoc_STRVAR(stackless_doc,
"The Stackless module allows you to do multitasking without using threads.\n\
The essential objects are tasklets and channels.\n\
Please refer to their documentation.\n\
");


/* This function is called to make sure the type has
 * space in its tp_as_mapping to hold the slpslots.  If it has
 * the Py_TPFLAGS_HAVE_STACKLESS_EXTENSION bit, then any struct present
 * is deemed large enough.  But if it is missing, then a struct already
 * present needs to be grown.
 *
 * This function is the only place where we set Py_TPFLAGS_HAVE_STACKLESS_EXTENSION
 * for heap types (== type with Py_TPFLAGS_HEAPTYPE). This way we can free
 * the memory later in type_dealloc() in typeobject.c.
 */
int
slp_prepare_slots(PyTypeObject * type)
{
    PyMappingMethods *map, *old = type->tp_as_mapping;
    if (old == NULL || !(type->tp_flags & Py_TPFLAGS_HAVE_STACKLESS_EXTENSION)) {
        /* need to allocate a new struct */

        /* For heap types ensure, that 'old' is part of the
         * PyHeapTypeObject. Otherwise we would leak 'old'.
         */
        assert(!(type->tp_flags & Py_TPFLAGS_HEAPTYPE) ||
                (old == (PyMappingMethods*)
                        &((PyHeapTypeObject*)type)->as_mapping));

        map = (PyMappingMethods *)PyObject_Calloc(1, sizeof(PyMappingMethods));
        if (map == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (old) {
            /* copy the old method slots, and mark the type */
            memcpy(map, old, offsetof(PyMappingMethods, slpflags));
        }
        /* We are leaking 'map' and 'old', except for heap types
         * (with Py_TPFLAGS_HEAPTYPE set)). Theses types have a
         * tp_dealloc function, which frees 'map'.
         * For heap types 'old' is a member of PyHeapTypeObject and
         * must not be freed.
         */
        type->tp_as_mapping = map;
        type->tp_flags |= Py_TPFLAGS_HAVE_STACKLESS_EXTENSION;
    }
    return 0;
}

/* initial seeding of the stackless slot information into the types.
 * it will then be propagated using slot copying
 */
static int init_stackless_methods(void)
{
#if !STACKLESS_NO_TYPEINFO
    _stackless_method *p = _stackless_methtable;

    for (; p->type != NULL; p++) {
        PyTypeObject *t = p->type;
        size_t ind = p->offset & MFLAG_IND;
        size_t ofs = p->offset - ind;

        if (ind)
            t = *((PyTypeObject **)t);
        /* this type has stackless slot methods.  Set the flag */
        if (slp_prepare_slots(t))
            return -1;
        ((signed char *) t->tp_as_mapping)[ofs] = -1;
        if (t->tp_as_mapping->slpflags.tp_call)
            t->tp_flags |= Py_TPFLAGS_HAVE_STACKLESS_CALL;  /* shortcut flag */
    }
#endif
    return 0;
}

/* this one must be called very early, before modules are used */
int
_PyStackless_InitTypes(void)
{
    /* record the thread state for thread support */
    slp_initial_tstate = PyThreadState_GET();

    if (0
        || slp_init_bombtype()
        || init_stackless_methods()
        || PyType_Ready(&PyTasklet_Type) /* need this early for the main tasklet */
        )
        return 0;
    return -1;
}

void
PyStackless_Fini(void)
{
    slp_scheduling_fini();
    slp_cframe_fini();
    slp_stacklesseval_fini();
}

PyMODINIT_FUNC
PyInit__stackless(void)
{
    PyObject *slp_module, *dict, *tmp;
    static struct PyModuleDef stacklessmodule = {
        PyModuleDef_HEAD_INIT,
        "_stackless",
        stackless_doc,
        -1,
        stackless_methods,
    };

    if (0
        || init_cframetype()
        || PyType_Ready(&PyChannel_Type)
        || PyType_Ready(&PyAtomic_Type)
        )
        return NULL;

    /* Create the module and add the functions */
    slp_module = PyModule_Create(&stacklessmodule);
    if (slp_module == NULL)
        return NULL;

    dict = PyModule_GetDict(slp_module);

#define INSERT(name, object) \
    if (PyDict_SetItemString(dict, name, (PyObject*)object) < 0) goto fail;

    INSERT("cframe",    &PyCFrame_Type);
    INSERT("cstack",    &PyCStack_Type);
    INSERT("bomb",        &PyBomb_Type);
    INSERT("tasklet",   &PyTasklet_Type);
    INSERT("channel",   &PyChannel_Type);
    INSERT("atomic",    &PyAtomic_Type);
    INSERT("pickle_with_tracing_state", Py_False);

    tmp = get_test_nostacklesscallobj();
    if (!tmp)
        goto fail;
    INSERT("_test_nostacklesscall", tmp);
    Py_DECREF(tmp);

    /* add the prickelpit submodule */
    tmp = init_prickelpit();
    if (tmp == NULL)
        goto fail;
    INSERT("_wrap", tmp);
    Py_DECREF(tmp);
    if (PyDict_SetItemString(PyImport_GetModuleDict(),
            "_stackless._wrap", tmp))
        goto fail;;

    return slp_module;
fail:
    Py_XDECREF(slp_module);
    return NULL;
#undef INSERT
}

#endif
