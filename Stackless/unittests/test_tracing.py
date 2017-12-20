from __future__ import absolute_import
import unittest
import stackless
import sys
import inspect
from support import test_main  # @UnusedImport
from support import StacklessTestCase


class TestTracingFlag(StacklessTestCase):
    # Test that the PyThreadState tracing flag is preserved.
    # The tracing flag in the PyThreadState structure indicates,
    # that a tracing function is executed.
    # To test this flag, a tasklet switch must occur during tracing.
    # In a real application this could happen if a watchdog timeout
    # is set.
    # Because every trace function has C-state, this test can't test
    # the preservation of the tracing flag in the soft-switching code.
    def setUp(self):
        super(TestTracingFlag, self).setUp()
        if sys.gettrace():
            self.skipTest("Trace function already active")
        self.tracecount = 0
        self.addCleanup(sys.settrace, None)
        self.scheduleErrors = []

    def foo(self):
        pass

    def trace_with_schedule(self, frame, event, arg):
        # a trace function that switches tasks
        count = self.tracecount
        try:
            self.tracecount += 1
            tasklet = stackless.current
            # print "   TraceFunc: %d %s %s in %s, line %s" % \
            # (count, id(tasklet), event, frame.f_code.co_name, frame.f_lineno)
            self.assertGreater(stackless.current.nesting_level, 0)
            self.assertIsNotNone(tasklet.trace_function)
            self.foo()
            self.assertEqual(self.tracecount, count + 1)
            stackless.schedule()
            count = self.tracecount
            self.foo()
            tasklet = stackless.current
            self.assertIsNotNone(tasklet.trace_function)
            self.assertEqual(self.tracecount, count)
        except Exception:
            self.scheduleErrors.append(sys.exc_info())
            sys.exc_clear()
        return stackless.current.trace_function

    def task(self):
        count = self.tracecount
        self.foo()
        self.assertGreater(self.tracecount, count)

    def testTracingFlagIsPreserved(self):
        # create two tasklets and trace them
        # the trace function calls stackless.schedule
        tf = self.trace_with_schedule
        t1 = stackless.tasklet(self.task)()
        t1.trace_function = tf
        t2 = stackless.tasklet(self.task)()
        t2.trace_function = tf
        stackless.run()
        self.assertIsNone(sys.gettrace())
        self.assertGreater(self.tracecount, 0)
        # print "tracecount", self.tracecount
        self.assertListEqual(self.scheduleErrors, [])


# Test tasklet.trace_function and tasklet.profile_function
class TestTracingProperties(StacklessTestCase):
    def setUp(self):
        super(TestTracingProperties, self).setUp()
        if sys.gettrace() or sys.getprofile():
            self.skipTest("Trace or profile function already active")
        self.tracecount = 0
        self.addCleanup(sys.settrace, None)
        self.addCleanup(sys.setprofile, None)

    def tearDown(self):
        pass

    def nullTraceFunc(self, frame, event, arg):
        self.tracecount += 1
        return None

    def task(self, expected_trace_function=None, expected_profile_function=None):
        # this task tests that the trace/profile function has the
        # expected value
        tasklet = stackless.current
        self.assertIs(sys.gettrace(), expected_trace_function)
        self.assertIs(tasklet.trace_function, expected_trace_function)
        self.assertIs(sys.getprofile(), expected_profile_function)
        self.assertIs(tasklet.profile_function, expected_profile_function)

    def task2(self, expected_trace_function=None, expected_profile_function=None):
        # task switches tasklets and then tests that the trace/profile function has the
        # expected value
        stackless.schedule()
        tasklet = stackless.current
        self.assertIs(sys.gettrace(), expected_trace_function)
        self.assertIs(tasklet.trace_function, expected_trace_function)
        self.assertIs(sys.getprofile(), expected_profile_function)
        self.assertIs(tasklet.profile_function, expected_profile_function)

    def testSetTraceOnCurrent(self):
        tf = self.nullTraceFunc
        tasklet = stackless.current
        self.assertIsNone(sys.gettrace())
        self.assertIsNone(sys.getprofile())
        self.assertIsNone(tasklet.trace_function)
        self.assertIsNone(tasklet.profile_function)
        tasklet.trace_function = tf
        self.assertIs(sys.gettrace(), tf)
        self.assertIsNone(sys.getprofile())
        self.assertIs(tasklet.trace_function, tf)
        self.assertIsNone(tasklet.profile_function)
        tasklet.trace_function = None
        self.assertIsNone(sys.gettrace())
        self.assertIsNone(sys.getprofile())
        self.assertIsNone(tasklet.trace_function)
        self.assertIsNone(tasklet.profile_function)
        self.assertGreater(self.tracecount, 0)

    def testSetTraceOnDeadTasklet(self):
        t = stackless.tasklet()
        self.assertIsNone(t.trace_function)
        self.assertRaisesRegex(RuntimeError, "tasklet is not alive",
                                setattr, t, "trace_function", self.nullTraceFunc)

    def testSetProfileOnDeadTasklet(self):
        t = stackless.tasklet()
        self.assertIsNone(t.trace_function)
        self.assertRaisesRegex(RuntimeError, "tasklet is not alive",
                                setattr, t, "profile_function", self.nullTraceFunc)

    def testSetTraceOnTasklet1(self):
        # Change trace state of a non-current tasklet
        # create tasklet, set trace, clear trace, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task)()
        self.assertTrue(t.alive)
        self.assertIsNone(t.trace_function)
        t.trace_function = tf
        self.assertIs(t.trace_function, tf)
        self.assertIsNone(t.profile_function)
        t.trace_function = None
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertEqual(self.tracecount, 0)

    def testSetTraceOnTasklet2(self):
        # Change trace state of a non-current tasklet
        # create tasklet, set trace, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task)(expected_trace_function=tf)
        self.assertTrue(t.alive)
        self.assertIsNone(t.trace_function)
        t.trace_function = tf
        self.assertIs(t.trace_function, tf)
        self.assertIsNone(t.profile_function)
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)

    def _testSetTraceOnTasklet3(self, t, tf):
        t = stackless.tasklet(self.task2)()
        t.trace_function = tf
        t.run()
        self.assertIs(t.trace_function, tf)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)
        t.trace_function = None
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)

    def testSetTraceOnTasklet3a(self):
        # Change trace state of a non-current tasklet
        # create tasklet (soft-switched), set trace, run, clear trace, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task2)()
        self._testSetTraceOnTasklet3(t, tf)

    def testSetTraceOnTasklet3b(self):
        # Change trace state of a non-current tasklet
        # create tasklet (hard-switched), set trace, run, clear trace, run
        tf = self.nullTraceFunc
        # enforce hard switching
        t = stackless.tasklet(stackless.test_cstate)(self.task2)
        self._testSetTraceOnTasklet3(t, tf)

    def _testSetTraceOnTasklet4(self, t, tf):
        t.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        t.trace_function = tf
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)

    def testSetTraceOnTasklet4a(self):
        # Change trace state of a non-current tasklet
        # create tasklet (soft-switched), run, set trace, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task2)(expected_trace_function=tf)
        self._testSetTraceOnTasklet4(t, tf)

    def testSetTraceOnTasklet4b(self):
        # Change trace state of a non-current tasklet
        # create tasklet (hard-switched), run, set trace, run
        tf = self.nullTraceFunc
        # enforce hard switching
        t = stackless.tasklet(stackless.test_cstate)
        t(lambda: self.task2(expected_trace_function=tf))
        self._testSetTraceOnTasklet4(t, tf)

    def testSetProfileOnTasklet1(self):
        # Change profile state of a non-current tasklet
        # create tasklet, set profile, clear profile, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task)()
        self.assertTrue(t.alive)
        self.assertIsNone(t.profile_function)
        t.profile_function = tf
        self.assertIs(t.profile_function, tf)
        self.assertIsNone(t.trace_function)
        t.profile_function = None
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertEqual(self.tracecount, 0)

    def testSetProfileOnTasklet2(self):
        # Change profile state of a non-current tasklet
        # create tasklet, set profile, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task)(expected_profile_function=tf)
        self.assertTrue(t.alive)
        self.assertIsNone(t.profile_function)
        t.profile_function = tf
        self.assertIs(t.profile_function, tf)
        self.assertIsNone(t.trace_function)
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)

    def _testSetProfileOnTasklet3(self, t, tf):
        t = stackless.tasklet(self.task2)()
        t.profile_function = tf
        t.run()
        self.assertIs(t.profile_function, tf)
        self.assertIsNone(t.trace_function)
        self.assertGreater(self.tracecount, 0)
        t.profile_function = None
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)

    def testSetProfileOnTasklet3a(self):
        # Change profile state of a non-current tasklet
        # create tasklet (soft-switched), set profile, run, clear profile, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task2)()
        self._testSetProfileOnTasklet3(t, tf)

    def testSetProfileOnTasklet3b(self):
        # Change profile state of a non-current tasklet
        # create tasklet (hard-switched), set profile, run, clear profile, run
        tf = self.nullTraceFunc
        # enforce hard switching
        t = stackless.tasklet(stackless.test_cstate)(self.task2)
        self._testSetProfileOnTasklet3(t, tf)

    def _testSetProfileOnTasklet4(self, t, tf):
        t.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        t.profile_function = tf
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)

    def testSetProfileOnTasklet4a(self):
        # Change profile state of a non-current tasklet
        # create tasklet (soft-switched), run, set profile, run
        tf = self.nullTraceFunc
        t = stackless.tasklet(self.task2)(expected_profile_function=tf)
        self._testSetProfileOnTasklet4(t, tf)

    def testSetProfileOnTasklet4b(self):
        # Change profile state of a non-current tasklet
        # create tasklet (hard-switched), run, set profile, run
        tf = self.nullTraceFunc
        # enforce hard switching
        t = stackless.tasklet(stackless.test_cstate)
        t(lambda: self.task2(expected_profile_function=tf))
        self._testSetProfileOnTasklet4(t, tf)

    def testSetTraceAndProfileOnTasklet(self):
        # test that you can set both, trace and profile
        tf = self.nullTraceFunc
        pf = self.nullTraceFunc
        t = stackless.tasklet(self.task)(expected_trace_function=tf,
                                         expected_profile_function=pf)
        self.assertTrue(t.alive)
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        t.trace_function = tf
        self.assertIs(t.trace_function, tf)
        self.assertIsNone(t.profile_function)
        t.profile_function = pf
        self.assertIs(t.trace_function, tf)
        self.assertIs(t.profile_function, pf)
        stackless.run()
        self.assertIsNone(t.trace_function)
        self.assertIsNone(t.profile_function)
        self.assertGreater(self.tracecount, 0)

    def testForeignThread(self):
        """Ensure that you can't set a trace/profile function on the current tasklet of a different thread"""
        try:
            import threading
        except ImportError:
            self.skipTest("requires threading")

        tasklet = stackless.current
        tf = self.nullTraceFunc
        ex = []

        def target():
            try:
                self.assertRaises(RuntimeError, setattr, tasklet, "trace_function", tf)
                self.assertRaises(RuntimeError, setattr, tasklet, "profile_function", tf)
                self.assertRaises(RuntimeError, setattr, tasklet, "trace_function", None)
                self.assertRaises(RuntimeError, setattr, tasklet, "profile_function", None)
            except:
                ex.appen(sys.exc_info()[1])

        t = threading.Thread(target=target)
        t.start()
        t.join()
        if ex:
            raise ex[0]


# Test tracing. A simplified version of demo/tracing.py
class mutex:
    def __init__(self, capacity=1):
        self.queue = stackless.channel()
        self.capacity = capacity

    def lock(self):
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
        currentTasklet = stackless.getcurrent()
        atomic = currentTasklet.set_atomic(True)
        try:
            if self.queue.balance < 0:
                self.queue.send(None)
            else:
                self.capacity += 1
        finally:
            currentTasklet.set_atomic(atomic)


class TestTracing(StacklessTestCase):

    def setUp(self):
        super(TestTracing, self).setUp()
        if sys.gettrace() or sys.getprofile():
            self.skipTest("Trace or profile function already active")
        self.addCleanup(sys.settrace, None)
        self.addCleanup(stackless.set_schedule_callback, None)
        self.schedule_callback_exc = []
        self.unexpected_trace_events = []
        self.m = mutex()

    def setup_seen(self, tasklets, functions):
        expected_lines = []
        for func in functions:
            lines, start = inspect.getsourcelines(func)
            expected_lines.extend(range(start, start + len(lines)))
        self.seen = {}
        for t in tasklets:
            self.seen.update(((id(t), l), 0) for l in expected_lines)

    def TraceFunc(self, frame, event, arg):
        if event == 'call' and frame.f_code.co_name in ('schedule_cb', ):
            sys.settrace(self.NullTraceFunc)
            return self.ResumeTracingTraceFunc
        tasklet = stackless.current
        # print "         TraceFunc: %s %s in %s, line %s" % \
        #  (id(tasklet), event, frame.f_code.co_name, frame.f_lineno)

        ok = True
        if event in ('call', 'line', 'return'):
            key = (id(tasklet), frame.f_lineno)
            try:
                count = self.seen[key]
            except KeyError:
                ok = False
            else:
                self.seen[key] = count + 1
        else:
            ok = False
        if not ok:
            self.unexpected_trace_events.append((tasklet, event,
                                                 frame.f_code.co_name,
                                                 frame.f_lineno))

        if event in ('call', 'line', 'exception'):
            return self.TraceFunc
        return None

    def ResumeTracingTraceFunc(self, frame, event, arg):
        if event == 'return':
            sys.settrace(self.TraceFunc)
        return None

    def NullTraceFunc(self, frame, event, arg):
        return None

    def schedule_cb(self, prev, next):
        try:
            if not next.is_main:
                tf = self.TraceFunc
            else:
                tf = None
            f = next.frame
            if next is stackless.current:
                f = f.f_back
            while f is not None:
                f.f_trace = tf
                f = f.f_back
            next.trace_function = tf
        except:
            self.schedule_callback_exc.append(sys.exc_info())

    def foo(self, name, what):
        pass

    def f(self):
        name = id(stackless.getcurrent())
        self.foo(name, "acquiring")
        self.m.lock()
        self.foo(name, "switching")
        stackless.schedule()
        self.foo(name, "releasing")
        self.m.unlock()

    def testTracing(self):
        t1 = stackless.tasklet(self.f)()
        t2 = stackless.tasklet(self.f)()
        t3 = stackless.tasklet(self.f)()

        self.setup_seen((t1, t2, t3),
                        (self.f, self.foo, mutex.lock, mutex.unlock))
        stackless.set_schedule_callback(self.schedule_cb)

        stackless.run()
        stackless.set_schedule_callback(None)

        seen = len([c for c in self.seen.values() if c > 0])
        self.assertListEqual(self.schedule_callback_exc, [])
        self.assertListEqual(self.unexpected_trace_events, [])
        # We won't see every line, because of if/else, try/except, comments, ...
        # But we should see about 80%
        self.assertGreater(float(seen) / len(self.seen), 0.75)

if __name__ == "__main__":
    # sys.argv = ['', 'Test.testName']
    unittest.main()
