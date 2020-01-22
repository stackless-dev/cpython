#
# -*- coding: utf-8 -*-
#

"""
A test script to analyse the pickling related part of
https://github.com/stackless-dev/stackless/issue/61
"""

from __future__ import absolute_import, print_function, unicode_literals, division

import stackless
from collections import namedtuple

FrameState = namedtuple("FrameState", ('f_code', 'valid', 'exec_name', 'f_globals', 'have_locals',
                                       'f_locals', 'f_trace', 'exc_as_tuple', 'f_lasti', 'f_lineno',
                                       'blockstack_as_tuple', 'localsplus_as_tuple'))


def reduce_current():
    result = []

    def reduce_current2():
        """This function has exactly one local variable (func), one cellvar (result2) and one freevar (result)"""
        result2 = result

        def func(current):
            result2.append(stackless._wrap.frame.__reduce__(current.frame))
        stackless.tasklet().bind(func, (stackless.current,)).run()
        return result[0]
    return reduce_current2()


def main():
    soft = stackless.enable_softswitch(False)
    try:
        p_hard = reduce_current()
    finally:
        stackless.enable_softswitch(soft)
    p_soft = reduce_current()
    c = p_soft[1][0]
    c = dict(co_argcount=c.co_argcount,
             co_nlocals=c.co_nlocals,
             co_varnames=c.co_varnames,
             co_cellvars=c.co_cellvars,
             co_freevars=c.co_freevars,
             co_consts=c.co_consts,
             co_names=c.co_names,
             co_stacksize=c.co_stacksize)

    state_soft = dict(FrameState(*p_soft[2])._asdict())
    state_hard = dict(FrameState(*p_hard[2])._asdict())
    del state_hard['f_globals']
    del state_soft['f_globals']
    import pprint
    print("code")
    pprint.pprint(c)
    print("soft switching")
    pprint.pprint(state_soft)
    print("hard switching")
    pprint.pprint(state_hard)

if __name__ == "__main__":
    main()
