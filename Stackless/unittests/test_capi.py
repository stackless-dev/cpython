#
# -*- coding: utf-8 -*-
#
#  Copyright 2018 science + computing ag
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#  limitations under the License.

"""Test the (Stackless)-Python C-API

Tests not relevant for pure Python code.
"""

from __future__ import print_function, absolute_import, division

import inspect
import io
import stackless
import sys
import time
import traceback
import unittest

from support import test_main  # @UnusedImport
from support import StacklessTestCase, withThreads, require_one_thread

if withThreads:
    try:
        import thread as _thread
    except ImportError:
        import _thread
else:
    _thread = None


class Test_PyEval_EvalFrameEx(StacklessTestCase):
    @staticmethod
    def function(arg):
        return arg

    def call_PyEval_EvalFrameEx(self, *args, **kw):
        f = self.function
        if inspect.ismethod(f):
            f = f.__func__
        return stackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, args, **kw)

    def test_free_vars(self):
        # stackless.test_PyEval_EvalFrameEx can't handle code, that contains free variables.
        x = None

        def f():
            return x
        self.assertTupleEqual(f.__code__.co_freevars, ('x',))
        self.assertRaises(ValueError, stackless.test_PyEval_EvalFrameEx, f.__code__, f.__globals__)

    def test_0_args(self):
        self.assertRaises(UnboundLocalError, self.call_PyEval_EvalFrameEx)

    def test_1_args(self):
        self.assertEqual(self.call_PyEval_EvalFrameEx(47110815), 47110815)

    def test_2_args(self):
        self.assertRaises(ValueError, self.call_PyEval_EvalFrameEx, 4711, None)

    def test_cstack_spilling(self):
        # Force stack spilling. 16384 is the value of CSTACK_WATERMARK from slp_platformselect.h
        self.call_PyEval_EvalFrameEx(None, alloca=16384 * 8)

    def test_stack_unwinding(self):
        # Calling the __init__ method of a new-style class involves stack unwinding
        class C(object):
            def __init__(self):
                self.initialized = True

        def f(C):
            return C()

        c = stackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (C,))
        self.assertIsInstance(c, C)
        self.assertTrue(c.initialized)

    def test_tasklet_switch(self):
        # test a tasklet switch out of the running frame
        self.other_done = False

        def other_tasklet():
            self.other_done = True

        def f():
            stackless.run()
            return 666

        stackless.tasklet(other_tasklet)()
        result = stackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__)
        self.assertTrue(self.other_done)
        self.assertEqual(result, 666)

    def test_throw(self):
        def f():
            raise RuntimeError("must not be called")

        try:
            exc = ZeroDivisionError("test")
            stackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (), throw=exc)
        except ZeroDivisionError:
            e, tb = sys.exc_info()[1:]
            self.assertIs(e, exc)
            self.assertEqual(traceback.extract_tb(tb)[-1][2], f.__name__)
        else:
            self.fail("expected exception")

    @unittest.skipUnless(withThreads, "test requires threading")
    def test_no_main_tasklet(self):
        def f(self, lock):
            self.thread_done = True
            lock.release()

        def g(func, *args, **kw):
            return func(*args, **kw)

        lock = _thread.allocate_lock()

        # first a dry run to check the threading logic
        lock.acquire()
        self.thread_done = False
        _thread.start_new_thread(g, (stackless.test_PyEval_EvalFrameEx, f.__code__, f.__globals__, (self, lock)))
        lock.acquire()
        self.assertTrue(self.thread_done)
        # return  # uncomment to skip the real test
        # and now the real smoke test: now PyEval_EvalFrameEx gets called without a main tasklet
        self.thread_done = False
        _thread.start_new_thread(stackless.test_PyEval_EvalFrameEx, (f.__code__, f.__globals__, (self, lock)))
        lock.acquire()
        self.assertTrue(self.thread_done)

    @unittest.skipUnless(withThreads, "test requires threading")
    @require_one_thread  # we fiddle with sys.stderr
    def test_throw_no_main_tasklet(self):
        def f():
            raise RuntimeError("must not be called")

        exc = ZeroDivisionError("test")
        saved_sys_stderr = sys.stderr
        sys.stderr = iobuffer = io.StringIO()
        try:
            _thread.start_new_thread(stackless.test_PyEval_EvalFrameEx, (f.__code__, f.__globals__, ()), dict(throw=exc))
            time.sleep(0.1)
        finally:
            sys.stderr = saved_sys_stderr
        msg = str(iobuffer.getvalue())
        self.assertNotIn("Pending error while entering Stackless subsystem", msg)
        self.assertIn(str(exc), msg)
        #print(msg)


if __name__ == "__main__":
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
