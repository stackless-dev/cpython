import sys
import types
import unittest
import cPickle as pickle
import gc

from stackless import schedule, tasklet, stackless

from support import StacklessTestCase


#because test runner instances in the testsuite contain copies of the old stdin/stdout thingies,
#we need to make it appear that pickling them is ok, otherwise we will fail when pickling
#closures that refer to test runner instances
import copy_reg
import sys
def reduce(obj):
    return object, () #just create an empty object instance
copy_reg.pickle(type(sys.stdout), reduce, object)

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
        rectest(nrec, lev+1, lst)
    else:
        schedule()
    return lst

class TaskletChannel:
    def __init__(self):
        self.channel = stackless.channel()
    def run(self):
        self.channel.receive()

class CustomTasklet(tasklet):
    __slots__ = [ "name" ]

def listtest(n, when):
    for i in range(n):
        if i == when:
            schedule()
    return i

def xrangetest(n, when):
    for i in xrange(n):
        if i == when:
            schedule()
    return i

def enumeratetest(n, when):
    for i, ig in enumerate([None] * n):
        if i == when:
            schedule()
    return i

def dicttestiterkeys(n, when):
    for i in dict([ (i, i) for i in range(n) ]).iterkeys():
        if i == when:
            schedule()
    return n

def dicttestitervalues(n, when):
    for i in dict([ (i, i) for i in range(n) ]).itervalues():
        if i == when:
            schedule()
    return n

def dicttestiteritems(n, when):
    for (i, j) in dict([ (i, i) for i in range(n) ]).iteritems():
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

def in_psyco():
    try:
        return __in_psyco__
    except NameError:
        return False
    
def is_soft():
    softswitch = stackless.enable_softswitch(0)
    stackless.enable_softswitch(softswitch)
    return softswitch and not in_psyco()

class TestPickledTasklets(StacklessTestCase):
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

        if self.verbose: print "starting tasklet"
        t.run()

        self.assertEqual(is_empty(), True)

        # do we want to do this??
        #t.tempval = None

        if self.verbose: print "pickling"
        pi = pickle.dumps(t)

        # if self.verbose: print repr(pi)
        # why do we want to remove it?
        # t.remove()

        if self.verbose: print "unpickling"
        ip = pickle.loads(pi)

        if self.verbose: print "starting unpickled tasklet"
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

    # compatibility to 2.2.3
    global have_enumerate
    try:
        enumerate
        have_enumerate = True
    except NameError:
        have_enumerate = False

    global have_fromkeys
    try:
        {}.fromkeys
        have_fromkeys = True
    except AttributeError:
        have_fromkeys = False

class TestConcretePickledTasklets(TestPickledTasklets):
    def testClassPersistence(self):
        t1 = CustomTasklet(nothing)()
        s = pickle.dumps(t1)
        t2 = pickle.loads(s)
        self.assertEqual(t1.__class__, t2.__class__)

    def testGenerator(self):
        self.run_pickled(genoutertest, 20, 13)

    def testList(self):
        self.run_pickled(listtest, 20, 13)

    def testXrange(self):
        self.run_pickled(xrangetest, 20, 13)

    def testRecursive(self):
        self.run_pickled(rectest, 13)

    ## Pickling of all three dictionary iterator types.

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

    if have_enumerate:
        def testEnumerate(self):
            self.run_pickled(enumeratetest, 20, 13)

    def testTuple(self):
        self.run_pickled(tupletest, 20, 13)

    def testGeneratorScheduling(self):
        self.run_pickled(genschedoutertest, 20, 13)

    def testRecursiveLambda(self):
        recurse = lambda self, next: \
            next-1 and self(self, next-1) or (schedule(), 42)[1]
        pickle.loads(pickle.dumps(recurse))
        self.run_pickled(recurse, recurse, 13)

    def testRecursiveEmbedded(self):
        # Avoid self references in this function, to prevent crappy unit testing framework
        # magic from getting pickled and refusing to unpickle.
        def rectest(verbose, nrec, lev=0):
            if verbose: print str(nrec), lev
            if lev < nrec:
                rectest(verbose, nrec, lev+1)
            else:
                schedule()
        self.run_pickled(rectest, self.verbose, 13)

    def testCell(self):
        self.run_pickled(cellpickling)

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
        s = pickle.dumps(t)
        # Unpickle it again, but don't hold a reference.
        pickle.loads(s)
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
            l = [ 1, 2, 3, 4 ]
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

        pickle.dumps(t1)

    def testSubmoduleImporting(self):
        # When a submodule was pickled, it would unpickle as the
        # module instead.
        import xml.sax
        m1 = xml.sax
        m2 = pickle.loads(pickle.dumps(m1))
        self.assertEqual(m1, m2)

    def testFunctionModulePreservation(self):
        # The 'module' name on the function was not being preserved.
        f1 = lambda: None
        f2 = pickle.loads(pickle.dumps(f1))
        self.assertEqual(f1.__module__, f2.__module__)

class TestOldStackless_WrapFactories(StacklessTestCase):
    def testXrange(self):
        # stackless python prior to 2.7.3 used to register its own __reduce__
        # method for xrange with copy_reg. This method returnd the function
        # stackless._wrap.xrange as xrange factory.
        #
        # This test case ensures that stackless can still unpickle old pickles:

        # an old pickle, protocol 0 of xrange(123, 798, 45)
        p = 'cstackless._wrap\nxrange\n(I123\nI798\nI45\ntR(tb.'
		
        # Output of
        # from pickletools import dis; dis(p)
        #    0: c    GLOBAL     'stackless._wrap xrange'
        #   24: (    MARK
        #   25: I        INT        123
        #   30: I        INT        798
        #   35: I        INT        45
        #   39: t        TUPLE      (MARK at 24)
        #   40: R    REDUCE
        #   41: (    MARK
        #   42: t        TUPLE      (MARK at 41)
        #   43: b    BUILD
        #   44: .    STOP
        # highest protocol among opcodes = 0
		
        x = pickle.loads(p)
        self.assertIs(type(x), xrange)
        self.assertEqual(repr(x), repr(xrange(123, 798, 45)))

if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')
    unittest.main()
