#
# -*- coding: utf-8 -*-
#
#  Copyright 2016 science + computing ag
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

"""Test the interpreter and thread shutdown

Test for functional requirements

If a thread ends (main-tasklet ended), Python calls PyThreadState_Clear(). Within PyThreadState_Clear()
Stackless Python must remove the reference to the thread from all cstate-objects.

 - If a tasklet has a nesting level > 0, the interpreter must kill the tasklet. This
   creates a new temporary main tasklet.
 - Otherwise, if the tasklet has nesting level 0, the interpreter must not touch the tasklet.

The manual (chapter "threads") explains the details. This behaviour is consistent with tasklet_dealloc(),
which resurrects and kills tasklets with a non-zero C-stack only.

Test for defects go into test_defects
"""

from __future__ import print_function, absolute_import, division
import sys
import stackless
import codecs
import os
import time
import collections
import unittest
import subprocess
try:
    import thread
    import threading
    withThreads = True
except:
    withThreads = False
from support import test_main  # @UnusedImport
from support import StacklessTestCase
from textwrap import dedent


@unittest.skipUnless(withThreads, "requires thread support")
class TestThreadShutdown(StacklessTestCase):

    def _test_thread_shutdown(self, func, expect_kill):

        self.tasklet_killed = False
        self.other_thread_started = False
        self.main_during_kill = None
        self.main_other_thread = None

        def tlet_wrapper():
            self.main_other_thread = main = stackless.main
            try:
                func(main)
            except TaskletExit:
                self.tasklet_killed = True
                self.main_during_kill = stackless.getmain()
                raise
            else:
                self.fail("func must block!")

        tlet_running = stackless.tasklet().bind(tlet_wrapper, ())

        def other_thread_main():
            # print("other thread started")
            self.assertIs(stackless.main, stackless.current)
            tlet_running.bind_thread()
            tlet_running.run()
            self.assertTrue(tlet_running.alive)
            self.assertTrue(tlet_running.scheduled)
            self.other_thread_started = True

        self.assertIs(stackless.main, stackless.current)
        t = threading.Thread(target=other_thread_main, name="other thread")
        t.start()
        t.join()
        time.sleep(0.1)  # give other thread some time to die
        self.assertTrue(self.other_thread_started)
        self.assertFalse(tlet_running.alive)
        if self.tasklet_killed:
            self.assertIsInstance(self.main_during_kill, stackless.tasklet)
            self.assertIsNot(self.main_other_thread, self.main_during_kill)
            self.assertIsNot(stackless.current, self.main_during_kill)
            self.assertIsNot(tlet_running, self.main_during_kill)
        if expect_kill:
            self.assertTrue(self.tasklet_killed)
        else:
            self.assertFalse(self.tasklet_killed)

    def testThreadShutdown_nl0(self):
        def func(main):
            main.run()
        self._test_thread_shutdown(func, not stackless.enable_softswitch(None))

    def testThreadShutdown_nl1(self):
        def func(main):
            apply(main.run, ())
        self._test_thread_shutdown(func, True)

    def testThreadShutdown_nl0_blocked(self):
        c = stackless.channel()

        def func(main):
            c.receive()
        self._test_thread_shutdown(func, not stackless.enable_softswitch(None))

    def testThreadShutdown_nl1_blocked(self):
        c = stackless.channel()

        def func(main):
            apply(c.receive, ())
        self._test_thread_shutdown(func, True)


class TestShutdown(StacklessTestCase):
    @unittest.skipUnless(withThreads, "requires thread support")
    def test_deep_thread(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import threading
            import stackless
            import time
            import sys
            from stackless import _test_nostacklesscall as apply

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            RECURSION_DEPTH = 200

            event = threading.Event()

            def recurse():
                if stackless.current.nesting_level < RECURSION_DEPTH:
                    apply(recurse)
                else:
                    event.set()
                    time.sleep(10)

            t = threading.Thread(target=recurse, name="other_thread")
            t.daemon = True
            t.start()
            event.wait(10)
            # print("end")
            sys.stdout.flush()
            sys.exit(42)
            """)] + args)
        self.assertEqual(rc, 42)

    def test_deep_tasklets(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import stackless
            import sys
            from stackless import _test_nostacklesscall as apply

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            RECURSION_DEPTH = 200

            def recurse():
                if stackless.current.nesting_level < RECURSION_DEPTH:
                    stackless.schedule()
                    apply(recurse)
                else:
                    stackless.schedule_remove()

            tasklets = []
            for i in range(20):
                task = stackless.tasklet(recurse)()
                tasklets.append(task)  # keep the tasklet objects alive

            stackless.run()
            # print("end")
            sys.stdout.flush()
            sys.exit(42)
            """)] + args)
        self.assertEqual(rc, 42)

    def test_exit_in_deep_tasklet1(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import stackless
            import sys
            from stackless import _test_nostacklesscall as apply

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            RECURSION_DEPTH = 200

            def recurse():
                if stackless.current.nesting_level < RECURSION_DEPTH:
                    stackless.schedule()
                    apply(recurse)
                else:
                    sys.exit(43)  # deeply nested, non main tasklet

            tasklets = []
            for i in range(20):
                task = stackless.tasklet(recurse)()
                tasklets.append(task)  # keep the tasklet objects alive

            try:
                stackless.run()
            except TaskletExit:
                # print("Got TaskletExit", repr(stackless.current.cstate))
                sys.stdout.flush()
                sys.exit(42)

            print("OOPS, must not be reached")
            sys.stdout.flush()
            sys.exit(44)
            """)] + args)
        self.assertEqual(rc, 42)

    def test_exit_in_deep_tasklet2(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import stackless
            import sys
            from stackless import _test_nostacklesscall as apply

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            RECURSION_DEPTH = 200

            last = None

            def recurse():
                global last
                if stackless.current.nesting_level < RECURSION_DEPTH:
                    stackless.schedule()
                    apply(recurse)
                else:
                    last = stackless.current
                    sys.exit(42)  # deeply nested, non main tasklet

            tasklets = []
            for i in range(20):
                task = stackless.tasklet(recurse)()
                tasklets.append(task)  # keep the tasklet objects alive

            try:
                stackless.run()
            except TaskletExit:
                # print("Got TaskletExit", repr(stackless.current.cstate))
                sys.stdout.flush()
                last.run()  # switch back
                sys.exit(43)

            print("OOPS, must not be reached")
            sys.stdout.flush()
            sys.exit(44)
            """)] + args)
        self.assertEqual(rc, 42)

    def test_deep_Py_Exit(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/
        try:
            import ctypes  # @UnusedImport
        except ImportError:
            self.skipTest("test requires ctypes")

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import stackless
            import sys
            import ctypes
            from stackless import _test_nostacklesscall as apply

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            RECURSION_DEPTH = 200

            Py_Exit = ctypes.pythonapi.Py_Exit
            Py_Exit.restype = None
            Py_Exit.argtypes = (ctypes.c_int,)

            def recurse():
                if stackless.current.nesting_level < RECURSION_DEPTH:
                    apply(recurse)
                else:
                    sys.stdout.flush()
                    Py_Exit(42)

            recurse()

            print("OOPS, must not be reached")
            sys.stdout.flush()
            sys.exit(43)
            """)] + args)
        self.assertEqual(rc, 42)

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_other_thread_Py_Exit(self):
        # test for issue #103 https://github.com/stackless-dev/stackless/issues/103/
        try:
            import ctypes  # @UnusedImport
        except ImportError:
            self.skipTest("test requires ctypes")

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import

            import stackless
            import sys
            import ctypes
            import threading
            import time

            if "--hard" in sys.argv:
                stackless.enable_softswitch(False)

            Py_Exit = ctypes.pythonapi.Py_Exit
            Py_Exit.restype = None
            Py_Exit.argtypes = (ctypes.c_int,)

            def exit():
                # print("Calling Py_Exit(42)")
                time.sleep(0.01)
                sys.stdout.flush()
                Py_Exit(42)

            t = threading.Thread(target=exit, name="othere_thread")
            t.daemon = True
            t.start()
            time.sleep(1)

            print("OOPS, must not be reached")
            sys.stdout.flush()
            sys.exit(43)
            """)] + args)
        self.assertEqual(rc, 42)

    @unittest.skipUnless(withThreads, "requires thread support")
    def test_kill_modifies_slp_cstack_chain(self):
        # test for issue #105 https://github.com/stackless-dev/stackless/issues/105/

        args = []
        if not stackless.enable_softswitch(None):
            args.append("--hard")

        rc = subprocess.call([sys.executable, "-s", "-S", "-E", "-c", dedent("""
            from __future__ import print_function, absolute_import, division

            import threading
            import stackless
            import time
            import sys
            from stackless import _test_nostacklesscall as apply

            DEBUG = False
            event = threading.Event()

            if not DEBUG:
                def print(*args, **kw):
                    pass

            def mytask():
                stackless.schedule_remove()

            class TaskHolder(object):
                def __init__(self, task):
                    self.task = task

                def __del__(self):
                    while self.task.alive:
                        time.sleep(0.1)
                        print("TaskHolder.__del__, task1 is still alive")
                    print("TaskHolder.__del__: task1 is now dead")
                    self.task = None

            def other_thread():
                # create a paused tasklet
                task1 = stackless.tasklet(apply)(mytask)
                stackless.run()
                assert(task1.alive)
                assert(task1.paused)
                assert(task1.nesting_level > 0)
                assert(task1 is task1.cstate.task)
                assert(len(str(task1.cstate)) > 0)
                assert(sys.getrefcount(task1.cstate) - sys.getrefcount(object()) == 1)
                assert(task1.tempval is task1)
                assert(sys.getrefcount(task1) - sys.getrefcount(object()) == 2)
                task1.tempval = TaskHolder(task1)
                assert(sys.getrefcount(task1) - sys.getrefcount(object()) == 2)
                print("task1", task1)
                print("task1.cstate", repr(task1.cstate))
                print("ending main tasklet of other_thread, going run the scheduler")
                task1 = None
                event.set()

                # sleep for a long time
                t = 10000
                while t > 0:
                    try:
                        t -= 1
                        stackless.run()
                        time.sleep(0.01)
                    except:
                        pass

            t = threading.Thread(target=other_thread, name="other_thread")
            t.daemon = True
            t.start()
            event.wait(5.0)
            print("end")
            sys.exit(42)
        """)] + args)
        self.assertEqual(rc, 42)


class TestInterpreterShutdown(unittest.TestCase):
    # intentionally not a subclass of StacklessTestCase.

    def _test_shutdown_with_thread(self, case, is_hard, allowed_errors=(), debug=False):
        # test for issue #81 https://github.com/stackless-dev/stackless/issues/81/
        if not withThreads:
            self.skipTest("requires thread support")
        script = __file__
        if script.endswith("pyc") or script.endswith("pyo"):
            script = script[:-1]
        args = [sys.executable, "-S", "-E", script, "IST"]
        args.append("--running")  # always needed. Killing blocked threads is not possible
        if is_hard:
            args.append("--hard")
        if debug:
            args.append("--debug")
        if case is not None:
            args.append(str(int(case)))
        rc = subprocess.call(args)
        rc_ok = Detector.EXIT_BASE + 1
        if rc - rc_ok in allowed_errors:
            # killing a hard switched running tasklet is always unreliable, because
            # the bomb can fail to explode
            print("The previous error message is harmless, if it occurs just rarely", file=sys.stderr)
        else:
            self.assertEqual(rc, rc_ok)

    def test_shutdown_soft_all(self):
        self._test_shutdown_with_thread(None, False)

    def test_shutdown_hard_all(self):
        self._test_shutdown_with_thread(None, True)

    def test_shutdown_soft_nothing(self):
        self._test_shutdown_with_thread(-1, False)

    def test_shutdown_hard_nothing(self):
        self._test_shutdown_with_thread(-1, True)

    def test_shutdown_soft_paused_nl0(self):
        self._test_shutdown_with_thread(0, False)

    def test_shutdown_soft_paused_nl1(self):
        self._test_shutdown_with_thread(1, False)

    def test_shutdown_soft_blocked_nl0(self):
        self._test_shutdown_with_thread(2, False)

    def test_shutdown_soft_blocked_nl1(self):
        self._test_shutdown_with_thread(3, False)

    def test_shutdown_soft_running_nl0(self):
        self._test_shutdown_with_thread(4, False)

    def test_shutdown_soft_running_nl1(self):
        self._test_shutdown_with_thread(5, False)

    def test_shutdown_hard_paused_nl0(self):
        self._test_shutdown_with_thread(0, True)

    def test_shutdown_hard_paused_nl1(self):
        self._test_shutdown_with_thread(1, True)

    def test_shutdown_hard_blocked_nl0(self):
        self._test_shutdown_with_thread(2, True)

    def test_shutdown_hard_blocked_nl1(self):
        self._test_shutdown_with_thread(3, True)

    def test_shutdown_hard_running_nl0(self):
        self._test_shutdown_with_thread(4, True)

    def test_shutdown_hard_running_nl1(self):
        self._test_shutdown_with_thread(5, True)


# Module globals are cleared before __del__ is run.
# So we save functions, objects, ... in the class dict.
class Detector(object):
    debug = '--debug' in sys.argv
    EXIT_BASE = 20
    os_exit = os._exit
    os_linesep = '\n'
    str = str
    time_sleep = time.sleep

    def __init__(self, out, checks, tasklets):
        self.out_write = sys.stdout.write
        self.out_flush = sys.stdout.flush
        self.output = []

        self.out = out
        self.checks = checks
        self.tasklets = tasklets

        # register the detector in the codecs search path
        # it will be cleared after clearing the thread states
        # this way the detector runs very late
        def dummy_codecs_search_function(name):
            return None
        codecs.register(dummy_codecs_search_function)
        dummy_codecs_search_function.x = self

    def __del__(self):
        if self.debug:
            self.print("Detector running")
            self.print(*self.out)
        code = 0
        i = 0
        for (name, tlet) in self.tasklets:
            if tlet.frame is None and tlet.thread_id == -1:
                if self.debug:
                    self.print("Good, tasklet is dead: ", name)
                i += 1
                continue
            if tlet.frame is not None:
                self.print("ERROR, tasklet alive: ", name)
            if tlet.thread_id != -1:
                self.print("ERROR, tasklet has thread_id: ", name, " :", tlet.thread_id)
            code |= 1 << i
            i += 1
        i = 0
        for err in self.checks:
            if err is None:
                i += 1
                continue
            self.print("ERROR, ", err)
            code |= 1 << i
            i += 1
        code += self.EXIT_BASE + 1
        if self.debug:
            self.print("Exit code: ", code)
        self.out_write("".join(self.output))
        self.out_flush()
        self.os_exit(code)

    def print(self, *args):
        for a in args:
            self.output.append(self.str(a))
        self.output.append(self.os_linesep)


# Module globals are cleared while other_thread runs.
# So we save functions, objects, ... in the class dict.
class Test(object):
    CASES = (("tlet_paused", "paused", False),
             ("tlet_blocked", "blocked", False),
             ("tlet_running", "scheduled", True),
             )

    debug = Detector.debug
    running = '--running' in sys.argv
    try:
        case = int(sys.argv[-1])
    except Exception:
        case = None
    stackless_getcurrent = stackless.getcurrent
    time_sleep = time.sleep
    os_linesep = Detector.os_linesep
    TaskletExit = TaskletExit

    def __init__(self):
        self.out = collections.deque()
        self.checks = []
        self.tasklets = []
        self.channel = stackless.channel()

    def tlet_running(self, nesting_level, case):
        assert bool(self.stackless_getcurrent().nesting_level) is nesting_level, \
            "wrong nesting level, expected %s, actual %s" % (nesting_level, bool(self.stackless_getcurrent().nesting_level))
        self.main.run()
        while 1:  # True becomes None during interpreter shutdown
            self.out.append(case)
            self.stackless_getcurrent().next.run()

    def tlet_paused(self, nesting_level, case):
        assert bool(self.stackless_getcurrent().nesting_level) is nesting_level, \
            "wrong nesting level, expected %s, actual %s" % (nesting_level, bool(self.stackless_getcurrent().nesting_level))
        self.main.switch()

    def tlet_blocked(self, nesting_level, case):
        assert bool(self.stackless_getcurrent().nesting_level) is nesting_level, \
            "wrong nesting level, expected %s, actual %s" % (nesting_level, bool(self.stackless_getcurrent().nesting_level))
        self.channel.receive()

    def wrapper(self, func_name, index, case, nesting_level, expect_kill, may_kill):
        func = getattr(self, func_name)
        msg = "killed other thread %s, case %d (with C-state %s)" % (func_name, case, bool(nesting_level))
        msg_killed = "Done: %s%s" % (msg, self.os_linesep)
        if stackless.enable_softswitch(None):
            assert self.stackless_getcurrent().nesting_level == 0
        if expect_kill:
            self.checks[index] = "not " + msg
            checks_killed = None
        elif may_kill:
            checks_killed = None
            self.checks[index] = None
        else:
            self.checks[index] = None
            checks_killed = "unexpectedly " + msg

        try:
            if nesting_level == 0:
                func(bool(self.stackless_getcurrent().nesting_level), case)
            else:
                apply(func, (True, case))
            # From here on modules and globals are unavailable.
            # Only local variables and closures can be used.
        except self.TaskletExit:
            self.checks[index] = checks_killed
            self.out.append(msg_killed)
            raise

    def prep(self, case_nr, index):
        case = self.CASES[case_nr >> 1]
        nesting_level = case_nr & 1
        expect_kill = bool(not stackless.enable_softswitch(None) or nesting_level)
        tlet = stackless.tasklet(self.wrapper)(case[0], index, case_nr, nesting_level, expect_kill, case[2])
        self.tasklets.append(("%s, case %d" % (case[0], case_nr), tlet))
        tlet.run()
        assert getattr(tlet, case[1]), "tasklet is not " + case[1]

    def other_thread_main(self):
        assert stackless.main is stackless.current
        self.main = stackless.main
        if self.debug:
            print("other thread started, soft %s, running %s, nl %d" % (stackless.enable_softswitch(None), self.running, self.main.nesting_level))

        if isinstance(self.case, int) and self.case >= 0:
            assert 0 <= self.case < len(self.CASES) * 2
            self.checks.append("failed to start %s, case %d" % (self.CASES[self.case >> 1][0], self.case))
        elif self.case is None:
            for i in range(len(self.CASES) * 2):
                self.checks.append("failed to start %s, case %d" % (self.CASES[i >> 1][0], i))

        self.checks.append("not killed other thread main tasklet")

        if isinstance(self.case, int) and self.case >= 0:
            self.prep(self.case, 0)
        elif self.case is None:
            for i in range(len(self.CASES) * 2):
                self.prep(i, i)
        self.tasklets.append(("main", self.main))

        if self.debug:
            print("tasklets prepared: ", self.tasklets)
        try:
            ready.release()
            # From here on modules and globals are unavailable.
            # Only local variables and closures can be used.
            if self.running:
                while 1:  # True becomes None during interpreter shutdown
                    try:
                        self.out.append('.')
                        self.stackless_getcurrent().next.run()
                    except self.TaskletExit:
                        raise  # setting this to pass causes a crash.
                    except:
                        self.out.append(self.os_linesep + "oops" + self.os_linesep)
                        raise
            else:
                self.time_sleep(999999)
        except self.TaskletExit:
            self.checks[-1] = None
            self.out.append("Done: other thread killed main tasklet" + self.os_linesep)
            raise


def interpreter_shutdown_test():
    global ready
    stackless.enable_softswitch('--hard' not in sys.argv)
    sys.stdout = sys.stderr

    # This lock is used as a simple event variable.
    ready = thread.allocate_lock()
    ready.acquire()

    test = Test()
    detector = Detector(test.out, test.checks, test.tasklets)
    assert sys.getrefcount(detector) - sys.getrefcount(object()) == 2
    detector = None  # the last ref is now in the codecs search path
    thread.start_new_thread(test.other_thread_main, ())
    ready.acquire()  # Be sure the other thread is ready.
    # print("at end")
    sys.exit(Detector.EXIT_BASE)  # trigger interpreter shutdown


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "IST":
        interpreter_shutdown_test()
    else:
        if not sys.argv[1:]:
            sys.argv.append('-v')
        unittest.main()
