from __future__ import print_function, absolute_import, division
import sys
import _thread as thread
import stackless
import builtins
import os
import time
import collections
from stackless import _test_nostacklesscall as apply

# sys.argv.extend(['--debug', '--running', '--hard', '0'])

stackless.enable_softswitch('--hard' not in sys.argv)
sys.stdout = sys.stderr

# This lock is used as a simple event variable.
ready = thread.allocate_lock()
ready.acquire()


def unbind_restorable_tasklets(tid=None):
    current = stackless.current
    first = current.cstate
    unbound = set()
    found = True
    while found:
        with stackless.atomic():
            found = False
            cstate = first.next
            while cstate is not first:
                tlet_running = cstate.task
                cstate = cstate.next
                if tlet_running is None:
                    continue
                if tlet_running.alive and tlet_running.restorable and not tlet_running.is_main and not tlet_running.is_current:
                    if tid is not None and tid != tlet_running.thread_id:
                        continue
                    assert tlet_running is not current
                    if tlet_running.blocked:
                        tlet_running.bind_thread()
                        tlet_running.kill(pending=True)  # unblock the tasklet
                    tlet_running.remove()
                    tlet_running.bind(None)
                    found = True
                    unbound.add(id(tlet_running))
        time.sleep(0)
    print("unbound:", ["%08x" % i for i in unbound])


# Module globals are cleared before __del__ is run
# So we save functions, objects, ... in a class dict.
class Detector(object):
    debug = '--debug' in sys.argv
    EXIT_BASE = 20
    out_write = sys.stdout.write
    out_flush = sys.stdout.flush
    os_exit = os._exit
    os_linesep = '\n'
    str = str

    def __init__(self, out, checks, tasklets):
        self.out = out
        self.checks = checks
        self.tasklets = tasklets

        # In Py_Finalize() the PyImport_Cleanup() runs shortly after
        # slp_kill_tasks_with_stacks(NULL).
        # As very first action of PyImport_Cleanup() the Python
        # interpreter sets builtins._ to None.
        # Therefore we can use this assignment to trigger the execution
        # of this detector. At this point the interpreter is still almost fully
        # intact.
        builtins._ = self

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
        self.os_exit(code)

    def print(self, *args):
        for a in args:
            self.out_write(self.str(a))
        self.out_write(self.os_linesep)
        self.out_flush()


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
                apply(func, True, case)
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
            if self.running:
                while 1:  # True becomes None during interpreter shutdown
                    try:
                        self.out.append('.')
                        self.stackless_getcurrent().next.run()
                    except self.TaskletExit:
                        raise
                    except:
                        self.out.append(self.os_linesep + "oops" + self.os_linesep)
                        raise
            else:
                self.time_sleep(999999)
        except self.TaskletExit:
            self.checks[-1] = None
            self.out.append("Done: other thread killed main tasklet" + self.os_linesep)
            raise

test = Test()
detector = Detector(test.out, test.checks, test.tasklets)
assert sys.getrefcount(detector) - sys.getrefcount(object()) == 2
detector = None  # the last ref is now in the codecs search path
thread.start_new_thread(test.other_thread_main, ())
ready.acquire()  # Be sure the other thread is ready.
print("at end")
# unbind_restorable_tasklets()
# print("unbinding done")
sys.exit(Detector.EXIT_BASE)  # trigger interpreter shutdown
