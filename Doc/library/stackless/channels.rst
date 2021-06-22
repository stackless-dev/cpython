.. _channels:

*******************************************
Channels --- Communication between tasklets
*******************************************

A channel object is used for communication between tasklets.

By sending on a channel within a tasklet, another tasklet that is waiting to
receive on the channel is resumed.  If there is no waiting receiver, then the
sender is suspended.

By receiving on a channel within a tasklet, another tasklet that is waiting
on the channel to send is resumed.  If there is no waiting sender, then the
receiver is suspended.

--------------------
Channels and threads
--------------------

Channels are thread-safe.  This means that tasklets running in one thread can
use them to communicate with tasklets running in another.  This is covered
in the :doc:`Threads and Stackless <threads>` section.

---------------------
The ``channel`` class
---------------------

.. class:: channel()

.. method:: channel.send(value)

   Send a value over the channel.  If no other tasklet is already receiving on
   the channel, the sender will be blocked.  Otherwise, the receiver will be
   activated immediately, and the sender is put at the end of the runnables
   list.

   Example - sending a value over a channel::

       >>> c = stackless.channel()
       >>> def sender(chan, value):
       ...     chan.send(value)
       ...
       >>> stackless.tasklet(sender)(5)
       >>> stackless.run()
       >>> print(c.receive())
       5

.. method:: channel.receive()

   Receive a value over the channel.  If no other tasklet is already sending
   on the channel, the receiver will be blocked. Otherwise, the sender will be
   activated immediately, and the receiver is put at the end of the runnables
   list.

   Example - receiving a value over a channel::

       >>> c = stackless.channel()
       >>> def receiver(chan):
       ...     value = chan.receive()
       ...     print(value)
       ...
       >>> stackless.tasklet(receiver)(c)
       >>> stackless.run()
       >>> c.send(5)
       5

.. method:: channel.send_exception(exc, *args)

   Send an exception over the channel.  The behaviour is the same as for
   :meth:`send`, however the receiving tasklet has the exception raised
   on it rather than receiving a value as it expects.

   Example - sending an exception to a waiting receiver::

       >>> c = stackless.channel()
       >>> def receiver(chan):
       ...     chan.receive()
       ...
        >>> stackless.tasklet(receiver)(c)
        <stackless.tasklet object at 0x01BB8130>
        >>> stackless.run()
        >>> c.send_exception(Exception, "xxx")
        Traceback (most recent call last):
          File "<stdin>", line 1, in <module>
          File "<stdin>", line 2, in receiver
        Exception: xxx

.. method:: channel.send_throw(exc[, val[, tb]])

   Send an exception over a channel.  The arguments have the same semantics as
   for the `raise` keyword.  This allows an existing exception to be sent
   over a channel, and the traceback to be propagated

   Example::

       try:
           foo()
       except Exception:
           mychannel.send_throw(*sys.exc_info())

.. method:: channel.send_sequence(seq)

   Send a stream of values over the channel.  Combined with a generator, this
   is a very efficient way to build fast pipes. This method returns the length
   of *seq*.

   This method is equivalent to::

       def send_sequence(channel, sequence):
           length = 0
           for item in sequence:
               channel.send(item)
               length += 1
           return length

   Example - sending a sequence over a channel::

       >>> def sender(channel):
       ...     channel.send_sequence(sequence)
       ...
       >>> def receiver(channel):
       ...     count = 0
       ...     while count < len(sequence):
       ...             count += 1
       ...             value = channel.receive()
       ...             print(value)
       ...
       >>> c = stackless.channel()
       >>> stackless.tasklet(sender)(c)
       <stackless.tasklet object at 0x01BB84F0>
       >>> stackless.tasklet(receiver)(c)
       <stackless.tasklet object at 0x01BB8170>
       >>> sequence = range(4)
       >>> sequence
       [0, 1, 2, 3]
       >>> stackless.run()
       0
       1
       2
       3

.. method:: channel.__iter__()

   Channels can work as an iterator.  When they are used in this way, call
   overhead is removed on the receiving side, making it an efficient approach.

   The receiver does not know, if the sender will send further objects
   or not. Therefore the sender must notify the receiver about the
   end-of-iteration condition. Currently this requires sending a
   :exc:`StopIteration` over the channel (i.e. by
   calling ``channel.send_exception(StopIteration)``).

   .. note::

      A future version of Stackless may send a :exc:`StopIteration` automatically,
      if you close the channel.

   Example - iterating over a channel::

       >>> def sender(channel):
       ...     for value in sequence:
       ...         channel.send(value)
       ...     channel.send_exception(StopIteration)
       ...
       >>> def receiver(channel):
       ...     for value in channel:
       ...         print(value)
       ...
       >>> c = stackless.channel()
       >>> stackless.tasklet(sender)(c)
       <stackless.tasklet object at 0x01BB84F0>
       >>> stackless.tasklet(receiver)(c)
       <stackless.tasklet object at 0x01BB8170>
       >>> sequence = range(4)
       >>> sequence
       [0, 1, 2, 3]
       >>> stackless.run()
       0
       1
       2
       3

   Of course you can combine :meth:`send_sequence` with iterating over a channel::

      >>> def sender(channel, sequence):
      ...     channel.send_sequence(sequence)
      ...     channel.send_exception(StopIteration)
      ...
      >>> def receiver(channel):
      ...     for value in channel:
      ...         print value
      ...
      >>> c = stackless.channel()
      >>> stackless.tasklet(sender)(c, range(4))
      <_stackless.tasklet object at 0x0244E0E8>
      >>> stackless.tasklet(receiver)(c)
      <_stackless.tasklet object at 0x0244E140>
      >>> stackless.run()
      0
      1
      2
      3

.. method:: channel.next()

   Part of the :ref:`iteration protocol <typeiter>`.  Either returns the next value, or raises
   :exc:`StopIteration`.

.. method:: channel.open()

   Reopen a channel, see :meth:`close`.

   .. note::

      This functionality is rarely used in practice.

.. method:: channel.close()

   Prevents the channel queue from growing.  If the channel is not empty, the
   flag :attr:`closing` becomes ``True``.  If the channel is empty, the flag
   :attr:`closed` becomes ``True``.

   .. note::

      This functionality is rarely used in practice.

The following attributes can be used to select how the channel should behave
with regard to performed channel actions and the scheduling of involved
tasklets.

.. attribute:: channel.preference

   The :attr:`preference` attribute allows you to customise how the channel
   actions :attr:`send` or :attr:`receive` work with the scheduler.

   There are three valid values you can assign it:

     +----+------------------------------------+
     | -1 | Prefer the receiver (the default). |
     +----+------------------------------------+
     |  1 | Prefer the sender.                 |
     +----+------------------------------------+
     |  0 | Do not prefer anything.            |
     +----+------------------------------------+

   It can be very important to your code behaving predictably, that it does
   one particular side of the channel action during the call.  This might
   be that in a :attr:`send` action, the sending tasklet is blocked and
   rescheduled, while the waiting receiving tasklet is given the sent
   value and continues executing immediately.  It might be that in a
   :attr:`send` action, the sending tasklet returns immediately to continue
   execution while the receiving tasklet is rescheduled.  It might even
   be that both tasklets are scheduled.

   The key to getting channel actions to work the way you want is to
   understand what "prefer" means.  The tasklet that is preferred is simply
   the one whose execution resumes immediately, while the other tasklet
   is resumes execution when it next gets scheduled.

   So if you do not want your send operations to block, you might set your
   :attr:`preference` attribute to ``1``.  In this way, you could then
   send to all waiting receivers without blocking, as shown in the
   :ref:`pumping the scheduler <slp-chan-pref-ex1>` idiom described
   elsewhere in this documentation.

   On the other hand, if you have a channel you are receiving a lot of data
   through, you might want to collect all the waiting data in the most
   efficient way - without blocking.

   Example - receiving it all, without blocking::

       channel.preference = -1

       while channel.balance > 0:
           total += channel.receive()

   In fact, by using the handy :attr:`tasklet.block_trap` attribute, that
   this does not block can be easy verified.

   Example - verified receiving without blocking::

       channel.preference = -1

       old_value = stackless.current.block_trap
       stackless.current.block_trap = True
       try:
           while channel.balance > 0:
               total += channel.receive()
       finally:
           stackless.current.block_trap = old_value

.. attribute:: channel.schedule_all

   Setting this attribute to ``1`` overrides the value assigned to the
   :attr:`preference` attribute.  If set to ``1``, then any channel
   action will result in involved tasklets being scheduled to continue
   execution later.

Read-only attributes are provided for checking channel state and contents.

.. attribute:: channel.balance

   The number of tasklets waiting to send (>0) or receive (<0).

   Example - reawakening all blocked senders::

       >>> while channel.balance > 0:
       ...     channel.send(None)

.. attribute:: channel.closing

   The value of this attribute is ``True`` when :meth:`close` has been called.

.. attribute:: channel.closed

   The value of this attribute is ``True`` when :meth:`close` has been called
   and the channel is empty.

.. attribute:: channel.queue

   This value of this attribute is the first tasklet in the chain of tasklets
   that are blocked on the channel.  If the value is :data:`None`, then the
   channel is empty.

   Example - printing out the chain of tasklets blocked on the channel::

       >>> t = channel.queue
       >>> idx = 0
       >>> while t is not None:
       ...     print(idx, id(t))
       ...     t = t.next
       ...     idx += 1
       ... else:
       ...     print("The channel is empty.")
