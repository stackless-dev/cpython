#
# -*- coding: utf-8 -*-
#

"""
This file was needed to figure out a deep problem with generator pickling.
I'm adding this file to the unittests as a template to cure similar
problems in the future. This file only works in a debug build.

Run with python -i ...
"""

from __future__ import absolute_import, print_function, division

import sys
import gc
import pickle
import os
import stackless

try:
    genschedoutertest  # @UndefinedVariable
except NameError:
    sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     os.pardir, "unittests")))
    from test_pickle import genschedoutertest

t = stackless.tasklet(genschedoutertest)(20, 13)  # @UndefinedVariable
t.run()
s = pickle.dumps(t)
t.run()
del t
pre = None
post = None
newob = None
del pre, post, newob
gc.collect()
pre = stackless._stackless._get_all_objects()
post = pre[:]

print("refs before unpickling, objects", sys.gettotalrefcount(), len(pre))
pickle.loads(s).run()
post = None
gc.collect()
post = stackless._stackless._get_all_objects()
for i, ob in enumerate(post):
    if id(ob) == id(pre):
        del post[i]
del i, ob
gc.collect()
print("refs after  unpickling, objects", sys.gettotalrefcount(), len(post))
newob = post[:len(post) - len(pre)]
print("look into newob")
del pre, post
gc.collect()
