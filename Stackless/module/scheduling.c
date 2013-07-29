#include "Python.h"

#ifdef STACKLESS
#include "core/stackless_impl.h"
#include "core/stackless_tealet.h"

#ifdef WITH_THREAD
#include "pythread.h"
#endif

/******************************************************

  The Bomb object -- making exceptions convenient

 ******************************************************/

static PyBombObject *free_list = NULL;
static int numfree = 0;         /* number of bombs currently in free_list */
#define MAXFREELIST 20          /* max value for numfree */

static void
bomb_dealloc(PyBombObject *bomb)
{
    _PyObject_GC_UNTRACK(bomb);
    Py_XDECREF(bomb->curexc_type);
    Py_XDECREF(bomb->curexc_value);
    Py_XDECREF(bomb->curexc_traceback);
    if (numfree < MAXFREELIST) {
        ++numfree;
        bomb->curexc_type = (PyObject *) free_list;
        free_list = bomb;
    }
    else
        PyObject_GC_Del(bomb);
}

static int
bomb_traverse(PyBombObject *bomb, visitproc visit, void *arg)
{
    Py_VISIT(bomb->curexc_type);
    Py_VISIT(bomb->curexc_value);
    Py_VISIT(bomb->curexc_traceback);
    return 0;
}

static void
bomb_clear(PyBombObject *bomb)
{
    Py_CLEAR(bomb->curexc_type);
    Py_CLEAR(bomb->curexc_value);
    Py_CLEAR(bomb->curexc_traceback);
}

static PyBombObject *
new_bomb(void)
{
    PyBombObject *bomb;

    if (free_list == NULL) {
        bomb = PyObject_GC_New(PyBombObject, &PyBomb_Type);
        if (bomb == NULL)
            return NULL;
    }
    else {
        assert(numfree > 0);
        --numfree;
        bomb = free_list;
        free_list = (PyBombObject *) free_list->curexc_type;
        _Py_NewReference((PyObject *) bomb);
    }
    bomb->curexc_type = NULL;
    bomb->curexc_value = NULL;
    bomb->curexc_traceback = NULL;
    PyObject_GC_Track(bomb);
    return bomb;
}

static PyObject *
bomb_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"type", "value", "traceback", NULL};
    PyBombObject *bomb = new_bomb();

    if (bomb == NULL)
        return NULL;

    if (PyTuple_GET_SIZE(args) == 1 &&
        PyTuple_Check(PyTuple_GET_ITEM(args, 0)))
        args = PyTuple_GET_ITEM(args, 0);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOO:bomb", kwlist,
                                     &bomb->curexc_type,
                                     &bomb->curexc_value,
                                     &bomb->curexc_traceback)) {
        Py_DECREF(bomb);
        return NULL;
    }
    Py_XINCREF(bomb->curexc_type);
    Py_XINCREF(bomb->curexc_value);
    Py_XINCREF(bomb->curexc_traceback);
    return (PyObject*) bomb;
}

PyObject *
slp_make_bomb(PyObject *klass, PyObject *args, char *msg)
{
    PyBombObject *bomb;
    PyObject *tup;

    if (! (PyObject_IsSubclass(klass, PyExc_BaseException) == 1 ||
           PyString_Check(klass) ) ) {
        char s[256];

        sprintf(s, "%.128s needs Exception or string"
                   " subclass as first parameter", msg);
        TYPE_ERROR(s, NULL);
    }
    if ( (bomb = new_bomb()) == NULL)
        return NULL;
    Py_INCREF(klass);
    bomb->curexc_type = klass;
    tup = Py_BuildValue(PyTuple_Check(args) ? "O" : "(O)", args);
    bomb->curexc_value = tup;
    if (tup == NULL) {
        Py_DECREF(bomb);
        return NULL;
    }
    return (PyObject *) bomb;
}

PyObject *
slp_exc_to_bomb(PyObject *exc, PyObject *val, PyObject *tb)
{
    PyBombObject *bomb;

    /* normalize the exceptions according to "raise" semantics */
    if (tb && tb != Py_None && !PyTraceBack_Check(tb)) {
        PyErr_SetString(PyExc_TypeError,
            "third argument must be a traceback object");
        return NULL;
    }
    if (PyExceptionClass_Check(exc)) {
        ; /* it will be normalized on other side */
        /*PyErr_NormalizeException(&typ, &val, &tb);*/
    } else if (PyExceptionInstance_Check(exc)) {
        /* Raising an instance.  The value should be a dummy. */
        if (val && val != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                "instance exception may not have a separate value");
            return NULL;
        }
        /* Normalize to raise <class>, <instance> */
        val = exc;
        exc = PyExceptionInstance_Class(exc);
    } else {
        /* Not something you can raise.  throw() fails. */
        PyErr_Format(PyExc_TypeError,
            "exceptions must be classes, or instances, not %s",
            Py_TYPE(exc)->tp_name);
        return NULL;
    }

    bomb = new_bomb();
    if (bomb == NULL)
        return NULL;

    Py_XINCREF(exc);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    bomb->curexc_type = exc;
    bomb->curexc_value = val;
    bomb->curexc_traceback = tb;
    return (PyObject *) bomb;
}

PyObject *
slp_curexc_to_bomb(void)
{
    PyBombObject *bomb = new_bomb();

    if (bomb != NULL)
        PyErr_Fetch(&bomb->curexc_type, &bomb->curexc_value,
                    &bomb->curexc_traceback);
    return (PyObject *) bomb;
}

/* set exception, consume bomb reference and return NULL */

PyObject *
slp_bomb_explode(PyObject *_bomb)
{
    PyBombObject* bomb = (PyBombObject*)_bomb;

    assert(PyBomb_Check(bomb));
    Py_XINCREF(bomb->curexc_type);
    Py_XINCREF(bomb->curexc_value);
    Py_XINCREF(bomb->curexc_traceback);
    PyErr_Restore(bomb->curexc_type, bomb->curexc_value,
                  bomb->curexc_traceback);
    Py_DECREF(bomb);
    return NULL;
}

static PyObject *
bomb_reduce(PyBombObject *bomb)
{
    PyObject *tup;

    tup = slp_into_tuple_with_nulls(&bomb->curexc_type, 3);
    if (tup != NULL)
        tup = Py_BuildValue("(O()O)", &PyBomb_Type, tup);
    return tup;
}

static PyObject *
bomb_setstate(PyBombObject *bomb, PyObject *args)
{
    if (PyTuple_GET_SIZE(args) != 4)
        VALUE_ERROR("bad exception tuple for bomb", NULL);
    bomb_clear(bomb);
    slp_from_tuple_with_nulls(&bomb->curexc_type, args);
    Py_INCREF(bomb);
    return (PyObject *) bomb;
}

static PyMemberDef bomb_members[] = {
    {"type",            T_OBJECT, offsetof(PyBombObject, curexc_type), READONLY},
    {"value",           T_OBJECT, offsetof(PyBombObject, curexc_value), READONLY},
    {"traceback",       T_OBJECT, offsetof(PyBombObject, curexc_traceback), READONLY},
    {0}
};

static PyMethodDef bomb_methods[] = {
    {"__reduce__",              (PyCFunction)bomb_reduce,       METH_NOARGS},
    {"__reduce_ex__",           (PyCFunction)bomb_reduce,       METH_VARARGS},
    {"__setstate__",            (PyCFunction)bomb_setstate,     METH_O},
    {NULL,     NULL}             /* sentinel */
};

PyDoc_STRVAR(bomb__doc__,
"A bomb object is used to hold exceptions in tasklets.\n\
Whenever a tasklet is activated and its tempval is a bomb,\n\
it will explode as an exception.\n\
\n\
You can create a bomb by hand and attach it to a tasklet if you like.\n\
Note that bombs are 'sloppy' about the argument list, which means that\n\
the following works, although you should use '*sys.exc_info()'.\n\
\n\
from stackless import *; import sys\n\
t = tasklet(lambda:42)()\n\
try: 1/0\n\
except: b = bomb(sys.exc_info())\n\
\n\
t.tempval = b\n\
t.run()  # let the bomb explode");

PyTypeObject PyBomb_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "stackless.bomb",
    sizeof(PyBombObject),
    0,
    (destructor)bomb_dealloc,                   /* tp_dealloc */
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
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC, /* tp_flags */
    bomb__doc__,                                /* tp_doc */
    (traverseproc)bomb_traverse,                /* tp_traverse */
    (inquiry) bomb_clear,                       /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    bomb_methods,                               /* tp_methods */
    bomb_members,                               /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    bomb_new,                                   /* tp_new */
    _PyObject_GC_Del,                           /* tp_free */
};


/*******************************************************************

  Exception handling revised.

  Every tasklet has its own exception state. The thread state
  exists only once for the current thread, so we need to simulate
  ownership.

  Whenever a tasklet is run for the first time, it should clear
  the exception variables and start with a clean state.

  When it dies normally, it should clean up these.

  When a transfer occours from one tasklet to another,
  the first tasklet should save its exception in local variables,
  and the other one should restore its own.

  When a tasklet dies with an uncaught exception, this will
  be passed on to the main tasklet. The main tasklet must
  clear its exception state, if any, and take over those of
  the tasklet resting in peace.

  2002-08-08 This was a misconception. This is not the current
  exception, but the one which ceval provides for exception
  handlers. That means, sharing of this variable is totally
  unrelated to the current error, and we always swap the data.

  Added support for trace/profile as well.

 ********************************************************************/

slp_schedule_hook_func *_slp_schedule_fasthook;
PyObject *_slp_schedule_hook;

/* scheduler monitoring */

int
slp_schedule_callback(PyTaskletObject *prev, PyTaskletObject *next)
{
    PyObject *args;
    PyObject *tmp;
    PyThreadState *ts = PyThreadState_GET();

    if (prev == NULL) prev = (PyTaskletObject *)Py_None;
    if (next == NULL) next = (PyTaskletObject *)Py_None;
    args = Py_BuildValue("(OO)", prev, next);
    if (args != NULL) {
        PyObject *type, *value, *traceback, *ret;

        /* store the del post switch away temporarily to not have it
         * released in the dispatching loop we are about to invoke
         */
        tmp = ts->st.del_post_switch;
        ts->st.del_post_switch = NULL;

        PyErr_Fetch(&type, &value, &traceback);
        ret = PyObject_Call(_slp_schedule_hook, args, NULL);

        assert(ts->st.del_post_switch == NULL);
        ts->st.del_post_switch = tmp;

        if (ret != NULL)
            PyErr_Restore(type, value, traceback);
        else {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
        }
        Py_XDECREF(ret);
        Py_DECREF(args);
        return ret ? 0 : -1;
    }
    else
        return -1;
}

#define NOTIFY_SCHEDULE(prev, next, errflag) \
    if (_slp_schedule_fasthook != NULL) { \
        int ret; \
        if (ts->st.schedlock) { \
            RUNTIME_ERROR( \
                "Recursive scheduler call due to callbacks!", \
                errflag); \
        } \
        ts->st.schedlock = 1; \
        ret = _slp_schedule_fasthook(prev, next); \
        ts->st.schedlock = 0; \
        if (ret) return errflag; \
    }

static void
kill_wrap_bad_guy(PyTaskletObject *prev, PyTaskletObject *bad_guy)
{
    /*
     * just in case a transfer didn't work, we pack the bad
     * tasklet into the exception and remove it from the runnables.
     *
     */
    PyThreadState *ts = PyThreadState_GET();
    PyObject *newval = PyTuple_New(2);
    if (bad_guy->next != NULL) {
        ts->st.current = bad_guy;
        slp_current_remove();
    }
    /* restore last tasklet */
    if (prev->next == NULL)
        slp_current_insert(prev);
    ts->frame = prev->f.frame;
    ts->st.current = prev;
    if (newval != NULL) {
        /* merge bad guy into exception */
        PyObject *exc, *val, *tb;
        PyErr_Fetch(&exc, &val, &tb);
        PyTuple_SET_ITEM(newval, 0, val);
        PyTuple_SET_ITEM(newval, 1, (PyObject*)bad_guy);
        Py_INCREF(bad_guy);
        PyErr_Restore(exc, newval, tb);
    }
}

/* slp_schedule_task is moved down and merged with soft switching */

/* non-recursive scheduling */

static PyObject *
restore_exception(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = (PyCFrameObject *) f;

    f = cf->f_back;
    ts->exc_type = cf->ob1;
    ts->exc_value = cf->ob2;
    ts->exc_traceback = cf->ob3;
    cf->ob1 = cf->ob2 = cf->ob3 = NULL;
    Py_DECREF(cf);
    ts->frame = f;
    return STACKLESS_PACK(retval);
}

static PyObject *
restore_tracing(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = (PyCFrameObject *) f;

    f = cf->f_back;
    ts->c_tracefunc = (Py_tracefunc)cf->any1;
    ts->c_profilefunc = (Py_tracefunc)cf->any2;
    ts->c_traceobj = cf->ob1;
    ts->c_profileobj = cf->ob2;
    ts->tracing = cf->i;
    ts->use_tracing = cf->n;
    /* the objects *have* extra references here */
    Py_DECREF(cf);
    ts->frame = f;
    return STACKLESS_PACK(retval);
}

/* jumping from a soft tasklet to a hard switched */

static PyObject *
jump_soft_to_hard(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();

    ts->frame = f->f_back;

    /* reinstate the del_post_switch */
    assert(ts->st.del_post_switch == NULL);
    ts->st.del_post_switch = ((PyCFrameObject*)f)->ob1;
    ((PyCFrameObject*)f)->ob1 = NULL;

    Py_DECREF(f);
    /* ignore retval. everything is in the tasklet. */
    Py_DECREF(retval); /* consume ref according to protocol */
    slp_transfer_return(ts->st.current->cstate);
    /* we either have an error or don't come back, so: */
    return NULL;
}


/* combined soft/hard switching */


/* check whether a different thread can be run */

static int
is_thread_runnable(PyThreadState *ts)
{
#ifdef WITH_THREAD
    if (ts == PyThreadState_GET())
        return 0;
    return !ts->st.thread.is_blocked;
#endif
    return 0;
}

static int
check_for_deadlock(void)
{
    PyThreadState *ts = PyThreadState_GET();
    PyInterpreterState *interp = ts->interp;

    /* see if anybody else will be able to run */

    for (ts = interp->tstate_head; ts != NULL; ts = ts->next)
        if (is_thread_runnable(ts))
            return 0;
    return 1;
}

static PyObject *
make_deadlock_bomb(void)
{
    PyErr_SetString(PyExc_RuntimeError,
        "Deadlock: the last runnable tasklet cannot be blocked.");
    return slp_curexc_to_bomb();
}

#ifdef WITH_THREAD

/* make sure that locks live longer than their threads */

static void
destruct_lock(PyObject *capsule)
{
    PyThread_type_lock lock = PyCapsule_GetPointer(capsule, 0);
    if (lock) {
        PyThread_acquire_lock(lock, 0);
        PyThread_release_lock(lock);
        PyThread_free_lock(lock);
    }
}

static PyObject *
new_lock(void)
{
    PyThread_type_lock lock;

    lock = PyThread_allocate_lock();
    if (lock == NULL) return NULL;

    return PyCapsule_New(lock, 0, destruct_lock);
}

#define get_lock(obj) PyCapsule_GetPointer(obj, 0)

#define acquire_lock(lock, flag) PyThread_acquire_lock(get_lock(lock), flag)
#define release_lock(lock) PyThread_release_lock(get_lock(lock))

static int schedule_thread_block(PyThreadState *ts)
{
    assert(!ts->st.thread.is_blocked);
    /* create on demand the lock we use to block */
    if (ts->st.thread.block_lock == NULL) {
        if (!(ts->st.thread.block_lock = new_lock()))
            return -1;
        acquire_lock(ts->st.thread.block_lock, 1);
    }

    /* block */
    ts->st.thread.is_blocked = 1;
    Py_BEGIN_ALLOW_THREADS
    acquire_lock(ts->st.thread.block_lock, 1);
    Py_END_ALLOW_THREADS

    /* Now we have switched (on this thread), clear any post-switch stuff.
     * We may have a valuable "tmpval" here
     * because of channel switching, so be careful to maintain that.
     */
    if (ts->st.del_post_switch) {
        PyObject *tmp;
        TASKLET_CLAIMVAL(ts->st.current, &tmp);
        Py_CLEAR(ts->st.del_post_switch);
        TASKLET_SETVAL_OWN(ts->st.current, tmp);
    }
    return 0;
}

static void schedule_thread_unblock(PyThreadState *nts)
{
    if (nts->st.thread.is_blocked) {
        nts->st.thread.is_blocked = 0;
        release_lock(nts->st.thread.block_lock);
    }
}

void slp_thread_unblock(PyThreadState *nts)
{
    schedule_thread_unblock(nts);
}

#else

void slp_thread_unblock(PyThreadState *nts)
{}

#endif

static int
schedule_task_block(PyObject **result, PyTaskletObject *prev, int stackless, int *did_switch)
{
    PyThreadState *ts = PyThreadState_GET();
    PyObject *retval, *tmpval=NULL;
    PyTaskletObject *next = NULL;
    int fail, revive_main = 0;

#ifdef WITH_THREAD
    if ( !(ts->st.runflags & Py_WATCHDOG_THREADBLOCK) && ts->st.main->next == NULL)
        /* we also must never block if watchdog is running not in threadblocking mode */
        revive_main = 1;

    if (revive_main)
        assert(ts->st.main->next == NULL); /* main must be floating */
#endif

    if (revive_main || check_for_deadlock()) {
        if (revive_main || (ts == slp_initial_tstate && ts->st.main->next == NULL)) {
            /* emulate old revive_main behavior:
             * passing a value only if it is an exception
             */
            if (PyBomb_Check(prev->tempval)) {
                TASKLET_CLAIMVAL(ts->st.main, &tmpval);
                TASKLET_SETVAL(ts->st.main, prev->tempval);
            }
            fail = slp_schedule_task(result, prev, ts->st.main, stackless, did_switch);
            if (fail && tmpval != NULL)
                TASKLET_SETVAL_OWN(ts->st.main, tmpval);
            else
                Py_XDECREF(tmpval);
            return fail;
        }
        if (!(retval = make_deadlock_bomb()))
            return -1;
        TASKLET_CLAIMVAL(ts->st.main, &tmpval);
        TASKLET_SETVAL_OWN(prev, retval);
        fail = slp_schedule_task(result, prev, prev, stackless, did_switch);
        if (fail && tmpval != NULL)
            TASKLET_SETVAL_OWN(ts->st.main, tmpval);
        else
            Py_XDECREF(tmpval);
        return fail;
    }
#ifdef WITH_THREAD
    if (schedule_thread_block(ts))
        return -1;

    /* now we should have something in the runnable queue */
    next = slp_current_remove();
    if (!next) {
        /* weird, but what the h */
        next = prev;
        Py_INCREF(next);
    }
#else
    next = prev;
    Py_INCREF(next);
#endif
    /* this must be after releasing the locks because of hard switching */
    fail = slp_schedule_task(result, prev, next, stackless, did_switch);
    Py_DECREF(next);
    return fail;
}

#ifdef WITH_THREAD
static int
schedule_task_interthread(PyObject **result,
                            PyTaskletObject *prev,
                            PyTaskletObject *next,
                            int stackless,
                            int *did_switch)
{
    PyThreadState *nts = next->tstate;
    int fail;

    /* get myself ready, since the previous task is going to continue on the
     * current thread
     */
    fail = slp_schedule_task(result, prev, prev, stackless, did_switch);
    if (fail)
        return fail;

    /* put the next tasklet in the target thread's queue */
   if (next->flags.blocked) {
        /* unblock from channel */
        slp_channel_remove_slow(next, NULL, NULL, NULL);
        slp_current_insert(next);
    }
    else if (next->next == NULL) {
        /* reactivate floating task */
        Py_INCREF(next);
        slp_current_insert(next);
    }

    /* unblock the thread if required */
    schedule_thread_unblock(nts);

    return 0;
}

#endif

/* deal with soft interrupts by modifying next to specify the main tasklet */
static void slp_schedule_soft_irq(PyThreadState *ts, PyTaskletObject *prev,
                                                   PyTaskletObject **next, int not_now)
{
    PyTaskletObject *tmp;
    assert(*next);
    if(!prev->flags.pending_irq || !(ts->st.runflags & PY_WATCHDOG_SOFT) )
        return; /* no soft interrupt pending */

    prev->flags.pending_irq = 0;
    if (ts->st.main->next != NULL)
        return; /* main isn't floating, we are probably raising an exception */

    /* if were were swithing from main or to main, we don't do anything */
    if (prev == ts->st.main || *next == ts->st.main)
        return;

    if (not_now || !TASKLET_NESTING_OK(prev)) {
        /* pass the irq flag to the next tasklet */
        (*next)->flags.pending_irq = 1;
        return;
    }

    /* Soft interrupt.  Signal that an interrupt took place by placing
     * the "next" tasklet into interrupted (it would have been run) */
    assert(ts->st.interrupted == NULL);
    ts->st.interrupted = (PyObject*)*next;
    Py_INCREF(ts->st.interrupted);

    /* restore main.  insert it before the old next, so that the old next get
     * run after it
     */
    tmp = ts->st.current;
    ts->st.current = *next;
    slp_current_insert(ts->st.main);
    Py_INCREF(ts->st.main);
    ts->st.current = tmp;

    *next = ts->st.main;
}


static int
slp_schedule_task_prepared(PyThreadState *ts, PyObject **result, PyTaskletObject *prev,
                        PyTaskletObject *next, int stackless,
                        int *did_switch);


int
slp_schedule_task(PyObject **result, PyTaskletObject *prev, PyTaskletObject *next, int stackless,
                  int *did_switch)
{
    PyThreadState *ts = PyThreadState_GET();
    PyChannelObject *u_chan = NULL;
    PyTaskletObject *u_next;
    int u_dir;
    int inserted = 0;
    int fail;

    *result = NULL;
    if (did_switch)
        *did_switch = 0; /* only set this if an actual switch occurs */

    if (next == NULL)
        return schedule_task_block(result, prev, stackless, did_switch);

#ifdef WITH_THREAD
    if (next->tstate != ts) {
        return schedule_task_interthread(result, prev, next, stackless, did_switch);
    }
#endif

    /* switch trap. We don't trap on interthread switches because they
     * don't cause a switch on the local thread.
     */
    if (ts->st.switch_trap) {
        if (prev != next) {
            PyErr_SetString(PyExc_RuntimeError, "switch_trap");
            return -1;
        }
    }

    /* prepare the new tasklet */
    if (next->flags.blocked) {
        /* unblock from channel */
        slp_channel_remove_slow(next, &u_chan, &u_dir, &u_next);
        slp_current_insert(next);
        inserted = 1;
    }
    else if (next->next == NULL) {
        /* reactivate floating task */
        Py_INCREF(next);
        slp_current_insert(next);
        inserted = 1;
    }

    fail = slp_schedule_task_prepared(ts, result, prev, next, stackless, did_switch);
    if (fail && inserted) {
        slp_current_uninsert(next);
        if (u_chan)
            slp_channel_insert(u_chan, next, u_dir, u_next);
        else
            Py_DECREF(next);
    }
    return fail;
}

static int
slp_schedule_task_prepared(PyThreadState *ts, PyObject **result, PyTaskletObject *prev, PyTaskletObject *next, int stackless,
                  int *did_switch)
{
    PyObject *retval;
    int (*transfer)(PyThreadState *, tealet_t *, PyTaskletObject *);

    /* remove the no-soft-irq flag from the runflags */
    int no_soft_irq = ts->st.runflags & PY_WATCHDOG_NO_SOFT_IRQ;
    ts->st.runflags &= ~PY_WATCHDOG_NO_SOFT_IRQ;

    slp_schedule_soft_irq(ts, prev, &next, no_soft_irq);

    if (prev == next) {
        TASKLET_CLAIMVAL(prev, &retval);
        if (PyBomb_Check(retval))
            retval = slp_bomb_explode(retval);
        *result = retval;
        return 0;
    }

    NOTIFY_SCHEDULE(prev, next, -1);

    if (!(ts->st.runflags & PY_WATCHDOG_TOTALTIMEOUT))
        ts->st.ticker = ts->st.interval; /* reset timeslice */
    prev->recursion_depth = ts->recursion_depth;
    prev->f.frame = ts->frame;

    if (!stackless || ts->st.nesting_level != 0)
        goto hard_switching;

    /* start of soft switching code */

    /* handle exception */
    if (ts->exc_type == Py_None) {
        Py_XDECREF(ts->exc_type);
        ts->exc_type = NULL;
    }
    if (ts->exc_type != NULL) {
        /* build a shadow frame if we are returning here*/
        if (ts->frame != NULL) {
            PyCFrameObject *f = slp_cframe_new(restore_exception, 1);
            if (f == NULL)
                return -1;
            f->ob1 = ts->exc_type;
            f->ob2 = ts->exc_value;
            f->ob3 = ts->exc_traceback;
            prev->f.frame = (PyFrameObject *) f;
        }
        ts->exc_type = ts->exc_value =
                           ts->exc_traceback = NULL;
    }
    if (ts->use_tracing || ts->tracing) {
        /* build a shadow frame if we are returning here */
        if (ts->frame != NULL) {
            PyCFrameObject *f = slp_cframe_new(restore_tracing, 1);
            if (f == NULL)
                return -1;
            f->any1 = ts->c_tracefunc;
            f->any2 = ts->c_profilefunc;
            f->ob1 = ts->c_traceobj;
            f->ob2 = ts->c_profileobj;
            /* trace/profile does not add references */
            Py_XINCREF(f->ob1);
            Py_XINCREF(f->ob2);
            f->i = ts->tracing;
            f->n = ts->use_tracing;
            prev->f.frame = (PyFrameObject *) f;
        }
        ts->c_tracefunc = ts->c_profilefunc = NULL;
        ts->c_traceobj = ts->c_profileobj = NULL;
        ts->tracing = ts->use_tracing = 0;
    }
    ts->frame = next->f.frame;
    next->f.frame = NULL;

    ts->recursion_depth = next->recursion_depth;

    ts->st.current = next;
    if (did_switch)
        *did_switch = 1;

    if (next->cstate) {
        /* create a helper frame to restore the target stack */
        ts->frame = (PyFrameObject *)
                    slp_cframe_new(jump_soft_to_hard, 1);
        if (ts->frame == NULL) {
            ts->frame = prev->f.frame;
            return -1;
        }

        /* Move the del_post_switch into the cframe for it to resurrect it.
         * switching isn't complete until after it has run
         */
        ((PyCFrameObject*)ts->frame)->ob1 = ts->st.del_post_switch;
        ts->st.del_post_switch = NULL;

        /* note that we don't explode any bomb now and leave it in next->tempval */
        /* retval will be ignored eventually */
        retval = next->tempval;
        Py_INCREF(retval);
        *result = STACKLESS_PACK(retval);
        return 0;
    }

    TASKLET_CLAIMVAL(next, &retval);
    if (PyBomb_Check(retval))
        retval = slp_bomb_explode(retval);

    *result = STACKLESS_PACK(retval);
    return 0;

hard_switching:
    /* since we change the stack we must assure that the protocol was met */
    STACKLESS_ASSERT();

    /* note: nesting_level is handled in slp_transfer */
    
    ts->st.current = next;

    if (ts->exc_type == Py_None) {
        Py_XDECREF(ts->exc_type);
        ts->exc_type = NULL;
    }
    ts->recursion_depth = next->recursion_depth;
    ts->frame = next->f.frame;
    next->f.frame = NULL;

    ++ts->st.nesting_level;
    if (ts->exc_type != NULL || ts->use_tracing || ts->tracing)
        transfer = slp_transfer_with_exc;
    else
        transfer = slp_transfer;

    if (transfer(ts, next->cstate, prev) >= 0) {
        --ts->st.nesting_level;
        TASKLET_CLAIMVAL(prev, &retval);
        if (PyBomb_Check(retval))
            retval = slp_bomb_explode(retval);
        if (did_switch)
            *did_switch = 1;
        *result = retval;
        return 0;
    }
    else {
        --ts->st.nesting_level;
        kill_wrap_bad_guy(prev, next);
        return -1;
    }
}

int
initialize_main_and_current(void)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *task;
    PyObject *noargs;

    /* refuse executing main in an unhandled error context */
    if (! (PyErr_Occurred() == NULL || PyErr_Occurred() == Py_None) ) {
#ifdef _DEBUG
        PyObject *type, *value, *traceback;
        PyErr_Fetch(&type, &value, &traceback);
        Py_XINCREF(type);
        Py_XINCREF(value);
        Py_XINCREF(traceback);
        PyErr_Restore(type, value, traceback);
        printf("Pending error while entering Stackless subsystem:\n");
        PyErr_Print();
        printf("Above exception is re-raised to the caller.\n");
        PyErr_Restore(type, value, traceback);
#endif
    return 1;
    }

    noargs = PyTuple_New(0);
    task = (PyTaskletObject *) PyTasklet_Type.tp_new(
                &PyTasklet_Type, noargs, NULL);
    Py_DECREF(noargs);
    if (task == NULL) return -1;
    task->cstate = NULL;
    task->tstate = ts;
    task->nesting_level = 0;
    ts->st.main = task;
    Py_INCREF(task);
    slp_current_insert(task);
    ts->st.current = task;

    NOTIFY_SCHEDULE(NULL, task, -1);

    return 0;
}


/* scheduling and destructing the previous one */

static PyObject *
schedule_task_destruct(PyTaskletObject *prev, PyTaskletObject *next)
{
    /*
     * The problem is to leave the dying tasklet alive
     * until we have done the switch.  We use the st->ts.del_post_switch
     * field to help us with that, someone else with decref it.
     */
    PyThreadState *ts = PyThreadState_GET();
    PyObject *retval;

    /* we should have no nesting level */
    assert(ts->st.nesting_level == 0);
    /* even there is a (buggy) nesting, ensure soft switch */
    if (ts->st.nesting_level != 0) {
        /* TODO: Old message with no context for what to do.  Revisit?
        printf("XXX error, nesting_level = %d\n", ts->st.nesting_level); */
        ts->st.nesting_level = 0;
    }

    /* update what's not yet updated

    Normal tasklets when created have no recursion depth yet, but the main
    tasklet is initialized in the middle of an existing indeterminate call
    stack.  Therefore it is not guaranteed that there is not a pre-existing
    recursion depth from before its initialization. So, assert that this
    is zero, or that we are the main tasklet being destroyed (see tasklet_end)
    */
    assert(ts->recursion_depth == 0 || (ts->st.main == NULL && prev == next));
    prev->recursion_depth = 0;
    assert(ts->frame == NULL);
    prev->f.frame = NULL;

    /* The tasklet can only be cleanly decrefed after we have completely
     * switched to another one, because the decref can trigger tasklet
     * swithes. this would otherwise mess up our soft switching.  Generally
     * nothing significant must happen once we are unwinding the stack.
     */
    assert(ts->st.del_post_switch == NULL);
    ts->st.del_post_switch = (PyObject *)prev;

    /* do a soft switch */
    if (prev != next) {
        int switched;
        int fail = slp_schedule_task(&retval, prev, next, 1, &switched);
        if (!switched || fail)
            /* something happened, cancel prev's decref */
            ts->st.del_post_switch  = 0;
        if (fail)
            /* the interface of this function can't deal with failing to switch */
            return NULL;
    } else {
        /* main is exiting */
        TASKLET_CLAIMVAL(prev, &retval);
        if (PyBomb_Check(retval))
            retval = slp_bomb_explode(retval);
    }
    return retval;
}

/* defined in pythonrun.c */
extern void PyStackless_HandleSystemExit(void);
/* defined in stacklessmodule.c */
extern int PyStackless_CallErrorHandler(void);

static PyObject *
tasklet_end(PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *task = ts->st.current;
    PyTaskletObject *next;

    int ismain = task == ts->st.main;

    /*
     * see whether we have a SystemExit, which is no error.
     * Note that TaskletExit is a subclass.
     * otherwise make the exception into a bomb.
     */
    if (retval == NULL) {
        if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
            /* but if it is truly a SystemExit on the main thread, we want the exit! */
            if (ts == slp_initial_tstate && !PyErr_ExceptionMatches(PyExc_TaskletExit))
                PyStackless_HandleSystemExit();
            PyErr_Clear();
            Py_INCREF(Py_None);
            retval = Py_None;
        }
        else {
            if (!ismain) {
                /* non-main tasklets get the chance to handle errors.
                 * errors in the handlers (or a non-present one)
                 * result in the standard behaviour, transfering the error
                 * to the main tasklet
                 */
                if (!PyStackless_CallErrorHandler()) {
                    retval = Py_None;
                    Py_INCREF(Py_None);
                }
            }
            if (retval == NULL)
                retval = slp_curexc_to_bomb();
            if (retval == NULL)
                return NULL;
        }
    }

    /*
     * put the result back into the dead tasklet, to be retrieved
     * by schedule_task_destruct(), or cleared there
     */
    TASKLET_SETVAL(task, retval);

    /* remove from runnables */
    slp_current_remove();

    /*
     * clean up any current exception - this tasklet is dead.
     * This only happens if we are killing tasklets in the middle
     * of their execution.
     */
    if (ts->exc_type != NULL && ts->exc_type != Py_None) {
        Py_DECREF(ts->exc_type);
        Py_XDECREF(ts->exc_value);
        Py_XDECREF(ts->exc_traceback);
        ts->exc_type = ts->exc_value = ts->exc_traceback = NULL;
    }

    /* capture all exceptions */
    if (ismain) {
        /*
         * Main wants to exit. We clean up, but leave the
         * runnables chain intact.
         */
        ts->st.main = NULL;
        Py_DECREF(retval);
        retval = schedule_task_destruct(task, task);
        Py_DECREF(task);
        return retval;
    }

    next = ts->st.current;
    if (next == NULL) {
        int blocked = ts->st.main->flags.blocked;

        if (blocked) {
            char *txt;
            /* main was blocked and nobody can send */
            if (blocked < 0)
                txt = "the main tasklet is receiving"
                    " without a sender available.";
            else
                txt = "the main tasklet is sending"
                    " without a receiver available.";
            PyErr_SetString(PyExc_StopIteration, txt);
            /* fall through to error handling */
            Py_DECREF(retval);
            retval = slp_curexc_to_bomb();
            if (retval == NULL)
                return NULL;
        }
        next = ts->st.main;
    }

    if (PyBomb_Check(retval)) {
        /*
         * error handling: continue in the context of the main tasklet.
         */
        next = ts->st.main;
        TASKLET_SETVAL(next, retval);
    }
    Py_DECREF(retval);

    return schedule_task_destruct(task, next);
}

/*
  the following functions only have to handle "real"
  tasklets, those which need to switch the C stack.
  The "soft" tasklets are handled by frame pushing.
  It is not so much simpler than I thought :-(
*/

PyObject *
slp_run_tasklet(PyFrameObject *f)
{
    PyThreadState *ts = PyThreadState_GET();
    PyObject *retval;

    if ( (ts->st.main == NULL) && initialize_main_and_current()) {
        ts->frame = NULL;
        return NULL;
    }
    ts->frame = f;

    TASKLET_CLAIMVAL(ts->st.current, &retval);

    if (PyBomb_Check(retval))
        retval = slp_bomb_explode(retval);
    while (ts->st.main != NULL) {
        /* XXX correct condition? or current? */
        retval = slp_frame_dispatch_top(retval);
        retval = tasklet_end(retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(retval);
        /* if we softswitched out from the tasklet end */
        Py_CLEAR(ts->st.del_post_switch);
    }
    return retval;
}

/* Clear out the free list */

void
slp_scheduling_fini(void)
{
    while (free_list != NULL) {
        PyBombObject *bomb = free_list;
        free_list = (PyBombObject *) free_list->curexc_type;
        PyObject_GC_Del(bomb);
        --numfree;
    }
    assert(numfree == 0);
}

#endif
