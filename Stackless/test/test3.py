import stackless, random
from timeit import default_timer as timer

wait = stackless.schedule

class InterruptTasklet(Exception):
    pass

def uniquetask(prevtasklet):
    if prevtasklet is not None and prevtasklet.alive:
        print "sending exception"
        prevtasklet.raise_exception(InterruptTasklet, InterruptTasklet())
        print "exception was sent"
    return stackless.getcurrent()

def runner(callable, args, kwds):
    try:
        callable(*args, **kwds)
    except InterruptTasklet:
        print "exception caught"

def schedule(callable, *args, **kwds):
    t = stackless.tasklet(runner)(callable, args, kwds)
    t.insert()


class ActiveSprite:

    def __init__(self):
        self.setimg = None

    def cyclic(self, images):
        self.setimg = uniquetask(self.setimg)
        for img in images:
            print "cyclic:", img
            wait()


def normal_frame():
    print "frame:"
    wait()


s = ActiveSprite()
schedule(s.cyclic, "abcd")

s = ActiveSprite()
schedule(s.cyclic, "123")


tim = timer()
while timer() - tim < 5:
    normal_frame()
    if random.random() < 0.3:
        schedule(s.cyclic, "ghijk")
