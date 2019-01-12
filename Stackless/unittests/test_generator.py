from __future__ import absolute_import
import unittest
import gc
import stackless
import types
import pickle
import contextlib
import sys
import contextvars
import asyncio

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

    def run_GC_test(self, task, arg):
        tasklet = stackless.tasklet(task)(arg)
        tasklet.run()
        if False:  # To test the generator / coroutine / async gen
            tasklet.run()
            self.assertFalse(tasklet.alive)
            return
        self.assertTrue(tasklet.paused)
        tasklet.tempval = None
        with captured_stderr() as stringio:
            # must not throw or output
            if tasklet.restorable:
                tasklet.bind(None)
            # make sure, that t=None kills the last reference
            self.assertEqual(sys.getrefcount(tasklet), 2)
            tasklet = None
        self.assertEqual(stringio.getvalue(), "")

    def testGCRunningGenerator(self):
        def gen():
            try:
                # print("gen nesting level: ", stackless.current.nesting_level)
                stackless.schedule_remove()
                yield 1
            except:  # @IgnorePep8
                # print("exception in gen:", sys.exc_info())
                raise

        def task(generator):
            li = [i for i in generator]
            self.assertListEqual(li, [1])

        self.run_GC_test(task, gen())

    def testGCRunningCoroutine(self):
        async def coro():
            try:
                # print("coro nesting level: ", stackless.current.nesting_level)
                stackless.schedule_remove()
            except:  # @IgnorePep8
                # print("exception in coro:", sys.exc_info())
                raise

        def task(c):
            self.assertRaises(StopIteration, c.send, None)

        self.run_GC_test(task, coro())

    def testGCRunningAsyncGen(self):
        async def asyncgen():
            try:
                # print("asyncgen nesting level: ", stackless.current.nesting_level)
                stackless.schedule_remove()
            except:  # @IgnorePep8
                # print("exception in asyncgen:", sys.exc_info())
                raise
            yield 100

        def task(ag):
            c = ag.__anext__()
            with self.assertRaises(StopIteration) as cm:
                c.send(None)
            self.assertEqual(cm.exception.value, 100)
            c = ag.__anext__()
            self.assertRaises(StopAsyncIteration, c.send, None)

        self.run_GC_test(task, asyncgen())


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


class TestStacklessOperations(StacklessTestCase):
    def assertLevel(self, expected=0):
        self.assertTrue(stackless.current.alive)
        if stackless.enable_softswitch(None):
            self.assertEqual(stackless.current.nesting_level, expected)
        else:
            self.assertGreater(stackless.current.nesting_level, expected)

    def gen_1_2(self):
        self.assertLevel()
        yield 1
        self.assertLevel()
        yield 2
        self.assertLevel()

    def supergen(self, gen):
        self.assertLevel()
        yield from gen
        self.assertLevel()

    @types.coroutine
    def wrapped_generator_coro(self):
        self.assertLevel()
        yield
        self.assertLevel()
        yield
        self.assertLevel()

    @types.coroutine
    def coro1yield(self):
        yield

    async def coro(self):
        self.assertLevel()
        await self.coro1yield()
        self.assertLevel()
        await self.coro1yield()
        self.assertLevel()

    async def agen_1_2(self):
        self.assertLevel()
        await self.coro1yield()
        yield 1
        await self.coro1yield()
        self.assertLevel()
        yield 2
        await self.coro1yield()
        self.assertLevel()

    @contextlib.asynccontextmanager
    async def async_ctx_mgr(self):
        self.assertLevel()
        await self.coro1yield()
        try:
            yield
        finally:
            self.assertLevel()
            await self.coro1yield()

    def run_until_complete(self, coro):
        n = 0
        it = coro.__await__()
        while True:
            try:
                x = it.send(None)
                self.assertIsNone(x)
                n += 1
            except StopIteration:
                return n

    def test_yield_from(self):
        li = [i for i in self.supergen(self.gen_1_2())]
        self.assertListEqual(li, [1, 2])

    def test_async_for(self):
        async def test():
            li = []
            async for v in self.agen_1_2():
                li.append(v)
            self.assertListEqual(li, [1, 2])

        t = test()
        n = self.run_until_complete(t)
        self.assertEqual(n, 3)

    def test_async_with(self):
        async def test():
            async with self.async_ctx_mgr():
                await self.coro()

        t = test()
        n = self.run_until_complete(t)
        self.assertEqual(n, 4)

    def test_coroutine_wrapper(self):
        async def test():
            await self.wrapped_generator_coro()

        t = test()
        n = self.run_until_complete(t)
        self.assertEqual(n, 2)

    def test_coroutine(self):
        async def test():
            await self.coro()

        t = test()
        n = self.run_until_complete(t)
        self.assertEqual(n, 2)

    def test_async_iter_asend_send(self):
        async def test():
            a = self.agen_1_2().__aiter__()
            x = a.asend(None).send(None)

        t = test()
        n = self.run_until_complete(t)
        self.assertEqual(n, 0)

    def test_context_run(self):
        contextvars.Context().run(self.assertLevel)

    #  needs Stackless pull request #188
    def xx_test_asyncio(self):
        async def test():
            try:
                await self.coro()
            finally:
                loop.stop()

        asyncio.set_event_loop(asyncio.new_event_loop())
        self.addCleanup(asyncio.set_event_loop, None)
        loop = asyncio.get_event_loop()
        task = asyncio.tasks._PyTask(test())
        asyncio.ensure_future(task)
        try:
            loop.run_forever()
        finally:
            loop.close()


if __name__ == '__main__':
    unittest.main()
