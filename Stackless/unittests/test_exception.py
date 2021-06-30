from __future__ import absolute_import

import unittest
import sys
import stackless

# from pprint import pprint

from support import test_main  # @UnusedImport
from support import (StacklessTestCase, StacklessPickleTestCase)


class TestException(StacklessTestCase):
    def testTaskletExitCode(self):
        # Tasklet exit was defined as the wrong kind of exception.
        # When its code attribute was accessed the runtime would
        # crash.  This has been fixed.
        exc = TaskletExit()
        exc.code


class TestExcStatePickling(StacklessPickleTestCase):
    def test_tasklet_reduce_wo_exc(self):
        t = stackless.tasklet(lambda: None)()
        state = t.__reduce__()
        self.assertIsInstance(state, tuple)
        self.assertEqual(len(state), 3)
        state = state[2]
        self.assertIsInstance(state, tuple)
        self.assertEqual(len(state), 12)
        self.assertTupleEqual(state[4:8], (None, None, None, None))

    def test_tasklet_pickle_with_exc(self):
        self.skipUnlessSoftswitching()

        e1 = ZeroDivisionError("test")
        e2 = NotImplementedError("TEST")

        def task_inner():
            stackless.schedule_remove((2, sys.exc_info()))
            try:
                raise e2
            except Exception:
                stackless.schedule_remove((3, sys.exc_info()))
            stackless.schedule_remove((4, sys.exc_info()))

        def task_outer():
            stackless.schedule_remove((0, sys.exc_info()))
            try:
                raise e1
            except Exception:
                stackless.schedule_remove((1, sys.exc_info()))
                task_inner()
                stackless.schedule_remove((5, sys.exc_info()))
            stackless.schedule_remove((6, sys.exc_info()))

        t = stackless.tasklet(task_outer)()
        expected_exc = (
            (None, None),               # 0
            (ZeroDivisionError, e1),    # 1
            (ZeroDivisionError, e1),    # 2
            (NotImplementedError, e2),  # 3
            (ZeroDivisionError, e1),    # 4
            (ZeroDivisionError, e1),    # 5
            (None, None),               # 6
        )
        pickles = []
        stackless.run()
        for i, exc in enumerate(expected_exc):
            # pprint(t.tempval)
            self.assertEqual(t.tempval[0], i)
            self.assertTupleEqual(t.tempval[1][:2], exc)
            pickles.append(self.dumps(t))
            t.insert()
            stackless.run()
        self.assertFalse(t.alive)
        t = None

        for n, p in enumerate(pickles):
            t = self.loads(p)
            t.insert()
            stackless.run()
            for i, exc in enumerate(expected_exc[n + 1:], n + 1):
                # pprint(t.tempval)
                self.assertEqual(t.tempval[0], i)
                self.assertIs(t.tempval[1][0], exc[0])
                if exc[1] is None:
                    self.assertIsNone(t.tempval[1][1])
                else:
                    self.assertIsNot(t.tempval[1][1], exc[1])
                    self.assertIs(type(t.tempval[1][1]), type(exc[1]))
                t.insert()
                stackless.run()
            self.assertFalse(t.alive)

    def test_generator_pickle_with_exc(self):
        self.skipUnlessSoftswitching()

        e1 = ZeroDivisionError("test")
        e2 = NotImplementedError("TEST")

        def task_inner():
            stackless.schedule_remove((2, sys.exc_info()))
            yield 3
            try:
                raise e2
            except Exception:
                yield 4
                stackless.schedule_remove((5, sys.exc_info()))
                yield 6
            yield 7
            stackless.schedule_remove((8, sys.exc_info()))

        def task_outer():
            stackless.schedule_remove((0, sys.exc_info()))
            try:
                raise e1
            except Exception:
                stackless.schedule_remove((1, sys.exc_info()))
                for x in task_inner():
                    stackless.schedule_remove((x, sys.exc_info()))
            stackless.schedule_remove((9, sys.exc_info()))

        t = stackless.tasklet(task_outer)()
        expected_exc = (
            (None, None),               # 0
            (ZeroDivisionError, e1),    # 1
            (ZeroDivisionError, e1),    # 2
            (ZeroDivisionError, e1),    # 3
            (ZeroDivisionError, e1),    # 4
            (NotImplementedError, e2),  # 5
            (ZeroDivisionError, e1),    # 6
            (ZeroDivisionError, e1),    # 7
            (ZeroDivisionError, e1),    # 8
            (None, None),               # 9
        )
        pickles = []
        stackless.run()
        for i, exc in enumerate(expected_exc):
            # pprint(t.tempval)
            self.assertEqual(t.tempval[0], i)
            self.assertTupleEqual(t.tempval[1][:2], exc)
            pickles.append(self.dumps(t))
            t.insert()
            stackless.run()
        self.assertFalse(t.alive)
        t = None

        for n, p in enumerate(pickles):
            t = self.loads(p)
            t.insert()
            stackless.run()
            for i, exc in enumerate(expected_exc[n + 1:], n + 1):
                # pprint(t.tempval)
                self.assertEqual(t.tempval[0], i)
                self.assertIs(t.tempval[1][0], exc[0])
                if exc[1] is None:
                    self.assertIsNone(t.tempval[1][1])
                else:
                    self.assertIsNot(t.tempval[1][1], exc[1])
                    self.assertIs(type(t.tempval[1][1]), type(exc[1]))
                t.insert()
                stackless.run()
            self.assertFalse(t.alive)


if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
