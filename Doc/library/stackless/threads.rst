.. _slp-threads:

*********************************
Threads --- Threads and Stackless
*********************************

Stackless is a lightweight threading solution.  It works by
scheduling its tasklets within the CPU time allocated to the real thread
that the |PY| interpreter, and therefore the scheduler running within it, is on.

Stackless does not:

 * Magically move tasklets between threads to do some wonderful
   load balancing.
 * Magically remove the global interpreter lock.
 * Solve all your scalability needs out of the box.

But it does allow its functionality to be used flexibly, when you
want to make use of more than one thread.

--------------------
Tasklets and Threads
--------------------

A tasklet usually has an associated thread. This thread is identified by the
attribute :attr:`tasklet.thread_id`. A newly created tasklet is always
associated with its creator thread.

The associated thread of a tasklet changes only as the result of
a :meth:`tasklet.bind_thread` call or if the associated thread
terminates. In the later case the thread id becomes ``-1``. Application
code can bind the tasklet to another thread using :meth:`~tasklet.bind_thread`.

When a thread dies, only tasklets with a C-state are actively killed.
Soft-switched tasklets simply stop. All tasklets bound to the thread will
lose their thread-state, which means that their :attr:`~tasklet.thread_id`
will report as ``-1``. This also includes soft-switched tasklets,
which share a C-state.

The reason Stackless kills tasklets with C-state is that not doing so
can cause serious leaks when a C-state is not unwound. If Stackless runs
in verbose mode (command line option :option:`-v` or :envvar:`PYTHONVERBOSE`),
Stackless prints a warning message, if it deallocates a tasklet
with a C-state. Stackless cannot
kill soft-switched tasklets, because there is no central list of them.
Stackless only knows about the hard-switched ones.

Threads that end really should make sure that they finish whatever worker
tasklets they have going by manually killing (``tasklet.kill()``) or
unbinding (``tasklet.bind(None)``) them, but that is up to application code.

During interpreter shutdown Stackless kills other daemon threads (non-daemon
are already dead at this point), if they execute Python code or switch
tasklets. This way Stackless tries to avoid access violations, that might
happen later after clearing the thread state structures.

----------------------
A scheduler per thread
----------------------

The operating system thread that the |PY| runtime is started in and runs on,
is called the main thread.  The typical use of Stackless, is to run the
scheduler in this thread.  But there is nothing that prevents a different
scheduler, and therefore a different set of tasklets, from running in every
|PY| thread you care to start.


Example - scheduler per thread::

    import threading
    import stackless

    def secondary_thread_func():
        print("THREAD(2): Has", stackless.runcount, "tasklets in its scheduler")

    def main_thread_func():
        print("THREAD(1): Waiting for death of THREAD(2)")
        while thread.is_alive():
            stackless.schedule()
        print("THREAD(1): Death of THREAD(2) detected")

    mainThreadTasklet = stackless.tasklet(main_thread_func)()

    thread = threading.Thread(target=secondary_thread_func)
    thread.start()

    stackless.run()

Output::
    THREAD(2): Has 1 tasklets in its scheduler
    THREAD(1): Waiting for death of THREAD(2)
    THREAD(1): Death of THREAD(2) detected

This example demonstrates that there actually are two independent schedulers
present, one in each participating |PY| thread.  We know that the main
thread has one manually created tasklet running, in addition to its main
tasklet which is running the scheduler.  If the secondary thread is truly
independent, then when it runs it should have a tasklet count of ``1``
representing its own main tasklet.  And this is indeed what we see.

See also:

  * The attribute :attr:`stackless.threads`.
  * The attribute :attr:`tasklet.thread_id`.
  * The method :meth:`tasklet.bind_thread`.

.. _slp-threads-channel:

------------------------
Channels are thread-safe
------------------------

Whether or not you are running a scheduler on multiple threads, you can still
communicate with a thread that is running a scheduler using a
:class:`channel` object.

Example - interthread channel usage::

    import threading
    import stackless

    commandChannel = stackless.channel()

    def master_func():
        commandChannel.send("ECHO 1")
        commandChannel.send("ECHO 2")
        commandChannel.send("ECHO 3")
        commandChannel.send("QUIT")

    def slave_func():
        print("SLAVE STARTING")
        while 1:
            command = commandChannel.receive()
            print("SLAVE:", command)
            if command == "QUIT":
                break
        print("SLAVE ENDING")

    def scheduler_run(tasklet_func):
        t = stackless.tasklet(tasklet_func)()
        while t.alive:
            stackless.run()

    thread = threading.Thread(target=scheduler_run, args=(master_func,))
    thread.start()

    scheduler_run(slave_func)

Output::

    SLAVE STARTING
    SLAVE: ECHO 1
    SLAVE: ECHO 2
    SLAVE: ECHO 3
    SLAVE: QUIT
    SLAVE ENDING

This example runs *slave_func* as a tasklet on the main thread, and
*master_func* as a tasklet on a secondary thread that is manually created.
The idea is that the master thread tells the slave thread what to do, with
a ``QUIT`` message meaning that it should exit.

.. note::

    The reason the scheduler is repeatedly run in a loop, is because when a
    scheduler has no remaining tasklets scheduled within it, it will exit.
    As there is only one tasklet in each thread, as each channel operation in
    the thread blocks the calling tasklet, the scheduler will exit.  Linking
    how long the scheduler is driven to the lifetime of all tasklets that it
    handles, ensures correct behaviour.

