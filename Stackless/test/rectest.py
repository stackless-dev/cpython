import sys
pr = 1

def f(x):
    if x % 1000 == 0 and pr: print x
    if x:
        f(x-1)
    if x % 1000 == 0 and pr: print x

# these are deprecated
##sys.enable_stackless(1)
##print "slicing interval =", sys.getslicinginterval()

sys.setrecursionlimit(min(sys.maxint, 2**31-1))
f(100000)
