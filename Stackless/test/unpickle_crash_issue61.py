#
# -*- coding: utf-8 -*-
#

"""
A test program for https://github.com/stackless-dev/stackless/issue/61
"""

from __future__ import absolute_import, print_function, unicode_literals, division

import pickle
import pickletools
import stackless


def pickle_current_frame():
    result = []  # comment this line to avoid the crash

    def func(current):
        result.append(pickle.dumps(current.frame, -1))
    stackless.tasklet().bind(func, (stackless.current,)).run()
    return result[0]


def main():
    from sys import argv
    if '--pickle' in argv:
        soft = stackless.enable_softswitch(False)  # no crash, if soft switching
        try:
            p = pickle_current_frame()
        finally:
            stackless.enable_softswitch(soft)
            p = pickletools.optimize(p)
        print('Pickle as bytes: ', repr(p))
    else:
        if bytes is str:
            # pickle created with Stackless v2.7.6r3, hg commt id 67088aa2da77
            p = b'\x80\x02c_stackless._wrap\nframe\nc_stackless._wrap\ncode\nq\x01(K\x00K\x01K\x03J\x03`\x03\x00U?g\x00\x00\x89\x00\x00\x87\x00\x00f\x01\x00d\x01\x00\x86\x00\x00}\x00\x00t\x00\x00j\x01\x00\x83\x00\x00j\x02\x00|\x00\x00t\x00\x00j\x03\x00f\x01\x00\x83\x02\x00j\x04\x00\x83\x00\x00\x01\x88\x00\x00d\x02\x00\x19SNh\x01(K\x01K\x01K\x04J\x13`\x03\x00U \x88\x00\x00j\x00\x00t\x01\x00j\x02\x00|\x00\x00j\x03\x00d\x01\x00\x83\x02\x00\x83\x01\x00\x01d\x00\x00SNJ\xff\xff\xff\xff\x86(U\x06appendU\x06pickleU\x05dumpsU\x05frametU\x07currentq\n\x85U)Stackless/test/unpickle_crash_ticket61.pyU\x04funcq\rK\x12U\x02\x00\x01U\x06resultq\x0f\x85)tR)bK\x00\x87(U\tstacklessU\x07taskletU\x04bindh\nU\x03runth\r\x85U)Stackless/test/unpickle_crash_ticket61.pyU\x14pickle_current_frameK\x0fU\x08\x00\x01\x06\x02\x0f\x02"\x01)h\x0f\x85tRq\x1f)b\x85R(h\x1fK\x00U\x10eval_frame_valuec__builtin__\ngetattr\nc__builtin__\n__import__\n(U\x08__main__))U\x00\x85tRU\x08__dict__\x86RK\x00}NNK3K\x0f)Ntb.'
        else:
            # pickle created with Stackless v3.3.5
            p = b'\x80\x03c_stackless._wrap\nframe\nc_stackless._wrap\ncode\nq\x01(K\x00K\x00K\x01K\x03K\x03CBg\x00\x00\x89\x00\x00\x87\x00\x00f\x01\x00d\x01\x00d\x02\x00\x86\x00\x00}\x00\x00t\x00\x00j\x01\x00\x83\x00\x00j\x02\x00|\x00\x00t\x00\x00j\x03\x00f\x01\x00\x83\x02\x00j\x04\x00\x83\x00\x00\x01\x88\x00\x00d\x03\x00\x19S(Nh\x01(K\x01K\x00K\x01K\x04K\x13C \x88\x00\x00j\x00\x00t\x01\x00j\x02\x00|\x00\x00j\x03\x00d\x02\x00\x83\x02\x00\x83\x01\x00\x01d\x00\x00SNK\x01J\xff\xff\xff\xff\x87(X\x06\x00\x00\x00appendX\x06\x00\x00\x00pickleX\x05\x00\x00\x00dumpsX\x05\x00\x00\x00frametX\x07\x00\x00\x00currentq\n\x85X)\x00\x00\x00Stackless/test/unpickle_crash_ticket61.pyq\x0cX\x04\x00\x00\x00funcq\rK\x0fC\x02\x00\x01X\x06\x00\x00\x00resultq\x0f\x85)tR)bX"\x00\x00\x00pickle_current_frame.<locals>.funcK\x00t(X\t\x00\x00\x00stacklessX\x07\x00\x00\x00taskletX\x04\x00\x00\x00bindh\nX\x03\x00\x00\x00runth\r\x85h\x0cX\x14\x00\x00\x00pickle_current_frameK\x0cC\x08\x00\x01\x06\x02\x12\x02"\x01)h\x0f\x85tRq\x1f)b\x85R(h\x1fK\x00X\x10\x00\x00\x00eval_frame_valuecbuiltins\ngetattr\ncimportlib\nimport_module\nX\x08\x00\x00\x00__main__\x85RX\x08\x00\x00\x00__dict__\x86RK\x00}NNK6K\x0c)Ntb.'

    if '--dis' in argv:
        pickletools.dis(p)
    else:
        frame = pickle.loads(p)
        frame.f_locals  # this line crashes Stackless
        print("No Crash, OK")

if __name__ == "__main__":
    main()
