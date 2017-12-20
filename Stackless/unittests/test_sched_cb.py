from __future__ import absolute_import
import unittest
import stackless
import gc
import traceback
import sys

from support import test_main  # @UnusedImport
from support import StacklessTestCase


class SchedulingMonitor:
    "A scheduling monitor acting as a callback for set_schedule_callback()."

    def __init__(self):
        self.count = 0

    def __call__(self, prevTasklet, nextTasklet):
        self.count += 1


class SchedulingCallbackTestCase(StacklessTestCase):
    "A collection of scheduling callback tests."

    def setUp(self):
        super(SchedulingCallbackTestCase, self).setUp()
        self.addCleanup(stackless.set_schedule_callback, None)
        gc.collect()  # to avoid unexpected GC

    def test0(self):
        "Test callbacks before and after running isolated monitor."

        mon1 = SchedulingMonitor()
        stackless.set_schedule_callback(mon1)
        stackless.tasklet(stackless.test_cframe)(3)
        stackless.tasklet(stackless.test_cframe)(3)
        # precondition
        self.assertEqual(mon1.count, 0,
                         "No callbacks before running")
        # running
        stackless.run()
        # postcondition
        self.assertGreaterEqual(
            mon1.count, 2 * 3,
            "At least as may callbacks as many test_cframe calls")

    def test1(self):
        "Test multiple monitors, from test/test_set_schedule_callback.py"

        fu = self.assertTrue
        n = 2

        mon1 = SchedulingMonitor()
        stackless.set_schedule_callback(mon1)
        v = 3
        for i in range(n):
            stackless.tasklet(stackless.test_cframe)(v)
        c1 = mon1.count
        fu(c1 == 0, "No callbacks before running")
        stackless.run()
        c1 = mon1.count
        fu(c1 >= n * v, "At least as may callbacks as many test_cframe calls")

        mon2 = SchedulingMonitor()
        stackless.set_schedule_callback(mon2)
        v = 5
        for i in range(n):
            stackless.tasklet(stackless.test_cframe)(v)
        stackless.run()
        c2 = mon2.count
        fu(c2 >= n * v, "At least as may callbacks as many test_cframe calls")
        fu(mon1.count == c1, "No calls to previous callback")
        fu(c2 > c1, "More test_cframe calls => more callbacks on second run")

        stackless.set_schedule_callback(None)
        v = 7
        for i in range(n):
            stackless.tasklet(stackless.test_cframe)(v)
        stackless.run()
        c1p = mon1.count
        c2p = mon2.count
        fu(c1p == c1, "No calls to previous callback after setting it to None")
        fu(c2p == c2, "No calls to previous callback after setting it to None")

    def test2currentWithPrevAndNext(self):
        class Ex_Main(Exception):
            name = 'call_run'

        class Ex_A(Exception):
            name = 'task_a'

        def call_run():
            try:
                raise Ex_Main()
            except Exception:
                stackless.run()

        def task_a():
            try:
                raise Ex_A()
            except Exception:
                stackless.schedule()

        tasklet_a = stackless.tasklet(task_a)()
        tasklet_main = stackless.current
        # print "Tasklet a: %r" % (tasklet_a,)
        # print "Tasklet main: %r" % (tasklet_main,)

        switches = []

        def cb(prev, next):
            # find the real current according to the information in the frame stack
            ex = sys.exc_info()
            f = sys._getframe()
            current = stackless.current

            while f is not None:
                if f.f_code in (call_run.__code__, task_a.__code__):
                    name = f.f_code.co_name
                    break
                f = f.f_back
            else:
                # Can't deduce the current tasklet from the stack
                name = None
                # print "Unexpected stack: current %r, prev %r, next %r" % (current, prev, next)
                # traceback.print_stack(file=sys.stdout)

            if current is tasklet_main:
                current_name = 'call_run'
            elif current is tasklet_a:
                current_name = 'task_a'
            else:
                print("Unexpected tasklet: current %r, prev %r, next %r" % (current, prev, next))
                traceback.print_stack(file=sys.stdout)
                current_name = None

            switches.append((name, ex[0], current_name))

        stackless.set_schedule_callback(cb)
        call_run()
        stackless.set_schedule_callback(None)
        for real_name, ex, current_name in switches:
            if real_name:
                self.assertEqual(real_name, current_name)
            if ex:
                self.assertEqual(ex.name, current_name)

    def testGetCallback(self):
        mon = SchedulingMonitor()
        self.assertIsNone(stackless.get_schedule_callback())
        old = stackless.set_schedule_callback(mon)
        self.assertIsNone(old)
        self.assertIs(stackless.get_schedule_callback(), mon)
        old = stackless.set_schedule_callback(None)
        self.assertIs(old, mon)
        self.assertIsNone(stackless.get_schedule_callback())

if __name__ == "__main__":
    unittest.main()
