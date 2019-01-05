*****
|SLP|
*****

:Author: Richard Tew (richard.m.tew@gmail.com) and Anselm Kruis
:Release: |release|
:Date: |today|

|SLP|__ is an enhanced version of the |PPL|\ [#PSFTRADEMARK]_.
It allows programmers to reap the benefits of thread-based programming without
the performance and complexity problems associated with conventional threads.
The microthreads that Stackless adds to the |PPL| are a cheap and lightweight
convenience, which if used properly, can not only serve as a way to structure
an application or framework, but by doing so improve program structure and
facilitate more readable code.

If you are reading this text as part of a version of a |PY| interpreter you have installed,
then you have installed |SLP| rather than |CPY|\ [#CPY]_.

__ http://www.stackless.com

Overview / API
==============

Unless actual use is made of the enhanced functionality that Stackless adds
to |CPY|, Stackless will behave exactly the same as |CPY| would and is used
in exactly the same way.  This functionality is exposed as a framework
through the :mod:`stackless` module.


.. toctree::
   :maxdepth: 3

   whatsnew/stackless.rst
   library/stackless/stackless.rst
   c-api/stackless.rst

.. I'd really like the "What you need to know", "External resources" and
.. "History" sections to appear in the table of contents.. but this does
.. not seem possible to do.  Let's hope people read down far enough to
.. see them.

What you need to know
=====================

|SLP| provides a minimal framework and it is not accompanied by any
support functionality, that would otherwise address the common needs that may
arise when building a more targeted framework around it.

Blocking operations
-------------------

When operations are invoked that block the |PY| interpreter, the user needs
to be aware that this inherently blocks all running tasklets.  Until the tasklet
that engaged that operation is complete, the |PY| interpreter and therefore
scheduler is blocked on that operation and in that tasklet.  Operations that
block the interpreter are often related to synchronous IO (file reading
and writing, socket operations, interprocess communication and more), although
:func:`time.sleep` should also be kept in mind.  The user is advised to choose
asynchronous versions of IO functionality.

Some third-party modules are available that replace standard library
functionality with Stackless-compatible versions.  The advantage of this
approach is that other modules which use that standard functionality,
also work with Stackless when the replacement is installed.  The
`Stackless socket
<http://code.google.com/p/stacklessexamples/wiki/StacklessNetworking>`_
module is the most commonly used replacement module.

Exceptions
----------

Certain exceptions that may occur within tasklets, are expected to reach all
the way up the call stack to the scheduler.  This means that naively using
the ``except`` statement may result in hard to track down problems.

.. In an ideal world, rather than linking to TaskletExit via ref, I
.. would link to it properly with exc.  However, this does not seem
.. to work.  So, until this can be resolved, ref is it. -- richard.

A description of the problem with bare ``except`` statements can be read in
the documentation for the :ref:`TaskletExit <slp-exc>` exception.

Debugging
---------

The Stackless scheduling mechanism changes the way the |PY| debugging hooks
work so that debugging hooks are set per-tasklet rather than per-thread.
However, very few debuggers, certainly none of those in the standard library
take this into account.  As a result of this, debugging is unlikely to work
with special handling being worked into your use of Stackless.

A description of this problem can be read in the Stackless :doc:`debugging
documentation <library/stackless/debugging>`.

External resources
==================

There are a range of resources available outside of this document. However most of them are
nowadays quite dated:

 * The Stackless `mailing list <http://www.stackless.com/mailman/listinfo/stackless>`_.
 * The Stackless `examples project <https://bitbucket.org/stackless-dev/stacklessexamples>`_.
 * Grant Olson's tutorial: `Introduction to Concurrent Programming with Stackless Python <http://www.grant-olson.net/projects/introduction-to-stackless-python.html>`_.

History
=======

Continuations are a feature that require the programming language they are
part of, to be implemented in a way conducive to their presence.  In order to
add them to the |PPL|, Christian Tismer modified it
extensively.  The primary way in which it was modified, was to make it Stackless.
And so, his fork of |CPY| was named |SLP|.

Now, storing data on the stack locks execution on an operating system thread to
the current executing functionality, until that functionality completes and the
stack usage is released piece by piece.  In order to add continuations to |CPY|,
that data needed to be stored on the heap instead, therefore decoupling the
executing functionality from the stack.  With the extensive changes this required
in place, Christian `released Stackless Python
<http://mail.python.org/pipermail/python-dev/2000-January/001835.html>`_.

Maintaining the fork of a programming language is a lot of work, and when the
programming language changes in ways that are incompatible with the changes in
the fork, then that work is sigificantly increased.  Over time it became
obvious that the amount of changes to |CPY| were too much weight to carry,
and Christian contemplated a rewrite of Stackless.  It became obvious that a
`simpler approach
<http://mail.python.org/pipermail/python-dev/2002-January/019671.html>`_ could
be taken, where Stackless was no longer stackless and no longer had continuations.

Following the rewrite, a framework was designed and added `inspired by
<http://www.stackless.com/pipermail/stackless/2002-May/000114.html>`_ coming from
`CSP <http://usingcsp.com/>`_ and the `Limbo
<http://en.wikipedia.org/wiki/Limbo_%28programming_language%29>`_ programming
language.  From this point on, Stackless was in a state where it contained the
minimum functionality to give the benefits it aimed to provide, with the
minimum amount of work required to keep it maintained.

A few years later in 2004, while sprinting on Stackless in Berlin, Christian and
Armin Rigo `came up with a way
<http://www.stackless.com/pipermail/stackless-dev/2004-March/000022.html>`_ to
take the core functionality of Stackless and build an extension module that provided
it.  This was the creation of greenlets, which are very likely a more popular
tool than Stackless itself today.  The greenlet source code in practice can be
used as the base for green threading functionality not just in the |PPL|, but in
other programming languages and projects.

With |SLP| a solid product, Christian's focus moved onto other
projects, `PyPy <http://pypy.org>`_ among them.  One of his interests in PyPy was
a proper implementation of the Stackless functionality, where it could be
integrated as a natural part of any |PY| interpreter built.

For a while, |SLP| languished, with no new versions to match the
releases of |CPY| itself.  Then in 2006, CCP sent Kristján Valur Jonsson and
Richard Tew to PyCon where `they sprinted
<http://zope.stackless.com/Members/rmtew/News%20Archive/pycon2006/news_item_view>`_
with the aid of Christian Tismer.  The result was an up to date release of |SLP|.
From this point in time, maintaining and releasing |SLP|
has been undertaken by Richard and Kristján. A few years later Anselm Kruis
joined the team.

.. [#PSFTRADEMARK]
   "Python" and the Python logos are trademarks or registered trademarks of the
   *Python Software Foundation*, used by |SLP| with permission from the Foundation.
   See http://www.python.org/psf/trademarks/ for details.

.. [#CPY]
   With the term "|CPY|" we refer to the reference implementation of the |PPL|
   that is released by the *Python Software Foundation* on http://www.python.org.

