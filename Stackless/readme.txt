About Stackless Python
======================

You can find information about Stackless Python in the following
places:

1. The Manual

Stackless extends the Python documentation. The entry point
is the file Doc/stackless-python.rst. Or read the documentation online
at http://stackless.readthedocs.io

2. The Stackless Web Site http://www.stackless.com

The FAQ (https://bitbucket.org/stackless-dev/stackless/wiki/FAQ) has
a list of further readings.

3. The Mailing List

If you still have questions, don't hesitate to post to the
Stackless mailing list: http://www.stackless.com/mailman/listinfo/stackless

4. The Source

It is the ultimate truth. Read it at https://github.com/stackless-dev/stackless

Building Stackless
------------------

Just follow the build instructions for regular Python. If you define
the C-preprocessor symbol STACKLESS_OFF, you get a Python interpreter
without Stackless. It should behave exactly like the corresponding
version of regular Python. Any difference constitutes a bug.
(Windows 64bit only: if you define STACKLESS_OFF, you also need
to clear the content of the file Stackless\platf\switch_x64_masm.asm:
  c:\> echo END > Stackless\platf\switch_x64_masm.asm )

Contributing
------------

Contributions are always welcome. Please open an issue or create
a pull request at https://bitbucket.org/stackless-dev/stackless.


##################################################################
#                                                                #
#   WARNING: the following text is of historical interest only   #
#                                                                #
##################################################################

A small update as of 2006-03-01

Meanwhile a long time has passed since the initial Stackless
version which was from 1999 somehwere.

In 2003, the fabulous PyPy porject was started, which is still
performing very well. I have implemented Stackless for PyPy
(with lots of support from Armin), and it is so incredibly
much nicer. No more fiddling with existing calling conventions,
no compromizes, everything that needs to be stackless also is.

Unfortuantely, PyPy is still not fast and complete enough to
take over. This means, my users are still whining for an update
all the time CPython gets an update.
And maintaining this code gets more and more a nightmare for
me, since I have the nice PyPy version, and I hate hacking this
clumsy C code again and again.

One nice thing to record here is the fact that CCPGames is now
supporting me eahily. Not by sponsorship (which doesn't help
very much, since money can hardly help with the pain of
porting), but they really stepped in and ported Stackless for
Python 2.4.2. I was involved quite much, but mainly giving hints
and finding bugs. I hope very much that I can leave this maintenance
work to them in the future.

One thing that I might do anyway is back-porting the new coroutine
interface. It is even smaller than greenlets and probably the
very minimum possible. It would make sense to build this into
Stackless to get rid of some early design decisions. Anyway, this
has to wait until the PyPy version is stabilized and had some
revisions.

Having said all this, the description of current stackless is
still not there, and the following old text is no longer the
truth, since much more was done.
Especially, the abandonship was undone, and Stackless is now
at version 3.1, which is a quite complicated merger between
the 1.0 and 2.0 designes, supporting both cooperative switching
and stack switching, with the addition of partially complete
support for real OS threads as a tasklet. Phew.

*****************************************************************************

  Introduction to the ideas of the new Stackless Python
  Draft, CT 20020122
  Update 20020524: Software solution abandoned!

  Please review!

  Stackless Python now tries to follow some different goals:

  - the implementation should be as understandable as possible.
      (this part is slightly obscure due to my nature,
	   but he is still eager to learn)

  - the users requests should be followed first.
    Especially, coroutines and microthreads are of larger interest.
	Generators are less interesting, since Python 2.2 includes a
	marveless, stack based implementation which is hard to improve.
	The abilities of continuations have been experienced by only
	a very small group of insiders, and they agreed that these
	are better not made available fo the public. Just too powerful.

  - my own porting efforts should be minimized.
    I am now trying to reduce the Stackless patches to their bare
	minimum. Partially, I avoid constructs which can be of value,
	but would require massive code changes. Partially, I'm now
	no longer reluctant to use techniques which are an absolute
	no-no, if I would again try to get into the distribution.
    My current strategy is to minimize the patches in a way that
	makes inclusion irrelevant. I.e. try to make very few changes,
	and keep the distro's and Stackless's intersection to the minimum.


*****************************************************************************

  A Bit Of History.

  I have spent about three years developing Stackless Python in
  several versions. This was very hard work, and it didn't pay
  off in the way I intended. Especially, Stackless Python was never
  understood good enough to be incorporated into the Standard Python
  distribution. Instead of a warm welcome and instructions how to
  make the sources suitable for inclusion, I got rejections and dissings,
  especially from Guido. At that time I was really disappointed, and
  it took some time to understand that his reaction was absolutely
  correct. The code was simply too tricky and complicated, and the
  implementation effort necessary to get complete stacklessness was high.
  Also, I was too much bound to the incredible concept of continuations,
  which finally killed my success.

  Today, I have a different point of view:
  Stackless is not ripe for Python, and Python is not ripe for Stackless.
  Both parties have their position. Stackless has a couple of users
  which have an absolute need for its feature meanwhile, and my work
  also continuously gets some sponsorship from game developers.
  In April, I was also hired by IronPort to work on Stackless, which is great.

*****************************************************************************

  The New Concept!

  After an unsuccessful port to Python 2.1, my first desperate attempt to
  break out was a complete redesign, based upon assembly callbacks only.
  Fortunately, my brain recovers quickly from such events. Years of
  thinking cannot be replaced by quick thoughts, and well-thought concepts
  are immutable against quick mind changes.

  Nevertheless, an eternal problem was the impossibility to make Python
  really completely stackless. This impossibility was produced by myself,
  since I still believed to be incorporated. This had the implication of
  never ever using any platform specific stuff like assembly or compiler
  dependant constructs. Having to do everything by highly complicated
  rewrites of standard concepts was the real show stoppers. The lack of
  support for third party extension modules did the rest.

  After all, these problems have vanished. It turned out that people are
  happy if they get their Stackless, and they don't care how we achieve
  this. On the other hand, doing platform specific stuff is hard, and
  I cannot support all of it. Of course there are helpers. but what is
  the general strategy?


  The answer is:
  ==============

  We do no longer support a native, compatible Stackless implementation
  but a hardware/platform dependant version only. This gives me the
  most effect with minimum maintenance work.

  My basic concept is to have a single place on the C stack where all
  Python calls end up with. This place is state-less. It does not keep
  state on the C stack. Given that, it is possible to switch between
  frames which match this condition. You may call these coroutines or
  microthreads, but it doesn't matter. We are dealing with frame chains
  which can be switched against their Python state structure.

  How was old Stackless stateless?
  Well, it tried it's best. It was tried to turn every recursive call
  to the nterpreter into tail recursion. That means: Try to avoid a
  recursive interpreter call. Prepare everything for its call, create
  all the parameters, but then do not recursively run the frame, but
  try to postpone this action, letting a toplevel interpeter act on it.
  This concept worked very well, despite Python methods. The latter
  either have post conditions which have to be met, or they follow
  a difficult protocol in object.c or abstract.c, which makes it nearly
  impossible to turn them into tail position, without a complete
  rewrite of the above modules, including all non-trivial object
  impementatiions.
  Conclusion: Old Stackless was good but incomplete.

  How is the new Stackless stateless?
  Well, in principle, it is the same. Just the technique used is
  a bit different. I do not rewrite tons of recursive code
  into something tail-recursive. Instead, I wrap up all the stack
  data that makes up a frame's C impact, put it into a structure
  and stick it into a memory area in the frame. After this, the C
  stack is cleared from the frame specific stuff.
  The frame contains a procedure which is able to restore this frame
  state on demand.

  The interested reader will have noticed, that this concept covers
  all of the former Stackless approach, but also much more:
  This concept always works, not only with Python functions, but
  also with all methods, and eve with C extensions. Whatever binary
  calls are triggered by a frame, regardless what kind of C callback
  it may be, everything can be considered as the activation part of
  a Python frame. When the frame is about to be left, this data has
  to be saved. When the frame resumes, this data has to be restored
  before.

  The key idea is to wrap every interpreter recursion's
  C stack frame into some structure. This structure is saved with
  the frame, when a new frame is executed.
  The f_execute field is changed to a function which re-establishes
  the stack environment which the function needs for executing..

to be continued...
