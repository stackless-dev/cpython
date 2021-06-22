.. _stackless-debugging:

***********************************************
Debugging and Tracing --- How Stackless differs
***********************************************

Debugging tools, like those used for tracing, are implemented through
calls to the :func:`sys.settrace` function.  Now, in |CPY|, when
this has been called any code that runs within the operating system thread
is covered by it.  In Stackless however, this function only covers the
current tasklet. Stackless provides the tasklet attributes
:attr:`~tasklet.trace_function` and :attr:`~tasklet.profile_function` to
get and set the trace/profile function of a particular tasklet.

The debugging related modules, whether :doc:`in the standard library
<../debug>` or not, do not take this difference into account.  They are not
likely to work, and if they do, are not likely to work in the way you expect.
In an ideal world, |SLP| might include modified versions of these
modules, and patches adding them would be most welcome.

If you want working debugging for |SLP|, at this time your best
option is to use the `WingWare Python IDE <http://wingware.com>`_
or the `Eclipse IDE with the PyDev-Plugin <http://pydev.org>`_.
Both have gone out of their way to add and support |SLP| development.

.. note::

    In the past, the possibility of ditching the per-tasklet behaviour for
    the standard per-thread behaviour has been broached on the mailing list.
    Given the lack of movement on usability for this part of Stackless, it is
    not unlikely that this suggested change will be revisited.

----------------
Tracing tasklets
----------------

In order to get debugging support working on a per-tasklet basis, you need to
ensure you enable tracing for all tasklets. This can be archived by the
schedule callback. This callback sees every task switch. Here is
a complete example::

    from __future__ import absolute_import, print_function

    import sys
    import stackless
    import traceback


    class NamedTasklet(stackless.tasklet):
        __slots__ = ("name",)

        def __init__(self, func, name=None):
            stackless.tasklet.__init__(self, func)
            if name is None:
                name = "at %08x" % (id(self))
            self.name = name

        def __repr__(self):
            return "<tasklet %s>" % (self.name)


    class Mutex(object):

        def __init__(self, capacity=1):
            self.queue = stackless.channel()
            self.capacity = capacity

        def isLocked(self):
            '''return non-zero if locked'''
            return self.capacity == 0

        def lock(self):
            '''acquire the lock'''
            currentTasklet = stackless.getcurrent()
            atomic = currentTasklet.set_atomic(True)
            try:
                if self.capacity:
                    self.capacity -= 1
                else:
                    self.queue.receive()
            finally:
                currentTasklet.set_atomic(atomic)

        def unlock(self):
            '''release the lock'''
            currentTasklet = stackless.getcurrent()
            atomic = currentTasklet.set_atomic(True)
            try:
                if self.queue.balance < 0:
                    self.queue.send(None)
                else:
                    self.capacity += 1
            finally:
                currentTasklet.set_atomic(atomic)

    m = Mutex()


    def task():
        name = stackless.getcurrent().name
        print(name, "acquiring")
        m.lock()
        print(name, "switching")
        stackless.schedule()
        print(name, "releasing")
        m.unlock()


    def trace_function(frame, event, arg):
        if frame.f_code.co_name in ('schedule_cb', 'channel_cb'):
            return None
        print("         trace_function: %s %s in %s, line %s" %
              (stackless.current, event, frame.f_code.co_name, frame.f_lineno))
        if event in ('call', 'line', 'exception'):
            return trace_function
        return None


    def channel_cb(channel, tasklet, sending, willblock):
        tf = tasklet.trace_function
        try:
            tasklet.trace_function = None
            print("Channel CB, tasklet %r, %s%s" %
                  (tasklet, ("recv", "send")[sending], ("", " will block")[willblock]))
        finally:
            tasklet.trace_function = tf


    def schedule_cb(prev, next):
        # During a tasklet switch (during the execution of this function) the
        # the result of stackless.getcurrent() is implementation defined.
        # Therefore this function avoids any assumptions about the current tasklet.
        current_tf = sys.gettrace()
        try:
            sys.settrace(None)  # don't trace this callback
            current_frame = sys._getframe()
            if current_tf is None:
                # also look at the previous frame, in case this callback is exempt
                # from tracing
                f_back = current_frame.f_back
                if f_back is not None:
                    current_tf = f_back.f_trace

            current_info = "Schedule CB "
            if not prev:
                print("%sstarting %r" % (current_info, next))
            elif not next:
                print("%sending %r" % (current_info, prev))
            else:
                print("%sjumping from %s to %s" % (current_info, prev, next))

            # Inform about the installed trace functions
            prev_tf = current_tf if prev.frame is current_frame else prev.trace_function
            next_tf = current_tf if next.frame is current_frame else next.trace_function
            print("    Current trace functions: prev: %r, next: %r" % (prev_tf, next_tf))

            # Eventually set a trace function
            if next is not None:
                if not next.is_main:
                    tf = trace_function
                else:
                    tf = None
                print("    Setting trace function for next: %r" % (tf,))
                # Set the "global" trace function for the tasklet
                next.trace_function = tf
                # Set the "local" trace function for each frame
                # This is required, if the tasklet is already running
                frame = next.frame
                if frame is current_frame:
                    frame = frame.f_back
                while frame is not None:
                    frame.f_trace = tf
                    frame = frame.f_back
        except:
            traceback.print_exc()
        finally:
            sys.settrace(current_tf)

    if __name__ == "__main__":
        if len(sys.argv) > 1 and sys.argv[1] == 'hard':
            stackless.enable_softswitch(False)

        stackless.set_channel_callback(channel_cb)
        stackless.set_schedule_callback(schedule_cb)

        NamedTasklet(task, "tick")()
        NamedTasklet(task, "trick")()
        NamedTasklet(task, "track")()

        stackless.run()

        stackless.set_channel_callback(None)
        stackless.set_schedule_callback(None)


-------------------------
``settrace`` and tasklets
-------------------------

.. note::

   This section is out dated and only of historical interest. Since the
   implementation of :attr:`~tasklet.trace_function` and
   :attr:`~tasklet.profile_function` a debugger can enable tracing or profiling
   within the schedule callback without monkey patching.

In order to get debugging support working on a per-tasklet basis, you need to
ensure you call :func:`sys.settrace` for all tasklets.  Vilhelm Saevarsson
`has an email
<http://www.stackless.com/pipermail/stackless/2007-October/003074.html>`_
giving code and a description of the steps required including potentially
unforeseen circumstances, in the Stackless mailing list archives.

Vilhelm's code::

    import sys
    import stackless

    def contextDispatch( prev, next ):
        if not prev: #Creating next
            # I never see this print out
            print("Creating ", next)
        elif not next: #Destroying prev
            # I never see this print out either
            print("Destroying ", prev)
        else:
            # Prev is being suspended
            # Next is resuming
            # When worker tasklets are resuming and have
            # not been set to trace, we make sure that
            # they are tracing before they run again
            if not next.frame.f_trace:
                # We might already be tracing so ...
                sys.call_tracing(next.settrace, (traceDispatch, ))

    stackless.set_schedule_callback(contextDispatch)

    def __call__(self, *args, **kwargs):
         f = self.tempval
         def new_f(old_f, args, kwargs):
             sys.settrace(traceDispatch)
             old_f(*args, **kwargs)
             sys.settrace(None)
         self.tempval = new_f
         stackless.tasklet.setup(self, f, args, kwargs)

    def settrace( self, tb ):
        self.frame.f_trace = tb
        sys.settrace(tb)

    stackless.tasklet.__call__ = __call__
    stackless.tasklet.settrace = settrace

The key actions taken by this code:

 * Wrap the creation of tasklets, so that the debugging hook is installed
   when the tasklet is first run.
 * Intercept scheduling events, so that tasklets that were created before
   debugging was engaged, have the debugging hook installed before they are
   run again.
