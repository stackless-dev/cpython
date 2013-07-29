#
# -*- coding: utf-8 -*-
#
#  Copyright 2013 science + computing ag
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
   
import unittest
import stackless

    
def _TestCallNestingLevel_callee(self, testcase, expected_nesting_level):
    tasklet = stackless.getcurrent()
    testcase.assertEqual(tasklet.nesting_level, expected_nesting_level)

class TestCallNestingLevel(unittest.TestCase):
    
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

class TestStacklessCallFlag(unittest.TestCase):
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
            __call__ = stackless._test_nostacklesscall
        class SubClassNoSlpCallable(SuperClassNoSlpCallable):
            pass
        
        self.SuperClass = SuperClass
        self.SubClass = SubClass
        self.SuperClassCallable = SuperClassCallable
        self.SubClassCallable = SubClassCallable
        self.SuperClassNoSlpCallable = SuperClassNoSlpCallable
        self.SubClassNoSlpCallable = SubClassNoSlpCallable
        
    def testInitialFlags(self):
        self.assertNotStacklessCall(type(stackless._test_nostacklesscall))

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
        self.SuperClassCallable.__call__ = stackless._test_nostacklesscall

        self.assertStacklessCall(self.SuperClassCallable)
        self.assertStacklessCall(self.SubClassCallable)
        
    def testDelCallable(self):        
        del self.SuperClassCallable.__call__

        self.assertNotStacklessCall(self.SuperClassCallable)
        self.assertNotStacklessCall(self.SubClassCallable)

class TestNonStacklessCall(unittest.TestCase):
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
        self.post_callback_called = False
        self.callback_called = False
        class A(object):
            def __call__(self):
                pass
        self.A = A
        a = A()
        class B(object):
            __call__ = a
        A.__call__ = stackless._test_nostacklesscall
        self.B = B
        
    def callDirect(self):
        if not stackless.enable_softswitch(None):
            # without soft switching, we always get an increased nesting level
            return
        self.assertEqual(stackless.getcurrent().nesting_level, 0)
        
        a=self.A()
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

        b=self.B()
        result = b(self.callback, 4711, post_callback=self.post_callback)
        
        self.assertTrue(self.callback_called)
        self.assertTrue(self.post_callback_called)
        self.assertEqual(result, 4711)

    def testCallIndirect(self):
        stackless.tasklet(self.callIndirect)()
        stackless.run()

if __name__ == "__main__":
    import sys
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
