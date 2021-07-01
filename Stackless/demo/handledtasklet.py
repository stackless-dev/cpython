"""
This is an example of an exception handled
tasklet type.
It also shows some flaw in the tasklet design:
I don't provide ways to override __init__,
because it is not called, and it would also
make sense to override bind().

A first version using a
        def handler(self, *args,**kwds):
did not work for some reason. It seems that
there are deficiencies in the argument handling
of bound methods.
Will try to enhance this,soon.
"""

from stackless import *


class HandledTasklet(tasklet):

    def __new__(self, func):
        def handler(*args, **kwds):
            try:
                func(*args, **kwds)
            except Exception as e:
                print("captured", e)
                raise

        return tasklet.__new__(self, handler)
