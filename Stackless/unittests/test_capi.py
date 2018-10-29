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
import os
import subprocess
import glob
import pickle
import _teststackless

from support import test_main  # @UnusedImport
from support import (StacklessTestCase, withThreads, require_one_thread,
                     testcase_leaks_references)

from test.support import args_from_interpreter_flags, temp_cwd

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
        return _teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, args, **kw)

    def test_free_vars(self):
        # _teststackless.test_PyEval_EvalFrameEx can't handle code, that contains free variables.
        x = None

        def f():
            return x
        self.assertTupleEqual(f.__code__.co_freevars, ('x',))
        self.assertRaises(ValueError, _teststackless.test_PyEval_EvalFrameEx, f.__code__, f.__globals__)

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

        c = _teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (C,))
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
        result = _teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__)
        self.assertTrue(self.other_done)
        self.assertEqual(result, 666)

    def test_throw(self):
        def f():
            raise RuntimeError("must not be called")

        try:
            exc = ZeroDivisionError("test")
            _teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (), throw=exc)
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
        _thread.start_new_thread(g, (_teststackless.test_PyEval_EvalFrameEx, f.__code__, f.__globals__, (self, lock)))
        lock.acquire()
        self.assertTrue(self.thread_done)
        # return  # uncomment to skip the real test
        # and now the real smoke test: now PyEval_EvalFrameEx gets called without a main tasklet
        self.thread_done = False
        _thread.start_new_thread(_teststackless.test_PyEval_EvalFrameEx, (f.__code__, f.__globals__, (self, lock)))
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
            _thread.start_new_thread(_teststackless.test_PyEval_EvalFrameEx, (f.__code__, f.__globals__, ()), dict(throw=exc))
            time.sleep(0.1)
        finally:
            sys.stderr = saved_sys_stderr
        msg = str(iobuffer.getvalue())
        self.assertNotIn("Pending error while entering Stackless subsystem", msg)
        self.assertIn(str(exc), msg)
        #print(msg)

    def test_oldcython_frame(self):
        # A test for Stackless issue #168
        self.assertEqual(self.call_PyEval_EvalFrameEx(47110816, oldcython=True), 47110816)

    def test_oldcython_frame_code_is_1st_arg_good(self):
        # A pathological test for Stackless issue #168
        def f(code):
            return code

        def f2(code):
            return code

        self.assertIs(_teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (f.__code__,), oldcython=False), f.__code__)
        self.assertIs(_teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (f2.__code__,), oldcython=True), f2.__code__)

    @testcase_leaks_references("f->f_code get overwritten without Py_DECREF")
    def test_oldcython_frame_code_is_1st_arg_bad(self):
        # A pathological test for Stackless issue #168
        def f(code):
            return code

        # we can't fix this particular case:
        #  - running code object is its 1st arg and
        #  - oldcython=True,
        # because a fix would result in a segmentation fault, if the number of
        # arguments is to low (test case test_0_args)
        self.assertRaises(UnboundLocalError, _teststackless.test_PyEval_EvalFrameEx, f.__code__, f.__globals__, (f.__code__,), oldcython=True)

    def test_other_code_object(self):
        # A pathological test for Stackless issue #168
        def f(arg):
            return arg

        def f2(arg):
            return arg

        self.assertIs(_teststackless.test_PyEval_EvalFrameEx(f.__code__, f.__globals__, (f2,), code2=f2.__code__), f2)


OBJ1 = object()
OBJ2 = object()


class Test_SoftSwitchableExtensionFunctions(StacklessTestCase):
    # Tests for PyStackless_CallFunction()
    def _test_func(self, func):

        is_soft = stackless.enable_softswitch(None)
        self.tasklet_result = None

        def task(action, retval):
            r = func(action, retval)
            self.tasklet_result = r

        t = stackless.tasklet(task)(2, OBJ1)
        t.remove()
        t.run()
        self.assertTrue(t.alive)
        self.assertFalse(t.scheduled)
        self.assertEqual(t.restorable, is_soft)
        self.assertIs(t.tempval, OBJ1)
        # is_soft = False
        if is_soft:
            p1 = pickle.dumps(t)
        t.tempval = OBJ2
        t.insert()
        stackless.run()
        self.assertTrue(t.alive)
        self.assertFalse(t.scheduled)
        self.assertIs(t.tempval, OBJ2)
        if is_soft:
            p2 = pickle.dumps(t)

        t.insert()
        stackless.run()
        self.assertEqual(self.tasklet_result, 2)
        self.assertFalse(t.alive)

        # the same for action == 1, schedule, but don't remove
        self.tasklet_result = None
        t = stackless.tasklet(task)(1, OBJ1)
        stackless.run()
        self.assertEqual(self.tasklet_result, 2)
        self.assertFalse(t.alive)

        # the same for action == 0, don't schedule
        self.tasklet_result = None
        t = stackless.tasklet(task)(0, OBJ1)
        stackless.run()
        self.assertEqual(self.tasklet_result, 2)
        self.assertFalse(t.alive)

        if not is_soft:
            return

        # the same for p2
        self.tasklet_result = None
        t = pickle.loads(p2)
        self.assertTrue(t.alive)
        t.insert()
        stackless.run()
        self.assertEqual(self.tasklet_result, 2)
        self.assertFalse(t.alive)

        # the same for p1. It is expected to raise
        self.tasklet_result = None
        t = pickle.loads(p1)
        self.assertTrue(t.alive)
        t.insert()
        self.assertRaisesRegex(RuntimeError, 'execute_soft_switchable_func', stackless.run)
        self.assertEqual(self.tasklet_result, None)
        self.assertFalse(t.alive)

    def test_softswitchablemethod(self):
        f = _teststackless.SoftSwitchableDemo().demo
        self._test_func(f)

    def test_softswitchablefunc(self):
        f = _teststackless.softswitchablefunc
        self._test_func(f)


class TestDemoExtensions(unittest.TestCase):
    def setUp(self):
        super(TestDemoExtensions, self).setUp()
        demo_dir = os.path.normpath(os.path.join(os.path.abspath(__file__),
                                                 os.pardir, os.pardir, "demo",
                                                 self.id().rpartition('.')[2].partition('_')[2]))
        if not os.path.isdir(demo_dir):
            self.skipTest('Not a directory: ' + demo_dir)
        self.demo_dir = demo_dir
        self.verbose = True

    def _test_soft_switchable_extension(self):
        with temp_cwd(self.id()) as tmpdir:
            args = [sys.executable]
            args.extend(args_from_interpreter_flags())
            args.extend(["setup.py", "build", "-b", tmpdir, "install_lib", "-d", tmpdir, "--no-compile"])
            try:
                output = subprocess.check_output(args, stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT, cwd=self.demo_dir)
            except subprocess.CalledProcessError as e:
                if e.stdout:
                    sys.stdout.buffer.write(e.stdout)
                if e.stderr:
                    sys.stderr.buffer.write(e.stderr)
                raise
            if self.verbose:
                sys.stdout.buffer.write(output)
            py = glob.glob("*.py")
            self.assertEqual(len(py), 1)
            py = py[0]

            args = [sys.executable]
            args.extend(args_from_interpreter_flags())
            args.extend([py, '-v'])
            try:
                output = subprocess.check_output(args, stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                if e.stdout:
                    sys.stdout.buffer.write(e.stdout)
                if e.stderr:
                    sys.stderr.buffer.write(e.stderr)
                raise
            if self.verbose:
                sys.stdout.buffer.write(output)


if __name__ == "__main__":
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
