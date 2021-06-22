# become tasklet

def f(ch):
    print "hi there"
    print "now becoming a tasklet"
    #become_tasklet()
    got = tasklet().capture(42)
    print "captured", got
    print ch.receive()
    print "that's all, folks"

from stackless import *

ch = channel()
print "here is main"
print getcurrent()
print "now calling f(ch)"
t = f(ch)
print t
ch.send("great!")
print "end of main"
