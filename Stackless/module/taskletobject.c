/******************************************************

  The Tasklet

 ******************************************************/

#include "Python.h"

#ifdef STACKLESS
#include "core/stackless_impl.h"
#include "module/taskletobject.h"

void
slp_current_insert(PyTaskletObject *task)
{
    PyThreadState *ts = task->tstate;
    PyTaskletObject **chain = &ts->st.current;

    SLP_CHAIN_INSERT(PyTaskletObject, chain, task, next, prev);
    ++ts->st.runcount;
}

void
slp_current_insert_after(PyTaskletObject *task)
{
    PyThreadState *ts = task->tstate;
    PyTaskletObject *hold = ts->st.current;
    PyTaskletObject **chain = &ts->st.current;

    *chain = hold->next;
    SLP_CHAIN_INSERT(PyTaskletObject, chain, task, next, prev);
    *chain = hold;
    ++ts->st.runcount;
}

void
slp_current_uninsert(PyTaskletObject *task)
{
    PyThreadState *ts = task->tstate;
    PyTaskletObject *hold = ts->st.current;
    PyTaskletObject **chain = &ts->st.current;
    
    *chain = task;
    SLP_CHAIN_REMOVE(PyTaskletObject, chain, task, next, prev);
    *chain = hold;
    --ts->st.runcount;
}

PyTaskletObject *
slp_current_remove(void)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject **chain = &ts->st.current, *ret;

    --ts->st.runcount;
    SLP_CHAIN_REMOVE(PyTaskletObject, chain, ret, next, prev);
    return ret;
}

void
slp_current_unremove(PyTaskletObject* task)
{
    PyThreadState *ts = PyThreadState_GET();
    slp_current_insert(task);
    ts->st.current = task;
}

/*
 * Determine if a tasklet has C stack, and thus needs to
 * be switched to (killed) before it can be deleted.
 * Tasklets without C stack (in a soft switched state)
 * need only be released.
 */
static int
tasklet_has_c_stack(PyTaskletObject *t)
{
    return t->f.frame && t->cstate;
}

static int
tasklet_traverse(PyTaskletObject *t, visitproc visit, void *arg)
{
    PyFrameObject *f;

    /* tasklets that need to be switched to for the kill, can't be collected.
     * Only trivial decrefs are allowed during GC collect
     */
    if (tasklet_has_c_stack(t))
        PyObject_GC_Collectable((PyObject *)t, visit, arg, 0);

    /* we own the "execute reference" of all the frames */
    for (f = t->f.frame; f != NULL; f = f->f_back) {
        Py_VISIT(f);
    }
    Py_VISIT(t->tempval);
    return 0;
}

static void
tasklet_clear_frames(PyTaskletObject *t)
{
    /* release frame chain, we own the "execute reference" of all the frames */
    PyFrameObject *f;
    f = t->f.frame;
    t->f.frame = NULL;
    while (f != NULL) {
        PyFrameObject *tmp = f->f_back;
        f->f_back = NULL;
        Py_DECREF(f);
        f = tmp;
    }
}

static void
tasklet_clear(PyTaskletObject *t)
{
    tasklet_clear_frames(t);
    TASKLET_SETVAL(t, Py_None); /* always non-zero */
}

/*
 * the following function tries to ensure that a tasklet is
 * really killed. It is called in a context where we can't
 * afford that it will not be dead afterwards.
 * Reason: When clearing or resurrecting and killing, the
 * tasklet is in fact already dead, and the only case that
 * could revive it was that __del_ was defined.
 * But in the context of __del__, we can't do anything but rely
 * on proper destruction, since nobody will listen to an exception.
 */

static void
kill_finally (PyObject *ob)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *self = (PyTaskletObject *) ob;
    int is_mine = ts == self->tstate;

    /* this could happen if we have a refcount bug, so catch it here.
    assert(self != ts->st.current);
    It also gets triggered on interpreter exit when we kill the tasks
    with stacks (PyStackless_kill_tasks_with_stacks) and there is no
    way to differentiate that case.. so it just gets commented out.
    */

    self->flags.is_zombie = 1;
    while (self->f.frame != NULL) {
        PyTasklet_Kill(self);
        if (!is_mine)
            return; /* will be killed elsewhere */
    }
}


/* destructing a tasklet without destroying it */

static void
tasklet_dealloc(PyTaskletObject *t)
{
    PyObject_GC_UnTrack(t);
    if (tasklet_has_c_stack(t)) {
        /*
         * we want to cleanly kill the tasklet in the case it
         * was forgotten. One way would be to resurrect it,
         * but this is quite ugly with many ifdefs, see
         * classobject/typeobject.
         * Well, we do it.
         */
        if (slp_resurrect_and_kill((PyObject *) t, kill_finally)) {
            /* the beast has grown new references */
            PyObject_GC_Track(t);
            return;
        }
    }

    tasklet_clear_frames(t);
    if (t->tsk_weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *)t);
    Py_DECREF(t->tempval);
    Py_XDECREF(t->def_globals);
    t->ob_type->tp_free((PyObject*)t);
}


PyTaskletObject *
PyTasklet_New(PyTypeObject *type, PyObject *func)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *t;

    if (func != NULL && !PyCallable_Check(func))
        TYPE_ERROR("tasklet function must be a callable", NULL);
    if (type == NULL) type = &PyTasklet_Type;
    assert(PyType_IsSubtype(type, &PyTasklet_Type));
    t = (PyTaskletObject *) type->tp_alloc(type, 0);
    if (t != NULL) {
        *(int*)&t->flags = 0;
        t->next = NULL;
        t->prev = NULL;
        t->f.frame = NULL;
        if (func == NULL)
            func = Py_None;
        Py_INCREF(func);
        t->tempval = func; 
        t->tsk_weakreflist = NULL;
        t->cstate = 0;
        t->tstate = ts;
        t->nesting_level = 0;
        t->def_globals = PyEval_GetGlobals();
        Py_XINCREF(t->def_globals);
    }
    return t;
}

PyTaskletObject *
PyTasklet_Bind(PyTaskletObject *task, PyObject *func)
{
    if (func == NULL || !PyCallable_Check(func))
        TYPE_ERROR("tasklet function must be a callable", NULL);
    if (task->f.frame != NULL)
        RUNTIME_ERROR(
            "tasklet is already bound to a frame", NULL);
    TASKLET_SETVAL(task, func);
    Py_INCREF(task);
    return task;
}

PyDoc_STRVAR(tasklet_bind__doc__,
"Binding a tasklet to a callable object.\n\
The callable is usually passed in to the constructor.\n\
In some cases, it makes sense to be able to re-bind a tasklet,\n\
after it has been run, in order to keep its identity.\n\
Note that a tasklet can only be bound when it doesn't have a frame.\
");

static PyObject *
tasklet_bind(PyObject *self, PyObject *func)
{
    return (PyObject *) PyTasklet_Bind ( (PyTaskletObject *) self, func);
}

#define TASKLET_TUPLEFMT "iOiO"

static PyObject *
tasklet_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyFunctionObject *func = NULL;
    static char *kwlist[] = {"func", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:tasklet",
                                     kwlist, &func))
        return NULL;
    return (PyObject*) PyTasklet_New(type, (PyObject*)func);
}

/* tasklet pickling support */

PyDoc_STRVAR(tasklet_reduce__doc__,
"Pickling a tasklet for later re-animation.\n\
Note that a tasklet can always be pickled, unless it is current.\n\
Whether it can be run after unpickling depends on the state of the\n\
involved frames. In general, you cannot run a frame with a C state.\
");

/*
Notes on pickling:
We get into trouble with the normal __reduce__ protocol, since
tasklets tend to have tasklets in tempval, and this creates
infinite recursion on pickling.
We therefore adopt the 3-element protocol of __reduce__, where
the third thing is the argument tuple for __setstate__.
Note that we don't use None as the second tuple.
As explained in 'Pickling and unpickling extension types', this
would call a property __basicnew__. This is more complicated,
since __basicnew__ has no parameters, and we need to track
the tasklet type.
The easiest solution was to just use an empty tuple, which causes
simply the tasklet() call without parameters.
*/

static PyObject *
tasklet_reduce(PyTaskletObject * t)
{
    PyObject *tup = NULL, *lis = NULL;
    PyFrameObject *f;
    PyThreadState *ts = PyThreadState_GET();

    if (t == ts->st.current)
        RUNTIME_ERROR("You cannot __reduce__ the tasklet which is"
                      " current.", NULL);
    lis = PyList_New(0);
    if (lis == NULL) goto err_exit;
    f = t->f.frame;
    while (f != NULL) {
        if (PyList_Append(lis, (PyObject *) f)) goto err_exit;
        f = f->f_back;
    }
    if (PyList_Reverse(lis)) goto err_exit;
    tup = Py_BuildValue("(O()(" TASKLET_TUPLEFMT "))",
                        t->ob_type,
                        t->flags,
                        t->tempval,
                        t->nesting_level,
                        lis
                        );
err_exit:
    Py_XDECREF(lis);
    return tup;
}


PyDoc_STRVAR(tasklet_setstate__doc__,
"Tasklets are first created without parameters, and then __setstate__\n\
is called. This was necessary, since pickle has problems pickling\n\
extension types when they reference themselves.\
");

/* note that args is a tuple, although we use METH_O */

static PyObject *
tasklet_setstate(PyObject *self, PyObject *args)
{
    PyTaskletObject *t = (PyTaskletObject *) self;
    PyObject *tempval, *lis;
    int flags, nesting_level;
    PyFrameObject *f;
    Py_ssize_t i, nframes;
    int j;

    if (!PyArg_ParseTuple(args, "iOiO!:tasklet",
                          &flags,
                          &tempval,
                          &nesting_level,
                          &PyList_Type, &lis))
        return NULL;

    nframes = PyList_GET_SIZE(lis);
    TASKLET_SETVAL(t, tempval);

    /* There is a unpickling race condition.  While it is rare,
     * sometimes tasklets get their setstate call after the
     * channel they are blocked on.  If this happens and we
     * do not account for it, they will be left in a broken
     * state where they are on the channels chain, but have
     * cleared their blocked flag.
     *
     * We will assume that the presence of a chain, can only
     * mean that the chain is that of a channel, rather than
     * that of the main tasklet/scheduler. And therefore
     * they can leave their blocked flag in place because the
     * channel would have set it.
     */
    j = t->flags.blocked;
    *(int *)&t->flags = flags;
    if (t->next == NULL) {
        t->flags.blocked = 0;
    } else {
        t->flags.blocked = j;
    }

    /* t->nesting_level = nesting_level;
       XXX how do we handle this?
       XXX to be done: pickle the cstate without a ref to the task.
       XXX This should make it not runnable in the future.
     */
    if (nframes > 0) {
        PyFrameObject *back;
        f = (PyFrameObject *) PyList_GET_ITEM(lis, 0);

        if ((f = slp_ensure_new_frame(f)) == NULL)
            return NULL;
        back = f;
        for (i=1; i<nframes; ++i) {
            f = (PyFrameObject *) PyList_GET_ITEM(lis, i);
            if ((f = slp_ensure_new_frame(f)) == NULL)
                return NULL;
            Py_INCREF(back);
            f->f_back = back;
            back = f;
        }
        t->f.frame = f;
    }
    /* walk frames again and calculate recursion_depth */
    for (f = t->f.frame; f != NULL; f = f->f_back) {
        if (PyFrame_Check(f) && f->f_execute != PyEval_EvalFrameEx_slp) {
            /*
             * we count running frames which *have* added
             * to recursion_depth
             */
            ++t->recursion_depth;
        }
    }
    Py_INCREF(self);
    return self;
}

/* other tasklet methods */

PyDoc_STRVAR(tasklet_remove__doc__,
"Removing a tasklet from the runnables queue.\n\
Note: If this tasklet has a non-trivial C stack attached,\n\
it will be destructed when the containing thread state is destroyed.\n\
Since this will happen in some unpredictable order, it may cause unwanted\n\
side-effects. Therefore it is recommended to either run tasklets to the\n\
end or to explicitly kill() them.\
");


static int
PyTasklet_Remove_M(PyTaskletObject *task)
{
    PyObject *ret = PyStackless_CallMethod_Main(
        (PyObject*)task, "remove", NULL);

    return slp_return_wrapper(ret);
}

int
PyTasklet_Remove(PyTaskletObject *task)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return t->remove(task);
}

static TASKLET_REMOVE_HEAD(impl_tasklet_remove)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *hold = ts->st.current;

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) return PyTasklet_Remove_M(task);

    assert(ts->st.current != NULL);
    if (task->flags.blocked)
        RUNTIME_ERROR("You cannot remove a blocked tasklet.", -1);
    if (task == ts->st.current)
        RUNTIME_ERROR("The current tasklet cannot be removed.", -1);
    if (task->next == NULL)
        return 0;
    ts->st.current = task;
    slp_current_remove();
    ts->st.current = hold;
    Py_DECREF(task);
    return 0;
}

static TASKLET_REMOVE_HEAD(wrap_tasklet_remove)
{
    PyObject * ret = PyObject_CallMethod((PyObject *)task, "remove", NULL);

    return slp_return_wrapper(ret);
}

static PyObject *
tasklet_remove(PyObject *self)
{
    if (impl_tasklet_remove((PyTaskletObject*) self))
        return NULL;
    Py_INCREF(self);
    return self;
}


PyDoc_STRVAR(tasklet_insert__doc__,
"Insert this tasklet at the end of the scheduler list,\n\
given that it isn't blocked.\n\
Blocked tasklets need to be reactivated by channels.");

int
PyTasklet_Insert(PyTaskletObject *task)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return t->insert(task);
}

static TASKLET_INSERT_HEAD(impl_tasklet_insert)
{
    PyThreadState *ts = PyThreadState_GET();

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL)
        return slp_current_wrapper(PyTasklet_Insert, task);
    if (task->flags.blocked)
        RUNTIME_ERROR("You cannot run a blocked tasklet", -1);
    if (task->f.frame == NULL && task != ts->st.current)
        RUNTIME_ERROR("You cannot run an unbound(dead) tasklet", -1);
    if (task->next == NULL) {
        Py_INCREF(task);
        slp_current_insert(task);
        /* The tasklet may belong to a different thread, and that thread may
         * be blocked, waiting for something to do!
         */
        slp_thread_unblock(task->tstate);
    }
    return 0;
}

static TASKLET_INSERT_HEAD(wrap_tasklet_insert)
{
    PyObject * ret = PyObject_CallMethod(
        (PyObject *)task, "insert", NULL);

    return slp_return_wrapper(ret);
}

static PyObject *
tasklet_insert(PyObject *self)
{
    if (impl_tasklet_insert((PyTaskletObject*) self))
        return NULL;
    Py_INCREF(self);
    return self;
}


PyDoc_STRVAR(tasklet_run__doc__,
"Run this tasklet, given that it isn't blocked.\n\
Blocked tasks need to be reactivated by channels.");

static PyObject *
PyTasklet_Run_M(PyTaskletObject *task)
{
    return PyStackless_CallMethod_Main((PyObject*)task, "run", NULL);
}

int
PyTasklet_Run_nr(PyTaskletObject *task)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    slp_try_stackless = 1;
    return slp_return_wrapper(t->run(task));
}

int
PyTasklet_Run(PyTaskletObject *task)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return slp_return_wrapper(t->run(task));
}

static TASKLET_RUN_HEAD(impl_tasklet_run)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *ret;
    int inserted, fail;

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) return PyTasklet_Run_M(task);
    inserted = task->next == NULL;
    if (PyTasklet_Insert(task))
        return NULL;
    fail = slp_schedule_task(&ret, ts->st.current, task, stackless, 0);
    if (fail) {
        if (inserted) {
            /* we must undo the insertion that we did */
            slp_current_uninsert(task);
            Py_DECREF(task);
        }
    }
    return ret;
}

static TASKLET_RUN_HEAD(wrap_tasklet_run)
{
    return PyObject_CallMethod((PyObject *)task, "run", NULL);
}


static PyObject *
tasklet_run(PyObject *self)
{
    return impl_tasklet_run((PyTaskletObject *) self);
}

PyDoc_STRVAR(tasklet_set_atomic__doc__,
"t.set_atomic(flag) -- set tasklet atomic status and return current one.\n\
If set, the tasklet will not be auto-scheduled.\n\
This flag is useful for critical sections which should not be interrupted.\n\
usage:\n\
    tmp = t.set_atomic(1)\n\
    # do critical stuff\n\
    t.set_atomic(tmp)\n\
Note: Whenever a new tasklet is created, the atomic flag is initialized\n\
with the atomic flag of the current tasklet.\
Atomic behavior is additionally influenced by the interpreter nesting level.\
See set_ignore_nesting.\
");


int
PyTasklet_SetAtomic(PyTaskletObject *task, int flag)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return t->set_atomic(task, flag);
}

static TASKLET_SETATOMIC_HEAD(impl_tasklet_set_atomic)
{
    int ret = task->flags.atomic;

    task->flags.atomic = flag ? 1 : 0;
    if (task->flags.pending_irq && task == PyThreadState_GET()->st.current)
        slp_check_pending_irq();
    return ret;
}

static TASKLET_SETATOMIC_HEAD(wrap_tasklet_set_atomic)
{
    PyObject * ret = PyObject_CallMethod(
        (PyObject *)task, "set_atomic", "(i)", flag);

    return slp_int_wrapper(ret);
}

static PyObject *
tasklet_set_atomic(PyObject *self, PyObject *flag)
{
    if (! (flag && PyInt_Check(flag)) )
        TYPE_ERROR("set_atomic needs exactly one bool or integer",
                   NULL);
    return PyBool_FromLong(impl_tasklet_set_atomic(
        (PyTaskletObject*)self, PyInt_AS_LONG(flag)));
}


PyDoc_STRVAR(tasklet_set_ignore_nesting__doc__,
"t.set_ignore_nesting(flag) -- set tasklet ignore_nesting status and return current one.\n\
If set, the tasklet may be be auto-scheduled, even if its nesting_level is > 0.\n\
This flag makes sense if you know that nested interpreter levels are safe\n\
for auto-scheduling. This is on your own risk, handle with care!\n\
usage:\n\
    tmp = t.set_ignore_nesting(1)\n\
    # do critical stuff\n\
    t.set_ignore_nesting(tmp)\
");


int
PyTasklet_SetIgnoreNesting(PyTaskletObject *task, int flag)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return t->set_ignore_nesting(task, flag);
}

static TASKLET_SETIGNORENESTING_HEAD(impl_tasklet_set_ignore_nesting)
{
    int ret = task->flags.ignore_nesting;

    task->flags.ignore_nesting = flag ? 1 : 0;
    if (task->flags.pending_irq && task == PyThreadState_GET()->st.current)
        slp_check_pending_irq();
    return ret;
}

static TASKLET_SETIGNORENESTING_HEAD(wrap_tasklet_set_ignore_nesting)
{
    PyObject * ret = PyObject_CallMethod(
        (PyObject *)task, "set_ignore_nesting", "(i)", flag);

    return slp_int_wrapper(ret);
}

static PyObject *
tasklet_set_ignore_nesting(PyObject *self, PyObject *flag)
{
    if (! (flag && PyInt_Check(flag)) )
        TYPE_ERROR("set_ignore_nesting needs exactly one bool or integer",
                   NULL);
    return PyBool_FromLong(impl_tasklet_set_ignore_nesting(
    (PyTaskletObject*)self, PyInt_AS_LONG(flag)));
}


static int
bind_tasklet_to_frame(PyTaskletObject *task, PyFrameObject *frame)
{
    PyThreadState *ts = PyThreadState_GET();
    
    if (task->f.frame != NULL)
        RUNTIME_ERROR("tasklet is already bound to a frame", -1);
    task->f.frame = frame;
    task->tstate = ts;
    task->nesting_level = 0;
    return 0;
    /* note: We expect that f_back is NULL, or will be adjusted immediately */
}

/* this is also the setup method */

PyDoc_STRVAR(tasklet_setup__doc__, "supply the parameters for the callable");

static int
PyTasklet_Setup_M(PyTaskletObject *task, PyObject *args, PyObject *kwds)
{
    PyObject *ret = PyStackless_Call_Main((PyObject*)task, args, kwds);

    return slp_return_wrapper(ret);
}

int PyTasklet_Setup(PyTaskletObject *task, PyObject *args, PyObject *kwds)
{
    PyObject *ret = task->ob_type->tp_call((PyObject *) task, args, kwds);

    return slp_return_wrapper(ret);
}

static int
impl_tasklet_setup(PyTaskletObject *task, PyObject *args, PyObject *kwds)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *frame;
    PyObject *func;

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) return PyTasklet_Setup_M(task, args, kwds);

    func = task->tempval;
    if (func == NULL)
        RUNTIME_ERROR("the tasklet was not bound to a function", -1);
    if ((frame = (PyFrameObject *)
                 slp_cframe_newfunc(func, args, kwds, 0)) == NULL) {
        return -1;
    }
    if (bind_tasklet_to_frame(task, frame)) {
        Py_DECREF(frame);
        return -1;
    }
    TASKLET_SETVAL(task, Py_None);
    Py_INCREF(task);
    slp_current_insert(task);
    return 0;
}

static PyObject *
tasklet_setup(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTaskletObject *task = (PyTaskletObject *) self;

    if (impl_tasklet_setup(task, args, kwds))
        return NULL;
    Py_INCREF(task);
    return (PyObject*) task;
}


PyDoc_STRVAR(tasklet_throw__doc__,
             "tasklet.throw(exc, val, tb, immediate=True) -- raise an exception for the tasklet.\n\
             'exc', 'val' and 'tb' have the same semantics as Python's 'raise' statement.\n\
             If 'immediate' is True, the tasklet is immediately activated, otherwise, it is\n\
             merely made runnable");

static PyObject *
PyTasklet_Throw_M(PyTaskletObject *self, int immediate, PyObject *exc,
                           PyObject *val, PyObject *tb)
{
    if (!val)
        val = Py_None;
    if (!tb)
        tb = Py_None;
    return PyStackless_CallMethod_Main(
        (PyObject*)self, "throw", "(OOOi)", exc, val, tb, immediate);
}

int PyTasklet_Throw(PyTaskletObject *self, int immediate, PyObject *exc,
                             PyObject *val, PyObject *tb)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)Py_TYPE(self);

    return slp_return_wrapper(t->_throw(self, immediate, exc, val, tb));
}

static TASKLET_THROW_HEAD(impl_tasklet_throw)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *ret, *bomb, *tmpval;
    int fail;

    if (ts->st.main == NULL)
        return PyTasklet_Throw_M(self, immediate, exc, val, tb);

    bomb = slp_exc_to_bomb(exc, val, tb);
    if (bomb == NULL)
        return NULL;

    /* don't attempt to send to a dead tasklet.
     * f.frame is null for the running tasklet and a dead tasklet
     * A new tasklet has a CFrame
     */
    if (self->f.frame == NULL && self != self->tstate->st.current) {
        /* however, allow tasklet exit errors for already dead tasklets */
        if (PyObject_IsSubclass(((PyBombObject*)bomb)->curexc_type, PyExc_TaskletExit)) {
            Py_DECREF(bomb);
            Py_RETURN_NONE;
        }
        Py_DECREF(bomb);
        RUNTIME_ERROR("You cannot throw to a dead tasklet", NULL);
    }
    TASKLET_CLAIMVAL(self, &tmpval);
    TASKLET_SETVAL_OWN(self, bomb);
    if (immediate) {
        fail = slp_schedule_task(&ret, ts->st.current, self, stackless, 0);
    } else {
        PyChannelObject *u_chan = NULL;
        PyTaskletObject *u_next;
        int u_dir;
        /* Unblock it if required*/
        if (self->flags.blocked) {
            /* we claim the channel's reference */
            slp_channel_remove_slow(self, &u_chan, &u_dir, &u_next);
        }
        fail = PyTasklet_Insert(self);
        if (u_chan) {
            if (fail)
                slp_channel_insert(u_chan, self, u_dir, u_next);
            else
                Py_DECREF(self);
        }
        ret = fail ? NULL : Py_None;
        Py_XINCREF(ret);
    }
    if (fail)
        TASKLET_SETVAL_OWN(self, tmpval);
    else
        Py_DECREF(tmpval);
    return ret;
}

static TASKLET_THROW_HEAD(wrap_tasklet_throw)
{
    if (!val)
        val = Py_None;
    if (!tb)
        tb = Py_None;
    return PyObject_CallMethod(
        (PyObject *)self, "throw", "(OOOi)", exc, val, tb, immediate);
}

static PyObject *
tasklet_throw(PyObject *myself, PyObject *args, PyObject *kwds)
{
    STACKLESS_GETARG();
    PyObject *result = NULL;
    PyObject *exc, *val=Py_None, *tb=Py_None;
    int immediate = 1;
    char *kwlist[] = {"exc", "val", "tb", "immediate", 0};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOi:throw", kwlist, &exc, &val, &tb, &immediate))
        return NULL;
    STACKLESS_PROMOTE_ALL();
    result = impl_tasklet_throw(
        (PyTaskletObject*)myself, immediate, exc, val, tb);
    STACKLESS_ASSERT();
    return result;
}


PyDoc_STRVAR(tasklet_raise_exception__doc__,
"tasklet.raise_exception(exc, value) -- raise an exception for the tasklet.\n\
exc must be a subclass of Exception.\n\
The tasklet is immediately activated.");

static PyObject *
PyTasklet_RaiseException_M(PyTaskletObject *self, PyObject *klass,
                           PyObject *args)
{
    return PyStackless_CallMethod_Main(
        (PyObject*)self, "raise_exception", "(OO)", klass, args);
}

int PyTasklet_RaiseException(PyTaskletObject *self, PyObject *klass,
                             PyObject *args)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)self->ob_type;

    return slp_return_wrapper(t->raise_exception(self, klass, args));
}

static TASKLET_RAISE_EXCEPTION_HEAD(impl_tasklet_raise_exception)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *ret, *bomb, *tmpval;
    int fail;

    if (ts->st.main == NULL)
        return PyTasklet_RaiseException_M(self, klass, args);
    bomb = slp_make_bomb(klass, args, "tasklet.raise_exception");
    if (bomb == NULL)
        return NULL;
    /* if the tasklet is dead, do not run it (no frame) but explode */
    if (slp_get_frame(self) == NULL)
        return slp_bomb_explode(bomb);
    
    TASKLET_CLAIMVAL(self, &tmpval);
    TASKLET_SETVAL_OWN(self, bomb);
    fail = slp_schedule_task(&ret, ts->st.current, self, stackless, 0);
    if (fail)
        TASKLET_SETVAL_OWN(self, tmpval);
    else
        Py_DECREF(tmpval);
    return ret;
}


static TASKLET_RAISE_EXCEPTION_HEAD(wrap_tasklet_raise_exception)
{
    return PyObject_CallMethod(
        (PyObject *)self, "raise_exception", "(OO)", klass, args);
}

static PyObject *
tasklet_raise_exception(PyObject *myself, PyObject *args)
{
    STACKLESS_GETARG();
    PyObject *result = NULL;
    PyObject *klass = PySequence_GetItem(args, 0);

    if (klass == NULL)
        VALUE_ERROR("tasklet.raise_exception(e, v...)", NULL);
    args = PySequence_GetSlice(args, 1, PySequence_Size(args));
    if (!args) goto err_exit;
    STACKLESS_PROMOTE_ALL();
    result = impl_tasklet_raise_exception(
        (PyTaskletObject*)myself, klass, args);
    STACKLESS_ASSERT();
err_exit:
    Py_DECREF(klass);
    Py_XDECREF(args);
    return result;
}


PyDoc_STRVAR(tasklet_kill__doc__,
"tasklet.kill -- raise a TaskletExit exception for the tasklet.\n\
Note that this is a regular exception that can be caught.\n\
The tasklet is immediately activated.\n\
If the exception passes the toplevel frame of the tasklet,\n\
the tasklet will silently die.");

int PyTasklet_Kill(PyTaskletObject *task)
{
    PyTasklet_HeapType *t = (PyTasklet_HeapType *)task->ob_type;

    return slp_return_wrapper(t->kill(task));
}

static TASKLET_KILL_HEAD(impl_tasklet_kill)
{
    STACKLESS_GETARG();
    PyObject *noargs;
    PyObject *ret;

    /*
     * silently do nothing if the tasklet is dead.
     * simple raising would kill ourself in this case.
     */
    if (slp_get_frame(task) == NULL)
        Py_RETURN_NONE;

    /* we might be called after exceptions are gone */
    if (PyExc_TaskletExit == NULL) {
        PyExc_TaskletExit = PyString_FromString("zombie");
        if (PyExc_TaskletExit == NULL)
            return NULL; /* give up */
    }
    noargs = PyTuple_New(0);
    STACKLESS_PROMOTE_ALL();
    ret = impl_tasklet_raise_exception(task, PyExc_TaskletExit,
                                       noargs);
    STACKLESS_ASSERT();
    Py_DECREF(noargs);
    return ret;
}

static TASKLET_KILL_HEAD(wrap_tasklet_kill)
{
    return PyObject_CallMethod((PyObject *)task, "kill", NULL);
}

static PyObject *
tasklet_kill(PyObject *self)
{
    return impl_tasklet_kill((PyTaskletObject*)self);
}


/* attributes which are hiding in small fields */

static PyObject *
tasklet_get_blocked(PyTaskletObject *task)
{
    return PyBool_FromLong(task->flags.blocked);
}

int PyTasklet_GetBlocked(PyTaskletObject *task)
{
    return task->flags.blocked;
}


static PyObject *
tasklet_get_atomic(PyTaskletObject *task)
{
    return PyBool_FromLong(task->flags.atomic);
}

int PyTasklet_GetAtomic(PyTaskletObject *task)
{
    return task->flags.atomic;
}


static PyObject *
tasklet_get_ignore_nesting(PyTaskletObject *task)
{
    return PyBool_FromLong(task->flags.ignore_nesting);
}

int PyTasklet_GetIgnoreNesting(PyTaskletObject *task)
{
    return task->flags.ignore_nesting;
}


static PyObject *
tasklet_get_frame(PyTaskletObject *task)
{
    PyObject *ret = PyTasklet_GetFrame(task);
    if (ret == NULL)
        Py_RETURN_NONE;
    return ret;
}

PyObject *
PyTasklet_GetFrame(PyTaskletObject *task)
{
    PyFrameObject *f = (PyFrameObject *) slp_get_frame(task);

    while (f != NULL && !PyFrame_Check(f)) {
        f = f->f_back;
    }
    Py_XINCREF(f);
    return (PyObject *) f;
}


static PyObject *
tasklet_get_block_trap(PyTaskletObject *task)
{
    return PyBool_FromLong(task->flags.block_trap);
}

int PyTasklet_GetBlockTrap(PyTaskletObject *task)
{
    return task->flags.block_trap;
}


static int
tasklet_set_block_trap(PyTaskletObject *task, PyObject *value)
{
    if (!PyInt_Check(value))
        TYPE_ERROR("block_trap must be set to a bool or integer", -1);
    task->flags.block_trap = PyInt_AsLong(value) ? 1 : 0;
    return 0;
}

void PyTasklet_SetBlockTrap(PyTaskletObject *task, int value)
{
    task->flags.block_trap = value ? 1 : 0;
}


static PyObject *
tasklet_is_main(PyTaskletObject *task)
{
    return PyBool_FromLong(task == PyThreadState_GET()->st.main);
}

int
PyTasklet_IsMain(PyTaskletObject *task)
{
    return task == PyThreadState_GET()->st.main;
}


static PyObject *
tasklet_is_current(PyTaskletObject *task)
{
    return PyBool_FromLong(task == PyThreadState_GET()->st.current);
}

int
PyTasklet_IsCurrent(PyTaskletObject *task)
{
    return task == PyThreadState_GET()->st.current;
}


static PyObject *
tasklet_get_recursion_depth(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return PyInt_FromLong(ts->st.current == task ? ts->recursion_depth
                                                 : task->recursion_depth);
}

int
PyTasklet_GetRecursionDepth(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return ts->st.current == task ? ts->recursion_depth
                                  : task->recursion_depth;
}


static PyObject *
tasklet_get_nesting_level(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return PyInt_FromLong(
        ts->st.current == task ? ts->st.nesting_level
                               : task->nesting_level);
}

int
PyTasklet_GetNestingLevel(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return ts->st.current == task ? ts->st.nesting_level
                                  : task->nesting_level;
}


/* attributes which are handy, but easily computed */

static PyObject *
tasklet_alive(PyTaskletObject *task)
{
    return PyBool_FromLong(slp_get_frame(task) != NULL);
}

int
PyTasklet_Alive(PyTaskletObject *task)
{
    return slp_get_frame(task) != NULL;
}


static PyObject *
tasklet_paused(PyTaskletObject *task)
{
    return PyBool_FromLong(
        slp_get_frame(task) != NULL && task->next == NULL);
}

int
PyTasklet_Paused(PyTaskletObject *task)
{
    return slp_get_frame(task) != NULL && task->next == NULL;
}


static PyObject *
tasklet_scheduled(PyTaskletObject *task)
{
    return PyBool_FromLong(task->next != NULL);
}

int
PyTasklet_Scheduled(PyTaskletObject *task)
{
    return task->next != NULL;
}

static PyObject *
tasklet_restorable(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return PyBool_FromLong(
        0 == (ts->st.current == task ? ts->st.nesting_level
                                     : task->nesting_level) );
}

int
PyTasklet_Restorable(PyTaskletObject *task)
{
    PyThreadState *ts;

    ts = task->tstate;
    return 0 == (ts->st.current == task ? ts->st.nesting_level
                                        : task->nesting_level);
}

static PyObject *
tasklet_get_channel(PyTaskletObject *task)
{
    PyTaskletObject *prev = task->prev;
    PyObject *ret = Py_None;
    if (prev != NULL && task->flags.blocked) {
        /* search left, optimizing in-oder access */
        while (!PyChannel_Check(prev))
            prev = prev->prev;
        ret = (PyObject *) prev;
    }
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_get_next(PyTaskletObject *task)
{
    PyObject *ret = Py_None;

    if (task->next != NULL && PyTasklet_Check(task->next))
        ret = (PyObject *) task->next;
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_get_prev(PyTaskletObject *task)
{
    PyObject *ret = Py_None;

    if (task->prev != NULL && PyTasklet_Check(task->prev))
        ret = (PyObject *) task->prev;
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_thread_id(PyTaskletObject *task)
{
    return PyInt_FromLong(task->tstate->thread_id);
}

static PyMemberDef tasklet_members[] = {
    {"tempval", T_OBJECT, offsetof(PyTaskletObject, tempval), 0},
    /* blocked, slicing_lock, atomic and such are treated by tp_getset */
    {0}
};

static PyGetSetDef tasklet_getsetlist[] = {
    {"next", (getter)tasklet_get_next, NULL,
     PyDoc_STR("the next tasklet in a a circular list of tasklets.")},

    {"prev", (getter)tasklet_get_prev, NULL,
     PyDoc_STR("the previous tasklet in a circular list of tasklets")},

    {"_channel", (getter)tasklet_get_channel, NULL,
     PyDoc_STR("The channel this tasklet is blocked on, or None if it is not blocked.\n"
     "This computed attribute may cause a linear search and should normally\n"
     "not be used, or be replaced by a real attribute in a derived type.")
    },

    {"blocked", (getter)tasklet_get_blocked, NULL,
     PyDoc_STR("Nonzero if waiting on a channel (1: send, -1: receive).\n"
     "Part of the flags word.")},

    {"atomic", (getter)tasklet_get_atomic, NULL,
     PyDoc_STR("atomic inhibits scheduling of this tasklet. See set_atomic()\n"
     "Part of the flags word.")},

    {"ignore_nesting", (getter)tasklet_get_ignore_nesting, NULL,
     PyDoc_STR("unless ignore_nesting is set, any nesting level > 0 inhibits\n"
     "auto-scheduling of this tasklet. See set_ignore_nesting()\n"
     "Part of the flags word.")},

    {"frame", (getter)tasklet_get_frame, NULL,
     PyDoc_STR("the current frame of this tasklet. For the running tasklet,\n"
     "this is redirected to tstate.frame.")},

    {"block_trap", (getter)tasklet_get_block_trap,
                   (setter)tasklet_set_block_trap,
     PyDoc_STR("An individual lock against blocking on a channel.\n"
     "This is used as a debugging aid to find out undesired blocking.\n"
     "Instead of trying to block, an exception is raised.")},

    {"is_main", (getter)tasklet_is_main, NULL,
     PyDoc_STR("There always exists exactly one tasklet per thread which acts as\n"
     "main. It receives all uncaught exceptions and can act as a watchdog.\n"
     "This attribute is computed.")},

    {"is_current", (getter)tasklet_is_current, NULL,
     PyDoc_STR("There always exists exactly one tasklet per thread which is "
     "currently running.\n"
     "This attribute is computed.")},

    {"paused", (getter)tasklet_paused, NULL,
     PyDoc_STR("A tasklet is said to be paused if it is neither in the runnables list\n"
     "nor blocked, but alive. This state is entered after a t.remove()\n"
     "or by the main tasklet, when it is acting as a watchdog.\n"
     "This attribute is computed.")},

    {"scheduled", (getter)tasklet_scheduled, NULL,
     PyDoc_STR("A tasklet is said to be scheduled if it is either in the runnables list\n"
     "or waiting in a channel.\n"
     "This attribute is computed.")},

    {"recursion_depth", (getter)tasklet_get_recursion_depth, NULL,
     PyDoc_STR("The system recursion_depth is replicated for every tasklet.\n"
     "They all start running with a recursion_depth of zero.")},

    {"nesting_level", (getter)tasklet_get_nesting_level, NULL,
     PyDoc_STR("The interpreter nesting level is monitored by every tasklet.\n"
     "They all start running with a nesting level of zero.")},

    {"restorable", (getter)tasklet_restorable, NULL,
     PyDoc_STR("True, if the tasklet can be completely restored by pickling/unpickling.\n"
     "All tasklets can be pickled for debugging/inspection purposes, but an \n"
     "unpickled tasklet might have lost runtime information (C stack).")},

    {"alive", (getter)tasklet_alive, NULL,
     PyDoc_STR("A tasklet is alive if it has an associated frame.\n"
     "This attribute is computed.")},

    {"thread_id", (getter)tasklet_thread_id, NULL,
     PyDoc_STR("Return the thread id of the thread the tasklet belongs to.")},

    {0},
};

#define PCF PyCFunction
#define METH_VS METH_VARARGS | METH_STACKLESS
#define METH_KS METH_VARARGS | METH_KEYWORDS | METH_STACKLESS
#define METH_NS METH_NOARGS | METH_STACKLESS

static PyMethodDef tasklet_methods[] = {
    {"insert",                  (PCF)tasklet_insert,        METH_NOARGS,
      tasklet_insert__doc__},
    {"run",                     (PCF)tasklet_run,           METH_NS,
     tasklet_run__doc__},
    {"remove",                  (PCF)tasklet_remove,        METH_NOARGS,
     tasklet_remove__doc__},
    {"set_atomic",              (PCF)tasklet_set_atomic,    METH_O,
     tasklet_set_atomic__doc__},
    {"set_ignore_nesting", (PCF)tasklet_set_ignore_nesting, METH_O,
     tasklet_set_ignore_nesting__doc__},
    {"throw",                   (PCF)tasklet_throw,         METH_KS,
    tasklet_throw__doc__},
    {"raise_exception",         (PCF)tasklet_raise_exception, METH_VS,
    tasklet_raise_exception__doc__},
    {"kill",                    (PCF)tasklet_kill,          METH_NS,
     tasklet_kill__doc__},
    {"bind",                    (PCF)tasklet_bind,          METH_O,
     tasklet_bind__doc__},
    {"setup",                   (PCF)tasklet_setup,         METH_KEYWORDS,
     tasklet_setup__doc__},
    {"__reduce__",              (PCF)tasklet_reduce,        METH_NOARGS,
     tasklet_reduce__doc__},
    {"__reduce_ex__",           (PCF)tasklet_reduce,        METH_VARARGS,
     tasklet_reduce__doc__},
    {"__setstate__",            (PCF)tasklet_setstate,      METH_O,
     tasklet_setstate__doc__},
    {NULL,     NULL}             /* sentinel */
};

static PyCMethodDef tasklet_cmethods[] = {
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, insert),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, run),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, remove),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, set_atomic),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, set_ignore_nesting),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, raise_exception),
    CMETHOD_PUBLIC_ENTRY(PyTasklet_HeapType, tasklet, kill),
    CMETHOD_PUBLIC_ENTRY_EX(PyTasklet_HeapType, tasklet, throw, _throw),
    {NULL}                       /* sentinel */
};

PyDoc_STRVAR(tasklet__doc__,
"A tasklet object represents a tiny task in a Python thread.\n\
At program start, there is always one running main tasklet.\n\
New tasklets can be created with methods from the stackless\n\
module.\n\
");


PyTypeObject _PyTasklet_Type = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,
    "tasklet",
    sizeof(PyTaskletObject),
    0,
    (destructor)tasklet_dealloc,        /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    tasklet_setup,                      /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    PyObject_GenericSetAttr,            /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,            /* tp_flags */
    tasklet__doc__,                     /* tp_doc */
    (traverseproc)tasklet_traverse,     /* tp_traverse */
    (inquiry) tasklet_clear,            /* tp_clear */
    0,                                  /* tp_richcompare */
    offsetof(PyTaskletObject, tsk_weakreflist),
                                    /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    tasklet_methods,                    /* tp_methods */
    tasklet_members,                    /* tp_members */
    tasklet_getsetlist,                 /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    tasklet_new,                        /* tp_new */
    _PyObject_GC_Del,                   /* tp_free */
};
PyTypeObject *PyTasklet_TypePtr = NULL;

int init_tasklettype(void)
{
    PyTypeObject *t = &_PyTasklet_Type;

    if ( (t = PyFlexType_Build("stackless", "tasklet", t->tp_doc, t,
                               sizeof(PyTasklet_HeapType),
                               tasklet_cmethods) ) == NULL)
        return -1;
    PyTasklet_TypePtr = t;
    return 0;
}
#endif
