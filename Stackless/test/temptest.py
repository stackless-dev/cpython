# speed test
import time
import sys
import struct
from stackless import *
from stackless import test_cframe

enable_softswitch(1)


def f(x):
    print"f start/end"


def schedule_cb(prev, next):
    print
    if not prev:
        print "starting", next, next.nesting_level
    elif not next:
        print "ending", prev, prev.nesting_level
    else:
        print "suspending", prev, prev.nesting_level
        print "resuming", next, next.nesting_level

set_schedule_callback(schedule_cb)


class MyTasklet(tasklet):
    __slots__ = ["name"]

    def __init__(self, func, name=None):
        tasklet.__init__(self, func)
        if not name:
            name = "at " + hex(id(self))
        self.name = name

    def __new__(self, func, name):
        return tasklet.__new__(self, func)

    def __repr__(self):
        return "Tasklet %s" % self.name

t = MyTasklet(f, "function")(1)
t = MyTasklet(test_cframe, "test_cframe")(3)
print "schedule"
schedule()
print "run"
42 << 3
run()
set_schedule_callback(None)

adrsize = len(struct.pack("P", 0))


def stacksize(tasklet):
    stack = str(tasklet.cstate)
    return len(stack) / adrsize

# this thing exactly works if the below is set to 0.
# this is very bad. Move this to temptest and test it there!
# everything else seems to work.
tmp = enable_softswitch(0)


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

t = tasklet(f)(1, schedule)
print "Stack size of initial stub   =", stacksize(t)
schedule()
print "Stack size of frame tasklet  =", stacksize(t)
t = tasklet(test_cframe)(1)
schedule()
print "Stack size of cframe tasklet =", stacksize(t)
run()
enable_softswitch(tmp)
import gc
gc.collect()
