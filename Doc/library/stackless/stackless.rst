:mod:`stackless` --- The built-in extension module
==================================================

.. module:: stackless
   :synopsis: Access the enhanced functionality provided by |SLP|

.. moduleauthor:: Christian Tismer <tismer@stackless.com>
.. sectionauthor:: Richard Tew <richard.m.tew@gmail.com>

.. versionadded:: 1.5.2

The :mod:`stackless` module is the way in which programmers must access
the enhanced functionality provided by |SLP|.

.. toctree::

   tasklets.rst
   channels.rst
   scheduler.rst
   debugging.rst
   threads.rst
   pickling.rst

---------
Constants
---------

.. data:: PICKLEFLAGS_PRESERVE_TRACING_STATE

   This constant defines an option flag for the function
   :func:`pickle_flags`.

   If this flag is set, a pickled tasklet contains
   information about the tracing and/or profiling state of the tasklet.
   Usually there's no need to set this flag.

   .. versionadded:: 3.7


.. data:: PICKLEFLAGS_PRESERVE_AG_FINALIZER
          PICKLEFLAGS_RESET_AG_FINALIZER

   These two constants define the option flags for the function
   :func:`pickle_flags`.

   .. seealso:: :ref:`Pickling of Asynchronous Generators <slp_pickling_asyncgen>`

   .. versionadded:: 3.7

   .. note::
      These constants have been added on a provisional basis (see :pep:`411`
      for details.)

.. data:: PICKLEFLAGS_PICKLE_CONTEXT

   This constant defines an option flag for the function
   :func:`pickle_flags`.

   If this flag is set, |SLP| assumes that a :class:`~contextvars.Context` object
   is pickleable. As a consequence the state information returned by :meth:`tasklet.__reduce_ex__`
   includes the context of the tasklet.

   .. versionadded:: 3.7.6

   .. note::
      This constant has been added on a provisional basis (see :pep:`411`
      for details.)

---------
Functions
---------

The main scheduling related functions:

.. function:: run(timeout=0, threadblock=False, soft=False, ignore_nesting=False, totaltimeout=False)

   When run without arguments, scheduling is cooperative.
   It us up to you to ensure your tasklets yield, perhaps by calling
   :func:`schedule`, giving other tasklets a turn to run.  The scheduler
   will exit when there are no longer any runnable tasklets left within it.
   This might be because all the tasklets have exited, whether by completing
   or erroring, but it also might be because some are blocked on channels.
   You should not assume that when :func:`run` exits, your tasklets have
   all run to completion, unless you know for sure that is how you
   structured your application.

   The optional argument *timeout* is primarily used to run the scheduler
   in a different manner, providing pre-emptive scheduling.  A non-zero
   value indicates that as each tasklet is given a chance to run, it
   should only be allowed to run as long as the number of
   |PY| :mod:`virtual instructions <dis>` are below this value. If a
   tasklet hits this limit, then it is interrupted and the scheduler
   exits returning the now no longer scheduled tasklet to the caller.

   Example - run until 1000 opcodes have been executed::

       interrupted_tasklet = stackless.run(1000)
       # interrupted_tasklet is no longer scheduled, reschedule it.
       interrupted_tasklet.insert()
       # Now run your custom logic.
       ...

   The optional argument *threadblock* affects the way |SLP| works when
   channels are used for communication between threads.  Normally when
   the scheduler has no remaining tasklets to run besides the current one,
   the main tasklet is reawakened.  By engaging this option, if there are other
   running |PY| threads then the current one will instead block expecting
   them to eventually wake it up.

   The optional argument *soft* affects how pre-emptive scheduling behaves.
   When a pre-emptive interruption would normally occur, instead of
   interrupting and returning the running tasklet, the scheduler exits at
   the next convenient scheduling moment.

   The optional argument *ignore_nesting* affects the behaviour of the
   attribute :attr:`tasklet.nesting_level` on individual tasklets.  If set,
   interrupts are allowed at any interpreter nesting level, causing the
   tasklet-level attribute to be ignored.

   The optional argument *totaltimeout* affects how pre-emptive scheduling
   behaves.  Normally the scheduler is interrupted when any given
   tasklet has been running for *timeout* instructions.  If a value is
   given for *totaltimeout*, instead the scheduler is interrupted when it
   has run for *totaltimeout* instructions.

   This function can be called from any tasklet.  When called without
   arguments, the calls nest so that the innermost call will return
   once the run-queue is emptied.  Calls with a *timeout* argument
   however stack so that only the first one has any effect.  Subsequent
   calls with *timeout* behave as though timeout were omitted.  This allows
   a stackless application to be monitored on the outside without the
   inner application modifying the outer behaviour.

   .. note::

      The most common use of this function is to call it either without
      arguments, or with a value for *timeout*.

.. function:: schedule(retval=stackless.current)

   Yield execution of the currently running tasklet.  When called, the tasklet
   is blocked and moved to the end of the chain of runnable tasklets.  The
   next tasklet in the chain is executed next.

   If your application employs cooperative scheduling and you do not use
   custom yielding mechanisms built around channels, you will most likely
   call this in your tasklets.

   Example - typical usage of :func:`schedule`::

       stackless.schedule()

   As illustrated in the example, the typical use of this function ignores
   both the optional argument *retval* and the return value.  Note that as
   the variable name *retval* hints, the return value is the value of the
   optional argument.

.. function:: schedule_remove(retval=stackless.current)

   Yield execution of the currently running tasklet.  When called, the
   tasklet is blocked and removed from the chain of runnable tasklets.  The
   tasklet following calling tasklet in the chain is executed next.

   The most likely reason to use this, rather than :func:`schedule`, is to
   build your own yielding primitive without using channels.  This is where
   the otherwise ignored optional argument *retval* and the return value
   are useful.

   :attr:`tasklet.tempval` is used to store the value to be returned, and
   as expected, when this function is called it is set to *retval*.  Custom
   utility functions can take advantage of this and set a new value for
   :attr:`tasklet.tempval` before reinserting the tasklet back into the
   scheduler.

   Example - a utility function::

       def wait_for_result():
           waiting_tasklets.append(stackless.current)
           return stackless.schedule_remove()

       def event_callback(result):
           for tasklet in waiting_tasklets:
               tasklet.tempval = result
               tasklet.insert()

           waiting_tasklets = []

       def tasklet_function():
           result = wait_for_result()
           print("received result", result)

   One drawback of this approach over channels, is that it bypasses the
   useful :attr:`tasklet.block_trap` attribute.  The ability to guard against
   a tasklet being blocked on a channel, is in practice a useful ability to
   have.

Callback related functions:

.. function:: set_channel_callback(callable)

   Install a global channel callback.  Every send or receive action will result
   in *callable* being called.  Setting a value of :data:`None` will result in the
   callback being disabled. The function returns the previous channel callback or
   :data:`None` if none was installed.

   Example - installing a callback::

       def channel_cb(channel, tasklet, sending, willblock):
           pass

       stackless.set_channel_callback(channel_cb)

   The *channel* callback argument is the channel on which the action is
   being performed.

   The *tasklet* callback argument is the tasklet that is performing the
   action on *channel*.

   The *sending* callback argument is an integer, a non-zero value of which
   indicates that the channel action is a send rather than a receive.

   The *willblock* callback argument is an integer, a non-zero value of which
   indicates that the channel action will result in *tasklet* being blocked
   on *channel*.

.. function:: get_channel_callback()

   Get the current global channel callback. The function returns the
   current channel callback or :data:`None` if none was installed.

.. function:: set_schedule_callback(callable)

   Install a callback for scheduling.  Every scheduling event, whether
   explicit or implicit, will result in *callable* being called. The function
   returns the previous channel callback or :data:`None` if none was installed.

   Example - installing a callback::

       def schedule_cb(prev, next):
           pass

       stackless.set_schedule_callback(callable)

   The *prev* callback argument is the tasklet that was just running.

   The *next* callback argument is the tasklet that is going to run now.

   .. note::

      During the execution of the scheduler callback the return value
      of :func:`getcurrent` and the value of :attr:`current` are
      implementation defined. You are not allowed to execute any methods, that
      change the state of stackless for the current thread.

.. function:: get_schedule_callback()

   Get the current global schedule callback. The function returns the
   current schedule callback or :data:`None` if none was installed.

Scheduler state introspection related functions:

.. function:: get_thread_info(thread_id)

   Return a tuple containing the threads main tasklet, current tasklet and
   run-count.

   Example::

       main_tasklet, current_tasklet, runcount = get_thread_info(thread_id)

.. function:: getcurrent()

   Return the currently executing tasklet of this thread.

.. function:: getmain()

   Return the main tasklet of this thread.

.. function:: getruncount()

   Return the number of currently runnable tasklets.

.. function:: switch_trap(change)

   modify the ``switch trap`` level.  Returns its previous value.

   When the ``switch trap`` level is non-zero, any tasklet switching,
   e.g. due channel action or explicit, will result in a ``RuntimeError``
   being raised.  This can be useful to demark code areas that are supposed
   to run without switching, e.g.::

       stackless.switch_trap(1) # increase the trap level
       try:
           my_function_that_shouldnt_switch()
       finally:
           stackless.switch_trap(-1)

Pickling related functions:

.. function:: pickle_flags_default(new_default=-1, mask=-1)

   Get and set the per interpreter default value for pickle-flags.

   A number of option flags control various aspects of |SLP| pickling behaviour.
   See :func:`pickle_flags` for details.

   Whenever |PY| initialises a thread state, it copies the default-value to
   the thread state. Use function :func:`pickle_flags` to get and
   set the flags of the current thread only.

   :param new_default: The new default value for pickle-flags
   :type new_default: int
   :param mask: A bit mask, that indicates the valid bits in argument "new_default"
   :type mask: int
   :return: the previous default value for pickle-flags
   :rtype: int
   :raises ValueError: if you try to set undefined bits

   To inquire the value without changing it, omit the arguments (or set them to -1).

   .. versionadded:: 3.7


.. function:: pickle_flags(new_flags=-1, mask=-1)

   Get and set per thread pickle-flags.

   A number of option flags control various aspects of |SLP| pickling behaviour.
   Symbolic names for the flags are supplied as module constants, which can be
   :ref:`bitwise ORed <bitwise>` together and passed to this function.

   Currently the following pickle option flags are defined:

    - bit 0: :const:`PICKLEFLAGS_PRESERVE_TRACING_STATE`;
    - bit 1: :const:`PICKLEFLAGS_PRESERVE_AG_FINALIZER`;
    - bit 2: :const:`PICKLEFLAGS_RESET_AG_FINALIZER`.

   All other bits must be set to 0.

   :param new_flags: The new value for pickle-flags of the current thread
   :type new_flags: int
   :param mask: A bit mask, that indicates the valid bits in argument "new_flags"
   :type mask: int
   :return: the previous value of pickle-flags of the current thread
   :rtype: int
   :raises ValueError: if you try to set undefined bits

   To inquire the value without changing it, omit the arguments (or set them to -1).

   .. versionadded:: 3.7


Debugging related functions:

.. function:: enable_softswitch(flag)

   Control the switching behaviour.
   Tasklets can be either switched by moving C stack slices around
   or by avoiding stack changes at all. The latter is only possible
   in the top interpreter level. This flag exists once for the whole process.
   For inquiry only, use :data:`None` as the flag.
   By default, soft switching is enabled.

   Example - safely disabling soft switching::

       old_value = stackless.enable_softswitch(False)
       # Logic executed without soft switching.
       enable_softswitch(old_value)

   .. note::

       Disabling soft switching in this manner is exposed for timing and
       debugging purposes.

----------
Attributes
----------

   Rather unusually, the module contains *attributes* for convenient access to some
   methods.  Since this is not general practice and involves some hacks to attain,
   please consider this deprecated.  Use the corresponding module functions instead.

.. attribute:: current

   The currently executing tasklet of this thread.
   Equivalent function: :func:`getcurrent`.

.. attribute:: main

   The main tasklet of this thread.
   Equivalent function: :func:`getmain`.

.. attribute:: runcount

   The number of currently runnable tasklets.

   Example - usage::

       >>> stackless.runcount
       1

   .. note::

       The minimum value of :attr:`runcount` will be ``1``, as the calling
       tasklet will be included.

   Equivalent function: :func:`getruncount`.


.. attribute:: threads

   A list of all thread ids, starting with the id of the main thread.

   Example - usage::

       >>> stackless.threads
       [5148]


.. attribute:: pickle_with_tracing_state

   A boolean value, that indicates if a pickled tasklet contains
   information about the tracing and/or profiling state of the tasklet.

   .. deprecated:: 3.7

      It is now possible to use :func:`pickle_flags_default` and
      :func:`pickle_flags` with :const:`PICKLEFLAGS_PRESERVE_TRACING_STATE`.
      This attribute is now a wrapper around :func:`pickle_flags_default`
      and :func:`pickle_flags`.

.. _slp-exc:

----------
Exceptions
----------

.. py:currentmodule:: exceptions

.. exception:: TaskletExit

   This exception is used to silently kill a tasklet.  It should not be
   caught by your code, and along with other important exceptions like
   :exc:`SystemExit`, be propagated up to the scheduler.

   The following use of the ``except`` clause should be avoided::

       try:
           some_function()
       except:
           pass

   This will catch every exception raised within it, including
   :exc:`TaskletExit`.  Unless you guarantee you actually raise the exceptions
   that should reach the scheduler, you are better to use ``except`` in the
   following manner::

       try:
           some_function()
       except Exception:
           pass

   Here only the more common exceptions are caught, as the ones that should
   not be caught and discarded inherit from :exc:`BaseException`, rather than
   :exc:`Exception`.

   This class is derived from :exc:`SystemExit`. It is defind in the modules
   :mod:`exceptions` and :mod:`__builtin__`.

.. py:currentmodule:: stackless

-------
Classes
-------

.. class:: atomic

   This is a context manager class to help with setting up atomic sections.

   Use it like this::

       with stackless.atomic():
           sensitive_function()
           other_sensitive_function()

   Its definition is equivalent to the following, only faster::

       @contextlib.contextmanager
       def atomic():
           old = stackless.getcurrent().set_atomic(True)
           try:
               yield
           finally:
               stackless.getcurrent().set_atomic(old)
