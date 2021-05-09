from __future__ import absolute_import

import unittest
import threading
import stackless
from _stackless import _test_cframe_nr as test_cframe_nr, _test_outside as test_outside
from stackless import tasklet, channel, run
from _teststackless import test_cframe, test_cstate

from support import test_main  # @UnusedImport
from support import StacklessTestCase, withThreads, get_serial_last_jump


def current_initial_stub(threadid=-1):
    """Get the initial stub of the given thread.
    """
    # The second argument of get_thread_info() is intentionally undocumented.
    # See C source.
    return stackless.get_thread_info(threadid, 1 << 30)[5]


MAIN_INITIAL_STUB = current_initial_stub()


class TestOutside(StacklessTestCase):

    def test_outside1(self):
        ran = [False]

        def foo():
            ran[0] = True
        tasklet(foo)()
        test_outside()
        self.assertEqual(ran[0], True)

    def test_outside2(self):
        c = channel()
        n = 3
        last = [None]

        def source():
            for i in range(n):
                c.send(i)

        def sink():
            for i in range(n):  # @UnusedVariable
                last[0] = c.receive()
        tasklet(source)()
        tasklet(sink)()
        test_outside()
        self.assertEqual(last[0], n - 1)

    def test_outside3(self):
        c = channel()
        n = 3
        last = [None]

        def source():
            test_cstate(_source)  # make sure we hardswitch

        def _source():
            for i in range(n):
                c.send(i)

        def sink():
            for i in range(n):  # @UnusedVariable
                last[0] = c.receive()

        def createSource():
            tasklet(source)()
        tasklet(createSource)()  # create source from a different cstate
        test_outside()  # create the source tasklet and run it until it blocks

        # now create the sink tasklet
        tasklet(sink)()
        test_cstate(test_outside)  # change the stack before calling test_outside
        self.assertEqual(last[0], n - 1)

    def test_outside4(self):
        tasklet(test_cframe)(100)
        test_outside()

    def test_outside5(self):
        tasklet(test_cframe_nr)(100)
        test_outside()

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_main_exit_with_serial_mismatch(self):
        # Test the exit of a main-tasklet with a current C-stack serial number
        # unequal to the serial number of the initial stub.
        # The purpose of this test is to exercise a special code path in
        # slp_tasklet_end(PyObject *retval), scheduling.c:1444

        # Note: this test only works in thread for two reasons:
        #       - The main tasklet terminates
        #       - The sequence of operations used to change the serial of the
        #         main tasklet depends on the fact, that the thread state is
        #         not the initial thread state of the interpreter.

        # sanity checks
        self.assertEqual(get_serial_last_jump(), MAIN_INITIAL_STUB.serial)
        self.result = None

        def run():
            try:
                # Test preconditions: current is main at level 0
                if stackless.enable_softswitch(None):
                    self.assertEqual(stackless.current.nesting_level, 0)
                self.assertIs(stackless.current, stackless.main)
                thread_initial_stub = current_initial_stub()
                # sanity check
                self.assertEqual(get_serial_last_jump(), thread_initial_stub.serial)

                # a tasklet with a different C-stack
                #   test_cstate forces a hard switch from schedule_remove
                t = tasklet(test_cstate)(stackless.schedule_remove)
                t_cstate = t.cstate

                # now t has a cstate, that belongs to thread_initial_stub
                # check it
                self.assertIsNot(t_cstate, thread_initial_stub)
                self.assertEqual(t_cstate.serial, thread_initial_stub.serial)

                # Run t from a new entry point.
                # Hard switch to t,
                #   run t and
                #   hard switch back to the main tasklet
                test_outside()  # run scheduled tasklets
                self.assertEqual(t.nesting_level, 1)  # It was a hard switch back to main

                # t has now it's own stack created from a different entry point
                self.assertIsNot(t.cstate, t_cstate)
                self.assertNotEqual(t.cstate.serial, thread_initial_stub.serial)
                t_serial = t.cstate.serial

                # soft-to-hard switch to t, finish t and soft-switch back
                t.run()

                self.assertEqual(t.nesting_level, 0)  # Soft switching was possible
                if stackless.enable_softswitch(None):
                    # Final test: the current serial has changed.
                    self.assertNotEqual(get_serial_last_jump(), thread_initial_stub.serial)
                    self.assertEqual(get_serial_last_jump(), t_serial)
                else:
                    self.assertEqual(get_serial_last_jump(), thread_initial_stub.serial)
            except Exception as e:
                self.result = e
            else:
                self.result = False

        thread = threading.Thread(target=run, name=str(self.id()))
        thread.start()
        thread.join()
        if self.result:
            raise self.result
        self.assertIs(self.result, False)

    def test_call_from_non_main_tasklet(self):
        done = False
        def task():
            nonlocal done
            done = True

        stackless.tasklet(task)()
        stackless.tasklet(test_outside)().switch()
        self.assertTrue(done)

    def test_call_from_non_main_tasklet2(self):
        self.assertRaisesRegex(RuntimeError, "main tasklet is still scheduled", stackless.tasklet(test_outside)().run)


class TestCframe(StacklessTestCase):
    n = 100

    def test_cframe(self):
        tasklet(test_cframe)(self.n)
        tasklet(test_cframe)(self.n)
        run()

    def test_cframe_nr(self):
        tasklet(test_cframe_nr)(self.n)
        tasklet(test_cframe_nr)(self.n)
        run()


if __name__ == '__main__':
    import sys
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
