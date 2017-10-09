# a little example of tasklets and channels.
# don't use this in production code, it is really nonsense :-)

# We want to sort some random numbers using tasklets.
# The idea is to create a bulk of tasklets which call schedule()
# a number of times. When the count is at zero, the tasklet
# send its result over a channel.
# Guess the result!

import random
from stackless import *

numbers = range(20)
random.shuffle(numbers)
print numbers
# [16, 13, 12, 5, 6, 4, 7, 1, 9, 17, 15, 14, 10, 8, 0, 3, 11, 18, 2, 19]


def counter(n, ch):
    for i in range(n):
        schedule()
    ch.send(n)

ch = stackless.channel()
for each in numbers:
    stackless.tasklet(counter)(each, ch)

stackless.run()
# now we should have a sorted chain of results in ch
while ch.queue:
    print ch.receive()
