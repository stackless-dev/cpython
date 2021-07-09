.. _stackless-pickling:

**********************************************
Pickling --- Serialisation of running tasklets
**********************************************

One of the most impressive features of |SLP|, is the ability to pickle
tasklets.  This allows you to take a tasklet mid-execution, serialise it to
a chunk of data and then unserialise that data at a later point, creating a
new tasklet from it that resumes where the last left off.

What makes this
particularly impressive is the fact that the |PY| :mod:`pickle` structure
is platform independent.  Code can for instance initially be run on a x86
Windows machine, then interrupted, pickled and sent over the network to be
resumed on an ARM Linux machine.

Example - pickling a tasklet::

    >>> def func():
    ...    busy_count = 0
    ...    while 1:
    ...        busy_count += 1
    ...        if busy_count % 10 == 0:
    ...            print(busy_count)
    ...
    >>> stackless.tasklet(func)()
    <stackless.tasklet object at 0x01BD16B0>
    >>> t1 = stackless.run(100)
    10
    20
    >>> s = pickle.dumps(t1)
    >>> t1.kill()
    >>> t2 = pickle.loads(s)
    >>> t2.insert()
    >>> stackless.run(100)
    30
    40
    50

In the above example, a tasklet is created that increments the counter
*busy_count* and outputs the value when it is a multiple of ``10``.

Run the tasklet for a while::

    >>> t1 = stackless.run(100)
    10
    20

The tasklet has been interrupted at some point in its execution.  If
it were to be resumed, we would expect its output to be the values following
those previously displayed.

Serialise the tasklet::

    >>> s = pickle.dumps(t1)

As any other object is pickled, so are tasklets.  In this case, the serialised
representation of the tasklet is a string, stored in *s*.

Destroy the tasklet::

    >>> t1.kill()

We want to show that the old code cannot be resumed, and in order to do so, we
destroy the tasklet it was running within.

Unserialise the stored representation::

    >>> t2 = pickle.loads(s)

As any other object is unpickled, so are tasklets.  We take the string and
by unpickling it, get a new tasklet object back.

Schedule the new tasklet::

    >>> t2.insert()

Now the newly recreated tasklet is inserted into the scheduler, so that when
the scheduler is next run, the tasklet is resumed.

Run the scheduler::

    >>> stackless.run(100)
    30
    40
    50
    <stackless.tasklet object at 0x01BD1D30>

When the scheduler is run, the values displayed are indeed the ones that
follow those displayed by the original tasklet.  The value returned by
:func:`stackless.run` is not stored in a variable this time, so the
interpreter displays the recreated tasklet.  You can see that it has a
different address than *t1*, which was displayed earlier.

.. note::

    It should be possible to pickle any tasklets that you might want to.
    However, not all tasklets can be unpickled.  One of the cases in which
    this is true, is where not all the functions called by the code within
    the tasklet are |PY| functions.  The |SLP| pickling mechanism
    has no ability to deal with C functions that may have been called.

.. note::

    It is not possible to unpickle running tasklets which were pickled by a
    different minor version of |SLP|. A running tasklet contains frame
    objects and frame objects contain code objects. And code objects are
    usually incompatible between different minor versions of |CPY|.

.. note::

    If you pickle a tasklet, its :class:`~contextvars.Context` won't be pickled,
    because :class:`~contextvars.Context` objects can't be pickled. See
    :pep:`567` for an explanation.

    It is sometimes possible enable pickling of :class:`~contextvars.Context` objects
    in an application specific way (see for instance: :func:`copyreg.pickle` or
    :attr:`pickle.Pickler.dispatch_table` or :attr:`pickle.Pickler.persistent_id`).
    Such an application can set the pickle flag
    :const:`~stackless.PICKLEFLAGS_PICKLE_CONTEXT` to include the
    context in the pickled state of a tasklet.

    Another option is to subclass :class:`tasklet` and overload the methods
    :meth:`tasklet.__reduce_ex__` and :meth:`tasklet.__setstate__` to
    pickle the values of particular :class:`~contextvars.ContextVar` objects together
    with the tasklet.


======================
Pickling other objects
======================

In order to be able to pickle tasklets |SLP| needs to be able to pickle
several other objects, which can't be pickled by |CPY|. If the module
:mod:`stackless` gets imported for the first time, |SLP|
uses :func:`copyreg.pickle` to register “reduction” functions for the following
types:
:data:`~types.AsyncGeneratorType`,
:data:`~types.CodeType`,
:data:`~types.CoroutineType`,
:data:`~types.FunctionType`,
:data:`~types.GeneratorType`,
:data:`~types.ModuleType`,
:data:`~types.TracebackType`,
:ref:`Cell Objects <cell-objects>`,
C-types PyAsyncGenASend and PyAsyncGenAThrow (see :pep:`525`) as well as
all kinds of :ref:`Dictionary view objects <dict-views>`.

Code
====

|SLP| can pickle :data:`~types.CodeType` objects.

.. versionchanged:: 3.8
   The pickled representation of a code object contains the bytecode version number (:data:`~importlib.util.MAGIC_NUMBER`).
   If a program tries to unpickle a code object with a wrong bytecode version number, then |SLP|

   * emits a ``RuntimeWarning('Unpickling code object with invalid magic number %ld')`` and
   * prepends the *co_code* attribute of the unpickled code object with an invalid |PY| bytecode instruction. This way any attempt
     to execute the code object raises :exc:`SystemError`.

Frames
======

|SLP| can pickle frames, but only as part of a
tasklet, a traceback-object, a generator, a coroutine or an asynchronous
generator. |SLP| does not register a "reduction" function for
:data:`~types.FrameType`. This way |SLP| stays compatible with application
code that registers its own "reduction" function for :data:`~types.FrameType`.

It is not possible to execute an unpickled frame, if the tasklet the original frame belonged to was
not :attr:`~tasklet.restorable`. In this case the frame is marked as invalid and any attempt
to execute it raises

.. versionchanged:: 3.8
   If a program tries to unpickle a frame using a code object whose first bytecode instruction is invalid, then |SLP|
   marks the frame as invalid. Any attempt to execute the frame raises :exc:`RuntimeError`.


Functions
=========

|SLP| can pickle functions including lambda-objects objects by value.

.. versionchanged:: 3.8
   If a program tries to unpickle a function using a code object whose first bytecode instruction is invalid, then |SLP|
   emits a ``RuntimeWarning('Unpickling function with invalid code object: %V')``. Any attempt
   to execute the function raises :exc:`SystemError`.

.. _slp_pickling_asyncgen:

Asynchronous Generators
=======================

.. versionadded:: 3.7

At C-level asynchronous generators have an attribute ``ag_finalizer`` and a flag,
if ag_finalizer has been initialised. The value of ``ag_finalizer`` is a callable
|PY|-object, which has been set by :func:`sys.set_asyncgen_hooks`.
You can use :func:`stackless.pickle_flags` to control how |SLP| pickles and
unpickles an asynchronous generator.

Pickling
--------

By default (no flags set) |SLP| does not pickle ``ag_finalizer`` but a
marker, if a ``ag_finalizer`` has been set.
If :const:`~stackless.PICKLEFLAGS_PRESERVE_AG_FINALIZER` has been set,
|SLP| pickles ``ag_finalizer`` by value.
Otherwise, if :const:`~stackless.PICKLEFLAGS_RESET_AG_FINALIZER` has
been set, |SLP| pickles ``ag_finalizer`` as uninitialised.

Unpickling
----------

By default |SLP| initialises the generator upon unpickling using the
``firstiter`` and ``finalizer`` values set by :func:`sys.set_asyncgen_hooks`,
if ``ag_finalizer`` of the original asynchronous generator was initialised.
If :const:`~stackless.PICKLEFLAGS_PRESERVE_AG_FINALIZER` has been set and if
``ag_finalizer`` has been pickled by value, |SLP| unpickles
``ag_finalizer`` by value.
Otherwise, if :const:`~stackless.PICKLEFLAGS_RESET_AG_FINALIZER` has
been set, |SLP| unpickles ``ag_finalizer`` as uninitialised.
