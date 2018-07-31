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
import stackless
import sys
import unittest

from support import test_main  # @UnusedImport
from support import StacklessTestCase


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


if __name__ == "__main__":
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
