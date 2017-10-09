#!/usr/bin/env python
# -*- coding: utf-8 -*-
from __future__ import division, absolute_import, print_function, unicode_literals

import sys
import time

PY3 = sys.version_info[0] >= 3

# speed test
if PY3:
    import _thread as thread
    range = range
else:
    import thread


class StacklessError(Exception):
    pass

try:
    from stackless import *
    from stackless import test_cframe, test_cframe_nr, test_outside
    IS_SLP = True
except ImportError:
    IS_SLP = False
    print("This is not Stackless Python. Most tests will not work.")

    def schedule(*args):
        raise StacklessError
    test_outside = test_cframe = test_cframe_nr = schedule

    def enable_softswitch(n):
        pass

    class stackless:
        debug = 0  # assume to be tested with normal C-Python(r)
        uncollectables = []
    tasklist = []

    class tasklet(object):

        def __init__(self, func):
            self.func = func
            tasklist.append(self)

        def __call__(self, *args):
            self.args = args

        def run(self):
            self.func(*self.args)

    class channel:

        def __init__(self):
            raise StacklessError

    def run():
        global tasklist
        try:
            for each in tasklist:
                each.run()
        finally:
            tasklist = []

print(sys.version)

args_given = None
try:
    args_given = int(sys.argv[1])
except:
    if not PY3:  # PY3 does it alone
        sys.exc_clear()


def f(n, sched):
    for i in range(0, n, 20):
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()
        sched()


def cleanup():
    # kill uncollectables from hard switching and threads
    for task in stackless.uncollectables:
        if task.blocked:
            task.kill()


def tester(func, niter, args, msg, ntasks=10, run=run):
    print("%8d %s" % (niter, msg, ), end=' ')
    clock = time.clock
    args = (niter // ntasks,) + args
    diff = 0
    try:
        for i in range(ntasks):
            tasklet(func)(*args)
        start = clock()
        run()
        diff = clock() - start
        if diff == 0:
            print('no timing possible')
        else:
            print("took %9.5f seconds, rate = %10d/s" % (diff, niter / diff))
    except StacklessError:
        print("could not run, this is not Stackless")
    return diff

# generator test


def gf(n):
    for i in range(0, n, 20):
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i
        yield i


def gentest(n):
    for i in gf(n):
        pass


def channel_sender(chan, nest=0, bulk=0):
    if nest:
        return channel_sender(chan, nest - 1)
    if bulk:
        data = range(20)
        send_seq = chan.send_sequence
        send_exc = chan.send_exception
        while 1:
            send_seq(data)
            send_exc(StopIteration)
    send = chan.send
    while 1:
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)

# alternate, for real application cases


def channel_sender(chan, nest=0, bulk=0):
    if nest:
        return channel_sender(chan, nest - 1)
    if bulk:
        data = range(20)
        send_seq = chan.send_sequence
        send_exc = chan.send_exception

        def yielder():
            while 1:
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                yield 1
                send_exc(StopIteration)
#        while 1:
#            send_seq(yielder())
#            send_exc(StopIteration)
        send_seq(yielder())
    send = chan.send
    while 1:
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)
        send(42)


def chantest(n, nest=0, use_thread=False, bulk=False):
    if nest:
        return chantest(n, nest - 1, use_thread)
    chan = channel()
    chan.preference = 0  # fastest
    if use_thread:
        thread.start_new_thread(channel_sender, (chan,))
        # wait for thread
        while not chan.balance:
            schedule()
    else:
        tasklet(channel_sender)(chan, nest, bulk)
    if bulk:
        for i in range(0, n, 20):
            # list(chan)
            for i in chan:
                pass
        return

    recv = chan.receive

    def xrecv():
        if chan.balance:
            print("receiving from", id(chan.queue), "siblings=", getruncount())
        else:
            print("blocking")
            chan.receive()
            getcurrent().next.run()
            schedule()
    for i in range(0, n, 20):
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()
        recv()

if 0:
    def cb(*args):
        print(args)
    set_channel_callback(cb)

niter = 10000000

if stackless.debug:
    niter = 20000

if args_given:
    niter = args_given

# clear out every runnable that may be left over from something

if IS_SLP:
    while stackless.runcount > 1:
        try:
            stackless.current.next.kill()
        except:
            print("*** GHOST ALERT ***")
            raise

enable_softswitch(0)
res = []
res.append(tester(f, niter, (schedule,), "frame switches     "))
enable_softswitch(1)
res.append(tester(f, niter, (schedule,), "frame softswitches "))
res.append(tester(f, niter, (sys._getframe,), "cfunction calls    "))
res.append(tester(test_cframe_nr, niter, (), "cframe softswitches"))
enable_softswitch(0)
res.append(tester(chantest, niter, (), "channel hard top   "))
res.append(tester(chantest, niter, (3,), "channel hard nest 3"))
enable_softswitch(1)
res.append(tester(chantest, niter, (), "channel soft       "))
res.append(tester(chantest, niter, (0, 0, 1), "channel iterator   "))
res.append(tester(chantest, niter // 10, (0, 1), "channel real thread"))
res.append(tester(f, niter, (lambda: 0,), "function calls     "))
res.append(tester(gentest, niter, (), "generator calls    "))
res.append(tester(test_cframe, niter // 10, (), "cframe from outside", 1, test_outside))
res.append(tester(test_cframe, niter, (), "cframe switches    "))
res.append(tester(test_cframe, niter, (100,), "cframe 100 words   "))

if IS_SLP:
    if len(res) >= 2:
        print("The penalty per stack word is about %0.3f percent of raw switching." % (
            (res[-1] - res[-2]) / res[-2]))

    import struct
    adrsize = len(struct.pack("P", 0))

    def stacksize(tasklet):
        stack = bytes(tasklet.cstate)
        return len(stack) / adrsize

    tmp = enable_softswitch(0)
    t = tasklet(f)(1, schedule)
    print("Stack size of initial stub   =", stacksize(t))
    schedule()
    print("Stack size of frame tasklet  =", stacksize(t))
    t = tasklet(test_cframe)(1)
    schedule()
    print("Stack size of cframe tasklet =", stacksize(t))

    enable_softswitch(tmp)
    # clear all tasklets
    run()
    cleanup()

results_2002_07_28 = """
D:\slpdev\src\2.2\src\PCbuild>\python22\python taskspeed.py
hey this is sitepython
10000000 frame switches   took 3.46863 seconds, rate = 2882985/s
10000000 cfunction calls  took 2.07762 seconds, rate = 4813209/s
10000000 cframe switches  took 1.79576 seconds, rate = 5568666/s
10000000 cframe 100 words took 3.36050 seconds, rate = 2975750/s
The penalty per stack word is about 0.871 percent of raw switching.
Stack size of initial stub   = 16
Stack size of frame tasklet  = 57
Stack size of cframe tasklet = 30
"""
# this will become better by at least 5 words, soon,
# since I'm going to make the switching functions parameterless.

# here we are, after the fact :-)
results_2002_08_15 = """
D:\slpdev\src\2.2\src\PCbuild>python taskspeed.py
10000000 frame switches   took 3.40898 seconds, rate = 2933428/s
10000000 cfunction calls  took 2.29988 seconds, rate = 4348046/s
10000000 cframe switches  took 1.50435 seconds, rate = 6647368/s
10000000 cframe 100 words took 3.14673 seconds, rate = 3177897/s
The penalty per stack word is about 1.092 percent of raw switching.
Stack size of initial stub   = 13
Stack size of frame tasklet  = 51
Stack size of cframe tasklet = 24
"""
import gc
gc.collect()
