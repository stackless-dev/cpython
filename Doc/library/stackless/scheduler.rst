.. _stackless-scheduler:

**************************************
The scheduler --- How tasklets are run
**************************************

The two main ways in which Stackless schedules its tasklets are
pre-emptive scheduling and cooperative scheduling.  However, there
are many ways these two approaches can be used to suit the needs
of a given application.

======================
Cooperative scheduling
======================

The simplest way to run the scheduler is cooperatively.  Programmers still
need to know when blocking will occur in their code and deal with it
appropriately.  But unlike pre-emptive scheduling, they can know exactly
where blocking will occur and this in turn allows them a good idea
about how their code will behave in cooperation with whatever else may
be running in tasklets.

Example - running a simple function within a tasklet::

    >>> def function(n):
    ...     for i in range(n):
    ...         print(i+1)
    ...         stackless.schedule()
    ...
    >>> stackless.tasklet(function)(3)
    >>> stackless.run()
    1
    2
    3

In the example above, a function *func* is defined, then a tasklet created for it
to be called within.  The scheduler is then run, which schedules the tasklet
four times.  The fourth time the tasklet is scheduled, the function exits and
because of this the tasklet also.

The code to run in a tasklet::

    def function(n):
        for i in range(n):
            print(i+1)
            stackless.schedule()

The first step is having code to run in a tasklet.  Here
*func* simply loops *n* times, in each iteration printing which
iteration it is, then giving the other scheduled tasklets a chance to run
by calling :func:`stackless.schedule`.

Creating the tasklet::

    stackless.tasklet(function)(3)

With *func* to run, a tasklet is bound to it and arguments
provided which in this case are a value of ``3`` for *n*.  When the tasklet is
first scheduled and *func* first called within it, the arguments are
passed into the call.  The act of creating a tasklet in this way automatically
inserts it into the scheduler, so there is no need to hold a reference to it.

Running the scheduler::

    stackless.run()

Next the scheduler is run.  It will exit when there are no tasklets remaining
within it.  Note that this tasklet will be scheduled four times.  The first
will be the initial run of the tasklet, which will start a call into *func*
within it providing whatever arguments were given (in this case for *n*).
The second and third are also the second and third prints, and finally a fourth
time when :func:`stackless.schedule` returns and the function exits and
therefore the tasklet also.

If the developer knows that as long as the application is to run, there will
be tasklets in the scheduler to be run as shown here, the application
can be driven by a call to :func:`stackless.run`.  However, it is not always
that simple and there are many reasons why the scheduler may be empty, and
repeated calls to :func:`stackless.run` need to be made after creation of
new tasklets or reinsertion of old ones which were blocked outside of it.

.. _uncooperative-tasklets:

--------------------------------
Detecting uncooperative tasklets
--------------------------------

In practice, it is very rare that a tasklet will run without yielding to allow
others to run.  More often than not, tasklets are blocked waiting for events to
occur often enough that they do not need to explicitly yield.  But occasionally
unforeseen situations can occur where code paths lead to yields not being hit,
or perhaps bad code enters an infinite loop.

With this in mind, it is often more useful to take advantage of the pre-emptive
scheduling functionality, to detect long running tasklets.  The way this works
is to pass in a sufficiently high timeout that the only tasklets which hit it
are those which are not yielding.

Idiom - detecting uncooperative tasklets::

    while 1:
        t = stackless.run(1000000)
        if t is not None:
            t.insert()

            print("*** Uncooperative tasklet", t, "detected ***")
            traceback.print_stack(t.frame)

Scheduling cooperatively, but pre-empting uncooperative tasklets::

    t = stackless.run(1000000)

As most tasklets will yield before having executed ``1000000`` instructions,
the only tasklets which will be interrupted and returned will be those that
are not yielding and therefore being uncooperative.

Ensuring the interrupted tasklet resumes::

    if t is not None:
        t.insert()

Interrupted tasklets are no longer in the scheduler.  We do not know what this
tasklet was doing, and to leave it uncompleted may depending on our application
be unacceptable.  The call to :meth:`tasklet.insert` puts the it at the
end of the list of runnable tasklets in the scheduler, forcibly ensuring the
others have a chance to run before it gets another.

It might also be reasonable to assume that any tasklet that gets interrupted
in this manner is behaving wrong, and that to kill it having recorded as
much information about it as possible (like its call stack) before doing so
is better.

Killing the interrupted tasklet::

    if t is not None:
        print("*** Uncooperative tasklet", t, "detected ***")
        traceback.print_stack(t.frame)

        t.kill()

.. note::

   Tasklets that do long running calls outside of |PY| code are not something
   this mechanism has any insight into.  These calls might be doing
   synchronous IO, complex :mod:`math` module operations that execute in the
   underlying C library or a range of other things.

---------------------
Pumping the scheduler
---------------------

The most obvious way to use Stackless is to put all your logic into tasklets
and to run the scheduler, where there is an expectation that when the
scheduler exits so does your application.  However, by taking this approach
your application is built within the Stackless framework and has to be
structured around being run within the scheduler.

This may be unacceptable if you want more control over how your application
runs or is structured.  It may seem to rule out the use of cooperative
scheduling and push you towards pre-emptive scheduling, so that your
application or framework can drive Stackless instead.

However, there is a way to retain the benefits of cooperative scheduling
and still have your application or framework in control.  This is called
pumping the scheduler.

Idiom - pumping the scheduler::

    def ApplicationMainLoop():
        while 1:
            ProcessMessages()
            ApplicationLoopStuff()
            Etc()

            stackless.run()

            RescheduleBlockedTasklets()

Pumping the scheduler works by having code that explicitly yields,
yield onto a channel, instead of calling :func:`stackless.schedule`
and instead yielding back into the scheduler.  By doing so, the
scheduler is empty after each tasklet that was scheduled has run.
This means that a scheduler run is effectively an act of running
each scheduled tasklet once, and it can be pumped within an
application or frameworks main loop.

Yielding outside of the scheduler::

    def CustomYield():
        customYieldChannel.receive()

Defining a custom function that code can call to yield their tasklets
outside of the scheduler, is as simple as having a channel for them
to wait on.  There will never be any tasklets sending on the channel,
so those that call will always block onto it.

Rescheduling blocked tasklets::

    def RescheduleBlockedTasklets():
        while customYieldChannel.balance < 0:
            customYieldChannel.send(None)

.. _slp-chan-pref-ex1:

When we want to reinsert all the blocked tasklets back into the
scheduler, we simply do sends on the channel as long as there are
receivers.  However, there is one situation we want to avoid.  We do
not want to run each receiving tasklet as we do a send to it.  In
order to avoid this, we need to make sure that our channel simply
inserts the receiving tasklet into the scheduler to be run in due
course instead.

Configuring the channel to be yielded onto::

    customYieldChannel = stackless.channel()
    customYieldChannel.preference = 1

This is a simple change to the :attr:`channel.preference` attribute
of the channel when it is created.

.. note::

    If you pump the scheduler, your tasklets cannot call
    :func:`stackless.schedule`.  To do so, without knowledge of what
    you are doing, will result in a tasklet that continuously
    gets scheduled.  And the call to :func:`stackless.run` will not
    exit until the tasklet yields in another manner out of the
    scheduler, errors or exits.

======================
Pre-emptive scheduling
======================

If you want a lot of the work of using operating system threads without a lot
of the benefits, then pre-emptive scheduling is a good choice.  Making the
scheduler work in a pre-emptive manner, is a simple matter of giving it a
timeout value.

Example - running a simple tasklet within the scheduler::

    >>> def function():
    ...     i = 0
    ...     while True:
    ...         print(i+1)
    ...         i += 1
    ...
    >>> stackless.tasklet(function)()
    >>> stackless.run(100)
    1
    2
    3
    4
    5
    6
    7
    8
    9
    10
    11
    <stackless.tasklet object at 0x01BCD0B0>

In this case, the scheduler runs until a maximum of ``100`` instructions have
been executed in the |PY| virtual machine.  At which point, whatever tasklet
is currently running is returned when :func:`stackless.run` exits.  The
standard way to employ this is to pump the scheduler, reinserting the
interrupted tasklet.

Idiom - pre-emptive scheduling::

    while True:
        ProcessMessages()
        ApplicationLoopStuff()
        Etc()

        t = stackless.run(100)
        if t is None:
            break

        t.insert()

Run the scheduler for ``100`` instructions::

    t = stackless.run(100)

There are two things to note here, if *t* is :data:`None` then there are no
tasklets in the scheduler to run.  If *t* is not :data:`None`, then it is an
interrupted tasklet that needs to be reinserted into the scheduler.

Detect an empty scheduler::

    if t is None:
        break

It may be that an empty scheduler indicates that all the work is done, or it
may not.  How this work is actually handled depends on the implementation
details of your solution.

Reinsert the interrupted tasklet::

    t.insert()

.. note::

    You are not running the scheduler for ``100`` instructions, you are
    running it until any subsequently scheduled tasklet runs for at least that
    many instructions.  If all your tasklets always explicitly yield before
    this many instructions have been executed, then the :func:`stackless.run`
    call will not exit until for some reason one does not.

--------------------------------------------
Running the scheduler for ``n`` instructions
--------------------------------------------

Running the scheduler until a scheduled tasklet runs for *n* consecutive
instructions is one way pre-emptive scheduling might work. However, if you
want to structure your application or framework in such a way that it drives
Stackless rather than the other way round, then you need the scheduler to
exit instead. The scheduler can be directed to work in this way, by
giving it a :func:`totaltimeout <stackless.run>` flag
value.

Idiom - pre-emptive scheduler pumping::

    while True:
        ProcessMessages()
        ApplicationLoopStuff()
        Etc()

        t = stackless.run(100, totaltimeout=True)
        if t is None:
            break

        t.insert()

.. _slp-exc-section:

==========
Exceptions
==========

Exceptions that occur within tasklets and are uncaught are raised out of the
:func:`stackless.run` call, to be handled by its caller.

Example - an exception raised out of the scheduler::

    >>> def func_loop():
    ...     while 1:
    ...         stackless.schedule()
    ...
    >>> def func_exception():
    ...     raise Exception("catch this")
    ...
    >>> stackless.tasklet(func_loop)()
    <stackless.tasklet object at 0x01C58EB0>
    >>> stackless.tasklet(func_exception)()
    <stackless.tasklet object at 0x01C58F70>
    >>> stackless.run()
    Traceback (most recent call last):
      File "<stdin>", line 1, in <module>
      File "<stdin>", line 2, in func_exception
    Exception: catch this

This may not be the desired behaviour, and a more acceptable one might be that
the exception is caught and dealt with in the tasklet it occurred in before
that tasklet exits.

---------------------------
Catching tasklet exceptions
---------------------------

We want to change the new behaviour to be:

#. The tasklet with the uncaught exception exits normally.
#. The uncaught exception is examined and handled before the tasklet exits.
#. The scheduler continues running.

There are two ways to accomplish these things.  You can either monkey-patch
the tasklet creation process, or you can use a custom function for all your
tasklet creation.

Example - a custom tasklet creation function::

    def new_tasklet(f, *args, **kwargs):
        def safe_tasklet():
            try:
                f(*args, **kwargs)
            except Exception:
                traceback.print_exc()

        return stackless.tasklet(safe_tasklet)()

    new_tasklet(some_function, 1, 2, 3, key="value")

Example - monkey-patching the tasklet creation process::

    def __call__(self, *args, **kwargs):
         f = self.tempval

         def new_f(old_f, args, kwargs):
             try:
                 old_f(*args, **kwargs)
             except Exception:
                 traceback.print_exc()

         self.tempval = new_f
         stackless.tasklet.setup(self, f, args, kwargs)

    stackless.tasklet.__call__ = __call__

    stackless.tasklet(some_function)(1, 2, 3, key=value)

Printing the call stack in the case of an exception is good enough for these
examples, but in practice the call stack might instead be recorded in a
database.

.. note::

  We catch :exc:`Exception` explicitly, rather than catching any exception
  which might occur.  The reason for this is to avoid catching exceptions we
  should not be catching like :exc:`SystemExit` or :ref:`TaskletExit
  <slp-exc>`, which derive from the lower level :exc:`BaseException`.
