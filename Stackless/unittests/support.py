import stackless
try:
    import threading
    withThreads = True
except:
    withThreads = False
import unittest

class StacklessTestCase(unittest.TestCase):
    def setUp(self):
        self.assertEqual(stackless.getruncount(), 1, "Leakage from other tests, with %d tasklets still in the scheduler" % (stackless.getruncount() - 1))
        if withThreads:
            self.assertEqual(threading.activeCount(), 1, "Leakage from other threads, with %d threads running (1 expected)" % (threading.activeCount()))

    def tearDown(self):
        # Tasklets created in pickling tests can be left in the scheduler when they finish.  We can feel free to
        # clean them up for the tests.  Any tests that expect to exit with no leaked tasklets should do explicit
        # assertions to check.
        mainTasklet = stackless.getmain()
        current = mainTasklet.next
        while current is not None and current is not mainTasklet:
            next = current.next
            current.kill()
            current = next
        self.assertEqual(stackless.getruncount(), 1, "Leakage from this test, with %d tasklets still in the scheduler" % (stackless.getruncount() - 1))
        if withThreads and threading.activeCount() > 1:
            activeThreads = threading.enumerate()
            activeThreads.remove(threading.currentThread())
            if activeThreads:
                activeThreads[0].join(0.5)
            if threading.activeCount() > 1:
                self.assertEqual(threading.activeCount(), 1, "Leakage from other threads, with %d threads running (1 expected)" % (threading.activeCount()))

class AsTaskletTestCase(StacklessTestCase):
    """A test case class, that runs tests as tasklets"""
    def setUp(self):
        self._ran_AsTaskletTestCase_setUp = True
        if stackless.enable_softswitch(None):
            self.assertEqual(stackless.current.nesting_level, 0)

        super(StacklessTestCase, self).setUp()  # yes, its intended: call setUp on the grand parent class
        self.assertEqual(stackless.getruncount(), 1, "Leakage from other tests, with %d tasklets still in the scheduler" % (stackless.getruncount() - 1))
        if withThreads:
            self.assertEqual(threading.activeCount(), 1, "Leakage from other threads, with %d threads running (1 expected)" % (threading.activeCount()))

    def run(self, result=None):
        c = stackless.channel()
        c.preference = 1 #sender priority
        def helper():
            try:
                c.send(super(AsTaskletTestCase, self).run(result))
            except:
                c.send_throw(*sys.exc_info())
        t = stackless.tasklet(helper)()
        result = c.receive()
        assert self._ran_AsTaskletTestCase_setUp
        return result