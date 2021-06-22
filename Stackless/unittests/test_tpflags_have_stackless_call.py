#
# -*- coding: utf-8 -*-
#
#  Copyright 2012 science + computing ag
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

"""
Test the stackless nesting level of instance calls

This is a test for issue http://www.stackless.com/ticket/18
"""

from __future__ import absolute_import
import unittest
import stackless
import gc
import sys
from support import test_main  # @UnusedImport
from support import StacklessTestCase


def _TestCallNestingLevel_callee(self, testcase, expected_nesting_level):
    tasklet = stackless.getcurrent()
    testcase.assertEqual(tasklet.nesting_level, expected_nesting_level)


class TestCallNestingLevel(StacklessTestCase):

    class SyntheticCallable(object):
        pass
    SyntheticCallable.__call__ = _TestCallNestingLevel_callee

    class ClassicSyntheticCallable:
        pass
    ClassicSyntheticCallable.__call__ = _TestCallNestingLevel_callee

    class PlainCallable(object):
        __call__ = _TestCallNestingLevel_callee

    def do_test(self, CallableClass, methodCall):
        if not stackless.enable_softswitch(None):
            # without soft switching, we always get an increased nesting level
            return

        tasklet = stackless.getcurrent()
        current_nesting_level = tasklet.nesting_level
        self.assertEqual(current_nesting_level, 0, msg="Precondition not met: nesting level is %d" % (current_nesting_level,))

        # create the callable object
        callable_object = CallableClass()
        self.assertTrue(callable(callable_object))

        if methodCall:
            # invoke the method __call__. This works fine
            callable_object.__call__(self, current_nesting_level)
        else:
            # call the callable object via __call__
            callable_object(self, current_nesting_level)

    def test_plainCallable_methodCall(self):
        stackless.tasklet(self.do_test)(self.PlainCallable, True)
        stackless.run()

    def test_plainCallable_instanceCall(self):
        stackless.tasklet(self.do_test)(self.PlainCallable, False)
        stackless.run()

    def test_syntheticCallable_methodCall(self):
        stackless.tasklet(self.do_test)(self.SyntheticCallable, True)
        stackless.run()

    def test_syntheticCallable_instanceCall(self):
        stackless.tasklet(self.do_test)(self.SyntheticCallable, False)
        stackless.run()

    def test_classicSyntheticCallable_methodCall(self):
        stackless.tasklet(self.do_test)(self.ClassicSyntheticCallable, True)
        stackless.run()

    def test_classicSyntheticCallable_instanceCall(self):
        stackless.tasklet(self.do_test)(self.ClassicSyntheticCallable, False)
        stackless.run()


class TestStacklessCallFlag(StacklessTestCase):
    """Test if the TPFLAGS_HAVE_STACKLESS_CALL has the expected value"""
    TPFLAGS_HAVE_STACKLESS_CALL = 1 << 15

    def assertStacklessCall(self, cls):
        self.assertTrue(cls.__flags__ & self.TPFLAGS_HAVE_STACKLESS_CALL, "%r lacks TPFLAGS_HAVE_STACKLESS_CALL" % (cls,))

    def assertNotStacklessCall(self, cls):
        self.assertFalse(cls.__flags__ & self.TPFLAGS_HAVE_STACKLESS_CALL, "%r has TPFLAGS_HAVE_STACKLESS_CALL" % (cls,))

    def setUp(self):
        super(TestStacklessCallFlag, self).setUp()

        class SuperClass(object):
            pass

        class SubClass(SuperClass):
            pass

        class SuperClassCallable(object):

            def __call__(self):
                pass

        class SubClassCallable(SuperClassCallable):
            pass

        class SuperClassNoSlpCallable(object):
            __call__ = stackless._stackless._test_nostacklesscall

        class SubClassNoSlpCallable(SuperClassNoSlpCallable):
            pass

        self.SuperClass = SuperClass
        self.SubClass = SubClass
        self.SuperClassCallable = SuperClassCallable
        self.SubClassCallable = SubClassCallable
        self.SuperClassNoSlpCallable = SuperClassNoSlpCallable
        self.SubClassNoSlpCallable = SubClassNoSlpCallable

    def testInitialFlags(self):
        self.assertNotStacklessCall(type(stackless._stackless._test_nostacklesscall))

        self.assertNotStacklessCall(self.SuperClass)
        self.assertNotStacklessCall(self.SubClass)
        self.assertStacklessCall(self.SuperClassCallable)
        self.assertStacklessCall(self.SubClassCallable)
        self.assertStacklessCall(self.SuperClassNoSlpCallable)
        self.assertStacklessCall(self.SubClassNoSlpCallable)

    def testMakeCallable(self):
        def __call__(s):
            pass

        self.SuperClass.__call__ = __call__

        self.assertStacklessCall(self.SuperClass)
        self.assertStacklessCall(self.SubClass)

    def testMakeCallable2(self):
        def __call__(s):
            pass

        self.SuperClassNoSlpCallable.__call__ = __call__

        self.assertStacklessCall(self.SuperClassNoSlpCallable)
        self.assertStacklessCall(self.SubClassNoSlpCallable)

    def testMakeNotSlpCallable(self):
        self.SuperClassCallable.__call__ = stackless._stackless._test_nostacklesscall

        self.assertStacklessCall(self.SuperClassCallable)
        self.assertStacklessCall(self.SubClassCallable)

    def testDelCallable(self):
        del self.SuperClassCallable.__call__

        self.assertNotStacklessCall(self.SuperClassCallable)
        self.assertNotStacklessCall(self.SubClassCallable)


class TestNonStacklessCall(StacklessTestCase):
    """Test calls of a type, that does not support the stackless call protocol"""
    TPFLAGS_HAVE_STACKLESS_CALL = 1 << 15

    def post_callback(self, result, iStackless, iSlp_try_stackless, **kw):
        self.assertEqual(iStackless, 0)
        self.assertEqual(result, 4711)
        self.post_callback_called = True
        return result

    def callback(self, arg, **kw):
        self.callback_called = True
        return arg

    def setUp(self):
        super(TestNonStacklessCall, self).setUp()
        self.post_callback_called = False
        self.callback_called = False

        class A(object):

            def __call__(self):
                pass
        self.A = A
        a = A()

        class B(object):
            __call__ = a
        A.__call__ = stackless._stackless._test_nostacklesscall
        self.B = B

    def callDirect(self):
        if not stackless.enable_softswitch(None):
            # without soft switching, we always get an increased nesting level
            return
        self.assertEqual(stackless.getcurrent().nesting_level, 0)

        a = self.A()
        result = a(self.callback, 4711, post_callback=self.post_callback)

        self.assertTrue(self.callback_called)
        self.assertTrue(self.post_callback_called)
        self.assertEqual(result, 4711)

    def testCallDirect(self):
        stackless.tasklet(self.callDirect)()
        stackless.run()

    def callIndirect(self):
        if not stackless.enable_softswitch(None):
            # without soft switching, we always get an increased nesting level
            return
        self.assertEqual(stackless.getcurrent().nesting_level, 0)

        b = self.B()
        result = b(self.callback, 4711, post_callback=self.post_callback)

        self.assertTrue(self.callback_called)
        self.assertTrue(self.post_callback_called)
        self.assertEqual(result, 4711)

    def testCallIndirect(self):
        stackless.tasklet(self.callIndirect)()
        stackless.run()


class TestInitIsStackless(StacklessTestCase):
    """Test, if __init__ supports the stackless protocoll"""
    def nestingLevelTest(self, cls):
        # check the nesting level and the ref counts
        def func(obj=None):
            if obj is None:
                self.assertGreater(stackless.current.nesting_level, 0)
                return None
            self.assertIs(obj.__class__, cls)
            obj.nesting_level = stackless.current.nesting_level
            return None

        cls(func).__class__  # the first access of __class__ releases 1 ref to None
        with stackless.atomic():
            # None-refcount needs protection, it the test suite is multithreaded
            gc.collect()
            rc_none = rc_none2 = sys.getrefcount(None)
            rc_cls = rc_cls2 = sys.getrefcount(cls)
            c = cls(func)
            rc_none2 = sys.getrefcount(None)
            rc_cls2 = sys.getrefcount(cls)
        self.assertEqual(rc_none, rc_none2)
        self.assertEqual(rc_cls + 1, rc_cls2)  # one ref for c
        self.assertEqual(sys.getrefcount(c) - sys.getrefcount(object()), 1)
        self.assertIs(c.__class__, cls)
        current_nesting_level = stackless.current.nesting_level
        if hasattr(c, "nesting_level"):
            if stackless.enable_softswitch(None):
                self.assertEqual(c.nesting_level, current_nesting_level)
            else:
                self.assertGreater(c.nesting_level, current_nesting_level)
        c = None
        gc.collect()
        self.assertEqual(sys.getrefcount(cls), rc_cls)

    def C__init__(self, func):
        return func(self)

    class C:
        def __init__(self, func):
            return func(self)

    class CSyn:
        pass
    CSyn.__init__ = C__init__

    class CNew(object):
        def __init__(self, func):
            return func(self)

    class CNewSyn(object):
        pass
    CNewSyn.__init__ = C__init__

    class CNonStackless:
        __init__ = stackless._stackless._test_nostacklesscall

    class CNewNonStackless(object):
        __init__ = stackless._stackless._test_nostacklesscall

    def testNestingLevelNew(self):
        self.nestingLevelTest(self.CNew)

    def testNestingLevelNewSynthetic(self):
        self.nestingLevelTest(self.CNewSyn)

    def testNestingLevel(self):
        self.nestingLevelTest(self.C)

    def testNestingLevelSynthetic(self):
        self.nestingLevelTest(self.CSyn)

    def testNestingLevelNonStackless(self):
        self.nestingLevelTest(self.CNonStackless)

    def testNestingLevelNewNonStackless(self):
        self.nestingLevelTest(self.CNewNonStackless)

    def returnTypeTest(self, cls):
        # check the nesting level and the ref counts
        def func(obj):
            return "not None"

        def helper():
            # we need a close control over references to None here
            try:
                cls(func)
            except TypeError:
                emsg = sys.exc_info()[1].args[0]
                return emsg
            self.fail("does not raise the expected exception")

        helper()  # warm up.
        gc.collect()
        with stackless.atomic():
            rc_none = sys.getrefcount(None)
            rc_cls = sys.getrefcount(cls)
            # we need a close control over references to None here
            emsg = helper()
            rc_none2 = sys.getrefcount(None)
            rc_cls2 = sys.getrefcount(cls)
        self.assertEqual(rc_cls, rc_cls2)
        self.assertEqual(rc_none, rc_none2)
        self.assertIn("__init__() should return None", emsg)

    def testReturnType(self):
        self.returnTypeTest(self.C)

    def testReturnTypeSynthetic(self):
        self.returnTypeTest(self.CSyn)

    def testReturnTypeNew(self):
        self.returnTypeTest(self.CNew)

    def testReturnTypeNewSynthetic(self):
        self.returnTypeTest(self.CNewSyn)

    def exceptionTest(self, cls):
        # check the nesting level and the ref counts
        def func(obj):
            1 / 0

        def helper():
            # we need a close control over references to None here
            try:
                cls(func)
            except ZeroDivisionError:
                emsg = sys.exc_info()[1].args[0]
                return emsg
            self.fail("does not raise the expected exception")

        emsg = ''
        helper()  # warm up
        gc.collect()
        with stackless.atomic():
            rc_none = sys.getrefcount(None)
            rc_cls = sys.getrefcount(cls)
            # we need a close control over references to None here
            emsg = helper()
            rc_none2 = sys.getrefcount(None)
            rc_cls2 = sys.getrefcount(cls)
        self.assertEqual(rc_cls, rc_cls2)
        self.assertEqual(rc_none, rc_none2)
        self.assertIn("division by zero", emsg)

    def testException(self):
        self.exceptionTest(self.C)

    def testExceptionSynthetic(self):
        self.exceptionTest(self.CSyn)

    def testExceptionNew(self):
        self.exceptionTest(self.CNew)

    def testExceptionNewSynthetic(self):
        self.exceptionTest(self.CNewSyn)

if __name__ == "__main__":
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
