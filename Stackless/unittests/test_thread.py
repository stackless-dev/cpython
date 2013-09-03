# import common

import pickle
import unittest
import stackless
import sys
import traceback
import contextlib

from support import StacklessTestCase, AsTaskletTestCase
try:
    import threading
    withThreads = True
except:
    withThreads = False


def GetRemoteTasklets(callables):
    """Get a non-scheduled tasklet on a remote thread"""
    import threading
    c = stackless.channel()
    def tfunc():
        # thread func.  Create a tasklet, remove it, and send it to the master.
        # then wait for the tasklet to finish.
        try:
            c2 = stackless.channel()
            tasklets = []
            for callable in callables:
                def helper(callable):
                    try:
                        callable()
                    except:
                        c2.send_throw(*sys.exc_info())
                    else:
                        c2.send(None)
                t = stackless.tasklet(helper)(callable)
                t.remove()
                tasklets.append(t)
            c.send(tasklets)
        except:
            c.send_throw(*sys.exc_info())
        stackless.__reduce__()
        for callable in callables:
            c2.receive()
        stackless.run() #drain the scheduler

    thread = threading.Thread(target=tfunc)
    thread.start()
    d = c.receive(), thread
    return d

def GetRemoteTasklet(callable, args):
    """Get a non-scheduled tasklet on a remote thread"""
    tasklets, thread = GetRemoteTasklets([lambda:callable(*args)])
    return tasklets[0], thread

@unittest.skipUnless(withThreads, "requires thread support")
class TestRemoteSchedule(AsTaskletTestCase):
    def setUp(self):
        super(TestRemoteSchedule, self).setUp()
        self.events = []
    def testFoo(self):
        def foo(): pass
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.run()
        finally:
            thread.join(2)

    def testRun(self):
        def foo():
            self.events.append(0)
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.run()
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(len(self.events))))

    def testInsert(self):
        def foo():
            self.events.append(0)
        t, thread = GetRemoteTasklet(foo, ())
        try:
            t.insert()
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(len(self.events))))


    def testRunOrder(self):
        def a():
            self.events.append(0)
        def b():
            self.events.append(1)
        def c():
            self.events.append(2)

        (t1, t2, t3), thread = GetRemoteTasklets((a, b, c))
        try:
            with stackless.atomic():
                t2.insert()
                t3.insert()
                t1.run() #t1 should run first
        finally:
            thread.join(2)
        self.assertEqual(self.events, list(range(3)))


#///////////////////////////////////////////////////////////////////////////////

if __name__ == '__main__':
    import sys
    if not sys.argv[1:]:
        sys.argv.append('-v')

    unittest.main()