.. highlightlang:: c

|SLP| C-API
===========

|SLP| provides the following C functions.

Tasklets
--------

.. c:function:: PyTaskletObject *PyTasklet_New(PyTypeObject *type, PyObject *func)

  Return a new tasklet object. *type* must be derived from :c:type:`PyTasklet_Type`
  or ``NULL``. *func* must be a callable object or ``NULL`` or :c:macro:`Py_None`. If *func*
  is ``NULL`` or :c:macro:`Py_None` you must set it later with :c:func:`PyTasklet_BindEx`.

.. todo: in the case where NULL is returned and slp_ensure_linkage fails no
   exception is set, which is in contrast elsewhere in the function.

.. c:function:: int PyTasklet_Setup(PyTaskletObject *task, PyObject *args, PyObject *kwds)

  Binds a tasklet function to parameters, making it ready to run and inserts in
  into the runnables queue.  Returns ``0`` if successful or ``-1`` in the case of failure.

.. c:function:: int PyTasklet_BindEx(PyTaskletObject *task, PyObject *func, PyObject *args, PyObject *kwargs)

  Binds a tasklet to a function and/or to parameters, making it ready to run. This is the C equivalent to
  method :py:meth:`tasklet.bind`. The arguments *func*, *args* and *kwargs* are optional and
  may be ``NULL`` or `Py_None`. Returns ``0`` if successful or ``-1`` in the case of failure.

.. c:function:: int PyTasklet_BindThread(PyTaskletObject *task, long thread_id)

  Binds a tasklet function to a thread. This is the C equivalent to
  method :py:meth:`tasklet.bind_thread`. Returns ``0`` if successful or ``-1`` in the case of failure.

.. c:function:: int PyTasklet_Run(PyTaskletObject *task)

  Forces *task* to run immediately.  Returns ``0`` if successful, and ``-1`` in the
  case of failure.

.. c:function:: int PyTasklet_Run_nr(PyTaskletObject *task)

  Forces *task* to run immediately, soft switching if possible.  Returns ``1`` if
  the call soft switched, ``0`` if the call hard switched and -1 in the case of
  failure.

.. c:function:: int PyTasklet_Switch(PyTaskletObject *task)

  Forces *task* to run immediately. The previous tasklet is paused.
  Returns ``0`` if successful, and ``-1`` in the
  case of failure.

.. c:function:: int PyTasklet_Switch_nr(PyTaskletObject *task)

  Forces *task* to run immediately, soft switching if possible.
  The previous tasklet is paused. Returns ``1`` if
  the call soft switched, ``0`` if the call hard switched and -1 in the case of
  failure.

.. c:function:: int PyTasklet_Remove(PyTaskletObject *task)

  Removes *task* from the runnables queue.  Be careful! If this tasklet has a C
  stack attached, you need to either resume running it or kill it.  Just dropping
  it might give an inconsistent system state.  Returns ``0`` if successful, and
  ``-1`` in the case of failure.

.. c:function:: int PyTasklet_Insert(PyTaskletObject *task)

  Insert *task* into the runnables queue, if it isn't already there.   If it is
  blocked or dead, the function returns ``-1`` and a :exc:`RuntimeError` is raised.

.. c:function:: int PyTasklet_RaiseException(PyTaskletObject *self, PyObject *klass, PyObject *args)

  Raises an instance of the *klass* exception on the *self* tasklet.  *klass* must
  be a subclass of :exc:`Exception`.  Returns ``1`` if the call soft switched, ``0``
  if the call hard switched and ``-1`` in the case of failure.

  .. note:: Raising :exc:`TaskletExit` on a tasklet can be done to silently kill
     it, see :c:func:`PyTasklet_Kill`.

.. c:function:: int PyTasklet_Throw(PyTaskletObject *self, int pending, PyObject *exc, PyObject *val, PyObject *tb)

  Raises (*exc*, *val*, *tb*) on the *self* tasklet. This is the C equivalent to
  method :py:meth:`tasklet.throw`. Returns ``1`` if the call soft switched, ``0``
  if the call hard switched and ``-1`` in the case of failure.

.. c:function:: int PyTasklet_Kill(PyTaskletObject *self)

  Raises :exc:`TaskletExit` on tasklet *self*.  This should result in *task* being
  silently killed. (This exception is ignored by tasklet_end and
  does not invoke main as exception handler.)
  Returns ``1`` if the call soft switched, ``0`` if the call hard
  switched and ``-1`` in the case of failure.

.. c:function:: int PyTasklet_KillEx(PyTaskletObject *self, int pending)

  Raises :exc:`TaskletExit` on tasklet *self*.  This is the C equivalent to
  method :py:meth:`tasklet.kill`.
  Returns ``1`` if the call soft switched, ``0`` if the call hard
  switched and ``-1`` in the case of failure.

.. c:function:: int PyTasklet_GetAtomic(PyTaskletObject *task)

  Returns ``1`` if *task* is atomic, otherwise ``0``.

.. c:function:: int PyTasklet_SetAtomic(PyTaskletObject *task, int flag)

  Returns ``1`` if *task* is currently atomic, otherwise ``0``.  Sets the
  atomic attribute to the logical value of *flag*.

.. c:function:: int PyTasklet_GetIgnoreNesting(PyTaskletObject *task)

  Returns ``1`` if *task* ignores its nesting level when choosing whether to
  auto-schedule it, otherwise ``0``.

.. c:function:: int PyTasklet_SetIgnoreNesting(PyTaskletObject *task, int flag)

  Returns the existing value of the *ignore_nesting* attribute for the tasklet
  *task*, setting it to the logical value of *flag*.  If true, the tasklet may
  be auto-scheduled even if its *nesting_level* is > ``0``.

.. c:function:: int PyTasklet_GetBlockTrap(PyTaskletObject *task)

  Returns ``1`` if *task* is designated as not being allowed to be blocked on a
  channel, otherwise ``0``.

.. c:function:: void PyTasklet_SetBlockTrap(PyTaskletObject *task, int value)

  Returns ``1`` if *task* was already designated as not being allowed to be blocked
  on a channel, otherwise ``0``.  This attribute is set to the logical value of
  *value*.

.. c:function:: PyObject *PyTasklet_GetFrame(PyTaskletObject *task)

  Returns the current frame that *task* is executing in, or *NULL*

.. c:function:: int PyTasklet_IsMain(PyTaskletObject *task)

  Returns ``1`` if *task* is the main tasklet, otherwise ``0``.

.. c:function:: int PyTasklet_IsCurrent(PyTaskletObject *task)

  Returns ``1`` if *task* is the current tasklet, otherwise ``0``.

.. c:function:: int PyTasklet_GetRecursionDepth(PyTaskletObject *task)

  Return the current recursion depth of *task*.

.. c:function:: int PyTasklet_GetNestingLevel(PyTaskletObject *task)

  Return the current nesting level of *task*.

.. c:function:: int PyTasklet_Alive(PyTaskletObject *task)

  Returns ``1`` if *task* is alive (has an associated frame), otherwise
  ``0`` if it is dead.

.. c:function:: int PyTasklet_Paused(PyTaskletObject *task)

  Returns ``1`` if *task* is paused, otherwise ``0``.  A tasklet is paused if it is
  alive, but not scheduled or blocked on a channel.

.. c:function:: int PyTasklet_Scheduled(PyTaskletObject *task)

  Returns ``1`` if *task* is scheduled, otherwise ``0``.  In the context of this
  function a tasklet is considered to be scheduled if it is alive, and in the
  scheduler runnables list or blocked on a channel.

.. c:function:: int PyTasklet_Restorable(PyTaskletObject *task)

  Returns ``1`` if *task* can be fully unpickled, otherwise ``0``.  A tasklet can
  be pickled whether it is fully restorable or not for the purposes of debugging
  and introspection.  A tasklet that has been hard-switched cannot be fully
  pickled, for instance.

Channels
--------

.. c:function:: PyChannelObject* PyChannel_New(PyTypeObject *type)

  Return a new channel object, or *NULL* in the case of failure.  *type* must be
  derived from :c:type:`PyChannel_Type` or be *NULL*, otherwise a :exc:`TypeError`
  is raised.

.. c:function:: int PyChannel_Send(PyChannelObject *self, PyObject *arg)

  Send *arg* on the channel *self*.  Returns ``0`` if the operation was
  successful, or ``-1`` in the case of failure.

.. c:function:: int PyChannel_Send_nr(PyChannelObject *self, PyObject *arg)

  Send *arg* on the channel *self*, soft switching if possible.  Returns ``1`` if
  the call soft switched, ``0`` if the call hard switched and -1 in the case of
  failure.

.. c:function:: PyObject *PyChannel_Receive(PyChannelObject *self)

  Receive on the channel *self*.  Returns a |PY| object if the operation was
  successful, or *NULL* in the case of failure.

.. c:function:: PyObject *PyChannel_Receive_nr(PyChannelObject *self)

  Receive on the channel *self*, soft switching if possible.  Returns a |PY|
  object if the operation was successful, :c:type:`Py_UnwindToken` if a soft switch
  occurred, or *NULL* in the case of failure.

.. c:function:: int PyChannel_SendException(PyChannelObject *self, PyObject *klass, PyObject *value)

  Returns ``0`` if successful or ``-1`` in the case of failure.  An instance of the
  exception type *klass* is raised on the first tasklet blocked on channel *self*.

.. c:function:: int PyChannel_SendThrow(PyChannelObject *self, PyObject *exc, PyObject *val, PyObject *tb)

  Returns ``0`` if successful or ``-1`` in the case of failure.
  (*exc*, *val*, *tb*) is raised on the first tasklet blocked on channel *self*.

.. c:function:: PyObject *PyChannel_GetQueue(PyChannelObject *self)

  Returns the first tasklet in the channel *self*'s queue, or *NULL* in the case
  the queue is empty.

.. c:function:: void PyChannel_Close(PyChannelObject *self)

  Marks the channel *self* as closing.  No further tasklets can be blocked on the
  it from this point, unless it is later reopened.

.. c:function:: void PyChannel_Open(PyChannelObject *self)

  Reopens the channel *self*.  This allows tasklets to once again send and receive
  on it, if those operations would otherwise block the given tasklet.

.. c:function:: int PyChannel_GetClosing(PyChannelObject *self)

  Returns ``1`` if the channel *self* is marked as closing, otherwise ``0``.

.. c:function:: int PyChannel_GetClosed(PyChannelObject *self)

  Returns ``1`` if the channel *self* is marked as closing and there are no tasklets
  blocked on it, otherwise ``0``.

.. c:function:: int PyChannel_GetPreference(PyChannelObject *self)

  Returns the current scheduling preference value of *self*.  See
  :attr:`channel.preference`.

.. c:function:: void PyChannel_SetPreference(PyChannelObject *self, int val)

  Sets the current scheduling preference value of *self*.  See
  :attr:`channel.preference`.

.. c:function:: int PyChannel_GetScheduleAll(PyChannelObject *self)

  Gets the *schedule_all* override flag for *self*.  See
  :attr:`channel.schedule_all`.

.. c:function:: void PyChannel_SetScheduleAll(PyChannelObject *self, int val)

  Sets the *schedule_all* override flag for *self*.  See
  :attr:`channel.schedule_all`.

.. c:function:: int PyChannel_GetBalance(PyChannelObject *self)

  Gets the balance for *self*.  See :attr:`channel.balance`.

stackless module
----------------

.. c:function:: PyObject *PyStackless_Schedule(PyObject *retval, int remove)

  Suspend the current tasklet and schedule the next one in the cyclic chain.
  if remove is nonzero, the current tasklet will be removed from the chain.
  retval = success  NULL = failure

.. c:function:: PyObject *PyStackless_Schedule_nr(PyObject *retval, int remove)

  retval = success  NULL = failure
  retval == Py_UnwindToken: soft switched

.. c:function:: int PyStackless_GetRunCount()

  get the number of runnable tasks of the current thread, including the current one.
  -1 = failure

.. c:function:: PyObject *PyStackless_GetCurrent()

  Get the currently running tasklet, that is, "yourself".

.. c:function:: long PyStackless_GetCurrentId()

  Get a unique integer ID for the current tasklet

  Threadsafe.

  This is useful for benchmarking code that
  needs to get some sort of a stack identifier and must
  not worry about the GIL being present and so on.

  .. note::

     1. the "main" tasklet on each thread will have the same id,
        even if a proper tasklet has not been initialized.

     2. IDs may get recycled for new tasklets.

.. c:function:: PyObject *PyStackless_RunWatchdog(long timeout)

  Runs the scheduler until there are no tasklets remaining within it, or until
  one of the scheduled tasklets runs for *timeout* VM instructions without
  blocking.  Returns *None* if the scheduler is empty, a tasklet object if that
  tasklet timed out, or *NULL* in the case of failure.  If a timed out tasklet
  is returned, it should be killed or reinserted.

  This function can only be called from the main tasklet.
  During the run, main is suspended, but will be invoked
  after the action. You will write your exception handler
  here, since every uncaught exception will be directed
  to main.

.. c:function:: PyObject *PyStackless_RunWatchdogEx(long timeout, int flags)

  Wraps :c:func:`PyStackless_RunWatchdog`, but allows its behaviour to be
  customised by the value of *flags* which may contain any of the following
  bits:

  ``Py_WATCHDOG_THREADBLOCK``
     Allows a thread to block if it runs out of tasklets.  Ideally
     it will be awakened by other threads using channels which its
     blocked tasklets are waiting on.

  ``PY_WATCHDOG_SOFT``
     Instead of interrupting a tasklet, we wait until the
     next tasklet scheduling moment to return.  Always returns
     *Py_None*, as everything is in order.

  ``PY_WATCHDOG_IGNORE_NESTING``
     Allows interrupts at all levels, effectively acting as
     though the *ignore_nesting* attribute were set on all
     tasklets.

  ``PY_WATCHDOG_TIMEOUT``
     Interprets *timeout* as a fixed run time, rather than a
     per-tasklet run limit.  The function will then attempt to
     interrupt execution once this many total opcodes have
     been executed since the call was made.

debugging and monitoring functions
----------------------------------

.. c:function:: int PyStackless_SetChannelCallback(PyObject *callable)

  channel debugging.  The callable will be called on every send or receive.
  Passing NULL removes the handler.
  Parameters of the callable:
  channel, tasklet, int sendflag, int willblock
  -1 = failure

.. c:function:: int PyStackless_SetScheduleCallback(PyObject *callable)

  scheduler monitoring.
  The callable will be called on every scheduling.
  Passing NULL removes the handler.
  Parameters of the callable: from, to
  When a tasklet dies, to is None.
  After death or when main starts up, from is None.
  -1 = failure

.. c:function:: void PyStackless_SetScheduleFastcallback(slp_schedule_hook_func func)

  Scheduler monitoring with a faster interface.

Interface functions
-------------------

Most of the above functions can be called both from "inside"
and "outside" stackless. "inside" means there should be a running
(c)frame on top which acts as the "main tasklet". The functions
do a check whether the main tasklet exists, and wrap themselves
if it is necessary.
The following routines are used to support this, and you may use
them as well if you need to make your specific functions always
available.

.. c:function:: PyObject *PyStackless_Call_Main(PyObject *func, PyObject *args, PyObject *kwds)

  Run any callable as the "main" |PY| function.  Returns a |PY| object, or
  *NULL* in the case of failure.

.. c:function:: PyObject *PyStackless_CallMethod_Main(PyObject *o, char *name, char *format, ...)

  Convenience: Run any method as the "main" |PY| function.  Wraps PyStackless_Call_Main.
