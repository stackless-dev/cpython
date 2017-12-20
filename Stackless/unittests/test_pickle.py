import sys
import types
import unittest
import gc
import inspect
import copy

from stackless import schedule, tasklet, stackless

from support import test_main  # @UnusedImport
from support import StacklessTestCase, StacklessPickleTestCase, get_reduce_frame


# because test runner instances in the testsuite contain copies of the old stdin/stdout thingies,
# we need to make it appear that pickling them is ok, otherwise we will fail when pickling
# closures that refer to test runner instances
import copyreg


def reduce(obj):
    return object, ()  # just create an empty object instance
copyreg.pickle(type(sys.stdout), reduce, object)

VERBOSE = False
glist = []


def nothing():
    pass


def accumulate(ident, func, *args):
    rval = (ident, func(*args))
    glist.append(rval)


def get_result():
    return glist.pop()


def is_empty():
    return len(glist) == 0


def reset():
    glist[:] = []


def rectest(nrec, lev=0, lst=None):
    if lst is None:
        lst = []
    lst.append(lev)
    if lev < nrec:
        rectest(nrec, lev + 1, lst)
    else:
        schedule()
    return lst


class TaskletChannel:

    def __init__(self):
        self.channel = stackless.channel()

    def run(self):
        self.channel.receive()


class CustomTasklet(tasklet):
    __slots__ = ["name"]


def listtest(n, when):
    for i in range(n):
        if i == when:
            schedule()
    return i


def xrangetest(n, when):
    for i in range(n):
        if i == when:
            schedule()
    return i


def enumeratetest(n, when):
    for i, ig in enumerate([None] * n):
        if i == when:
            schedule()
    return i


def dicttestiterkeys(n, when):
    for i in dict([(i, i) for i in range(n)]).keys():
        if i == when:
            schedule()
    return n


def dicttestitervalues(n, when):
    for i in dict([(i, i) for i in range(n)]).values():
        if i == when:
            schedule()
    return n


def dicttestiteritems(n, when):
    for (i, j) in dict([(i, i) for i in range(n)]).items():
        if i == when:
            schedule()
    return n


def settest(n, when):
    for i in set(range(n)):
        if i == when:
            schedule()
    return i


def tupletest(n, when):
    for i in tuple(range(n)):
        if i == when:
            schedule()
    return i


def genschedinnertest(n, when):
    for i in range(n):
        if i == when:
            schedule()
        yield i


def genschedoutertest(n, when):
    for x in genschedinnertest(n, when):
        pass
    return x


def geninnertest(n):
    for x in range(n):
        yield x


def genoutertest(n, when):
    for i in geninnertest(n):
        if i == when:
            schedule()
    return i


def cellpickling():
    """defect:  Initializing a function object with a partially constructed
       cell object
    """
    def closure():
        localvar = the_closure
        return
    the_closure = closure
    del closure
    schedule()
    return the_closure()


class CtxManager(object):

    def __init__(self, n, when, expect_type, suppress_exc):
        self.n = n
        assert when in ('enter', 'body', 'exit')
        self.when = when
        self.expect_type = expect_type
        self.suppress_exc = suppress_exc
        self.ran_enter = self.ran_exit = False

    def __enter__(self):
        self.ran_enter = True
        if self.when == 'enter':
            lst = rectest(self.n)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.ran_exit = True
        if not is_soft() and isinstance(exc_val, SystemExit):
            return False
        self.exc_type = exc_type
        if self.when == 'exit':
            lst = rectest(self.n)
        return self.suppress_exc


def ctxpickling(testCase, n, when, expect_type, suppress_exc):
    ctxmgr = CtxManager(n, when, expect_type, suppress_exc)

    ran_body = False
    try:
        with ctxmgr as c:
            ran_body = True
            if when == 'body':
                rectest(n)
            if expect_type:
                raise expect_type("raised by ctxpickling")
    except SystemExit:
        raise
    except:
        exc_info = sys.exc_info()
    else:
        exc_info = (None, None, None)

    testCase.assertEqual(ctxmgr.ran_enter, True, "__enter__ did not run")
    testCase.assertTrue(ctxmgr.ran_exit, "__exit__ did not run")
    if expect_type is not None:
        testCase.assertEqual(ctxmgr.exc_type, expect_type, "incorrect ctxmgr.exc_type: expected %r got %r" % (expect_type, ctxmgr.exc_type))
        if suppress_exc:
            testCase.assertEqual(exc_info, (None, None, None), "Suppress didn't work")
        else:
            testCase.assertEqual(exc_info[0], expect_type, "incorrect exc_type")
    else:
        testCase.assertEqual(ctxmgr.exc_type, None, "ctxmgr.exc_type is not None: %r" % (ctxmgr.exc_type,))
        testCase.assertEqual(exc_info, (None, None, None), "unexpected exception: " + repr(exc_info[1]))

    testCase.assertTrue(ran_body, "with block did not run")
    testCase.assertEqual(ctxmgr, c, "__enter__ returned wrong value")
    return "OK"


class InitTestClass:
    def __init__(self, testcase):
        testcase.started = True
        schedule()


def inittest(testcase, cls):
    # test pickling of stackless __init__
    testcase.started = False
    obj = cls(testcase)
    testcase.assertTrue(testcase.started)
    return "OK"


def is_soft():
    softswitch = stackless.enable_softswitch(0)
    stackless.enable_softswitch(softswitch)
    return softswitch


class TestPickledTasklets(StacklessPickleTestCase):

    def setUp(self):
        super(TestPickledTasklets, self).setUp()
        self.verbose = VERBOSE

    def tearDown(self):
        # Tasklets created in pickling tests can be left in the scheduler when they finish.  We can feel free to
        # clean them up for the tests.
        mainTasklet = stackless.getmain()
        current = mainTasklet.next
        while current is not mainTasklet:
            next = current.next
            current.kill()
            current = next

        super(TestPickledTasklets, self).tearDown()

        del self.verbose

    def run_pickled(self, func, *args):
        ident = object()
        t = tasklet(accumulate)(ident, func, *args)

        # clear out old errors
        reset()

        if self.verbose:
            print("starting tasklet")
        t.run()

        self.assertEqual(is_empty(), True)

        # do we want to do this??
        #t.tempval = None

        if self.verbose:
            print("pickling")
        pi = self.dumps(t)

        # if self.verbose: print repr(pi)
        # why do we want to remove it?
        # t.remove()

        if self.verbose:
            print("unpickling")
        ip = self.loads(pi)

        if self.verbose:
            print("starting unpickled tasklet")
        if is_soft():
            ip.run()
        else:
            self.assertRaises(RuntimeError, ip.run)
            return
        new_ident, new_rval = get_result()
        t.run()
        old_ident, old_rval = get_result()
        self.assertEqual(old_ident, ident)
        self.assertEqual(new_rval, old_rval)
        self.assertNotEqual(new_ident, old_ident)
        self.assertEqual(is_empty(), True)

    def testClassPersistence(self):
        t1 = CustomTasklet(nothing)()
        s = self.dumps(t1)
        t2 = self.loads(s)
        self.assertEqual(t1.__class__, t2.__class__)

    def testGenerator(self):
        self.run_pickled(genoutertest, 20, 13)

    def testList(self):
        self.run_pickled(listtest, 20, 13)

    def testXrange(self):
        self.run_pickled(xrangetest, 20, 13)

    def testXrangeExhausted(self):
        self.run_pickled(xrangetest, 20, 19)

    def testRecursive(self):
        self.run_pickled(rectest, 13)

    # Pickling of all three dictionary iterator types.

    # Test picking of the dictionary keys iterator.
    def testDictIterkeys(self):
        self.run_pickled(dicttestiterkeys, 20, 13)

    # Test picking of the dictionary values iterator.
    def testDictItervalues(self):
        self.run_pickled(dicttestitervalues, 20, 13)

    # Test picking of the dictionary items iterator.
    def testDictIteritems(self):
        self.run_pickled(dicttestiteritems, 20, 13)

    # Test pickling of iteration over the set type.
    def testSet(self):
        self.run_pickled(settest, 20, 13)

    def testEnumerate(self):
        self.run_pickled(enumeratetest, 20, 13)

    def testTuple(self):
        self.run_pickled(tupletest, 20, 13)

    def testGeneratorScheduling(self):
        self.run_pickled(genschedoutertest, 20, 13)

    def testRecursiveLambda(self):
        recurse = lambda self, next: next - 1 and self(self, next - 1) or (schedule(), 42)[1]  # @IgnorePep8
        self.loads(self.dumps(recurse))
        self.run_pickled(recurse, recurse, 13)

    def testRecursiveEmbedded(self):
        # Avoid self references in this function, to prevent crappy unit testing framework
        # magic from getting pickled and refusing to unpickle.
        def rectest(verbose, nrec, lev=0):
            if verbose:
                print(str(nrec), lev)
            if lev < nrec:
                rectest(verbose, nrec, lev + 1)
            else:
                schedule()
        self.run_pickled(rectest, self.verbose, 13)

    def testCell(self):
        self.run_pickled(cellpickling)

    def testCtx_enter_0(self):
        self.run_pickled(ctxpickling, self, 0, 'enter', None, False)

    def testCtx_enter_1(self):
        self.run_pickled(ctxpickling, self, 1, 'enter', None, False)

    def testCtx_enter_2(self):
        self.run_pickled(ctxpickling, self, 2, 'enter', RuntimeError, False)

    def testCtx_enter_3(self):
        self.run_pickled(ctxpickling, self, 1, 'enter', RuntimeError, True)

    def testCtx_body_0(self):
        self.run_pickled(ctxpickling, self, 0, 'body', None, False)

    def testCtx_body_1(self):
        self.run_pickled(ctxpickling, self, 1, 'body', None, False)

    def testCtx_body_2(self):
        self.run_pickled(ctxpickling, self, 2, 'body', RuntimeError, False)

    def testCtx_body_3(self):
        self.run_pickled(ctxpickling, self, 1, 'body', RuntimeError, True)

    def testCtx_body_4(self):
        self.run_pickled(ctxpickling, self, 2, 'body', RuntimeError, True)

    def testCtx_body_5(self):
        self.run_pickled(ctxpickling, self, 1, 'body', RuntimeError, False)

    def testCtx_body_6(self):
        self.run_pickled(ctxpickling, self, 0, 'body', RuntimeError, True)

    def testCtx_body_7(self):
        self.run_pickled(ctxpickling, self, 0, 'body', RuntimeError, False)

    def testCtx_exit_0(self):
        self.run_pickled(ctxpickling, self, 0, 'exit', None, False)

    def testCtx_exit_1(self):
        self.run_pickled(ctxpickling, self, 1, 'exit', None, False)

    def testCtx_exit_2(self):
        self.run_pickled(ctxpickling, self, 2, 'exit', RuntimeError, False)

    def testCtx_exit_3(self):
        self.run_pickled(ctxpickling, self, 0, 'exit', RuntimeError, True)

    def testCtx_exit_4(self):
        self.run_pickled(ctxpickling, self, 1, 'exit', RuntimeError, True)

    def test__init__(self):
        self.run_pickled(inittest, self, InitTestClass)

    def testFakeModules(self):
        types.ModuleType('fakemodule!')

    # Crash bug.  See SVN revision 45902.
    # Unpickling race condition where a tasklet unexpectedly had setstate
    # called on it before the channel it was blocked on.  This left it
    # linked to the channel but not blocked, which meant that when it was
    # scheduled it was not linked to the scheduler, and this caused a
    # crash when the scheduler tried to remove it when it exited.
    def testGarbageCollection(self):
        # Make a class that holds onto a reference to a channel
        # and then block it on that channel.
        tc = TaskletChannel()
        t = stackless.tasklet(tc.run)()
        t.run()
        # Pickle the tasklet that is blocking.
        s = self.dumps(t)
        # Unpickle it again, but don't hold a reference.
        self.loads(s)
        # Force the collection of the unpickled tasklet.
        gc.collect()

    def testSendSequence(self):
        # Send sequence when pickled was not handled.  It uses
        # a custom cframe execute function which was not recognised
        # by the pickling.
        #
        # Traceback (most recent call last):
        #   File ".\test_pickle.py", line 283, in testSendSequence
        #     pickle.dumps(t1)
        # ValueError: frame exec function at 1e00bf40 is not registered!

        def sender(chan):
            l = [1, 2, 3, 4]
            chan.send_sequence(l)

        def receiver(chan):
            length = 4
            while length:
                v = chan.receive()
                length -= 1

        c = stackless.channel()
        t1 = stackless.tasklet(sender)(c)
        t2 = stackless.tasklet(receiver)(c)
        t1.run()

        self.dumps(t1)

    def testSubmoduleImporting(self):
        # When a submodule was pickled, it would unpickle as the
        # module instead.
        import xml.sax
        m1 = xml.sax
        m2 = self.loads(self.dumps(m1))
        self.assertEqual(m1, m2)

    def testFunctionModulePreservation(self):
        # The 'module' name on the function was not being preserved.
        f1 = lambda: None  # @IgnorePep8
        f2 = self.loads(self.dumps(f1))
        self.assertEqual(f1.__module__, f2.__module__)


class TestFramePickling(StacklessTestCase):
    def test_get_set_reduce_frame(self):
        # test setting / getting the reduce frame function
        rf = get_reduce_frame()
        self.assertTrue(callable(rf))
        stackless._wrap.set_reduce_frame(None)
        self.assertIsNone(get_reduce_frame())
        stackless._wrap.set_reduce_frame(rf)
        self.assertIs(get_reduce_frame(), rf)

    def testLocalplus(self):
        result = []

        def reduce_current():
            """This function has exactly one local variable (func), one cellvar (result2) and one freevar (result)"""
            result2 = result  # create the cell variable

            def func(current):
                result2.append(stackless._wrap.frame.__reduce__(current.frame))
            stackless.tasklet().bind(func, (stackless.current,)).run()
            return result[0]

        cell_type = type(reduce_current.__closure__[0])

        state = reduce_current()[2]
        self.assertIsInstance(state, tuple)
        self.assertEqual(len(state), 12)

        code = state[0]
        self.assertIsInstance(code, types.CodeType)
        ncellvars = len(code.co_cellvars)
        nfreevars = len(code.co_freevars)
        self.assertEqual(ncellvars, 1)
        self.assertEqual(nfreevars, 1)

        localsplus_as_tuple = state[-1]
        valid = state[1]

        self.assertEqual(valid, int(stackless.enable_softswitch(None) and
                                    stackless.current.nesting_level == 0))
        self.assertIsInstance(localsplus_as_tuple, tuple)
        self.assertGreaterEqual(len(localsplus_as_tuple), 1 + code.co_nlocals + ncellvars + nfreevars)
        for i in range(ncellvars + nfreevars):
            cell = localsplus_as_tuple[1 + code.co_nlocals + i]
            self.assertIsInstance(cell, cell_type)
            self.assertIs(cell.cell_contents, result)

    def testSetstateFailure(self):
        # incomplete, just one of many failure modes of stackless._wrap.frame.__setstate__
        foo = "foo"

        def f1(bar="bar"):
            try:
                1 / 0
            except ZeroDivisionError:
                x = foo
                locals()
                return stackless._wrap.frame.__reduce__(sys._getframe())
            assert False

        def f2():
            try:
                1 / 0
            except ZeroDivisionError:
                return f1()

        r = f2()
        self.assertEqual(len(r), 3)
        wrap_frame = r[0](*r[1])
        self.assertIsInstance(wrap_frame, stackless._wrap.frame)
        invalid_state = r[2][:-2] + ((("Not a", "tuple of 3", "integers"),), r[2][-1])
        self.assertRaisesRegex(TypeError, "an integer is required", wrap_frame.__setstate__, invalid_state)
        # must not raise an assertion
        wrap_frame.__setstate__(r[2])
        self.assertIs(type(wrap_frame), types.FrameType)


class TestDictViewPickling(StacklessPickleTestCase):

    def testDictKeyViewPickling(self):
        # stackless python prior to 2.7.3 used to register its own __reduce__
        d = {1: 2}
        view1 = d.keys()
        view2 = self.loads(self.dumps(view1))
        self.assertEqual(list(view1), list(view2))

    def testDictItemViewPickling(self):
        d = {1: 2}
        view1 = d.items()
        view2 = self.loads(self.dumps(view1))
        self.assertEqual(list(view1), list(view2))

    def testDictValueViewPickling(self):
        d = {1: 2}
        view1 = d.values()
        view2 = self.loads(self.dumps(view1))
        self.assertEqual(list(view1), list(view2))


class TestTraceback(StacklessPickleTestCase):
    def testTracebackFrameLinkage(self):
        def a():
            # raise an exception
            1 // 0

        def b():
            return a()

        def c():
            return b()

        def d():
            try:
                c()
            except ZeroDivisionError:
                return sys.exc_info()[2]

        tb = d()  # this way tb dosn't reference the current frame
        innerframes_orig = inspect.getinnerframes(tb)
        p = self.dumps(tb)
        tb2 = self.loads(p)
        # basics
        innerframes_orig = inspect.getinnerframes(tb)
        self.assertIs(type(tb), type(tb2))
        self.assertIsNot(tb, tb2)
        innerframes = inspect.getinnerframes(tb2)
        # compare the content
        self.assertListEqual([i[1:] for i in innerframes_orig], [i[1:] for i in innerframes])
        # check linkage
        all_outerframes_orig = inspect.getouterframes(innerframes_orig[-1][0])
        all_outerframes = inspect.getouterframes(innerframes[-1][0])
        l = len(innerframes_orig)
        self.assertGreater(len(all_outerframes_orig), l)
        self.assertGreaterEqual(len(all_outerframes), l)
        # compare the content
        self.assertListEqual([i[1:] for i in all_outerframes_orig[:l - 1]], [i[1:] for i in all_outerframes[:l - 1]])


class TestCoroutinePickling(StacklessPickleTestCase):
    @types.coroutine
    def yield1(self, value):
        yield value

    async def c(self):
        await self.yield1(1)

    def test_without_pickling(self):
        c = self.c()
        self.assertIsInstance(c, types.CoroutineType)
        self.assertIsNone(c.cr_await)
        self.assertIsNotNone(c.cr_frame)
        self.assertEqual(c.send(None), 1)
        self.assertIsNotNone(c.cr_await)
        self.assertIsNotNone(c.cr_frame)
        self.assertRaises(StopIteration, c.send, None)
        self.assertIsNone(c.cr_await)
        self.assertIsNone(c.cr_frame)
        self.assertRaisesRegex(RuntimeError, "cannot reuse already awaited coroutine", c.send, None)

    def test_pickling1(self):
        c = self.c()
        p = self.dumps(c)
        c.send(None)
        c = self.loads(p)
        self.assertIsInstance(c, types.CoroutineType)
        self.assertIsNone(c.cr_await)
        self.assertIsNotNone(c.cr_frame)
        self.assertEqual(c.send(None), 1)
        self.assertRaises(StopIteration, c.send, None)

    def test_pickling2(self):
        c = self.c()
        self.assertEqual(c.send(None), 1)
        p = self.dumps(c)
        c = self.loads(p)
        self.assertIsInstance(c, types.CoroutineType)
        self.assertIsNotNone(c.cr_await)
        self.assertIsNotNone(c.cr_frame)
        self.assertRaises(StopIteration, c.send, None)

    def test_pickling3(self):
        c = self.c()
        self.assertEqual(c.send(None), 1)
        self.assertRaises(StopIteration, c.send, None)
        p = self.dumps(c)
        c = self.loads(p)
        self.assertIsInstance(c, types.CoroutineType)
        self.assertIsNone(c.cr_await)
        self.assertIsNone(c.cr_frame)
        self.assertRaisesRegex(RuntimeError, "cannot reuse already awaited coroutine", c.send, None)


class TestCopy(StacklessTestCase):
    ITERATOR_TYPE = type(iter("abc"))

    def _test(self, obj, *attributes, **kw):
        expected_type = kw.get("expected_type", type(obj))
        c = copy.copy(obj)
        try:
            obj_hash = hash(obj)
        except TypeError:
            obj_hash = None
        if obj_hash is None:
            self.assertIsNot(c, obj)
        if c is obj:
            return
        self.assertIs(type(c), expected_type)
        for name in attributes:
            value_obj = getattr(obj, name)
            value_c = getattr(c, name)
            # it is a shallow copy, therefore the attributes should
            # refer to the same objects
            self.assertIs(value_c, value_obj)
        return c

    def test_module_stackless(self):
        # test for issue 128
        self.assertIs(stackless, copy.copy(stackless))

    def test_code(self):
        def f():
            pass
        obj = f.__code__
        self._test(obj, "co_argcount", "co_nlocals", "co_stacksize", "co_flags",
                   "co_code", "co_consts", "co_names", "co_varnames", "co_filename",
                   "co_name", "co_firstlineno", "co_lnotab", "co_freevars", "co_cellvars")

    def test_cell(self):
        def create_cell(obj):
            return (lambda: obj).__closure__[0]
        obj = create_cell(None)
        self._test(obj, 'cell_contents')

    def test_function(self):
        def obj():
            pass
        self._test(obj, '__code__', '__globals__', '__name__', '__defaults__', '__closure__')

    def test_frame(self):
        obj = sys._getframe()
        # changed in version 3.4.4: Stackless no longer pretends to be able to
        # pickle arbitrary frames
        self.assertRaises(TypeError, self._test, obj)

    def test_traceback(self):
        try:
            1 // 0
        except ZeroDivisionError:
            obj = sys.exc_info()[2]
        self._test(obj, 'tb_frame', 'tb_next')

    def test_module(self):
        import tabnanny as obj
        self._test(obj, '__name__', '__dict__')

    def test_method(self):
        obj = self.id
        self._test(obj, '__func__', '__self__')

    def test_method_wrapper(self):
        obj = [].__len__
        self._test(obj, '__objclass__', '__self__')

    def test_generator(self):
        def g():
            yield 1
        obj = g()
        self._test(obj, 'gi_running', 'gi_code', '__name__', '__qualname__')

    def test_dict_keys(self):
        d = {1: 10, "a": "ABC"}
        obj = d.keys()
        self._test(obj)

    def test_dict_values(self):
        d = {1: 10, "a": "ABC"}
        obj = d.values()
        self._test(obj)

    def test_dict_items(self):
        d = {1: 10, "a": "ABC"}
        obj = d.items()
        self._test(obj)

    def test_coroutine(self):
        async def c():
            return 1
        obj = c()
        c = self._test(obj, 'cr_running', 'cr_code', '__name__', '__qualname__')
        self.assertRaises(StopIteration, obj.send, None)
        self.assertRaises(StopIteration, c.send, None)


if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
