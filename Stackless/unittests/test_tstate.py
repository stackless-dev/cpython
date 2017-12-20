from __future__ import absolute_import
import unittest
import sys
from pickle import Pickler, Unpickler
from io import BytesIO
from stackless import *

from support import test_main  # @UnusedImport
from support import StacklessTestCase, get_reduce_frame

# import os
# def debug():
#     if os.environ.get('PYDEV_COMPLETER_PYTHONPATH') and os.environ.get('PYDEV_COMPLETER_PYTHONPATH') not in sys.path:
#         sys.path.append(os.environ.get('PYDEV_COMPLETER_PYTHONPATH'))
#     from pydevd import settrace
#     settrace()
#
# def realframe(self):
#     """Get the real frame of the tasklet"""
#     r = self.__reduce__()
#     frames = r[2][3]
#     if frames:
#         return frames[-1]
#     return None
# tasklet.realframe = property(realframe)


#test that thread state is restored properly
class TestExceptionState(StacklessTestCase):
    def Tasklet(self):
        try:
            1 / 0
        except Exception:
            ei = sys.exc_info()
            self.assertEqual(ei[0], ZeroDivisionError)
            schedule()
            self.ran = True
            ei = sys.exc_info()
            self.assertEqual(ei[0], ZeroDivisionError)
            self.assertTrue("by zero" in str(ei[1]))

    def testExceptionState(self):
        t = tasklet(self.Tasklet)
        t()
        t.run()
        ei = sys.exc_info()
        self.assertEqual(ei, (None,) * 3)
        t.run()
        ei = sys.exc_info()
        self.assertEqual(ei, (None,) * 3)

    def testExceptionStatePickled(self):
        if not enable_softswitch(None):
            return
        t = tasklet(self.Tasklet)
        t()
        t.run()
        io = BytesIO()
        p = Pickler(io, -1)
        p.persistent_id = self.persistent_id
        p.dump(t)
        t.remove()
        t.bind(None)
        p = Unpickler(BytesIO(io.getvalue()))
        p.persistent_load = self.persistent_load
        t = p.load()
        p = None
        io = None
        self.ran = False
        t.run()
        self.assertTrue(self.ran)
        ei = sys.exc_info()
        self.assertEqual(ei, (None,) * 3)

    def persistent_id(self, obj):
        if obj is self:
            return 'self'
        return None

    def persistent_load(self, id):
        if id == 'self':
            return self
        raise ValueError('no id: ' + repr(id))


class TestTracingState(StacklessTestCase):
    def setUp(self):
        super(TestTracingState, self).setUp()
        self.trace = []

    def Callback(self, *args):
        self.trace.append(args)

    def foo(self):
        pass

    def persistent_id(self, obj):
        if obj is self:
            return 'self'
        return None

    def persistent_load(self, id):
        if id == 'self':
            return self
        raise ValueError('no id: ' + repr(id))

    def Tasklet(self, do_profile=False, do_trace=False, disable_trace_after_schedule=False):
        if do_profile:
            sys.setprofile(self.Callback)
            self.addCleanup(sys.setprofile, None)
        if do_trace:
            sys.settrace(self.Callback)
            self.addCleanup(sys.settrace, None)

        self.foo()
        n = len(self.trace)
        self.foo()
        n2 = len(self.trace)
        if do_profile or do_trace:
            self.assertGreater(n2, n)
        else:
            self.assertEqual(n2, n)

        schedule()

        self.foo()
        n = len(self.trace)
        self.foo()
        n2 = len(self.trace)
        if (do_profile or do_trace) and not disable_trace_after_schedule:
            self.assertGreater(n2, n)
        else:
            self.assertEqual(n2, n)

    def _testTracingOrProfileState(self, do_pickle=False, **kw):
        t = tasklet(self.Tasklet)
        t(**kw)
        t.run()

        self.foo()
        n = len(self.trace)
        self.foo()
        n2 = len(self.trace)
        self.assertEqual(n, n2)

        if do_pickle:
            io = BytesIO()
            p = Pickler(io, -1)
            p.persistent_id = self.persistent_id
            p.dump(t)
            t.remove()
            t.bind(None)
            p = Unpickler(BytesIO(io.getvalue()))
            p.persistent_load = self.persistent_load
            t = p.load()
            p = None
            io = None

        t.run()

        self.foo()
        n = len(self.trace)
        self.foo()
        n2 = len(self.trace)
        self.assertEqual(n, n2)


    # No tracing/profiling in main, tracing/profiling in tasklet
    def testNormalState(self):
        self._testTracingOrProfileState()

    @unittest.skipIf(sys.gettrace(), 'test manipulates debugger hooks')
    def testTracingState(self):
        self._testTracingOrProfileState(do_trace=True)

    def testProfileState(self):
        self._testTracingOrProfileState(do_profile=True)

    # Same as above, with pickling the state
    def testUnpickledState(self):
        if not enable_softswitch(None):
            return
        self._testTracingOrProfileState(do_pickle=True)

    def testUnpickledTracingState(self):
        if not enable_softswitch(None):
            return
        self._testTracingOrProfileState(do_pickle=True, do_trace=True,
                                        disable_trace_after_schedule=True)

    def testUnpickledTracingState2(self):
        if not enable_softswitch(None):
            return
        saved = stackless.pickle_with_tracing_state
        stackless.pickle_with_tracing_state = True
        try:
            self._testTracingOrProfileState(do_pickle=True, do_trace=True)
        finally:
            stackless.pickle_with_tracing_state = saved

    @unittest.skip('Not restorable')
    def testUnpickledProfileState(self):
        if not enable_softswitch(None):
            return
        self._testTracingOrProfileState(do_pickle=True, do_profile=True)


    # Test cases with traced main thread
    def TraceFunc(self, frame, event, arg):
        # print "TraceFunc: %s in %s %s, line %s" % (event, frame.f_code.co_name, frame.f_code.co_filename, frame.f_lineno)
        self.trace.append(('trace', frame.f_code.co_name, frame.f_lineno, event, arg))
        if event in ('call', 'line', 'exception') and len(self.trace) < 100:
            return self.TraceFunc
        return None

    def mark(self, text):
        f = sys._getframe(1)
        self.trace.append(('mark', f.f_code.co_name, text))

    def Tasklet2(self):
        self.mark('start')
        self.foo()
        self.mark('before schedule')
        schedule()
        self.mark('after schedule')
        self.foo()
        self.mark('end schedule')

    def callRun(self):
        self.mark('before run')
        run()
        self.mark('after run')

    @unittest.skipIf(sys.gettrace(), 'test manipulates debugger hooks')
    def testTracedMainNormalTasklet(self):
        # make sure, tracing is off.
        self.assertIsNone(sys._getframe(0).f_trace)

        # enable tracing
        self.addCleanup(sys.settrace, None)
        sys.settrace(self.TraceFunc)
        self.callRun()
        sys.settrace(None)
        baseline = self.trace[:]
        del self.trace[:]

        tasklet(self.Tasklet2)()
        sys.settrace(self.TraceFunc)
        self.callRun()
        sys.settrace(None)

        self.assertTrue(set(baseline).issubset(self.trace))
        trace_in_tasklet = [i for i in self.trace if i not in baseline]

        self.maxDiff = None
        expected = [('mark', self.Tasklet2.__name__, i) for i in
                    ('start', 'before schedule', 'after schedule', 'end schedule')]
        self.assertEqual(trace_in_tasklet, expected)

    # Test the combination of Exception and Trace State

    def Tasklet3(self):
        try:
            1 / 0
        except Exception:
            sys.settrace(self.Callback)
            self.addCleanup(sys.settrace, None)

            self.foo()
            n = len(self.trace)
            self.foo()
            n2 = len(self.trace)
            ei = sys.exc_info()

            self.assertEqual(ei[0], ZeroDivisionError)
            self.assertGreater(n2, n)

            schedule()

            ei = sys.exc_info()
            self.foo()
            n = len(self.trace)
            self.foo()
            n2 = len(self.trace)

            self.assertGreater(n2, n)
            self.assertEqual(ei[0], ZeroDivisionError)
            self.assertTrue("by zero" in str(ei[1]))

    def testExceptionAndTraceState(self):
        t = tasklet(self.Tasklet3)
        t()
        t.run()
        self.assertGreater(len(self.trace), 0)
        ei = sys.exc_info()
        self.assertEqual(ei, (None,) * 3)
        t.run()
        ei = sys.exc_info()
        self.assertEqual(ei, (None,) * 3)

    # test the reduce methods of frames and tasklet
    # regarding the trace handling

    def testReduceOfTracingState(self):
        t = tasklet(self.Tasklet)
        t(do_trace=True)
        t.run()

        frame = t.frame
        frame.f_trace = self.Callback
        self.assertIsNotNone(frame.f_trace)

        saved = stackless.pickle_with_tracing_state
        stackless.pickle_with_tracing_state = False
        import _stackless
        try:
            reduced_tasklet1 = t.__reduce__()
            reduced_frame1 = _stackless._wrap.frame.__reduce__(frame)
            stackless.pickle_with_tracing_state = True
            reduced_tasklet2 = t.__reduce__()
            reduced_frame2 = _stackless._wrap.frame.__reduce__(frame)
        finally:
            stackless.pickle_with_tracing_state = saved

        t.kill()

        # test the f_trace member
        self.assertIsNone(reduced_frame1[2][6])
        self.assertIs(reduced_frame2[2][6], frame.f_trace)

        if not enable_softswitch(None):
            return

        # test if the tracing cframe is present / not present
        reduce_frame = get_reduce_frame()
        self.assertListEqual(reduced_tasklet1[2][3], [reduce_frame(frame)])
        self.assertEqual(len(reduced_tasklet2[2][3]), 2)
        self.assertIs(reduced_tasklet2[2][3][0], reduce_frame(frame))
        self.assertIsInstance(reduced_tasklet2[2][3][1], stackless.cframe)

        cf = reduced_tasklet2[2][3][1]
        reduced_cframe = cf.__reduce__()
        self.assertEqual(reduced_cframe[2][1], 'slp_restore_tracing')
        self.assertIs(reduced_cframe[2][2][1].__func__, self.Callback.__func__)

if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
