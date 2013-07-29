#
# -*- coding: utf-8 -*-
#

"""
With the current stackless 2.7-slp, the following code crashes on exit

See http://www.stackless.com/ticket/21
"""

import stackless

stackless.tasklet(stackless.test_cframe)(1)
stackless.schedule()
