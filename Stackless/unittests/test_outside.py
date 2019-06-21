from __future__ import absolute_import
import unittest
from stackless import test_cframe, test_cframe_nr, test_outside, test_cstate
from stackless import tasklet, channel, run

from support import test_main  # @UnusedImport
from support import StacklessTestCase


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
            for i in range(n):
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
            for i in range(n):
                last[0] = c.receive()

        def createSource():
            tasklet(source)()
        tasklet(createSource)()  # create source from a different cstate
        test_outside()

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
