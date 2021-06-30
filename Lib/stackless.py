"""
stackless
This is the stackless module
"""
import sys
import types

import _stackless
from _stackless import *

def __reduce__():
    return "stackless"
def __reduce_ex__(*args):
    return "stackless"

PICKLEFLAGS_PRESERVE_TRACING_STATE = 1
PICKLEFLAGS_PRESERVE_AG_FINALIZER = 2
PICKLEFLAGS_RESET_AG_FINALIZER = 4
PICKLEFLAGS_PICKLE_CONTEXT = 8

# Backwards support for unpickling older pickles, even from 2.7
from _stackless import _wrap
sys.modules["stackless._wrap"] = _wrap
class range(object):
    """
    A fake range object that adds __setstate__ to mimic old
    custom pickle additions from from 2.7
    """
    def __init__(self, *args):
        self.args = args
    def __setstate__(self, *args):
        # fautly pickle code passes an empty tuple through!
        pass
    def __iter__(self):
        return iter(range(*self.args))
_wrap.range = range
del range

__all__ = ['atomic',
           'channel',
           'enable_softswitch',
           'get_channel_callback',
           'get_schedule_callback',
           'get_thread_info',
           'getcurrent',
           'getcurrentid',
           'getdebug',
           'getmain',
           'getruncount',
           'getthreads',
           'getuncollectables',
           'pickle_with_tracing_state',
           'run',
           'schedule',
           'schedule_remove',
           'set_channel_callback',
           'set_error_handler',
           'set_schedule_callback',
           'switch_trap',
           'tasklet',
           'stackless',  # ugly
           ]

# these definitions have no function, but they help IDEs (i.e. PyDev) to recognise
# expressions like "stackless.current" as well defined.
current = runcount = main = debug = uncollectables = threads = pickle_with_tracing_state = None

def _tasklet_get_unpicklable_state(tasklet):
    """Get a dict with additional state, that can't be pickled

    The method tasklet.__reduce_ex__() returns the picklable state and this
    function returns a tuple containing the rest.

    The items in the return value are:
    'context': the context of the tasklet

    Additional items may be added later.

    Note: this function has been added on a provisional basis (see :pep:`411` for details.)
    """
    if not isinstance(tasklet, _stackless.tasklet):
        raise TypeError("Argument must be a tasklet")

    with atomic():
        if tasklet.is_current or tasklet.thread_id != _stackless.current.thread_id:
            # - A current tasklet can't be reduced.
            # - We can't set pickle_flags for a foreign thread
            # To mitigate these problems, we copy the context to a new tasklet
            # (implicit copy by tasklet.__init__(callable, ...)) and reduce the new
            # context instead
            tasklet = tasklet.context_run(_stackless.tasklet, id)  # "id" is just an arbitrary callable

        flags = pickle_flags(PICKLEFLAGS_PICKLE_CONTEXT, PICKLEFLAGS_PICKLE_CONTEXT)
        try:
            return {'context': tasklet.__reduce__()[2][12]}
        finally:
            pickle_flags(flags, PICKLEFLAGS_PICKLE_CONTEXT)

def transmogrify():
    """
    this function creates a subclass of the ModuleType with properties.
    Stackless has historically had module properties, something very unusual in Python.
    We need to do that by replacing the current module object as it is being created

    Additionally this function performs a few initialisations.
    """
    from copyreg import pickle
    for name in dir(_wrap):
        cls = getattr(_wrap, name, None)
        if isinstance(cls, type) and cls.__name__ != "frame":
            pickle(cls.__bases__[0], cls.__reduce__)

    try:
        # in case of reload(stackless)
        reduce_frame = _wrap.reduce_frame
    except AttributeError:
        from weakref import WeakValueDictionary

        wrap_set_reduce_frame = _wrap.set_reduce_frame

        def set_reduce_frame(func):
            wrap_set_reduce_frame(func)
            _wrap.reduce_frame = func

        _wrap.set_reduce_frame = set_reduce_frame

        wrap_frame__reduce = _wrap.frame.__reduce__
        cache = WeakValueDictionary()

        class _Frame_Wrapper(object):
            """Wrapper for frames to be pickled"""
            __slots__ = ('__weakref__', 'frame')

            @classmethod
            def reduce_frame(cls, frame):
                oid = id(frame)
                try:
                    return cache[oid]
                except KeyError:
                    cache[oid] = reducer = cls(frame)
                    return reducer

            def __init__(self, frame):
                self.frame = frame

            def __reduce__(self):
                return wrap_frame__reduce(self.frame)
        reduce_frame = _Frame_Wrapper.reduce_frame
    _wrap.set_reduce_frame(reduce_frame)


    class StacklessModuleType(types.ModuleType):

        def current(self):
            return self.getcurrent()
        current = property(current, doc=getcurrent.__doc__)

        def runcount(self):
            return self.getruncount()
        runcount = property(runcount, doc=getruncount.__doc__)

        def main(self):
            return self.getmain()
        main = property(main, doc=getmain.__doc__)

        def debug(self):
            return self.getdebug()
        debug = property(debug, doc=getdebug.__doc__)

        def uncollectables(self):
            return self.getuncollectables()
        uncollectables = property(uncollectables, doc=getuncollectables);

        def threads(self):
            return self.getthreads()
        threads = property(threads, doc=getthreads.__doc__)

        # Pass module level flags into the builtin
        @property
        def pickle_with_tracing_state(self):
            """Will tasklet save their tracing state when pickled?"""
            return bool(self.pickle_flags() & self.PICKLEFLAGS_PRESERVE_TRACING_STATE)
        @pickle_with_tracing_state.setter
        def pickle_with_tracing_state(self, val):
            val = self.PICKLEFLAGS_PRESERVE_TRACING_STATE if val else 0
            with self.atomic():
                self.pickle_flags(val, self.PICKLEFLAGS_PRESERVE_TRACING_STATE)
                self.pickle_flags_default(val, self.PICKLEFLAGS_PRESERVE_TRACING_STATE)

    m = StacklessModuleType("stackless", __doc__)
    m.__dict__.update(globals())
    del m.transmogrify, m.types, m.sys, m._wrap

    # odd curiosity, the stackless module contains a reference to itself
    # deprecated, of course
    m.stackless = m

    sys.modules["stackless"] = m

transmogrify()
