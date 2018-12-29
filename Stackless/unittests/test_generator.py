from __future__ import absolute_import
import unittest
import gc
import stackless
import types
import pickle
import contextlib
import sys

from support import test_main  # @UnusedImport
from support import StacklessTestCase, captured_stderr


def f():
    yield 1


class TestGarbageCollection(StacklessTestCase):

    def testSimpleLeakage(self):
        leakage = []

        with stackless.atomic():
            gc.collect(2)
            before = frozenset(id(o) for o in gc.get_objects())

            for i in f():
                pass

            gc.collect(2)
            after = gc.get_objects()

        for x in after:
            if x is not before and id(x) not in before:
                leakage.append(x)

        if len(leakage):
            self.failUnless(len(leakage) == 0, "Leaked %s" % repr(leakage))

    def testGCRunningGenerator(self):
        def gen():
            try:
                stackless.schedule_remove()
                yield 1
            except:  # @IgnorePep8
                # print("exception in gen:", sys.exc_info())
                raise

        def task(generator):
            [i for i in generator]

        t = stackless.tasklet(task)(gen())
        stackless.run()
        self.assertTrue(t.paused)
        t.tempval = None
        with captured_stderr() as stringio:
            # must not throw or output
            if t.restorable:
                t.bind(None)
            # make sure, that t=None kills the last reference
            self.assertEqual(sys.getrefcount(t), 2)
            t = None
        self.assertEqual(stringio.getvalue(), "")


class TestGeneratorWrapper(StacklessTestCase):
    def test_run_wrap_generator(self):
        g = stackless._wrap.generator()
        self.assertIsInstance(g, types.GeneratorType)
        self.assertIsNot(type(g), types.GeneratorType)
        self.assertRaises(StopIteration, next, g)

    def test_wrap_generator_frame_code(self):
        g0 = stackless._wrap.generator()
        g1 = stackless._wrap.generator()
        self.assertIsInstance(g0.gi_frame, types.FrameType)
        self.assertIsInstance(g0.gi_code, types.CodeType)
        self.assertIs(g0.gi_code, g1.gi_code)
        self.assertIsNot(g0.gi_frame, g1.gi_frame)
        self.assertEqual(g0.__name__, "exhausted_generator")


class TestGeneratorPickling(StacklessTestCase):
    def test_name(self):
        def g():
            yield 1
        gen_orig = g()
        p = pickle.dumps(gen_orig)
        gen_new = pickle.loads(p)

        self.assertEqual(gen_new.__name__, gen_orig.__name__)

    def test_qualname(self):
        def g():
            yield 1
        gen_orig = g()
        p = pickle.dumps(gen_orig)
        gen_new = pickle.loads(p)

        self.assertEqual(gen_new.__qualname__, gen_orig.__qualname__)

    def test_exhausted(self):
        def g():
            yield 1
        gen_orig = g()
        self.assertEqual(gen_orig.__next__(), 1)
        self.assertRaises(StopIteration, gen_orig.__next__)
        p = pickle.dumps(gen_orig)
        self.assertRaises(StopIteration, gen_orig.__next__)
        gen_new = pickle.loads(p)
        self.assertRaises(StopIteration, gen_new.__next__)


class TestAsyncGenPickling(StacklessTestCase):
    def setUp(self):
        StacklessTestCase.setUp(self)
        self.firstiter_called = None
        self.finalizer_called = None
        self.finalizer2_called = None

    async def ag(self):
        yield 1
        yield 2

    @contextlib.contextmanager
    def asyncgen_hooks(self, firstiter, finalizer):
        old_agen_hooks = sys.get_asyncgen_hooks()
        sys.set_asyncgen_hooks(firstiter=firstiter,
                               finalizer=finalizer)
        try:
            yield None
        finally:
            sys.set_asyncgen_hooks(*old_agen_hooks)

    @contextlib.contextmanager
    def pickle_flags(self, flags):
        old_flags = stackless.pickle_flags(flags)
        try:
            yield old_flags
        finally:
            stackless.pickle_flags(old_flags)

    def finalizer(self, async_gen):
        self.finalizer_called = id(async_gen)

    def finalizer2(self, async_gen):
        self.finalizer2_called = id(async_gen)

    def firstiter(self, async_gen):
        self.firstiter_called = id(async_gen)

    def _test_finalizer(self, pf_dump, pf_load):
        ag = self.ag()
        id_ag = id(ag)
        with self.asyncgen_hooks(self.firstiter, self.finalizer):
            ag.__anext__().close()
            self.assertEqual(self.firstiter_called, id_ag)
        with self.pickle_flags(pf_dump):
            p = pickle.dumps(ag)

        del ag
        self.assertEqual(self.finalizer_called, id_ag)

        self.finalizer_called = self.finalizer2_called = self.firstiter_called = None

        with self.asyncgen_hooks(self.firstiter, self.finalizer2):
            with self.pickle_flags(pf_load):
                ag = pickle.loads(p)

        id_ag = id(ag)
        del ag
        self.assertEqual(self.firstiter_called,
                         id_ag if (pf_load, pf_dump) in ((0, 0), (2, 0), (0, 2)) else None)
        self.assertEqual(self.finalizer_called,
                         id_ag if (pf_load, pf_dump) in ((2, 2), ) else None)
        self.assertEqual(self.finalizer2_called,
                         id_ag if (pf_load, pf_dump) in ((0, 0), (2, 0), (0, 2)) else None)

        self.finalizer_called = self.finalizer2_called = self.firstiter_called = None

        with self.pickle_flags(pf_load):
            ag = pickle.loads(p)

        id_ag = id(ag)
        with self.asyncgen_hooks(self.firstiter, self.finalizer2):
            ag.__anext__().close()
        del ag
        self.assertEqual(self.firstiter_called,
                         id_ag if pf_load == 4 or pf_dump == 4 else None)
        self.assertEqual(self.finalizer_called,
                         id_ag if (pf_load, pf_dump) in ((2, 2), ) else None)
        self.assertEqual(self.finalizer2_called,
                         id_ag if pf_load == 4 or pf_dump == 4 else None)

    def test_finalizer(self):
        for pf_dump in (0, 2, 4):
            for pf_load in (0, 2, 4):
                with self.subTest(pf_dump=pf_dump, pf_load=pf_load):
                    self._test_finalizer(pf_dump, pf_load)


if __name__ == '__main__':
    unittest.main()
