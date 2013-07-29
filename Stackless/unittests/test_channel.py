import unittest
import stackless
try:
    import threading
    withThreads = True
except ImportError:
    withThreads = False
import sys
import traceback
import contextlib
from support import StacklessTestCase

@contextlib.contextmanager
def block_trap(trap=True):
    """
    A context manager to temporarily set the block trap state of the
    current tasklet.  Defaults to setting it to True
    """
    c = stackless.getcurrent()
    old = c.block_trap
    c.block_trap = trap
    try:
        yield
    finally:
        c.block_trap = old

class TestChannels(StacklessTestCase):
    def testBlockingSend(self):
        ''' Test that when a tasklet sends to a channel without waiting receivers, the tasklet is blocked. '''

        # Function to block when run in a tasklet.
        def f(testChannel):
            testChannel.send(1)

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()

        # The tasklet should be blocked.
        self.assertTrue(tasklet.blocked, "The tasklet should have been run and have blocked on the channel waiting for a corresponding receiver")

        # The channel should have a balance indicating one blocked sender.
        self.assertTrue(channel.balance == 1, "The channel balance should indicate one blocked sender waiting for a corresponding receiver")

    def testBlockingReceive(self):
        ''' Test that when a tasklet receives from a channel without waiting senders, the tasklet is blocked. '''

        # Function to block when run in a tasklet.
        def f(testChannel):
            testChannel.receive()

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()

        # The tasklet should be blocked.
        self.assertTrue(tasklet.blocked, "The tasklet should have been run and have blocked on the channel waiting for a corresponding sender")

        # The channel should have a balance indicating one blocked sender.
        self.assertEqual(channel.balance, -1, "The channel balance should indicate one blocked receiver waiting for a corresponding sender")

    def testNonBlockingSend(self):
        ''' Test that when there is a waiting receiver, we can send without blocking with normal channel behaviour. '''

        originalValue = 1
        receivedValues = []

        # Function to block when run in a tasklet.
        def f(testChannel):
            receivedValues.append(testChannel.receive())

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()

        # Make sure that the current tasklet cannot block when it tries to receive.  We do not want
        # to exit this test having clobbered the block trapping value, so we make sure we restore
        # it.
        oldBlockTrap = stackless.getcurrent().block_trap
        try:
            stackless.getcurrent().block_trap = True
            channel.send(originalValue)
        finally:
            stackless.getcurrent().block_trap = oldBlockTrap

        self.assertTrue(len(receivedValues) == 1 and receivedValues[0] == originalValue, "We sent a value, but it was not the one we received.  Completely unexpected.")

    def testNonBlockingReceive(self):
        ''' Test that when there is a waiting sender, we can receive without blocking with normal channel behaviour. '''
        originalValue = 1

        # Function to block when run in a tasklet.
        def f(testChannel, valueToSend):
            testChannel.send(valueToSend)

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel, originalValue)
        tasklet.run()

        # Make sure that the current tasklet cannot block when it tries to receive.  We do not want
        # to exit this test having clobbered the block trapping value, so we make sure we restore
        # it.
        oldBlockTrap = stackless.getcurrent().block_trap
        try:
            stackless.getcurrent().block_trap = True
            value = channel.receive()
        finally:
            stackless.getcurrent().block_trap = oldBlockTrap

        tasklet.kill()

        self.assertEqual(value, originalValue, "We received a value, but it was not the one we sent.  Completely unexpected.")

    def testMainTaskletBlockingWithoutASender(self):
        ''' Test that the last runnable tasklet cannot be blocked on a channel. '''
        self.assertEqual(stackless.getruncount(), 1, "Leakage from other tests, with tasklets still in the scheduler.")

        c = stackless.channel()
        self.assertRaises(RuntimeError, c.receive)

    @unittest.skipUnless(withThreads, "Compiled without threading")
    def testInterthreadCommunication(self):
        ''' Test that tasklets in different threads sending over channels to each other work. '''
        self.assertEqual(stackless.getruncount(), 1, "Leakage from other tests, with tasklets still in the scheduler.")

        commandChannel = stackless.channel()

        def master_func():
            commandChannel.send("ECHO 1")
            commandChannel.send("ECHO 2")
            commandChannel.send("ECHO 3")
            commandChannel.send("QUIT")

        def slave_func():
            while 1:
                command = commandChannel.receive()
                if command == "QUIT":
                    break

        def scheduler_run(tasklet_func):
            t = stackless.tasklet(tasklet_func)()
            while t.alive:
                stackless.run()

        thread = threading.Thread(target=scheduler_run, args=(master_func,))
        thread.start()

        scheduler_run(slave_func)

    def testSendException(self):

        # Function to send the exception
        def f(testChannel):
            testChannel.send_exception(ValueError, 1,2,3)

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()
        self.assertRaises(ValueError, channel.receive)
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()
        try:
            channel.receive()
        except ValueError, e:
            self.assertEqual(e.args, (1, 2, 3))

    def testSendThrow(self):

        # subfunction in tasklet
        def bar():
            raise ValueError, (1,2,3)

        # Function to send the exception
        def f(testChannel):
            try:
                bar()
            except Exception:
                testChannel.send_throw(*sys.exc_info())

        # Get the tasklet blocked on the channel.
        channel = stackless.channel()
        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()
        self.assertRaises(ValueError, channel.receive)

        tasklet = stackless.tasklet(f)(channel)
        tasklet.run()
        try:
            channel.receive()
        except ValueError:
            exc, val, tb = sys.exc_info()
            self.assertEqual(val.args, (1, 2, 3))

            # Check that the traceback is correct
            l = traceback.extract_tb(tb)
            self.assertEqual(l[-1][2], "bar")

    def testBlockTrapSend(self):
        '''Test that block trapping works when receiving'''
        channel = stackless.channel()
        count = [0]
        def f():
            with block_trap():
                self.assertRaises(RuntimeError, channel.send, None)
            count[0] += 1

        # Test on main tasklet and on worker
        f()
        stackless.tasklet(f)()
        stackless.run()
        self.assertEqual(count[0], 2)

def testBlockTrapRecv(self):
        '''Test that block trapping works when receiving'''
        channel = stackless.channel()
        count = [0]
        def f():
            with block_trap():
                self.assertRaises(RuntimeError, channel.receive)
            count[0] += 1

        f()
        stackless.tasklet(f)()
        stackless.run()
        self.assertEqual(count[0], 2)


if __name__ == '__main__':
    import sys
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
