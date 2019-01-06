from __future__ import absolute_import
# import common

import unittest
import stackless
import sys
import time
import struct
import _teststackless
from stackless import _test_nostacklesscall as apply_not_stackless

from support import test_main  # @UnusedImport
from support import StacklessTestCase, AsTaskletTestCase, testcase_leaks_references
try:
    import threading
    try:
        import thread
    except ImportError:
        import _thread as thread
    withThreads = True
except:
    withThreads = False

    class threading(object):
        Thread = object


class SkipMixin(object):

    def skipUnlessSoftswitching(self):
        if not stackless.enable_softswitch(None):
            self.skipTest("test requires softswitching")


def GetRemoteTasklets(callables):
    """Get a non-scheduled tasklet on a remote thread"""
    c = stackless.channel()

    def tfunc():
        # thread func.  Create a tasklet, remove it, and send it to the master.
        # then wait for the tasklet to finish.
        try:
            c2 = stackless.channel()
            tasklets = []
            for callable in callables:
                def helper(callable):
                    try:
                        callable()
                    except:
                        c2.send_throw(*sys.exc_info())
                    else:
                        c2.send(None)
                t = stackless.tasklet(helper)(callable)
                t.remove()
                tasklets.append(t)
            c.send(tasklets)
        except:
            c.send_throw(*sys.exc_info())
        stackless.__reduce__()
        for callable in callables:
            c2.receive()
        stackless.run()  # drain the scheduler

    thread = threading.Thread(target=tfunc)
    thread.start()
    d = c.receive(), thread
    return d


class LingeringThread(threading.Thread):
    """ A thread that lingers on after executing its main function"""

    def __init__(self, *args, **kwargs):
        self.real_target = kwargs["target"]
        kwargs["target"] = self.thread_func
        super(LingeringThread, self).__init__(*args, **kwargs)
        self.shutdown = threading.Event()

    def thread_func(self, *args, **kwargs):
        result = self.real_target(*args, **kwargs)
        self.linger()
        return result

    def linger(self):
        # wait until join is called
        self.shutdown.wait()

    def join(self):
        self.shutdown.set()
        super(LingeringThread, self).join()
        time.sleep(0.01)  # give the thread a chance to clean up

    def __enter__(self):
        pass

    def __exit__(self, ex, val, tb):
        self.join()


class SchedulingThread(LingeringThread):
    """ A thread that runs a scheduling loop after executing its main function"""

    def linger(self):
        while not self.shutdown.is_set():
            stackless.run()
            time.sleep(0.001)


def GetRemoteTasklet(callable, args):
    """Get a non-scheduled tasklet on a remote thread"""
    tasklets, thread = GetRemoteTasklets([lambda:callable(*args)])
    return tasklets[0], thread


@unittest.skipUnless(withThreads, "requires thread support")
class TestRemoteSchedule(AsTaskletTestCase):

    def setUp(self):
        super(TestRemoteSchedule, self).setUp()
        self.events = []

    def testFoo(self):
        def foo():
            pass
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.run()
        finally:
            thread.join(2)

    def testRun(self):
        def foo():
            self.events.append(0)
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.run()
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(len(self.events))))

    def testInsert(self):
        def foo():
            self.events.append(0)
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.insert()
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(len(self.events))))

    def testRunOrder(self):
        def a():
            self.events.append(0)

        def b():
            self.events.append(1)

        def c():
            self.events.append(2)

        (t1, t2, t3), thread = GetRemoteTasklets((a, b, c))
        try:
            with stackless.atomic():
                t2.insert()
                t3.insert()
                t1.run()  # t1 should run first
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(3)))


@unittest.skipUnless(withThreads, "requires thread support")
class TestRebindCrash(SkipMixin, StacklessTestCase):
    """A crash from Anselm Kruis, occurring when transferring tasklet to a thread"""

    def create_remote_tasklet(self, nontrivial=False, job=None):
        result = []
        e1 = threading.Event()
        e2 = threading.Event()

        def remove():
            stackless.schedule_remove(retval=None)

        def taskletfunc():
            result.append(stackless.getcurrent())
            if nontrivial:
                _teststackless.test_cstate(remove)
            else:
                remove()
            if job:
                job()

        def threadfunc():
            t = stackless.tasklet(taskletfunc)()
            t.run()
            e1.set()
            while not e2.is_set():
                stackless.run()
                time.sleep(0.001)
            e2.wait()  # wait until we can die
        t = threading.Thread(target=threadfunc)
        t.start()
        e1.wait()

        # callable to end the thread
        def end():
            e2.set()
            t.join()
        return end, result[0]

    def to_current_thread(self, task):
        """
        Get a tasklet for the current thread.

        If the tasklet already belongs to the current thread, this
        method returns the tasklet unmodified.

        Otherwise, this method tries to
        unbind the tasklet and returns a newly created tasklet. If
        unbinding fails, the method raises :exc:`RuntimeError`.
        """
        self.skipUnlessSoftswitching()
        if task.thread_id == thread.get_ident():
            return task
        reducedTask = task.__reduce__()
        # raise RuntimeError, if task is alive but not paused
        task.bind(None)

        if False:  # Stackless will crash if set to False
            frameList = reducedTask[2][3]
            for i in range(len(frameList)):
                frame = frameList[i]
                if isinstance(frame, stackless.cframe):
                    reducedFrame = frame.__reduce__()
                    newFrame = reducedFrame[0](*reducedFrame[1])
                    newFrame.__setstate__(reducedFrame[2])
                    frameList[i] = newFrame
        # rebind the task
        task = reducedTask[0](*reducedTask[1])
        for i in range(len(reducedTask[2][3])):
            if not isinstance(reducedTask[2][3][i], stackless.cframe):
                reducedTask[2][3][i] = reducedTask[2][3][i].frame
        task.__setstate__(reducedTask[2])
        return task

    def test_crash(self):
        self.skipUnlessSoftswitching()
        end, task = self.create_remote_tasklet()
        try:
            task = self.to_current_thread(task)
            task.run()
        finally:
            end()

    def test_no_rebind(self):
        result = []
        e = threading.Event()

        def job():
            result.append(thread.get_ident())
            e.set()
        end, task = self.create_remote_tasklet(job=job)
        try:
            task.run()
            e.wait()
            self.assertNotEqual(result[0], thread.get_ident())
        finally:
            end()

    def test_rebind(self):
        self.skipUnlessSoftswitching()
        result = []

        def job():
            result.append(thread.get_ident())
        end, task = self.create_remote_tasklet(job=job)
        try:
            task.bind_thread()
            task.run()
            self.assertEqual(result[0], thread.get_ident())
        finally:
            end()

    def test_rebind_nontrivial(self):
        end, task = self.create_remote_tasklet(nontrivial=True)
        try:
            self.assertRaisesRegex(RuntimeError, "C state", task.bind_thread)
        finally:
            end()


@unittest.skipUnless(withThreads, "requires thread support")
class RemoteTaskletTests(SkipMixin, StacklessTestCase):
    ThreadClass = LingeringThread

    def setUp(self):
        super(RemoteTaskletTests, self).setUp()
        self.taskletExecuted = False
        self.event = threading.Event()
        self.channel = stackless.channel()

    def create_tasklet(self, action, *args, **kw):
        self.tasklet = stackless.tasklet(action)(*args, **kw)
        self.event.set()

    def tasklet_action(self):
        self.taskletExecuted = True

    def create_thread_task(self, action=None):
        if not action:
            action = self.tasklet_action
        theThread = self.ThreadClass(target=self.create_tasklet,
                                     args=(action,))
        theThread.start()
        self.event.wait()
        self.event.clear()
        t = self.tasklet
        return theThread, t


class TestRemove(RemoteTaskletTests):

    def test_remove_balance(self):
        """ Test that remove from the runqueue of a remote thread does not affect the
        bookkeeping of the current thread.
        """
        before = stackless.getruncount()
        thread, task = self.create_thread_task()
        try:
            after = stackless.getruncount()
            self.assertEqual(before, after)
            task.remove()
            after = stackless.getruncount()
            # only the runnable count on the remote thread
            # should change
            self.assertEqual(before, after)
        finally:
            thread.join()

    def test_insert_balance(self):
        """ Test that insert into the runqueue of a remote thread does not affect the
        bookkeeping of the current thread.
        """
        thread, task = self.create_thread_task()
        try:
            task.remove()
            before = stackless.getruncount()
            task.insert()
            after = stackless.getruncount()
            # only the runnable count on the remote thread
            # should change
            self.assertEqual(before, after)
        finally:
            thread.join()


class DeadThreadTest(RemoteTaskletTests):

    def test_tasklet_from_dead_thread(self):
        theThread, t = self.create_thread_task()
        self.assertTrue(t.alive)
        theThread.join()
        time.sleep(0.01)  # give the thread a short time to clean up
        # now the tasklet should have been killed.
        self.assertFalse(t.alive)

    def test_removed_tasklet_from_dead_thread(self):
        theThread, t = self.create_thread_task()
        self.assertTrue(t.scheduled)
        t.remove()
        self.assertFalse(t.scheduled)
        theThread.join()
        time.sleep(0.01)  # give the thread a short time to clean up
        # now the tasklet should have been killed.
        self.assertFalse(t.alive)

    def test_rebound_tasklet_from_dead_thread(self):
        theThread, t = self.create_thread_task()
        t.remove()
        t.bind_thread()
        theThread.join()
        # now the tasklet should be alive
        self.assertTrue(t.alive)
        t.run()
        self.assertTrue(self.taskletExecuted)
        self.assertFalse(t.alive)

    def test_bind_runnable(self):
        theThread, t = self.create_thread_task()
        self.assertRaisesRegex(RuntimeError, "runnable", t.bind_thread)
        theThread.join()

    def test_death(self):
        """test tasklets from dead threads"""
        theThread, t = self.create_thread_task()
        with theThread:
            self.assertNotEqual(t.thread_id, -1)
        self.assertEqual(t.thread_id, -1)

    def test_rebind_from_dead(self):
        """test that rebinding a fresh tasklet from a dead thread works"""
        theThread, t = self.create_thread_task()
        with theThread:
            self.assertNotEqual(t.thread_id, -1)
        self.assertEqual(t.thread_id, -1)
        t.bind_thread()
        self.assertEqual(t.thread_id, stackless.getcurrent().thread_id)

    @testcase_leaks_references("test catches TaskletExit and refuses to die in its own thread")
    def test_rebind_from_dead_fail_cstate(self):
        # A test for https://bitbucket.org/stackless-dev/stackless/issues/92
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
        time.sleep(0.1)  # other_thread needs some time to be destroyed

        self.assertEqual(tlet.thread_id, -1)
        self.assertFalse(tlet.alive)
        self.assertFalse(tlet.restorable)
        self.assertGreater(tlet.nesting_level, 0)
        loop = False
        try:
            self.assertRaisesRegex(RuntimeError, "tasklet has C state on its stack", tlet.bind_thread)
        except AssertionError:
            tlet.kill()  # causes an assertion error in debug builds of 2.7.9-slp
            raise
        # the tasklet has no thread
        self.assertEqual(tlet.thread_id, -1)
        self.tasklet_is_uncollectable(tlet)

    def test_methods_on_dead(self):
        """test that tasklet methods on a dead tasklet behave well"""
        class MyException(Exception):
            pass

        theThread, t = self.create_thread_task()
        with theThread:
            self.assertNotEqual(t.thread_id, -1)
        self.assertEqual(t.thread_id, -1)

        self.assertFalse(t.alive)
        self.assertFalse(t.paused)
        self.assertFalse(t.blocked)
        self.assertFalse(t.scheduled)
        self.assertTrue(t.restorable)
        self.assertFalse(t.atomic)
        self.assertFalse(t.block_trap)
        self.assertFalse(t.ignore_nesting)
        self.assertIsNone(t.next)
        self.assertIsNone(t.prev)
        # must not raise an exception
        t.trace_function
        t.profile_function
        self.assertEqual(t.thread_id, -1)
        t.bind(None)
        self.assertEqual(t.thread_id, -1)
        t.remove()
        self.assertEqual(t.thread_id, -1)
        t.bind(lambda: None)
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.setup)
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.bind, lambda: None, ())
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.insert)
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.run)
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.switch)
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.raise_exception, MyException, 'test')
        self.assertEqual(t.thread_id, -1)
        self.assertRaises(RuntimeError, t.throw, MyException)
        self.assertEqual(t.thread_id, -1)
        t.__reduce__()
        self.assertEqual(t.thread_id, -1)
        t.set_atomic(t.set_atomic(True))
        self.assertEqual(t.thread_id, -1)
        t.set_ignore_nesting(t.set_ignore_nesting(1))
        self.assertEqual(t.thread_id, -1)
        t.bind(None)
        self.assertEqual(t.thread_id, -1)


class BindThreadTest(RemoteTaskletTests):
    """More unittests for tasklet.bind_thread"""

    def testForeignThread_scheduled(self):
        theThread, t = self.create_thread_task()
        try:
            self.assertEqual(t.thread_id, theThread.ident)
            self.assertTrue(t.alive)
            self.assertFalse(t.paused)
            t.remove()
            self.assertTrue(t.paused)

            t.bind_thread()

            self.assertTrue(t.alive)
            self.assertTrue(t.paused)

            self.assertNotEqual(t.thread_id, theThread.ident)
            self.assertEqual(t.thread_id, thread.get_ident())
            t.insert()
            self.assertFalse(t.paused)

            stackless.run()
            self.assertTrue(self.taskletExecuted)
            self.assertFalse(t.alive)
        finally:
            theThread.join()

    def test_bind_to_current_tid(self):
        current_id = stackless.getcurrent().thread_id
        self.assertEqual(current_id, thread.get_ident())

        theThread, t = self.create_thread_task()
        t.remove()
        with theThread:
            self.assertEqual(t.thread_id, theThread.ident)
            t.bind_thread(current_id)
        self.assertEqual(t.thread_id, current_id)
        t.run()
        self.assertTrue(self.taskletExecuted)
        self.assertFalse(t.alive)

    def test_bind_to_bogus_tid(self):
        current_id = stackless.getcurrent().thread_id
        self.assertEqual(current_id, thread.get_ident())

        theThread, t = self.create_thread_task()
        t.remove()
        with theThread:
            self.assertEqual(t.thread_id, theThread.ident)
            self.assertRaises(OverflowError, t.bind_thread, -2)
            # try the max long value, it is very likely not a valid id
            self.assertRaises(ValueError, t.bind_thread,
                              ((1 << (struct.calcsize('@L')*8-1))-1))
            t.bind_thread(current_id)
        self.assertEqual(t.thread_id, current_id)
        t.run()
        self.assertTrue(self.taskletExecuted)
        self.assertFalse(t.alive)


class SchedulingBindThreadTests(RemoteTaskletTests):
    ThreadClass = SchedulingThread

    def tasklet_action(self):
        self.channel.receive()
        self.taskletExecuted = True
        self.channel.send(None)

    def test_bind_to_other_tid(self):
        self.skipUnlessSoftswitching()
        current_id = stackless.getcurrent().thread_id
        self.assertEqual(current_id, thread.get_ident())

        theThread, t = self.create_thread_task()
        with theThread:
            otherThread, t2 = self.create_thread_task()
            with otherThread:
                self.assertEqual(t.thread_id, theThread.ident)
                t.bind_thread(otherThread.ident)
                self.assertEqual(t.thread_id, otherThread.ident)
                self.channel.send(None)
                self.channel.receive()
        self.assertTrue(self.taskletExecuted)
        self.assertFalse(t.alive)

    def tasklet_runnable_action(self):
        """A tasklet that keeps itself runnable"""
        while not self.channel.balance:
            stackless.schedule()
            time.sleep(0.001)
        self.channel.receive()

    def test_rebind_runnable(self):
        theThread, t = self.create_thread_task(self.tasklet_runnable_action)
        with theThread:
            self.assertRaisesRegex(RuntimeError, 'runnable', t.bind_thread)
            self.channel.send(None)


class SwitchTest(RemoteTaskletTests):
    ThreadClass = SchedulingThread

    def tasklet_action(self):
        stackless.schedule_remove()  # pause it
        self.taskletExecuted = True

    def test_switch(self):
        """Test that inter-thread switching fails"""
        theThread, t = self.create_thread_task()
        with theThread:
            time.sleep(0.01)
            self.assertTrue(t.paused)
            self.assertRaisesRegex(RuntimeError, "different thread", t.switch)


class SetupFromDifferentThreadTest(RemoteTaskletTests):
    # Test case for issue #60 https://bitbucket.org/stackless-dev/stackless/issue/60

    def create_tasklet(self, action, *args, **kw):
        self.tasklet = stackless.tasklet(action)
        self.event.set()

    def test_setup_from_other_thread(self):
        theThread, t = self.create_thread_task()
        t.setup()
        theThread.join()


@unittest.skipUnless(withThreads, "requires thread support")
class TestThreadLocalStorage(StacklessTestCase):
    class ObjectWithDestructor(object):
        def __init__(self, event):
            self.event = event

        def __del__(self):
            self.event.set()

    def test_destructor_at_end_of_thread(self):
        # Test case for issue #121 https://bitbucket.org/stackless-dev/stackless/issue/121
        # Run a destructor during clean up of thread local storage
        # Until issue #121 got fixed, this caused a reference leak
        tls = threading.local()
        deleted = threading.Event()

        def other_thread():
            tls.owd = self.ObjectWithDestructor(deleted)

        self.assertFalse(deleted.is_set())
        t = threading.Thread(target=other_thread, name="other thread")
        t.start()
        t.join()
        time.sleep(0.1)  # give the thread time to clean up
        self.assertTrue(deleted.is_set())


if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')

    unittest.main()
