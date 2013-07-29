# testing the new Stackless profile feature.
# there is no extra support for Stackless, yet.
# The only provided mechanism is that in case
# of profiling or tracing, these thread variables
# are saved and restored on a per-tasklet basis.

from stackless import *

import sys
sys.setrecursionlimit(min(sys.maxint, 2**31-1))

counts = [0, 0]
def func1(n):
    counts[0] += 1
    #print "func1 schedule", n
    schedule()
    if n:
        func2(n-1)

def func2(n):
    counts[1] += 1
    #print "func2 schedule", n
    schedule()
    if n:
        func1(n-1)

tasklet(func1)(123)
func1(1000)

print "run me under the profiler!"
print "true counts:", counts
