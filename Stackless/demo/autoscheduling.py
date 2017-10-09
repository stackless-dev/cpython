import sys
import stackless


def schedule_cb(task):
    if task:
        task.insert()


def autoschedule(bytecodes=None):
    if bytecodes is None:
        bytecodes = sys.getcheckinterval()
    while stackless.runcount > 1:
        schedule_cb(stackless.run(bytecodes))


if __name__ == "__main__":

    def run_atomic(fn, *args, **kwargs):
        current = stackless.current
        oldatomic = current.set_atomic(1)
        rval = fn(*args, **kwargs)
        current.set_atomic(oldatomic)
        return rval

    def print_name(name, count):
        print name, count

    # Helpers
    def runtask(name):
        for ii in range(1000):
            if ii % 50:
                run_atomic(print_name, name, ii)

    tasklets = [stackless.tasklet(runtask)(name) for name in ["one", "two", "three"]]
    autoschedule()
