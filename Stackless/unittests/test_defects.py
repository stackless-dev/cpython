from __future__ import print_function, absolute_import, division
import unittest
import stackless
import gc
import sys
import types
from io import BytesIO
import time
try:
    import threading
    withThreads = True
except:
    withThreads = False

from stackless import _test_nostacklesscall as apply_not_stackless
from support import test_main  # @UnusedImport
from support import (StacklessTestCase, captured_stderr, require_one_thread,
                     get_reduce_frame)


"""
Various regression tests for stackless defects.
Typically, one can start by adding a test here, then fix it.
Don't check in tests for un-fixed defects unless they are disabled (by adding a leading _)
"""


class TestTaskletDel(StacklessTestCase):
    # Defect:  If a tasklet's tempval contains any non-trivial __del__ function, it will cause
    # an assertion in debug mode due to violation of the stackless protocol.
    # The return value of the tasklet's function is stored in the tasklet's tempval and cleared
    # when the tasklet exits.
    # Also, the tasklet itself would have problems with a __del__ method.

    class ObjWithDel:

        def __del__(self):
            self.called_func()

        def called_func(self):
            pass  # destructor must call a function

    class TaskletWithDel(stackless.tasklet):

        def __del__(self):
            self.func()

        def func(self):
            pass

    class TaskletWithDelAndCollect(stackless.tasklet):

        def __del__(self):
            gc.collect()

    def BlockingReceive(self):
        # Function to block when run in a tasklet.
        def f():
            # must store c in locals
            c = stackless.channel()
            c.receive()
        return stackless.tasklet(f)()

    # Test that a tasklet tempval's __del__ operator works.
    def testTempval(self):
        def TaskletFunc(self):
            return self.ObjWithDel()

        stackless.tasklet(TaskletFunc)(self)
        stackless.run()

    # Test that a tasklet's __del__ operator works.
    def testTasklet(self):
        def TaskletFunc(self):
            pass

        self.TaskletWithDel(TaskletFunc)(self)
        stackless.run()

    # a gc.collect() in a tasklet's __del__ method causes
    def testCrash1(self):
        # we need a lost blocked tasklet here (print the ids for debugging)
        hex(id(self.BlockingReceive()))
        gc.collect()  # so that there isn't any garbage
        stackless.run()

        def TaskletFunc(self):
            pass
        hex(id(self.TaskletWithDelAndCollect(TaskletFunc)(self)))
        stackless.run()  # crash here

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_tasklet_dealloc_in_thread_shutdown(self):
        # Test for https://bitbucket.org/stackless-dev/stackless/issues/89
        def other_thread_main():
            # print("other thread started")
            self.assertIs(stackless.main, stackless.current)
            tasklet2 = stackless.tasklet(apply_not_stackless)(stackless.main.run,)
            # print("OT Main:", stackless.main)
            # print("OT tasklet2:", tasklet2)
            tasklet2.run()
            self.assertTrue(tasklet2.scheduled)
            self.other_thread_started = True
            # the crash from issue #89 happened during the shutdown of other thread

        self.other_thread_started = False
        self.assertIs(stackless.main, stackless.current)
        # print("Main Thread:", stackless.main)
        t = threading.Thread(target=other_thread_main, name="other thread")
        t.start()
        t.join()
        self.assertTrue(self.other_thread_started)
        # print("OK")


class Schedule(StacklessTestCase):

    def testScheduleRemove(self):
        # schedule remove doesn't work if it is the only tasklet running under watchdog
        def func(self):
            stackless.schedule_remove()
            self.fail("We shouldn't be here")
        stackless.run()  # flush all runnables
        t = stackless.tasklet(func)(self)
        stackless.run()
        t.kill()  # avoid a resource leak caused by an uncollectable tasklet

    @require_one_thread
    def testScheduleRemove2(self):
        # schedule remove doesn't work if it is the only tasklet with main blocked
        # main tasklet is blocked, this should raise an error
        def func(self, chan):
            self.assertRaises(RuntimeError, stackless.schedule_remove)
            chan.send(None)
        stackless.run()  # flush all runnables
        chan = stackless.channel()
        stackless.tasklet(func)(self, chan)
        chan.receive()

    def testScheduleRemove3(self):
        '''Schedule-remove the last reference to a tasklet 1'''
        def func():
            stackless.schedule_remove(None)
        stackless.tasklet(func)()
        stackless.run()

    def testScheduleRemove4(self):
        '''Schedule-remove the last reference to a tasklet 2'''
        def func():
            stackless.schedule_remove(None)
        stackless.tasklet(func)()
        stackless.schedule_remove(None)


class Channel(StacklessTestCase):

    def testTemporaryChannel(self):
        def f1():
            stackless.channel().receive()

        stackless.tasklet(f1)()
        old = stackless.enable_softswitch(True)
        try:
            stackless.run()
        finally:
            stackless.enable_softswitch(old)

    def testTemporaryChannel2(self):
        def f1():
            stackless.channel().receive()

        def f2():
            pass

        stackless.tasklet(f1)()
        stackless.tasklet(f2)()
        old = stackless.enable_softswitch(True)
        try:
            stackless.run()
        finally:
            stackless.enable_softswitch(old)


class TestInfiniteRecursion(StacklessTestCase):
    # test for http://www.stackless.com/ticket/20

    def testDirectRecursion(self):
        class A(object):
            # define __call__ in case http://www.stackless.com/ticket/18 is not fixed

            def __call__(self):
                pass
        A.__call__ = A()
        a = A()
        # might crash the Python(r) interpreter, if the recursion check does not kick in
        self.assertRaises(RuntimeError, a)

    def testIndirectDirectRecursion(self):
        class A(object):

            def __call__(self):
                pass

        class B(object):

            def __call__(self):
                pass
        A.__call__ = B()
        B.__call__ = A()
        a = A()
        self.assertRaises(RuntimeError, a)


class TestExceptionInScheduleCallback(StacklessTestCase):
    # Problem
    # Assertion failed: ts->st.current == NULL, file ..\Stackless\module\taskletobject.c, line 51
    # See https://bitbucket.org/stackless-dev/stackless/issue/38

    def scheduleCallback(self, prev, next):
        if next.is_main:
            raise RuntimeError("scheduleCallback")

    def testExceptionInScheduleCallback(self):
        stackless.set_schedule_callback(self.scheduleCallback)
        self.addCleanup(stackless.set_schedule_callback, None)
        stackless.tasklet(lambda: None)()
        with captured_stderr() as stderr:
            stackless.run()
        self.assertTrue("scheduleCallback" in stderr.getvalue())


class TestCrashUponFrameUnpickling(StacklessTestCase):

    def testCrasher(self):
        import pickle
        frame = sys._getframe()
        frameType = type(frame)
        while frame and frame.f_back:
            frame = frame.f_back
        frame = get_reduce_frame()(frame)
        p = pickle.dumps(frame, -1)
        frame = None
        frame = pickle.loads(p)
        self.assertIsInstance(frame, frameType)

        # this access crashes Stackless versions released before Feb 2nd 2014
        f_back = frame.f_back

        self.assertIsNone(f_back)

    def testMissingLocalsplusCrasher(self):
        # A test case for issue #61 https://bitbucket.org/stackless-dev/stackless/issue/61
        #
        # Some versions of stackless create pickles of frames with the localsplus tuple set to None.
        # This test creates a frame with localsplus=None and ensures that Python does not crash upon
        # accessing frame.f_locals
        def reduce_current():
            result = []

            def func(current):
                result.append(stackless._wrap.frame.__reduce__(current.frame))
            stackless.tasklet().bind(func, (stackless.current,)).run()
            return result[0]

        func, args, state = reduce_current()
        # state is a tuple of the form
        # ('f_code', 'valid', 'exec_name', 'f_globals', 'have_locals',
        #  'f_locals', 'f_trace', 'exc_as_tuple', 'f_lasti', 'f_lineno',
        #  'blockstack_as_tuple', 'localsplus_as_tuple')
        self.assertEqual(len(state), 12)

        state = list(state)

        # set valid=0, localsplus_as_tuple=None
        state[1] = 0
        state[-1] = None

        state = tuple(state)
        # create the frame
        frame = func(*args)
        frame.__setstate__(state)
        self.assertIsInstance(frame, types.FrameType)

        # this access crashes Stackless versions released before May 20th 2014
        f_locals = frame.f_locals

        self.assertIsInstance(f_locals, dict)


class TestShutdown(StacklessTestCase):

    def test_cstack_new(self):
        # test for issue #80 https://bitbucket.org/stackless-dev/stackless/issues/80/
        import subprocess
        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", """if 1:
            import stackless, sys
            from stackless import _test_nostacklesscall as apply_not_stackless

            def func():
                global channel
                assert stackless.current.nesting_level == 0
                assert apply_not_stackless(lambda : stackless.current.nesting_level) == 1, "apply_not_stackless does not recurse"
                apply_not_stackless(channel.receive)  # crash at nesting level 1

            channel = stackless.channel()
            task = stackless.tasklet().bind(func, ())  # simplest tasklet
            task.run()
            sys.exit(42)
            """])
        self.assertEqual(rc, 42)

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_interthread_kill(self):
        # test for issue #87 https://bitbucket.org/stackless-dev/stackless/issues/87/
        import subprocess
        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", """from __future__ import print_function, absolute_import\nif 1:
            import sys
            import _thread as thread
            import stackless
            import os
            import time
            from stackless import _test_nostacklesscall as apply_not_stackless

            # This lock is used as a simple event variable.
            ready = thread.allocate_lock()
            ready.acquire()

            if False:  # change to True to enable debug messages
                sys.stdout = sys.stderr  # C-assert messages go to STDERR
            else:
                def print(*args):
                    pass

            # Module globals are cleared before __del__ is run
            # So we save functions, objects, ... in a class dict.
            class C(object):
                time_sleep = time.sleep

                def other_thread_main(self):
                    print("other thread started")
                    assert stackless.main.nesting_level == 0
                    self.main = stackless.main
                    assert stackless.main is stackless.current
                    t1 = stackless.tasklet(apply_not_stackless)(self.main.switch)
                    t1.run()
                    assert t1.paused
                    assert t1.nesting_level == 1
                    print("OT-Main:", self.main)
                    print("OT-t1:", t1)

                    try:
                        ready.release()
                        while True:
                            self.main.run()
                    except TaskletExit:
                        self.time_sleep(999999)

            print("Main-Thread:", stackless.current)
            x = C()
            thread.start_new_thread(x.other_thread_main, ())
            ready.acquire()  # Be sure the other thread is waiting.
            x.main.kill()
            time.sleep(0.5)  # give other thread time to run
            # state is now: other thread has 2 tasklets:
            # - main tasklet is stackless and blocked in sleep(999999)
            # - t1 has a C-state and is paused
            print("at end")
            sys.stdout.flush()
            sys.exit(42)
            """])
        self.assertEqual(rc, 42)

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_tasklet_end_with_wrong_recursion_level(self):
        # test for issue #91 https://bitbucket.org/stackless-dev/stackless/issues/91/
        """A test for issue #91, wrong recursion level after tasklet re-binding

        Assertion failed: ts->recursion_depth == 0 || (ts->st.main == NULL && prev == next), file ../Stackless/module/scheduling.c, line 1291
        The assertion fails with ts->recursion_depth > 0

        It is in function
        static int schedule_task_destruct(PyObject **retval, PyTaskletObject *prev, PyTaskletObject *next):

        assert(ts->recursion_depth == 0 || (ts->st.main == NULL && prev == next));

        During thread shutdown in slp_kill_tasks_with_stacks() kills tasklet tlet after the main
        tasklet of other thread ended. To do so, it creates a new temporary main tasklet. The
        assertion failure happens during the end of the killed tasklet.
        """
        self.skipUnlessSoftswitching()
        if True:
            def print(*args):
                pass

        def tlet_inner():
            assert stackless.current.recursion_depth >= 2, "wrong recursion depth: %d" % (stackless.current.recursion_depth,)
            stackless.main.switch()

        def tlet_outer():
            tlet_inner()

        def other_thread_main():
            self.tlet = stackless.tasklet(tlet_outer)()
            self.assertEqual(self.tlet.recursion_depth, 0)
            print("Other thread main", stackless.main)
            print("Other thread paused", self.tlet)
            self.tlet.run()
            self.assertGreaterEqual(self.tlet.recursion_depth, 2)
            self.tlet.bind(lambda: None, ())  # requires soft switching
            self.assertEqual(self.tlet.recursion_depth, 0)
            # before issue #91 got fixed, the assertion violation occurred here

        print("Main thread", stackless.current)
        t = threading.Thread(target=other_thread_main, name="other thread")
        t.start()
        print("OK")
        t.join()
        print("Done")


class TestStacklessProtokoll(StacklessTestCase):
    """Various tests for violations of the STACKLESS_GETARG() STACKLESS_ASSERT() protocol

    See https://bitbucket.org/stackless-dev/stackless/issues/84
    """
    def test_invalid_args_channel_next(self):
        """test of typeobject.c wrap_next(...)"""
        func = stackless.channel().__next__
        # func(None) causes the crash
        self.assertRaises(TypeError, func, None)

    def test_invalid_args_tasklet_kill(self):
        func = stackless.tasklet().kill
        # func(False, None) causes the crash
        self.assertRaises(TypeError, func, False, None)


class TestCPickleBombHandling_Dict(dict):
    pass


class TestCPickleBombHandling_Cls(object):
    def __getstate__(self):
        try:
            started = self.started
        except AttributeError:
            pass
        else:
            self.started = None
            started.set()
        # print("started")
        time.sleep(0.05)  # give the other thread a chance to run
        return self.__dict__


class TestCPickleBombHandling(StacklessTestCase):
    def other_thread(self, pickler, c):
        try:
            pickler.dump(c)
        except TaskletExit:
            self.killed = None
        except:
            self.killed = sys.exc_info()
        else:
            self.killed = False

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_kill_during_cPickle_stack_switch(self):
        # this test kills the main/current tasklet of a other-thread,
        # which is fast-pickling a recursive structure. This leads to an
        # infinite recursion, which gets interrupted by a bomb thrown from
        # main-thread. Until issue #98 got fixed, this caused a crash.
        # See https://bitbucket.org/stackless-dev/stackless/issues/98
        buf = BytesIO()
        import _pickle as pickle
        pickler = pickle.Pickler(buf, protocol=-1)
        pickler.fast = 1

        started = threading.Event()

        c = TestCPickleBombHandling_Cls()
        c.started = started
        d = TestCPickleBombHandling_Dict()
        d[1] = d
        c.recursive = d
        self.killed = "undefined"
        t = threading.Thread(target=self.other_thread, name="other_thread", args=(pickler, c))
        t.start()
        started.wait()
        stackless.get_thread_info(t.ident)[0].kill(pending=True)
        # print("killing")
        t.join()
        if isinstance(self.killed, tuple):
            raise (self.killed[0], self.killed[1], self.killed[2])
        self.assertIsNone(self.killed)


class TestFrameClear(StacklessTestCase):
    def test_frame_clear(self):
        # a test for Stackless issue #66
        # https://bitbucket.org/stackless-dev/stackless/issues/66
        def generator():
            yield None

        geniter = generator()
        frame = geniter.gi_frame
        frame.clear()  # causes the failure


class TestContextManager(StacklessTestCase):
    def test_crash_on_WHY_SILENCED(self):
        current_switch = stackless.current.switch
        steps = []

        class CtxManager:
            def __enter__(self):
                steps.append(2)

            def __exit__(self, exc_type, exc_val, exc_tb):
                steps.append(4)
                current_switch()  # causes a stack corruption upon resuming __exit__
                steps.append(5)
                return True  # silence the exception

        def task():
            try:
                steps.append(1)
                return "OK"
            finally:
                with CtxManager():
                    steps.append(3)
                    1 // 0
                steps.append(6)
                # Stackless issue #115 ()
                # Leaving this finally block crashes Python,
                # because the interpreter stack is corrupt.

        t = stackless.tasklet(task)()
        t.run()
        self.assertListEqual(steps, [1, 2, 3, 4])
        t.run()
        r = t.tempval
        self.assertListEqual(steps, [1, 2, 3, 4, 5, 6])


class TestUnwinding(StacklessTestCase):
    # a test case for https://bitbucket.org/stackless-dev/stackless/issues/119
    # The macros STACKLESS_PACK(retval) / STACKLESS_UNPACK(retval) are not thread
    # safe. And thread switching can occur, if a destructor runs during stack unwinding.

    def test_interpreter_recursion(self):
        # This test calls pure Python-functions during unwinding.
        # The test ensures, that a recursively invoked interpreter does not
        # unwind the stack, while a higher level interpreter unwinds the stack.
        self.skipUnlessSoftswitching()

        def inner_detector():
            pass

        class Detector(object):
            def __del__(self):
                # print("__del__, going to sleep", file=sys.stderr)
                inner_detector()

        def inner_func():
            return 4711

        def func():
            return inner_func()

        func.detector = Detector()
        get_func = [func].pop
        del func
        # there is only one reference to func.

        # print("going to call func", file=sys.stderr)
        # call func and release the last reference to func. This way
        # Detector.__del__ runs during stack unwinding.
        # Until STACKLESS_PACK(retval) / STACKLESS_UNPACK(retval) becomes
        # thread safe, this raises SystemError or crashes with an assertion
        # failure.
        r = get_func()()
        # print("called func", file=sys.stderr)
        self.assertEqual(r, 4711)

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_concurrent_unwinding(self):
        # This test switches tasks during unwinding. The other tasks performs stackless
        # calls too. This test ensures, that the STACKLESS_PACK() / STACKLESS_UNPACK()
        # mechanism is thread safe.
        self.skipUnlessSoftswitching()

        terminate = False
        started = threading.Event()
        go = threading.Event()

        def other_thread_inner():
            started.set()
            self.assertEqual(stackless.current.nesting_level, 0)

        def other_thread():
            """A thread, that repeatedly calls other_thread_inner() at nesting level 0.
            """
            while not terminate:
                other_thread_inner()
                go.wait()  # used to start in a defined state

        class Detector(object):
            def __del__(self):
                # print("__del__, going to sleep", file=sys.stderr)
                go.set()  # make other_thread runnable
                time.sleep(0.1)  # release GIL
                # print("__del__, sleep done", file=sys.stderr)
                pass

        def inner_func():
            return 4711

        def func():
            return inner_func()

        t = threading.Thread(target=other_thread, name="other_thread")
        t.start()
        started.wait()
        time.sleep(0.05)  # give othere_thread time to reach go.wait()

        func.detector = Detector()
        get_func = [func].pop
        del func
        # there is only one reference to func.

        # print("going to call func", file=sys.stderr)
        try:
            # call func and release the last reference to func. This way
            # Detector.__del__ runs during stack unwinding.
            # Until STACKLESS_PACK(retval) / STACKLESS_UNPACK(retval) becomes
            # thread safe, this raises SystemError or crashes with an assertion
            # failure.
            r = get_func()()
        finally:
            # make sure, that other_thread terminates
            terminate = True
            go.set()  # just in case Detector.__del__() does not run
            t.join(.5)
            # print("called func", file=sys.stderr)
        self.assertEqual(r, 4711)


if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
