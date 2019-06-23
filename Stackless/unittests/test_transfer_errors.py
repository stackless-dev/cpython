from __future__ import absolute_import, division, print_function

import unittest
import sys
import stackless
import contextlib

from support import test_main  # @UnusedImport
from support import StacklessTestCase, testcase_leaks_references


class TestStackTransferFailures(StacklessTestCase):
    """Test the error handling of failures in slp_transfer()

    In theory, slp_transfer can't fail, but in reality there bugs. Unfortunately
    it is usually not possible to recover from slp_transfer failures. But we try to avoid
    undefined behavior.

    The test coverage is incomplete, patches are welcome. Especially failed soft-to-hard switches
    result in Py_FatalError("Soft-to-hard tasklet switch failed.").
    """
    def setUp(self):
        StacklessTestCase.setUp(self)

    @contextlib.contextmanager
    def corrupted_cstack_startaddr(self, tasklet):
        """Increment the cstack startaddr of this tasklet.

        Hard-switching to this C-stack will fail. This context is used
        by error handling tests
        """
        self.assertTrue(tasklet.alive)
        self.assertEqual(tasklet.thread_id, stackless.current.thread_id)
        self.assertFalse(tasklet.restorable)
        self.assertFalse(tasklet.is_current)
        cstate = tasklet.cstate
        self.assertNotEqual(cstate.startaddr, 0)  # not invalidated
        self.assertIs(cstate.task, tasklet)
        test_cframe_nr = stackless.test_cframe_nr
        # Otherwise undocumented and only for this test.
        # Add 1 to the startaddr of cstate
        test_cframe_nr(1, cstate_add_addr=cstate)
        try:
            yield
        finally:
            # Restore the pprevious value
            test_cframe_nr(-1, cstate_add_addr=cstate)

    def __task(self):
        stackless.schedule_remove(None)
        self.tasklet_done = True

    def prepare_tasklet(self, task=None):
        if task is None:
            task = self.__task

        self.tasklet_done = False
        t = stackless.tasklet(self.__task)()
        sw = stackless.enable_softswitch(False)
        try:
            t.run()
        finally:
            stackless.enable_softswitch(sw)
        return t

    def end(self, t):
        # if t.alive:
        t.run()
        self.assertTrue(self.tasklet_done)

    @testcase_leaks_references("unknown")
    def test_scheduler(self):
        t = self.prepare_tasklet()
        with self.corrupted_cstack_startaddr(t):
            t.insert()
            self.assertRaisesRegex(SystemError, 'bad stack reference in transfer', stackless.run)
        self.end(t)
    test_scheduler.enable_softswitch = False  # soft switching causes soft-to-hard

    @testcase_leaks_references("unknown")
    def test_run(self):
        t = self.prepare_tasklet()
        with self.corrupted_cstack_startaddr(t):
            self.assertRaisesRegex(SystemError, 'bad stack reference in transfer', t.run)
        self.end(t)
    test_run.enable_softswitch = False  # soft switching causes soft-to-hard

    @testcase_leaks_references("unknown")
    def test_switch(self):
        t = self.prepare_tasklet()
        with self.corrupted_cstack_startaddr(t):
            self.assertRaisesRegex(SystemError, 'bad stack reference in transfer', t.switch)
        self.end(t)
    test_switch.enable_softswitch = False  # soft switching causes soft-to-hard

    @testcase_leaks_references("unknown")
    def test_kill(self):
        t = self.prepare_tasklet()
        with self.corrupted_cstack_startaddr(t):
            self.assertRaisesRegex(SystemError, 'bad stack reference in transfer', t.kill)
        self.end(t)
    test_kill.enable_softswitch = False  # soft switching causes soft-to-hard


#///////////////////////////////////////////////////////////////////////////////

if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')

    unittest.main()
