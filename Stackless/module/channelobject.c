/******************************************************

  The Channel

 ******************************************************/

#include "Python.h"
#include "structmember.h"

#ifdef STACKLESS
#include "core/stackless_impl.h"
#include "channelobject.h"

Py_LOCAL_INLINE(PyChannelFlagStruc)
channel_flags_from_integer(int flags) {
#if defined(SLP_USE_NATIVE_BITFIELD_LAYOUT) && SLP_USE_NATIVE_BITFIELD_LAYOUT
    PyChannelFlagStruc f;
    Py_MEMCPY(&f, &flags, sizeof(f));
#else
    /* the portable way */
    PyChannelFlagStruc f = {0, };
    SLP_SET_BITFIELD(SLP_CHANNEL_FLAGS, f, flags, closing);
    SLP_SET_BITFIELD(SLP_CHANNEL_FLAGS, f, flags, preference);
    SLP_SET_BITFIELD(SLP_CHANNEL_FLAGS, f, flags, schedule_all);
#endif
    Py_BUILD_ASSERT(sizeof(f) == sizeof(flags));
    return f;
}

Py_LOCAL_INLINE(int)
channel_flags_as_integer(PyChannelFlagStruc flags) {
    int f;
    Py_BUILD_ASSERT(sizeof(f) == sizeof(flags));
#if defined(SLP_USE_NATIVE_BITFIELD_LAYOUT) && SLP_USE_NATIVE_BITFIELD_LAYOUT
    Py_MEMCPY(&f, &flags, sizeof(f));
#else
    /* the portable way */
    f = SLP_GET_BITFIELD(SLP_CHANNEL_FLAGS, flags, closing) |
            SLP_GET_BITFIELD(SLP_CHANNEL_FLAGS, flags, preference) |
            SLP_GET_BITFIELD(SLP_CHANNEL_FLAGS, flags, schedule_all);
#endif
    return f;
}

/* GC support.  The tasklets already know if they are collectable
 * or not.  If they are not, and referenced by the channel, then
 * no channel_clear() will be performed.  Thus, the GC support
 * routines don't have to be clever about resurrected tasklets.
 */
static int
channel_traverse(PyChannelObject *ch, visitproc visit, void *arg)
{
    PyTaskletObject *p;
    for (p = ch->head; p != (PyTaskletObject *) ch; p = p->next) {
        Py_VISIT(p);
    }
    return 0;
}

static void
channel_clear(PyObject *ob)
{
    PyChannelObject *ch = (PyChannelObject *) ob;
    assert(PyChannel_Check(ob));

    /*
     * remove all tasklets and hope they will die.
     * Note that the channel might receive new actions
     * during tasklet deallocation, so we don't even know
     * if it will have the same direction. :-/
     */
    ch->flags.closing = 1;
    while (ch->balance) {
        /* this function does nothing but setting task->flags.blocked = 0, so it's safe to use */
        ob = (PyObject *) slp_channel_remove(ch, NULL, NULL, NULL);
        Py_DECREF(ob);
    }
}

static void
channel_dealloc(PyObject *ob)
{
    PyChannelObject *ch = (PyChannelObject *) ob;


    PyObject_GC_UnTrack(ob);
    channel_clear(ob);
    if (ch->chan_weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *)ch);
    Py_TYPE(ob)->tp_free(ob);
}

/* see if a tasklet is queued on a channel */
#ifndef NDEBUG /* currently used only by assert */
static int
slp_channel_has_tasklet(PyChannelObject *channel,
                            PyTaskletObject *task)
{
    PyTaskletObject *t = channel->head;
    while(PyTasklet_Check(t)) {
        if (t == task)
            return 1;
        t = t->next;
    }
    return 0;
}
#endif

void
slp_channel_insert(PyChannelObject *channel, PyTaskletObject *task, int dir, PyTaskletObject *nexttask)
{
    if (nexttask)
        assert(slp_channel_has_tasklet(channel, nexttask));
    else
        nexttask = (PyTaskletObject*)channel;
    SLP_HEADCHAIN_INSERT(PyTaskletObject, nexttask, task, next, prev);
    assert(dir == -1 || dir == 1);
    assert(dir * channel->balance >= 0); /* we are going the right way */
    channel->balance += dir;
    task->flags.blocked = dir;
}

/* the special case to remove a specific tasklet */
PyTaskletObject *
slp_channel_remove(PyChannelObject *channel,
                            PyTaskletObject *task,
                            int *dir_out,
                            PyTaskletObject **next)
{
    /* note: we assume that the task is in the channel! */
    int dir = channel->balance > 0 ? 1 : -1;
    assert(channel->balance);
    if (task) {
        assert(PyTasklet_Check(task));
        assert(slp_channel_has_tasklet(channel, task));
    } else {
        task = channel->head;
        assert(PyTasklet_Check(task));
    }
    if (dir_out)
        *dir_out = dir;
    if (next)
        *next = task->next;
    channel->balance -= dir;
    SLP_HEADCHAIN_REMOVE(task, next, prev);
    task->flags.blocked = 0;
    return task;
}


/* freeing a tasklet without an explicit channel */

void
slp_channel_remove_slow(PyTaskletObject *task,
                            PyChannelObject **u_chan,
                            int *u_dir,
                            PyTaskletObject **u_next)
{
    PyChannelObject *channel;
    PyTaskletObject *prev = task->prev;

    assert(task->flags.blocked);
    while (!PyChannel_Check(prev))
        prev = prev->prev;
    channel = (PyChannelObject *) prev;
    if (u_chan)
        *u_chan = channel;
    slp_channel_remove(channel, task, u_dir, u_next);
}


PyChannelObject *
PyChannel_New(PyTypeObject *type)
{
    PyChannelObject *c;

    if (type == NULL)
        type = &PyChannel_Type;
    if (!PyType_IsSubtype(type, &PyChannel_Type))
        TYPE_ERROR("channel_new: type must be subtype of channel", NULL);
    c = (PyChannelObject *) type->tp_alloc(type, 0);
    if (c != NULL) {
        c->head = c->tail = (PyTaskletObject *) c;
        c->balance = 0;
        c->chan_weakreflist = NULL;
        memset(&c->flags, 0, sizeof(c->flags));
        c->flags.preference = -1; /* default fast receive */
    }
    return c;
}

static PyObject *
channel_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return (PyObject *)PyChannel_New(type);
}

static PyObject *
channel_get_queue(PyChannelObject *self)
{
    PyObject *ret = (PyObject*) self->head;

    if (ret == (PyObject *) self)
        ret = Py_None;
    Py_INCREF(ret);
    return ret;
}

PyObject *
PyChannel_GetQueue(PyChannelObject *self)
{
    return channel_get_queue(self);
}

static PyObject *
channel_get_closing(PyChannelObject *self)
{
    return PyBool_FromLong(self->flags.closing);
}

int
PyChannel_GetClosing(PyChannelObject *self)
{
    return self->flags.closing;
}

static PyObject *
channel_get_closed(PyChannelObject *self)
{
    return PyBool_FromLong(self->flags.closing && self->balance == 0);
}

int
PyChannel_GetClosed(PyChannelObject *self)
{
    return self->flags.closing && self->balance == 0;
}


static PyObject *
channel_get_preference(PyChannelObject *self)
{
    return PyLong_FromLong(self->flags.preference);
}

static int
channel_set_preference(PyChannelObject *self, PyObject *value)
{
    int val;

    if (!PyLong_Check(value))
        TYPE_ERROR("preference must be set to an integer", -1);
    val = PyLong_AsLong(value);
    self->flags.preference = val > 0 ? 1 : val < 0 ? -1 : 0;
    return 0;
}

int
PyChannel_GetPreference(PyChannelObject *self)
{
    return self->flags.preference;
}

void
PyChannel_SetPreference(PyChannelObject *self, int val)
{
    self->flags.preference = val > 0 ? 1 : val < 0 ? -1 : 0;
}

static PyObject *
channel_get_schedule_all(PyChannelObject *self)
{
    return PyLong_FromLong(self->flags.schedule_all);
}

static int
channel_set_schedule_all(PyChannelObject *self, PyObject *value)
{
    if (!PyLong_Check(value))
        TYPE_ERROR("preference must be set to a bool or integer", -1);
    self->flags.schedule_all = PyLong_AsLong(value) ? 1 : 0;
    return 0;
}

int
PyChannel_GetScheduleAll(PyChannelObject *self)
{
    return self->flags.schedule_all;
}

void
PyChannel_SetScheduleAll(PyChannelObject *self, int val)
{
    self->flags.schedule_all = val ? 1 : 0;
}

int
PyChannel_GetBalance(PyChannelObject *self)
{
    return self->balance;
}

static PyGetSetDef channel_getsetlist[] = {
    {"queue",                   (getter)channel_get_queue, NULL,
     PyDoc_STR("the chain of waiting tasklets.")},
    {"closing",                 (getter)channel_get_closing, NULL,
     PyDoc_STR("True when close was called.")},
    {"closed",                  (getter)channel_get_closed, NULL,
     PyDoc_STR("True when close was called and the channel is empty..")},
    {"preference",              (getter)channel_get_preference,
                            (setter)channel_set_preference,
     PyDoc_STR("-1 prefer receiver (default), 1 prefer sender, 0 don't\n"
     "prefer anything. See also schedule_all")},
    {"schedule_all",            (getter)channel_get_schedule_all,
                            (setter)channel_set_schedule_all,
     PyDoc_STR("schedule to the next runnable on any channel action.\n"
     "overrides preference.")},
    {0}
};


static PyMemberDef channel_members[] = {
    {"balance", T_INT, offsetof(PyChannelObject, balance), READONLY,
     PyDoc_STR("the number of tasklets waiting to send (>0) or receive (<0).")},
    {0}
};

/**********************************************************

  The central functions of the channel concept.
  A tasklet can either send or receive on a channel.
  A channel has a queue of waiting tasklets.
  They are either all waiting to send or all
  waiting to receive.
  Initially, a channel is in a neutral state.
  The queue is empty, there is no way to
  send or receive without becoming blocked.

  Sending 1):
    A tasklet wants to send and there is
    a queued receiving tasklet. The sender puts
    its data into the receiver, unblocks it,
    and inserts it at the top of the runnables.
    The receiver is scheduled.
  Sending 2):
    A tasklet wants to send and there is
    no queued receiving tasklet.
    The sender will become blocked and inserted
    into the queue. The next receiver will
    handle the rest through "Receiving 1)".
  Receiving 1):
    A tasklet wants to receive and there is
    a queued sending tasklet. The receiver takes
    its data from the sender, unblocks it,
    and inserts it at the end of the runnables.
    The receiver continues with no switch.
  Receiving 2):
    A tasklet wants to receive and there is
    no queued sending tasklet.
    The receiver will become blocked and inserted
    into the queue. The next sender will
    handle the rest through "Sending 1)".
 */


static void
channel_callback(PyObject *channel_hook, PyChannelObject *channel, PyTaskletObject *task, int sending, int willblock)
{
    PyObject *ret;
    PyObject *type, *value, *traceback;

    assert(channel_hook);
    PyErr_Fetch(&type, &value, &traceback);
    ret = PyObject_CallFunction(channel_hook, "(OOii)", channel,
                                task, sending, willblock);
    if (ret != NULL) {
        PyErr_Restore(type, value, traceback);
    }
    else {
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
    }

    Py_XDECREF(ret);
}

#define NOTIFY_CHANNEL(channel, task, dir, cando, res) \
    do { \
        PyObject * channel_hook = ts->interp->st.channel_hook; \
        if(channel_hook != NULL) { \
            if (ts->st.schedlock) {  \
                RUNTIME_ERROR("Recursive channel call due to callbacks!", res); \
            } \
            Py_INCREF(channel_hook);  /* Own a ref while calling channel_hook! */ \
            ts->st.schedlock = 1; \
            channel_callback(channel_hook, channel, task, dir > 0, !cando); \
            ts->st.schedlock = 0; \
            Py_DECREF(channel_hook); \
        } \
    } while(0)


int PyStackless_SetChannelCallback(PyObject *callable)
{
    PyThreadState * ts = PyThreadState_GET();
    if(callable != NULL && !PyCallable_Check(callable))
        TYPE_ERROR("channel callback must be callable", -1);
    Py_XINCREF(callable);
    Py_XSETREF(ts->interp->st.channel_hook, callable);
    return 0;
}

PyObject *
slp_get_channel_callback(void)
{
    PyObject *temp = PyThreadState_GET()->interp->st.channel_hook;
    Py_XINCREF(temp);
    return temp;
}

PyDoc_STRVAR(channel_send__doc__,
"channel.send(value) -- send a value over the channel.\n\
If no other tasklet is already receiving on the channel,\n\
the sender will be blocked. Otherwise, the receiver will\n\
be activated immediately, and the sender is put at the end of\n\
the runnables list.");

static PyObject *
PyChannel_Send_M(PyChannelObject *self, PyObject *arg)
{
    PyMethodDef def = {"send", (PyCFunction)PyChannel_Send, METH_O};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, "O", arg);
}

static int
generic_channel_cando(PyThreadState *ts, PyObject **result, PyChannelObject *self, int dir, int stackless);
static int
generic_channel_block(PyThreadState *ts, PyObject **result, PyChannelObject *self, int dir, int stackless);

/*
 * This generic function exchanges values over a channel.
 * the action can be either send or receive.
 * Note that this works even across threads. The insert action
 * uses the tstate which is stored in the target.
 */

static PyObject *
generic_channel_action(PyChannelObject *self, PyObject *arg, int dir, int stackless)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *source = ts->st.current;
    PyTaskletObject *target = self->head;
    int cando = dir > 0 ? self->balance < 0 : self->balance > 0;
    int interthread = cando ? target->cstate->tstate != ts : 0;
    PyObject *tmpval, *retval;
    int fail;

    assert(abs(dir) == 1);

    /* set the channel tmpval here, for the callback */
    TASKLET_CLAIMVAL(source, &tmpval);
    TASKLET_SETVAL(source, arg);

    /* note that notify might release the GIL. */
    /* XXX for the moment, we notify late on interthread */
    if (!interthread)
        NOTIFY_CHANNEL(self, source, dir, cando, NULL);

    if (cando)
        /* communication 1): there is somebody waiting */
        fail = generic_channel_cando(ts, &retval, self, dir, stackless);
    else
        fail = generic_channel_block(ts, &retval, self, dir, stackless);

    if (fail) {
        TASKLET_SETVAL_OWN(source, tmpval);
        return NULL;
    }
    Py_DECREF(tmpval);
        if (interthread)
            NOTIFY_CHANNEL(self, source, dir, cando, NULL);
    return retval;
}

static int
generic_channel_cando(PyThreadState *ts, PyObject **result, PyChannelObject *self, int dir, int stackless)
{
    PyTaskletObject *source = ts->st.current;
    PyTaskletObject *switchto, *target, *next;
    int interthread;
    uint8_t oldflags, runflags = 0;
    int switched, fail;

    /* swap data and perform necessary scheduling */

    switchto = target = slp_channel_remove(self, NULL, NULL, &next);
    interthread = target->cstate->tstate != ts;
    /* exchange data */
    TASKLET_SWAPVAL(source, target);

    if (interthread) {
        ; /* nothing happens, the target is merely made runnable */
    } else {
        if (self->flags.schedule_all) {
            /* target goes last */
            slp_current_insert(target);
            /* always schedule away from source */
            switchto = source->next;
        }
        else if (self->flags.preference == -dir) {
            /* move target after source */
            ts->st.current = source->next;
            slp_current_insert(target);
            ts->st.current = source;
            /* don't mess with this scheduling behaviour: */
            runflags = PY_WATCHDOG_NO_SOFT_IRQ;
        }
        else {
            /* otherwise we return to the caller */
            slp_current_insert(target);
            switchto = source;
            /* don't mess with this scheduling behaviour: */
            runflags = PY_WATCHDOG_NO_SOFT_IRQ;
        }
    }

    /* Make sure that the channel will exist past the actual switch, if
    * we are softswitching.  A temporary channel might disappear.
    */
    assert(ts->st.del_post_switch == NULL);
    if (source != target && Py_REFCNT(self)) {
        ts->st.del_post_switch = (PyObject*)self;
        Py_INCREF(self);
    }

    oldflags = ts->st.runflags;
    ts->st.runflags |= runflags; /* extra info for slp_schedule_task */
    fail = slp_schedule_task(result, source, switchto, stackless, &switched);

    if (fail || !switched)
        Py_CLEAR(ts->st.del_post_switch);
    if (fail) {
        ts->st.runflags = oldflags;
        if (!interthread) {
            slp_current_uninsert(target);
            ts->st.current = source;
        }
        slp_channel_insert(self, target, -dir, next);
        TASKLET_SWAPVAL(source, target);
    } else {
        if (interthread)
            Py_DECREF(target);
    }
    return fail;
}

static int
generic_channel_block(PyThreadState *ts, PyObject **result, PyChannelObject *self, int dir, int stackless)
{
    PyTaskletObject *target, *source = ts->st.current;
    int fail, switched;

    /* communication 2): there is nobody waiting, so we must switch */
    if (source->flags.block_trap)
        RUNTIME_ERROR("this tasklet does not like to be"
                        " blocked.", -1);
    if (self->flags.closing) {
        PyErr_SetString(PyExc_ValueError, "Send/receive operation on a closed channel");
        return -1;
    }

    slp_current_remove();
    slp_channel_insert(self, source, dir, NULL);
    target = ts->st.current;

    /* Make sure that the channel will exist past the actual switch, if
    * we are softswitching.  A temporary channel might disappear.
    */
    assert(ts->st.del_post_switch == NULL);
    if (Py_REFCNT(self)) {
        ts->st.del_post_switch = (PyObject*)self;
        Py_INCREF(self);
    }

    fail =  slp_schedule_task(result, source, target, stackless, &switched);
    if (fail || !switched)
        Py_CLEAR(ts->st.del_post_switch);
    if (fail) {
        /* undo our tasklet shuffling */
        slp_channel_remove(self, source, NULL, NULL);
        slp_current_unremove(source);
    }
    return fail;
}

static PyObject *
impl_channel_send(PyChannelObject *self, PyObject *arg)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();

    if(ts->st.main == NULL) return PyChannel_Send_M(self, arg);
    return generic_channel_action(self, arg, 1, stackless);
}

int
PyChannel_Send_nr(PyChannelObject *self, PyObject *arg)
{
    PyThreadState *ts = PyThreadState_GET();
    STACKLESS_PROPOSE_ALL(ts);
    return slp_return_wrapper(impl_channel_send(self, arg));
}

int
PyChannel_Send(PyChannelObject *self, PyObject *arg)
{
    return slp_return_wrapper_hard(impl_channel_send(self, arg));
}

static PyObject *
channel_send(PyObject *self, PyObject *arg)
{
    STACKLESS_GETARG();
    return generic_channel_action((PyChannelObject*)self, arg, 1, stackless);
}


PyDoc_STRVAR(channel_send_exception__doc__,
"channel.send_exception(exc, value) -- send an exception over the channel.\n\
exc must be a subclass of Exception.\n\
Behavior is like channel.send, but that the receiver gets an exception.");

static PyObject *
channel_send_exception(PyObject *myself, PyObject *args);

static PyObject *
PyChannel_SendException_M(PyChannelObject *self, PyObject *klass,
                          PyObject *args)
{
    PyMethodDef def = {"send_exception", (PyCFunction)channel_send_exception, METH_VARARGS};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, "OO", klass, args);
}

static PyObject *
impl_channel_send_exception(PyChannelObject *self, PyObject *klass, PyObject *args)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *bomb, *ret = NULL;

    assert(PyChannel_Check(self));
    if (ts->st.main == NULL)
        return PyChannel_SendException_M(self, klass, args);

    bomb = slp_make_bomb(klass, args, "channel.send_exception");
    if (bomb != NULL) {
        ret = generic_channel_action(self, bomb, 1, stackless);
        Py_DECREF(bomb);
    }
    return ret;
}

int
PyChannel_SendException(PyChannelObject *self, PyObject *klass, PyObject *args)
{
    return slp_return_wrapper_hard(impl_channel_send_exception(self, klass, args));
}

static PyObject *
channel_send_exception(PyObject *myself, PyObject *args)
{
    STACKLESS_GETARG();
    PyObject *retval = NULL;
    PyObject *klass = PySequence_GetItem(args, 0);

    if (klass == NULL)
        TYPE_ERROR("send_exception() takes at least 1 argument", NULL);
    args = PySequence_GetSlice(args, 1, PySequence_Size(args));
    if (!args) {
        goto err_exit;
    }
    STACKLESS_PROMOTE_ALL();
    retval = impl_channel_send_exception((PyChannelObject*)myself,
                                            klass, args);
    STACKLESS_ASSERT();
    if (retval == NULL || STACKLESS_UNWINDING(retval)) {
        goto err_exit;
    }
    Py_INCREF(Py_None);
    Py_SETREF(retval, Py_None);
err_exit:
    Py_DECREF(klass);
    Py_XDECREF(args);
    return retval;
}

PyDoc_STRVAR(channel_send_throw__doc__,
"channel.send_throw(typ[,val[,tb]]) -- send an exception over the channel.\n\
Behavior is like channel.send, but that the receiver gets an exception.");

static PyObject *
channel_send_throw(PyObject *myself, PyObject *args);

static PyObject *
PyChannel_SendThrow_M(PyChannelObject *self, PyObject *exc,
                          PyObject *val, PyObject *tb)
{
    PyMethodDef def = {"send_throw", (PyCFunction)channel_send_throw, METH_VARARGS};
    exc = exc ? exc : Py_None;
    val = val ? val : Py_None;
    tb = tb ? tb : Py_None;
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, "OOO", exc, val, tb);
}

static PyObject *
impl_channel_send_throw(PyChannelObject *self, PyObject *exc, PyObject *val, PyObject *tb)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *bomb, *ret = NULL;

    assert(PyChannel_Check(self));
    if (ts->st.main == NULL)
        return PyChannel_SendThrow_M(self, exc, val, tb);

    bomb = slp_exc_to_bomb(exc, val, tb);
    if (bomb != NULL) {
        ret = generic_channel_action(self, bomb, 1, stackless);
        Py_DECREF(bomb);
    }
    return ret;
}

int
PyChannel_SendThrow(PyChannelObject *self, PyObject *exc, PyObject *val, PyObject *tb)
{
    return slp_return_wrapper_hard(impl_channel_send_throw(self, exc, val, tb));
}

static PyObject *
channel_send_throw(PyObject *myself, PyObject *args)
{
    STACKLESS_GETARG();

    PyObject *typ;
    PyObject *tb = Py_None;
    PyObject *val = Py_None;
    PyObject *retval;
    if (!PyArg_UnpackTuple(args, "send_throw", 1, 3, &typ, &val, &tb))
        return NULL;

    STACKLESS_PROMOTE_ALL();
    retval = impl_channel_send_throw((PyChannelObject*)myself,
        typ, val, tb);
    STACKLESS_ASSERT();
    if (retval == NULL || STACKLESS_UNWINDING(retval)) {
        goto err_exit;
    }
    Py_INCREF(Py_None);
    Py_SETREF(retval, Py_None);
err_exit:
    return retval;
}

PyDoc_STRVAR(channel_receive__doc__,
"channel.receive() -- receive a value over the channel.\n\
If no other tasklet is already sending on the channel,\n\
the receiver will be blocked. Otherwise, the receiver will\n\
continue immediately, and the sender is put at the end of\n\
the runnables list.\n\
The above policy can be changed by setting channel flags.");

static PyObject *
PyChannel_Receive_M(PyChannelObject *self)
{
    PyMethodDef def = {"receive", (PyCFunction)PyChannel_Receive, METH_NOARGS};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, NULL);
}

static PyObject *
impl_channel_receive(PyChannelObject *self)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();

    if (ts->st.main == NULL) return PyChannel_Receive_M(self);
    return generic_channel_action(self, Py_None, -1, stackless);
}

PyObject *
PyChannel_Receive_nr(PyChannelObject *self)
{
    PyThreadState *ts = PyThreadState_GET();
    PyObject *ret;

    STACKLESS_PROPOSE_ALL(ts);
    ret = impl_channel_receive(self);
    STACKLESS_ASSERT();
    return ret;
}

PyObject *
PyChannel_Receive(PyChannelObject *self)
{
    PyObject *ret = impl_channel_receive(self);
    STACKLESS_ASSERT();
    assert(!STACKLESS_UNWINDING(ret));
    return ret;
}

static PyObject *
channel_receive(PyObject *self)
{
    return impl_channel_receive((PyChannelObject*)self);
}


/*********************************************************

  Sequences in channels.

  Channels can work as an iterator. This removes all
  call overhead on the receiver side and is optimum.

  There is still the sender who has to call a method to
  transfer data.
  But to my surprize, supporting a sequence protocol
  for sending does exactly the desired job.

  Example 1:

  def sender(ch):
    while condition:
      # compute a bulk of results
      ch.send:sequence(results)

  def receiver(ch):
    for each in ch:
      # process each

  Now, with a little layer, we can even avoid the list
  creation, and use a generator to yield the results
  into the channel. This is also helpful to overcome
  the generator restriction of only one yield level.

  Example 2:

  def sender(ch, src):

    def parser():
      while more_to_parse:
    # find next token
    if is_simple_token:
      yield token
    else:
      # got nested structure
      ch.send_sequence(parser())

    parser()

 *********************************************************/


/*
 * iterator extension.
 * This is probably the fastest way to run through a channel
 * from Python.
 */

static PyObject *
channel_iternext(PyChannelObject *self)
{
    STACKLESS_GETARG();

    if (self->flags.closing && self->balance <= 0) {
        /* signal the end of the iteration */
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }
    STACKLESS_PROMOTE_ALL();
    return impl_channel_receive(self);
}

static PyObject *
channel_getiter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

/*
 * sequence support for sending.
 * This is now absolutely unbeatable, because you can
 * let a generator yield into the channel, without
 * any extra method call. :-)
 */

PyDoc_STRVAR(channel_send_sequence__doc__,
"channel.send_sequence(seq) -- send a stream of values\n\
over the channel. Combined with a generator, this is\n\
a very efficient way to build fast pipes.");

/*
 * this is the straight-forward and simple implementation,
 * but here we have almost no speedup, since all switches
 * are hard.
 */

static PyObject *
_channel_send_sequence(PyChannelObject *self, PyObject *v)
{
    PyObject *it;
    int i;
    PyObject *ret;

    it = PyObject_GetIter(v);
    if (it == NULL)
        return NULL;

    /* Run iterator to exhaustion. */
    for (i = 0; ; i++) {
        PyObject *item = PyIter_Next(it);
        if (item == NULL) {
            if (PyErr_Occurred())
                goto error;
            break;
        }
        ret = impl_channel_send(self, item);
        Py_DECREF(item);
        if (ret == NULL)
            goto error;
        Py_DECREF(ret);
    }

    Py_DECREF(it);
    return PyLong_FromLong(i);

  error:
    Py_DECREF(it);
    return NULL;

}

/*
 * I tried to make this worker loop look like the simple
 * implementation. The problem is to leave and enter
 * the loop all the time. Hopefully the idea is still visible.
 */

PyObject *
slp_channel_seq_callback(PyFrameObject *_f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *f = (PyCFrameObject *) _f;
    PyChannelObject *ch;
    PyObject *item;
    int stage = f->n;

    /* prolog to re-enter the loop */
    if (stage == 1) {
        item = retval;
        goto back_with_data;
    }
    if (retval == NULL)
        goto exit_frame;

    Py_DECREF(retval);
    retval = NULL;

    if (stage == 2) {
        goto back_from_send;
    }

    /* Run iterator to exhaustion. */
    for (; ; f->i++) {
        /* get the data */
        STACKLESS_PROPOSE_ALL(ts);
        item = PyIter_Next(f->ob1);

        if (STACKLESS_UNWINDING(item)) {
            stage = f->n = 1;
            return item;
        }

back_with_data:
        if (item == NULL) {
            if (PyErr_Occurred()) {
                if (PyErr_ExceptionMatches(
                    PyExc_StopIteration))
                    PyErr_Clear();
                else
                    goto exit_frame;
            }
            break;
        }

        /* send the data */
        ch = (PyChannelObject *) f->ob2;
        STACKLESS_PROPOSE_ALL(ts);
        retval = impl_channel_send(ch, item);
        Py_DECREF(item);
        if (retval == NULL)
            goto exit_frame;
        if (STACKLESS_UNWINDING(retval)) {
            stage = f->n = 2;
            return retval;
        }
        Py_DECREF(retval);
back_from_send:
        ;
    }

    retval = PyLong_FromLong(f->i);
exit_frame:

    /* epilog to return from the frame */
    SLP_STORE_NEXT_FRAME(ts, f->f_back);
    Py_DECREF(f);
    return retval;
}

static PyObject *
channel_send_sequence(PyChannelObject *self, PyObject *v)
{
    STACKLESS_GETARG();
    PyThreadState *ts = PyThreadState_GET();
    PyObject *it;
    PyCFrameObject *f;

    if (!stackless)
        return _channel_send_sequence(self, v);

    it = PyObject_GetIter(v);
    if (it == NULL)
        return NULL;

    f = slp_cframe_new(slp_channel_seq_callback, 1);
    if (f == NULL)
        goto error;

    f->ob1 = it;
    Py_INCREF(self);
    f->ob2 = (PyObject *) self;
    f->i = 0;
    f->n = 0;
    SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *) f);
    Py_INCREF(Py_None);
    return STACKLESS_PACK(ts, Py_None);
error:
    Py_DECREF(it);
    return NULL;
}


PyDoc_STRVAR(channel_close__doc__,
"channel.close() -- stops the channel from enlarging its queue.\n\
\n\
If the channel is not empty, the flag 'closing' becomes true.\n\
If the channel is empty, the flag 'closed' becomes true.");

static PyObject *
channel_close(PyChannelObject *self)
{
    self->flags.closing = 1;

    Py_INCREF(Py_None);
    return Py_None;
}

void
PyChannel_Close(PyChannelObject *self)
{
    self->flags.closing = 1;
}

PyDoc_STRVAR(channel_open__doc__,
"channel.open() -- reopen a channel. See channel.close.");

static PyObject *
channel_open(PyChannelObject *self)
{
    self->flags.closing = 0;

    Py_INCREF(Py_None);
    return Py_None;
}

void
PyChannel_Open(PyChannelObject *self)
{
    self->flags.closing = 0;
}

PyDoc_STRVAR(channel_reduce__doc__,
"channel.__reduce__() -- currently does not distinguish threads.");

static PyObject *
channel_reduce(PyChannelObject * ch)
{
    PyObject *tup = NULL, *lis = NULL;
    PyTaskletObject *t;
    int i, n;

    lis = PyList_New(0);
    if (lis == NULL) goto err_exit;
    t = ch->head;
    n = abs(ch->balance);
    for (i = 0; i < n; i++) {
        if (PyList_Append(lis, (PyObject *) t)) goto err_exit;
        t = t->next;
    }
    tup = Py_BuildValue("(O()(iiO))",
                        Py_TYPE(ch),
                        ch->balance,
                        channel_flags_as_integer(ch->flags),
                        lis
                        );
err_exit:
    Py_XDECREF(lis);
    return tup;
}

PyDoc_STRVAR(channel_setstate__doc__,
"channel.__setstate__(balance, flags, [tasklets]) -- currently does not distinguish threads.");

static PyObject *
channel_setstate(PyObject *self, PyObject *args)
{
    PyChannelObject *ch = (PyChannelObject *) self;
    PyTaskletObject *t;
    PyObject *lis;
    int flags, balance;
    int dir;
    Py_ssize_t i, n;

    if (!PyArg_ParseTuple(args, "iiO!:channel",
                          &balance,
                          &flags,
                          &PyList_Type, &lis))
        return NULL;

    channel_clear((PyObject *) ch);
    n = PyList_GET_SIZE(lis);
    ch->flags = channel_flags_from_integer(flags);
    dir = balance > 0 ? 1 : -1;

    for (i = 0; i < n; i++) {
        t = (PyTaskletObject *) PyList_GET_ITEM(lis, i);

        if (PyTasklet_Check(t) && !t->flags.blocked) {
            Py_INCREF(t);
            slp_channel_insert(ch, t, dir, NULL);
        }
    }
    Py_INCREF(self);
    return self;
}


#define PCF PyCFunction
#define METH_KS METH_KEYWORDS | METH_STACKLESS
#define METH_VS METH_VARARGS | METH_STACKLESS
#define METH_NS METH_NOARGS | METH_STACKLESS
#define METH_OS METH_O | METH_STACKLESS

static PyMethodDef
channel_methods[] = {
    {"send",                (PCF)channel_send,              METH_OS,
     channel_send__doc__},
    {"send_exception",  (PCF)channel_send_exception,        METH_VS,
     channel_send_exception__doc__},
    {"send_throw",  (PCF)channel_send_throw,                METH_VS,
     channel_send_throw__doc__},
    {"receive",             (PCF)channel_receive,           METH_NS,
     channel_receive__doc__},
    {"close",               (PCF)channel_close,             METH_NOARGS,
    channel_close__doc__},
    {"open",                (PCF)channel_open,              METH_NOARGS,
    channel_open__doc__},
    {"__reduce__",              (PCF)channel_reduce,        METH_NOARGS,
     channel_reduce__doc__},
    {"__reduce_ex__",           (PCF)channel_reduce,        METH_VARARGS,
     channel_reduce__doc__},
    {"__setstate__",            (PCF)channel_setstate,      METH_O,
     channel_setstate__doc__},
    {"send_sequence",   (PCF)channel_send_sequence,       METH_OS,
     channel_send_sequence__doc__},
    {NULL,                  NULL}             /* sentinel */
};

PyDoc_STRVAR(channel__doc__,
"A channel object is used for communication between tasklets.\n\
By sending on a channel, a tasklet that is waiting to receive\n\
is resumed. If there is no waiting receiver, the sender is suspended.\n\
By receiving from a channel, a tasklet that is waiting to send\n\
is resumed. If there is no waiting sender, the receiver is suspended.\
");

#ifdef STACKLESS
static PyMappingMethods channel_as_mapping = {
    .slpflags.tp_iternext = -1,
};
#endif

PyTypeObject PyChannel_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "_stackless.channel",
    sizeof(PyChannelObject),
    0,
    (destructor)channel_dealloc,                /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_compare */
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    SLP_TP_AS_MAPPING(channel_as_mapping),      /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    PyObject_GenericGetAttr,                    /* tp_getattro */
    PyObject_GenericSetAttr,                    /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE |
        Py_TPFLAGS_HAVE_STACKLESS_EXTENSION,    /* tp_flags */
    channel__doc__,                             /* tp_doc */
    (traverseproc)channel_traverse,             /* tp_traverse */
    (inquiry) channel_clear,                    /* tp_clear */
    0,                                          /* tp_richcompare */
    offsetof(PyChannelObject, chan_weakreflist),
                                            /* tp_weaklistoffset */
    (getiterfunc)channel_getiter,               /* tp_iter */
    (iternextfunc)channel_iternext,             /* tp_iternext */
    channel_methods,                            /* tp_methods */
    channel_members,                            /* tp_members */
    channel_getsetlist,                         /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    channel_new,                                /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};

#endif
