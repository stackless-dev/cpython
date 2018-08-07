from __future__ import absolute_import
# import common

import unittest
import stackless
import sys
import traceback
import weakref
import types
import contextlib
import time
import os
import struct
from stackless import _test_nostacklesscall as apply_not_stackless
try:
    import _thread as thread
    import threading
    withThreads = True
except:
    withThreads = False

from support import test_main  # @UnusedImport
from support import StacklessTestCase, AsTaskletTestCase, require_one_thread, testcase_leaks_references


def is_soft():
    softswitch = stackless.enable_softswitch(0)
    stackless.enable_softswitch(softswitch)
    return softswitch


def runtask():
    x = 0
    # evoke pickling of an range object
    dummy = range(10)
    for ii in range(1000):
        x += 1


@contextlib.contextmanager
def switch_trapped():
    stackless.switch_trap(1)
    try:
        yield
    finally:
        stackless.switch_trap(-1)


class TestWatchdog(StacklessTestCase):

    def lifecycle(self, t):
        # Initial state - unrun
        self.assertTrue(t.alive)
        self.assertTrue(t.scheduled)
        self.assertEqual(t.recursion_depth, 0)
        # allow hard switching
        t.set_ignore_nesting(1)

        softSwitching = stackless.enable_softswitch(0)
        stackless.enable_softswitch(softSwitching)

        # Run a little
        res = stackless.run(10)
        self.assertEqual(t, res)
        self.assertTrue(t.alive)
        self.assertTrue(t.paused)
        self.assertFalse(t.scheduled)
        self.assertEqual(t.recursion_depth, softSwitching and 1 or 2)

        # Push back onto queue
        t.insert()
        self.assertFalse(t.paused)
        self.assertTrue(t.scheduled)

        # Run to completion
        stackless.run()
        self.assertFalse(t.alive)
        self.assertFalse(t.scheduled)
        self.assertEqual(t.recursion_depth, 0)

    def test_aliveness1(self):
        """ Test flags after being run. """
        t = stackless.tasklet(runtask)()
        self.lifecycle(t)

    @StacklessTestCase.prepare_pickle_test_method
    def test_aliveness2(self):
        """ Same as 1, but with a pickled unrun tasklet. """
        t = stackless.tasklet(runtask)()
        t_new = self.loads(self.dumps((t)))
        t.remove()
        t_new.insert()
        self.lifecycle(t_new)

    @StacklessTestCase.prepare_pickle_test_method
    def test_aliveness3(self):
        """ Same as 1, but with a pickled run(slightly) tasklet. """

        t = stackless.tasklet(runtask)()
        t.set_ignore_nesting(1)

        # Initial state - unrun
        self.assertTrue(t.alive)
        self.assertTrue(t.scheduled)
        self.assertEqual(t.recursion_depth, 0)

        softSwitching = stackless.enable_softswitch(0)
        stackless.enable_softswitch(softSwitching)

        # Run a little
        res = stackless.run(100)

        self.assertEqual(t, res)
        self.assertTrue(t.alive)
        self.assertTrue(t.paused)
        self.assertFalse(t.scheduled)
        self.assertEqual(t.recursion_depth, softSwitching and 1 or 2)

        # Now save & load
        dumped = self.dumps(t)
        t_new = self.loads(dumped)

        # Remove and insert & swap names around a bit
        t.remove()
        t = t_new
        del t_new
        t.insert()

        self.assertTrue(t.alive)
        self.assertFalse(t.paused)
        self.assertTrue(t.scheduled)
        self.assertEqual(t.recursion_depth, 1)

        # Run to completion
        if is_soft():
            stackless.run()
        else:
            t.kill()
        self.assertFalse(t.alive)
        self.assertFalse(t.scheduled)
        self.assertEqual(t.recursion_depth, 0)


class TestTaskletSwitching(StacklessTestCase):
    """Test the tasklet's own scheduling methods"""

    def test_raise_exception(self):
        c = stackless.channel()

        def foo():
            self.assertRaises(IndexError, c.receive)
        s = stackless.tasklet(foo)()
        s.run()  # necessary, since raise_exception won't automatically run it
        s.raise_exception(IndexError)

    def test_run(self):
        c = stackless.channel()
        flag = [False]

        def foo():
            flag[0] = True
        s = stackless.tasklet(foo)()
        s.run()
        self.assertEqual(flag[0], True)

    def test_switch_to_current(self):
        # See https://bitbucket.org/stackless-dev/stackless/issues/88
        current = stackless.current
        current.switch()
        current.switch()  # this second switch used to trigger an assertion violation


class TestTaskletThrowBase(object):

    def test_throw_noargs(self):
        c = stackless.channel()

        def foo():
            self.assertRaises(IndexError, c.receive)
        s = stackless.tasklet(foo)()
        s.run()  # It needs to have started to run
        self.throw(s, IndexError)
        self.aftercheck(s)

    def test_throw_args(self):
        c = stackless.channel()

        def foo():
            try:
                c.receive()
            except Exception as e:
                self.assertTrue(isinstance(e, IndexError))
                self.assertEqual(e.args, (1, 2, 3))
        s = stackless.tasklet(foo)()
        s.run()  # It needs to have started to run
        self.throw(s, IndexError, (1, 2, 3))
        self.aftercheck(s)

    def test_throw_inst(self):
        c = stackless.channel()

        def foo():
            try:
                c.receive()
            except Exception as e:
                self.assertTrue(isinstance(e, IndexError))
                self.assertEqual(e.args, (1, 2, 3))
        s = stackless.tasklet(foo)()
        s.run()  # It needs to have started to run
        self.throw(s, IndexError(1, 2, 3))
        self.aftercheck(s)

    def test_throw_exc_info(self):
        c = stackless.channel()

        def foo():
            try:
                c.receive()
            except Exception as e:
                self.assertTrue(isinstance(e, ZeroDivisionError))
        s = stackless.tasklet(foo)()
        s.run()  # It needs to have started to run

        def errfunc():
            1 / 0
        try:
            errfunc()
        except Exception:
            self.throw(s, *sys.exc_info())
        self.aftercheck(s)

    def test_throw_traceback(self):
        c = stackless.channel()

        def foo():
            try:
                c.receive()
            except Exception:
                s = "".join(traceback.format_tb(sys.exc_info()[2]))
                self.assertTrue("errfunc" in s)
        s = stackless.tasklet(foo)()
        s.run()  # It needs to have started to run

        def errfunc():
            1 / 0
        try:
            errfunc()
        except Exception:
            self.throw(s, *sys.exc_info())
        self.aftercheck(s)

    def test_new(self):
        c = stackless.channel()

        def foo():
            try:
                c.receive()
            except Exception as e:
                self.assertTrue(isinstance(e, IndexError))
                raise
        s = stackless.tasklet(foo)()
        self.assertEqual(s.frame, None)
        self.assertTrue(s.alive)
        # Test that the current "unhandled exception behaviour"
        # is invoked for the not-yet-running tasklet.

        def doit():
            self.throw(s, IndexError)
        if not self.pending:
            self.assertRaises(IndexError, doit)
        else:
            doit()
            self.assertRaises(IndexError, stackless.run)

    def test_kill_new(self):
        def t():
            self.assertFalse("should not run this")
        s = stackless.tasklet(t)()

        # Should not do anything
        s.throw(TaskletExit)
        # the tasklet should be dead
        stackless.run()
        self.assertRaisesRegex(RuntimeError, "dead", s.run)

    def test_dead(self):
        c = stackless.channel()

        def foo():
            c.receive()
        s = stackless.tasklet(foo)()
        s.run()
        c.send(None)
        stackless.run()
        self.assertFalse(s.alive)

        def doit():
            self.throw(s, IndexError)
        self.assertRaises(RuntimeError, doit)

    def test_kill_dead(self):
        c = stackless.channel()

        def foo():
            c.receive()
        s = stackless.tasklet(foo)()
        s.run()
        c.send(None)
        stackless.run()
        self.assertFalse(s.alive)

        def doit():
            self.throw(s, TaskletExit)
        # nothing should happen here.
        doit()

    def test_throw_invalid(self):
        s = stackless.getcurrent()

        def t():
            self.throw(s)
        self.assertRaises(TypeError, t)

        def t():  # @DuplicatedSignature
            self.throw(s, IndexError(1), (1, 2, 3))
        self.assertRaises(TypeError, t)


class TestTaskletThrowImmediate(StacklessTestCase, TestTaskletThrowBase):
    pending = False

    @classmethod
    def throw(cls, s, *args):
        s.throw(*args, pending=cls.pending)

    def aftercheck(self, s):
        # the tasklet ran immediately
        self.assertFalse(s.alive)


class TestTaskletThrowNonImmediate(TestTaskletThrowImmediate):
    pending = True

    def aftercheck(self, s):
        # After the throw, the tasklet still hasn't run
        self.assertTrue(s.alive)
        s.run()
        self.assertFalse(s.alive)


class TestSwitchTrap(StacklessTestCase):

    class SwitchTrap(object):

        def __enter__(self):
            stackless.switch_trap(1)

        def __exit__(self, exc, val, tb):
            stackless.switch_trap(-1)
    switch_trap = SwitchTrap()

    def test_schedule(self):
        s = stackless.tasklet(lambda: None)()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", stackless.schedule)
        stackless.run()

    def test_schedule_remove(self):
        main = []
        s = stackless.tasklet(lambda: main[0].insert())()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", stackless.schedule_remove)
        main.append(stackless.getcurrent())
        stackless.schedule_remove()

    def test_run(self):
        s = stackless.tasklet(lambda: None)()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", stackless.run)
        stackless.run()

    def test_run_specific(self):
        s = stackless.tasklet(lambda: None)()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", s.run)
        s.run()

    def test_run_paused(self):
        s = stackless.tasklet(lambda: None)
        s.bind(args=())
        self.assertTrue(s.paused)
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", s.run)
        self.assertTrue(s.paused)
        stackless.run()

    def test_send(self):
        c = stackless.channel()
        s = stackless.tasklet(lambda: c.receive())()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", c.send, None)
        c.send(None)

    def test_send_throw(self):
        c = stackless.channel()

        def f():
            self.assertRaises(NotImplementedError, c.receive)
        s = stackless.tasklet(f)()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", c.send_throw, NotImplementedError)
        c.send_throw(NotImplementedError)

    def test_receive(self):
        c = stackless.channel()
        s = stackless.tasklet(lambda: c.send(1))()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", c.receive)
        self.assertEqual(c.receive(), 1)

    def test_receive_throw(self):
        c = stackless.channel()
        s = stackless.tasklet(lambda: c.send_throw(NotImplementedError))()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", c.receive)
        self.assertRaises(NotImplementedError, c.receive)

    def test_raise_exception(self):
        c = stackless.channel()

        def foo():
            self.assertRaises(IndexError, c.receive)
        s = stackless.tasklet(foo)()
        s.run()  # necessary, since raise_exception won't automatically run it
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", s.raise_exception, RuntimeError)
        s.raise_exception(IndexError)

    def test_kill(self):
        c = stackless.channel()

        def foo():
            self.assertRaises(TaskletExit, c.receive)
        s = stackless.tasklet(foo)()
        s.run()  # necessary, since raise_exception won't automatically run it
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", s.kill)
        s.kill()

    def test_run2(self):
        c = stackless.channel()

        def foo():
            pass
        s = stackless.tasklet(foo)()
        with self.switch_trap:
            self.assertRaisesRegex(RuntimeError, "switch_trap", s.run)
        s.run()


class TestKill(StacklessTestCase):
    SLP_TASKLET_KILL_REBINDS_THREAD = False  # see tasklet.c function impl_tasklet_kill()

    def test_kill_pending_true(self):
        killed = [False]

        def foo():
            try:
                stackless.schedule()
            except TaskletExit:
                killed[0] = True
                raise
        t = stackless.tasklet(foo)()
        t.run()
        self.assertFalse(killed[0])
        t.kill(pending=True)
        self.assertFalse(killed[0])
        t.run()
        self.assertTrue(killed[0])

    def test_kill_pending_False(self):
        killed = [False]

        def foo():
            try:
                stackless.schedule()
            except TaskletExit:
                killed[0] = True
                raise
        t = stackless.tasklet(foo)()
        t.run()
        self.assertFalse(killed[0])
        t.kill(pending=False)
        self.assertTrue(killed[0])

    def test_kill_current(self):
        killed = [False]

        def task():
            try:
                stackless.current.kill()
            except TaskletExit:
                killed[0] = True
                raise
        t = stackless.tasklet(task)()
        t.run()
        self.assertTrue(killed[0])
        self.assertFalse(t.alive)
        self.assertEqual(t.thread_id, stackless.current.thread_id)

    @unittest.skipUnless(withThreads, "requires thread support")
    @require_one_thread
    def test_kill_thread_without_main_tasklet(self):
        # this test depends on a race condition.
        # unfortunately I do not have any better test case

        # This lock is used as a simple event variable.
        ready = thread.allocate_lock()
        ready.acquire()

        channel = stackless.channel()
        tlet = stackless.tasklet()
        self.tlet = tlet

        class DelayError(Exception):
            def __str__(self):
                time.sleep(0.05)
                return super(DelayError, self).__str__()

        # catch stderr
        self.addCleanup(setattr, sys, "stderr", sys.stderr)
        sys.stderr = open(os.devnull, "w")
        self.addCleanup(sys.stderr.close)

        def other_thread_main():
            tlet.bind_thread()
            tlet.bind(channel.receive, ())
            tlet.run()
            ready.release()
            raise DelayError("a slow exception")
            # during the processing of this exception the
            # thread has no main tasklet. Exception processing
            # takes some time. During this time the main thread
            # kills the tasklet

        thread.start_new_thread(other_thread_main, ())
        ready.acquire()  # Be sure the other thread is ready.
        #print("at end")
        is_blocked = tlet.blocked
        #tlet.bind_thread()
        try:
            tlet.kill(pending=True)
        except RuntimeError as e:
            self.assertIn("Target thread isn't initialised", str(e))
            # print("got exception")
        else:
            # print("no exception")
            pass
        self.assertTrue(is_blocked)
        time.sleep(0.5)
        # print("unbinding done")

    def _test_kill_without_thread_state(self, nl, block):
        channel = stackless.channel()
        loop = True

        def task():
            while loop:
                try:
                    if block:
                        channel.receive()
                    else:
                        stackless.main.run()
                except TaskletExit:
                    pass

        def other_thread_main():
            tlet.bind_thread()
            tlet.run()

        if nl == 0:
            tlet = stackless.tasklet().bind(task, ())
        else:
            tlet = stackless.tasklet().bind(apply_not_stackless, (task,))
        t = threading.Thread(target=other_thread_main, name="other thread")
        t.start()
        t.join()
        time.sleep(0.05)  # time for other_thread to clear its state

        loop = False
        if block:
            self.assertTrue(tlet.blocked)
        else:
            self.assertFalse(tlet.blocked)
        self.assertFalse(tlet.alive)
        self.assertEqual(tlet.thread_id, -1)
        self.assertRaisesRegex(RuntimeError, "tasklet has no thread", tlet.throw, TaskletExit, pending=True)
        tlet.kill(pending=True)
        self.assertFalse(tlet.blocked)
        if self.SLP_TASKLET_KILL_REBINDS_THREAD and stackless.enable_softswitch(None) and nl == 0:
            # rebinding and soft switching
            self.assertTrue(tlet.scheduled)
            self.assertTrue(tlet.alive)
            tlet.remove()
            tlet.bind(None)
        else:
            # hard switching
            self.assertFalse(tlet.scheduled)
            self.assertIsNone(tlet.next)
            self.assertIsNone(tlet.prev)
            self.assertFalse(tlet.alive)
            tlet.remove()
            tlet.kill()

    @unittest.skipUnless(withThreads, "requires thread support")
    @testcase_leaks_references("chatches TaskletExit and does not die in its own thread", soft_switching=False)
    def test_kill_without_thread_state_nl0(self):
        return self._test_kill_without_thread_state(0, False)

    @unittest.skipUnless(withThreads, "requires thread support")
    @testcase_leaks_references("chatches TaskletExit and does not die in its own thread")
    def test_kill_without_thread_state_nl1(self):
        return self._test_kill_without_thread_state(1, False)

    @unittest.skipUnless(withThreads, "requires thread support")
    @testcase_leaks_references("chatches TaskletExit and does not die in its own thread", soft_switching=False)
    def test_kill_without_thread_state_blocked_nl0(self):
        return self._test_kill_without_thread_state(0, True)

    @unittest.skipUnless(withThreads, "requires thread support")
    @testcase_leaks_references("chatches TaskletExit and does not die in its own thread")
    def test_kill_without_thread_state_blocked_nl1(self):
        return self._test_kill_without_thread_state(1, True)


class TestErrorHandler(StacklessTestCase):

    def setUp(self):
        super(TestErrorHandler, self).setUp()
        self.handled = self.ran = 0
        self.handled_tasklet = None

    def test_set(self):
        def foo():
            pass
        self.assertEqual(stackless.set_error_handler(foo), None)
        self.assertEqual(stackless.set_error_handler(None), foo)
        self.assertEqual(stackless.set_error_handler(None), None)

    @contextlib.contextmanager
    def handlerctxt(self, handler):
        old = stackless.set_error_handler(handler)
        try:
            yield()
        finally:
            stackless.set_error_handler(old)

    def handler(self, exc, val, tb):
        self.assertTrue(exc)
        self.handled = 1
        self.handled_tasklet = stackless.getcurrent()

    def borken_handler(self, exc, val, tb):
        self.handled = 1
        raise IndexError("we are the mods")

    def get_handler(self):
        h = stackless.set_error_handler(None)
        stackless.set_error_handler(h)
        return h

    def func(self, handler):
        self.ran = 1
        self.assertEqual(self.get_handler(), handler)
        raise ZeroDivisionError("I am borken")

    def test_handler(self):
        stackless.tasklet(self.func)(self.handler)
        with self.handlerctxt(self.handler):
            stackless.run()
        self.assertTrue(self.ran)
        self.assertTrue(self.handled)

    def test_borken_handler(self):
        stackless.tasklet(self.func)(self.borken_handler)
        with self.handlerctxt(self.borken_handler):
            self.assertRaisesRegex(IndexError, "mods", stackless.run)
        self.assertTrue(self.ran)
        self.assertTrue(self.handled)

    def test_early_hrow(self):
        "test that we handle errors thrown before the tasklet function runs"
        s = stackless.tasklet(self.func)(self.handler)
        with self.handlerctxt(self.handler):
            s.throw(ZeroDivisionError, "thrown error")
        self.assertFalse(self.ran)
        self.assertTrue(self.handled)

    def test_getcurrent(self):
        # verify that the error handler runs in the context of the exiting tasklet
        s = stackless.tasklet(self.func)(self.handler)
        with self.handlerctxt(self.handler):
            s.throw(ZeroDivisionError, "thrown error")
        self.assertTrue(self.handled_tasklet is s)

        self.handled_tasklet = None
        s = stackless.tasklet(self.func)(self.handler)
        with self.handlerctxt(self.handler):
            s.run()
        self.assertTrue(self.handled_tasklet is s)

    def test_throw_pending(self):
        # make sure that throwing a pending error doesn't immediately throw
        def func(h):
            self.ran = 1
            stackless.schedule()
            self.func(h)
        s = stackless.tasklet(func)(self.handler)
        s.run()
        self.assertTrue(self.ran)
        self.assertFalse(self.handled)
        with self.handlerctxt(self.handler):
            s.throw(ZeroDivisionError, "thrown error", pending=True)
        self.assertEqual(self.handled_tasklet, None)
        with self.handlerctxt(self.handler):
            s.run()
        self.assertEqual(self.handled_tasklet, s)
#
# Test context manager soft switching support
# See http://www.stackless.com/ticket/22
#


def _create_contextlib_test_classes():
    import test.test_contextlib as module
    g = globals()
    for name in dir(module):
        obj = getattr(module, name, None)
        if not (isinstance(obj, type) and issubclass(obj, unittest.TestCase)):
            continue
        g[name] = type(name, (AsTaskletTestCase, obj), {})

_create_contextlib_test_classes()


class TestContextManager(StacklessTestCase):

    def nestingLevel(self):
        self.assertFalse(stackless.getcurrent().nesting_level)

        class C(object):

            def __enter__(self_):  # @NoSelf
                self.assertFalse(stackless.getcurrent().nesting_level)
                return self_

            def __exit__(self_, exc_type, exc_val, exc_tb):  # @NoSelf
                self.assertFalse(stackless.getcurrent().nesting_level)
                return False
        with C() as c:
            self.assertTrue(isinstance(c, C))

    def test_nestingLevel(self):
        if not stackless.enable_softswitch(None):
            # the test requires softswitching
            return
        stackless.tasklet(self.nestingLevel)()
        stackless.run()

    def test_tasklet_switch_in_exitfunc_after_return(self):
        # A test for Stackless issue #159.
        # The test must not crash python.

        class ContextManager(object):
            def __enter__(self):
                return None

            def __exit__(self, exc_type, exc_value, traceback):
                # soft switch to the main tasklet
                stackless.schedule_remove(4711)
                # return True to enable the code path used to silence an exception
                return True  # python used to crash here, until #159 was fixed

        def task():
            with ContextManager():
                # you can set a gdb breakpoint in stacklessmodule.c test_cframe()
                stackless.test_cframe(0, 0)
                # We need a return statement, to set stack TOP to an int()
                return None
            self.fail("Not reached")

        t = stackless.tasklet(task)()
        t.run()
        self.assertEqual(t.tempval, 4711)  # back from context manager __exit__
        t.run()
        self.assertFalse(t.alive)  # tasklet done


class TestAtomic(StacklessTestCase):
    """Test the getting and setting of the tasklet's 'atomic' flag, and the
       context manager to set it to True
    """

    def testAtomic(self):
        old = stackless.getcurrent().atomic
        try:
            val = stackless.getcurrent().set_atomic(False)
            self.assertEqual(val, old)
            self.assertEqual(stackless.getcurrent().atomic, False)

            val = stackless.getcurrent().set_atomic(True)
            self.assertEqual(val, False)
            self.assertEqual(stackless.getcurrent().atomic, True)

            val = stackless.getcurrent().set_atomic(True)
            self.assertEqual(val, True)
            self.assertEqual(stackless.getcurrent().atomic, True)

            val = stackless.getcurrent().set_atomic(False)
            self.assertEqual(val, True)
            self.assertEqual(stackless.getcurrent().atomic, False)

        finally:
            stackless.getcurrent().set_atomic(old)
        self.assertEqual(stackless.getcurrent().atomic, old)

    def testAtomicCtxt(self):
        old = stackless.getcurrent().atomic
        stackless.getcurrent().set_atomic(False)
        try:
            with stackless.atomic():
                self.assertTrue(stackless.getcurrent().atomic)
        finally:
            stackless.getcurrent().set_atomic(old)

    def testAtomicNopCtxt(self):
        old = stackless.getcurrent().atomic
        stackless.getcurrent().set_atomic(True)
        try:
            with stackless.atomic():
                self.assertTrue(stackless.getcurrent().atomic)
        finally:
            stackless.getcurrent().set_atomic(old)


class TestSchedule(AsTaskletTestCase):

    def setUp(self):
        super(TestSchedule, self).setUp()
        self.events = []

    def testSchedule(self):
        def foo(previous):
            self.events.append("foo")
            self.assertTrue(previous.scheduled)
        t = stackless.tasklet(foo)(stackless.getcurrent())
        self.assertTrue(t.scheduled)
        stackless.schedule()
        self.assertEqual(self.events, ["foo"])

    def testScheduleRemoveFail(self):
        def foo(previous):
            self.events.append("foo")
            self.assertFalse(previous.scheduled)
            previous.insert()
            self.assertTrue(previous.scheduled)
        t = stackless.tasklet(foo)(stackless.getcurrent())
        stackless.schedule_remove()
        self.assertEqual(self.events, ["foo"])


class TestBind(StacklessTestCase):

    def setUp(self):
        super(TestBind, self).setUp()
        self.finally_run_count = 0
        self.args = self.kwargs = None

    def task(self, with_c_state):
        try:
            if with_c_state:
                stackless.test_cstate(lambda: stackless.schedule_remove(None))
            else:
                stackless.schedule_remove(None)
        finally:
            self.finally_run_count += 1

    def argstest(self, *args, **kwargs):
        self.args = args
        self.kwargs = dict(kwargs)

    def assertArgs(self, args, kwargs):
        self.assertEqual(args, self.args)
        self.assertEqual(kwargs, self.kwargs)

    def test_bind(self):
        t = stackless.tasklet()
        wr = weakref.ref(t)

        self.assertFalse(t.alive)
        self.assertIsNone(t.frame)
        self.assertEqual(t.nesting_level, 0)

        t.bind(None)  # must not change the tasklet

        self.assertFalse(t.alive)
        self.assertIsNone(t.frame)
        self.assertEqual(t.nesting_level, 0)

        t.bind(self.task)
        t.setup(False)

        stackless.run()
        self.assertFalse(t.scheduled)
        self.assertTrue(t.alive)
        if stackless.enable_softswitch(None):
            self.assertTrue(t.restorable)
        self.assertIsInstance(t.frame, types.FrameType)

        t.insert()
        stackless.run()

        # remove the tasklet. Must run the finally clause
        t = None
        self.assertIsNone(wr())  # tasklet has been deleted
        self.assertEqual(self.finally_run_count, 1)

    def test_bind_fail_not_callable(self):
        class C(object):
            pass
        self.assertRaisesRegex(TypeError, "callable", stackless.getcurrent().bind, C())

    def test_unbind_ok(self):
        if not stackless.enable_softswitch(None):
            # the test requires softswitching
            return
        t = stackless.tasklet(self.task)(False)
        wr = weakref.ref(t)

        # prepare a paused tasklet
        stackless.run()
        self.assertFalse(t.scheduled)
        self.assertTrue(t.alive)
        self.assertEqual(t.nesting_level, 0)
        self.assertIsInstance(t.frame, types.FrameType)

        t.bind(None)
        self.assertFalse(t.alive)
        self.assertIsNone(t.frame)

        # remove the tasklet. Must not run the finally clause
        t = None
        self.assertIsNone(wr())  # tasklet has been deleted
        self.assertEqual(self.finally_run_count, 0)

    def test_unbind_fail_current(self):
        self.assertRaisesRegex(RuntimeError, "current tasklet", stackless.getcurrent().bind, None)

    def test_unbind_fail_scheduled(self):
        t = stackless.tasklet(self.task)(False)

        # prepare a paused tasklet
        stackless.run()
        t.insert()
        self.assertTrue(t.scheduled)
        self.assertTrue(t.alive)
        self.assertIsInstance(t.frame, types.FrameType)

        self.assertRaisesRegex(RuntimeError, "scheduled", t.bind, None)

    def test_unbind_fail_cstate(self):
        t = stackless.tasklet(self.task)(True)
        wr = weakref.ref(t)

        # prepare a paused tasklet
        stackless.run()
        self.assertFalse(t.scheduled)
        self.assertTrue(t.alive)
        self.assertGreaterEqual(t.nesting_level, 1)
        self.assertIsInstance(t.frame, types.FrameType)

        self.assertRaisesRegex(RuntimeError, "C state", t.bind, None)

        # remove the tasklet. Must run the finally clause
        t = None
        self.assertIsNone(wr())  # tasklet has been deleted
        self.assertEqual(self.finally_run_count, 1)

    def test_bind_noargs(self):
        t = stackless.tasklet(self.task)
        t.bind(self.argstest)
        self.assertRaises(RuntimeError, t.run)

    def test_bind_args(self):
        args = "foo", "bar"
        t = stackless.tasklet(self.task)
        t.bind(self.argstest, args)
        t.run()
        self.assertArgs(args, {})

        t = stackless.tasklet(self.task)
        t.bind(self.argstest, args=args)
        t.run()
        self.assertArgs(args, {})

    def test_bind_kwargs(self):
        t = stackless.tasklet(self.task)
        kwargs = {"hello": "world"}
        t.bind(self.argstest, None, kwargs)
        t.run()
        self.assertArgs((), kwargs)

        t = stackless.tasklet(self.task)
        t.bind(self.argstest, kwargs=kwargs)
        t.run()
        self.assertArgs((), kwargs)

    def test_bind_args_kwargs(self):
        args = ("foo", "bar")
        kwargs = {"hello": "world"}

        t = stackless.tasklet(self.task)
        t.bind(self.argstest, args, kwargs)
        t.run()
        self.assertArgs(args, kwargs)

        t = stackless.tasklet(self.task)
        t.bind(self.argstest, args=args, kwargs=kwargs)
        t.run()
        self.assertArgs(args, kwargs)

    def test_bind_args_kwargs_nofunc(self):
        args = ("foo", "bar")
        kwargs = {"hello": "world"}

        t = stackless.tasklet(self.argstest)
        t.bind(None, args, kwargs)
        t.run()
        self.assertArgs(args, kwargs)

        t = stackless.tasklet(self.argstest)
        t.bind(args=args, kwargs=kwargs)
        t.run()
        self.assertArgs(args, kwargs)

    def test_bind_args_not_runnable(self):
        args = ("foo", "bar")
        kwargs = {"hello": "world"}

        t = stackless.tasklet(self.task)
        t.bind(self.argstest, args, kwargs)
        self.assertFalse(t.scheduled)
        t.run()

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_unbind_main(self):
        self.skipUnlessSoftswitching()

        done = []

        def other():
            main = stackless.main
            self.assertRaisesRegex(RuntimeError, "can't unbind the main tasklet", main.bind, None)

        # the initial nesting level depends on the test runner.
        # We need a main tasklet with nesting_level == 0. Therefore we
        # use a thread
        def other_thread():
            self.assertEqual(stackless.current.nesting_level, 0)
            self.assertIs(stackless.current, stackless.main)
            stackless.tasklet(other)().switch()
            done.append(True)

        t = threading.Thread(target=other_thread, name="other thread")
        t.start()
        t.join()
        self.assertTrue(done[0])

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_rebind_main(self):
        # rebind the main tasklet of a thread. This is highly discouraged,
        # because it will deadlock, if the thread is a non daemon threading.Thread.
        self.skipUnlessSoftswitching()

        ready = thread.allocate_lock()
        ready.acquire()

        self.target_called = False
        self.main_returned = False

        def target():
            self.target_called = True
            ready.release()

        def other_thread_main():
            self.assertTrue(stackless.current.is_main)
            try:
                stackless.tasklet(stackless.main.bind)(target, ()).switch()
            finally:
                self.main_returned = True
                ready.release()

        thread.start_new_thread(other_thread_main, ())
        ready.acquire()

        self.assertTrue(self.target_called)
        self.assertFalse(self.main_returned)

    def test_rebind_recursion_depth(self):
        self.skipUnlessSoftswitching()
        self.recursion_depth_in_test = None

        def tasklet_outer():
            tasklet_inner()

        def tasklet_inner():
            stackless.main.switch()

        def test():
            self.recursion_depth_in_test = stackless.current.recursion_depth

        tlet = stackless.tasklet(tasklet_outer)()
        self.assertEqual(tlet.recursion_depth, 0)
        tlet.run()
        self.assertEqual(tlet.recursion_depth, 2)
        tlet.bind(test, ())
        self.assertEqual(tlet.recursion_depth, 0)
        tlet.run()
        self.assertEqual(tlet.recursion_depth, 0)
        self.assertEqual(self.recursion_depth_in_test, 1)

    @unittest.skipUnless(withThreads, "requires thread support")
    @testcase_leaks_references("Tasklet chatches TaskletExit and refuses to die in its thread")
    def test_unbind_fail_cstate_no_thread(self):
        # https://bitbucket.org/stackless-dev/stackless/issues/92
        loop = True

        def task():
            while loop:
                try:
                    stackless.main.switch()
                except TaskletExit:
                    pass

        def other_thread_main():
            tlet.bind_thread()
            tlet.run()

        tlet = stackless.tasklet().bind(apply_not_stackless, (task,))
        t = threading.Thread(target=other_thread_main, name="other thread")
        t.start()
        t.join()
        time.sleep(0.05)  # other_thread needs some time to be destroyed

        loop = False
        self.assertEqual(tlet.thread_id, -1)
        self.assertFalse(tlet.alive)
        self.assertFalse(tlet.restorable)
        self.assertGreater(tlet.nesting_level, 0)
        self.assertRaisesRegex(RuntimeError, "tasklet has C state on its stack", tlet.bind, None)

    def test_setup_fail_alive(self):
        # make sure, that you can't bind a tasklet, which is alive
        # https://bitbucket.org/stackless-dev/stackless/issues/106

        def task():
            t = stackless.current
            t.tempval = lambda: None
            self.assertTrue(t.alive)
            self.assertRaisesRegex(RuntimeError, "tasklet is alive", t.setup)

        t = stackless.tasklet(task, ())
        t.run()
        self.assertFalse(t.alive)


class TestSwitch(StacklessTestCase):
    """Test the new tasklet.switch() method, which allows
    explicit switching
    """

    def setUp(self):
        super(TestSwitch, self).setUp()
        self.source = stackless.getcurrent()
        self.finished = False
        self.c = stackless.channel()

    def target(self):
        self.assertTrue(self.source.paused)
        self.source.insert()
        self.finished = True

    def blocked_target(self):
        self.c.receive()
        self.finished = True

    def test_switch(self):
        """Simple switch"""
        t = stackless.tasklet(self.target)()
        t.switch()
        self.assertTrue(self.finished)

    def test_switch_self(self):
        t = stackless.getcurrent()
        t.switch()

    def test_switch_blocked(self):
        t = stackless.tasklet(self.blocked_target)()
        t.run()
        self.assertTrue(t.blocked)
        self.assertRaisesRegex(RuntimeError, "blocked", t.switch)
        self.c.send(None)
        self.assertTrue(self.finished)

    def test_switch_paused(self):
        t = stackless.tasklet(self.target)
        t.bind(args=())
        self.assertTrue(t.paused)
        t.switch()
        self.assertTrue(self.finished)

    def test_switch_trapped(self):
        t = stackless.tasklet(self.target)()
        self.assertFalse(t.paused)
        with switch_trapped():
            self.assertRaisesRegex(RuntimeError, "switch_trap", t.switch)
        self.assertFalse(t.paused)
        t.switch()
        self.assertTrue(self.finished)

    def test_switch_self_trapped(self):
        t = stackless.getcurrent()
        with switch_trapped():
            t.switch()  # ok, switching to ourselves!

    def test_switch_blocked_trapped(self):
        t = stackless.tasklet(self.blocked_target)()
        t.run()
        self.assertTrue(t.blocked)
        with switch_trapped():
            self.assertRaisesRegex(RuntimeError, "blocked", t.switch)
        self.assertTrue(t.blocked)
        self.c.send(None)
        self.assertTrue(self.finished)

    def test_switch_paused_trapped(self):
        t = stackless.tasklet(self.target)
        t.bind(args=())
        self.assertTrue(t.paused)
        with switch_trapped():
            self.assertRaisesRegex(RuntimeError, "switch_trap", t.switch)
        self.assertTrue(t.paused)
        t.switch()
        self.assertTrue(self.finished)


class TestModule(StacklessTestCase):

    def test_get_debug(self):
        self.assertIn(stackless.getdebug(), [True, False])

    def test_debug(self):
        self.assertIn(stackless.debug, [True, False])

    def test_get_uncollectables(self):
        self.assertEqual(type(stackless.getuncollectables()), list)

    def test_uncollectables(self):
        self.assertEqual(type(stackless.uncollectables), list)

    def test_get_threads(self):
        self.assertEqual(type(stackless.getthreads()), list)

    def test_threads(self):
        self.assertEqual(type(stackless.threads), list)


class TestCstate(StacklessTestCase):
    def test_cstate(self):
        self.assertIsInstance(stackless.main.cstate, stackless.cstack)

    def test_str_size(self):
        c = stackless.main.cstate
        s = str(c)
        self.assertEqual(len(s), c.size * struct.calcsize("P"))

    def test_nesting_level(self):
        c = stackless.main.cstate
        l1 = c.nesting_level
        self.assertIsInstance(l1, int)

    def test_chain(self):
        # create at least one additional C-stack
        t = stackless.tasklet(apply_not_stackless)(stackless.main.switch,)
        t.run()
        self.addCleanup(t.run)
        start = stackless.main.cstate
        c = start.next
        self.assertIsNot(c, start)
        while(c is not start):
            self.assertIsInstance(c, stackless.cstack)
            self.assertIs(c.prev.next, c)
            c = c.next


#///////////////////////////////////////////////////////////////////////////////

if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')

    unittest.main()
