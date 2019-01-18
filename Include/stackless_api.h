#ifndef STACKLESS_API_H
#define STACKLESS_API_H

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************

  Stackless Python Application Interface


  Note: Some switching functions have a variant with the
  same name, but ending on "_nr". These are non-recursive
  versions with the same functionality, but they might
  avoid a hard stack switch.
  Their return value is ternary, and they require the
  caller to return to its frame, properly.
  All three different cases must be treated.

  Ternary return from an integer function:
    value          meaning           action
     -1            failure           return NULL
      1            soft switched     return Py_UnwindToken
      0            hard switched     return Py_None

  Ternary return from a PyObject * function:
    value          meaning           action
    NULL           failure           return NULL
    Py_UnwindToken soft switched     return Py_UnwindToken
    other          hard switched     return value

  Note: Py_UnwindToken is *never* inc/decref'ed. Use the
        macro STACKLESS_UNWINDING(retval) to test for
        Py_UnwindToken

 ******************************************************/

#ifdef STACKLESS

#include "slp_structs.h"

/*
 * create a new tasklet object.
 * type must be derived from PyTasklet_Type or NULL.
 * func must (yet) be a callable object (normal usecase)
 */
PyAPI_FUNC(PyTaskletObject *) PyTasklet_New(PyTypeObject *type, PyObject *func);
/* 0 = success  -1 = failure */

/*
 * bind a tasklet function to parameters, making it ready to run,
 * and insert in into the runnables queue.
 */
PyAPI_FUNC(int) PyTasklet_Setup(PyTaskletObject *task, PyObject *args, PyObject *kwds);

/*
 * bind a tasklet function to parameters, making it ready to run.
 */
PyAPI_FUNC(int) PyTasklet_BindEx(PyTaskletObject *task, PyObject *func, PyObject *args, PyObject *kwargs);

/*
 * bind a tasklet function to a thread.
 */
PyAPI_FUNC(int) PyTasklet_BindThread(PyTaskletObject *task, unsigned long thread_id);

/*
 * forces the tasklet to run immediately.
 */
PyAPI_FUNC(int) PyTasklet_Run(PyTaskletObject *task);
/* 0 = success  -1 = failure */
PyAPI_FUNC(int) PyTasklet_Run_nr(PyTaskletObject *task);
/* 1 = soft switched  0 = hard switched  -1 = failure */

/*
 * raw switching.  The previous tasklet is paused.
 */
PyAPI_FUNC(int) PyTasklet_Switch(PyTaskletObject *task);
/* 0 = success  -1 = failure */
PyAPI_FUNC(int) PyTasklet_Switch_nr(PyTaskletObject *task);
/* 1 = soft switched  0 = hard switched  -1 = failure */

/*
 * removing a tasklet from the runnables queue.
 * Be careful! If this tasklet has a C stack attached,
 * you need to either continue it or kill it. Just dropping
 * might give an inconsistent system state.
 */
PyAPI_FUNC(int) PyTasklet_Remove(PyTaskletObject *task);
/* 0 = success  -1 = failure */

/*
 * insert a tasklet into the runnables queue, if it isn't
 * already in. Results in a runtime error if the tasklet is
 * blocked or dead.
 */
PyAPI_FUNC(int) PyTasklet_Insert(PyTaskletObject *task);
/* 0 = success  -1 = failure */

/*
 * raising an exception for a tasklet.
 * The provided class must be a subclass of Exception.
 * There is one special exception that does not invoke
 * main as exception handler: PyExc_TaskletExit
 * This exception is used to silently kill a tasklet,
 * see PyTasklet_Kill.
 */

PyAPI_FUNC(int) PyTasklet_RaiseException(PyTaskletObject *self,
                                         PyObject *klass, PyObject *args);
/* 0 = success  -1 = failure.
 * Note that this call always ends in some exception, so the
 * caller always should return NULL.
 */

/* Similar function, but using the "throw" semantics */
PyAPI_FUNC(int) PyTasklet_Throw(PyTaskletObject *self,
                     int pending, PyObject *exc,
                     PyObject *val, PyObject *tb);

/*
 * Killing a tasklet.
 * PyExc_TaskletExit is raised for the tasklet.
 * This exception is ignored by tasklet_end and
 * does not invode main as exception handler.
 */

PyAPI_FUNC(int) PyTasklet_Kill(PyTaskletObject *self);
PyAPI_FUNC(int) PyTasklet_KillEx(PyTaskletObject *self, int pending);
/* 0 = success  -1 = failure.
 * Note that this call always ends in some exception, so the
 * caller always should return NULL.
 */


/*
 * controlling the atomic flag of a tasklet.
 * an atomic tasklets will not be auto-scheduled.
 * note that this is an overridable attribute, for
 * monitoring purposes.
 */

PyAPI_FUNC(int) PyTasklet_SetAtomic(PyTaskletObject *task, int flag);
/* returns the old value and sets logical value of flag */

PyAPI_FUNC(int) PyTasklet_SetIgnoreNesting(PyTaskletObject *task, int flag);
/* returns the old value and sets logical value of flag */

/*
 * the following functions are not overridable so far.
 * I don't think this is an essental feature.
 */

PyAPI_FUNC(int) PyTasklet_GetAtomic(PyTaskletObject *task);
/* returns the value of the atomic flag */

PyAPI_FUNC(int) PyTasklet_GetIgnoreNesting(PyTaskletObject *task);
/* returns the value of the ignore_nesting flag */

PyAPI_FUNC(int) PyTasklet_GetPendingIrq(PyTaskletObject *task);
/* returns the value of the pending_irq flag */

PyAPI_FUNC(PyObject *) PyTasklet_GetFrame(PyTaskletObject *task);
/* returns the frame which might be NULL */

PyAPI_FUNC(int) PyTasklet_GetBlockTrap(PyTaskletObject *task);
/* returns the value of the bock_trap flag */

PyAPI_FUNC(void) PyTasklet_SetBlockTrap(PyTaskletObject *task, int value);
/* sets block_trap to the logical value of value */

PyAPI_FUNC(int) PyTasklet_IsMain(PyTaskletObject *task);
/* 1 if task is main, 0 if not */

PyAPI_FUNC(int) PyTasklet_IsCurrent(PyTaskletObject *task);
/* 1 if task is current, 0 if not */

PyAPI_FUNC(int) PyTasklet_GetRecursionDepth(PyTaskletObject *task);
/* returns the recursion depth of task */

PyAPI_FUNC(int) PyTasklet_GetNestingLevel(PyTaskletObject *task);
/* returns the nesting level of task,
 * i.e. the number of nested interpreters.
 */

PyAPI_FUNC(int) PyTasklet_Alive(PyTaskletObject *task);
/* 1 if the tasklet has a frame, 0 if it's dead */

PyAPI_FUNC(int) PyTasklet_Paused(PyTaskletObject *task);
/* 1 if the tasklet is paused, i.e. alive but in no chain, else 0 */

PyAPI_FUNC(int) PyTasklet_Scheduled(PyTaskletObject *task);
/* 1 if the tasklet is scheduled, i.e. in some chain, else 0 */

PyAPI_FUNC(int) PyTasklet_Restorable(PyTaskletObject *task);
/* 1 if the tasklet can execute after unpickling, else 0 */

/******************************************************

  channel related functions

 ******************************************************/

/*
 * create a new channel object.
 * type must be derived from PyChannel_Type or NULL.
 */
PyAPI_FUNC(PyChannelObject *) PyChannel_New(PyTypeObject *type);

/*
 * send data on a channel.
 * if nobody is listening, you will get blocked and scheduled.
 */
PyAPI_FUNC(int) PyChannel_Send(PyChannelObject *self, PyObject *arg);
/* 0 = success  -1 = failure */

PyAPI_FUNC(int) PyChannel_Send_nr(PyChannelObject *self, PyObject *arg);
/* 1 = soft switched  0 = hard switched  -1 = failure */

/*
 * receive data from a channel.
 * if nobody is talking, you will get blocked and scheduled.
 */
PyAPI_FUNC(PyObject *) PyChannel_Receive(PyChannelObject *self);
/* Object or NULL */
PyAPI_FUNC(PyObject *) PyChannel_Receive_nr(PyChannelObject *self);
/* Object, Py_UnwindToken or NULL */

/*
 * send an exception over a channel.
 * the exception will explode at the receiver side.
 * if nobody is listening, you will get blocked and scheduled.
 */
PyAPI_FUNC(int) PyChannel_SendException(PyChannelObject *self,
                                        PyObject *klass, PyObject *value);
/* 0 = success  -1 = failure */

/*
 * Similar, but using the same arguments as "raise"
 */;
PyAPI_FUNC(int) PyChannel_SendThrow(PyChannelObject *self, PyObject *exc, PyObject *val, PyObject *tb);

/* the next tasklet in the queue or None */
PyAPI_FUNC(PyObject *) PyChannel_GetQueue(PyChannelObject *self);

/* close() the channel */
PyAPI_FUNC(void) PyChannel_Close(PyChannelObject *self);

/* open() the channel */
PyAPI_FUNC(void) PyChannel_Open(PyChannelObject *self);

/* whether close() was called */
PyAPI_FUNC(int) PyChannel_GetClosing(PyChannelObject *self);

/* closing, and the queue is empty */

PyAPI_FUNC(int) PyChannel_GetClosed(PyChannelObject *self);

/*
 * preferred scheduling policy.
 * Note: the scheduling settings have no effect on interthread communication.
 * When threads are involved, we only transfer between the threads.
 */
PyAPI_FUNC(int) PyChannel_GetPreference(PyChannelObject *self);
/* -1 = prefer receiver  1 = prefer sender  0 no prference */

/* changing preference */

PyAPI_FUNC(void) PyChannel_SetPreference(PyChannelObject *self, int val);

/* whether we always schedule after any channel action (ignores preference) */

PyAPI_FUNC(int) PyChannel_GetScheduleAll(PyChannelObject *self);
PyAPI_FUNC(void) PyChannel_SetScheduleAll(PyChannelObject *self, int val);

/*
 *Get the current channel balance. Negative numbers are readers, positive
 * are writers
 */
PyAPI_FUNC(int) PyChannel_GetBalance(PyChannelObject *self);

/******************************************************

  stacklessmodule functions

 ******************************************************/

/*
 * suspend the current tasklet and schedule the next one in the cyclic chain.
 * if remove is nonzero, the current tasklet will be removed from the chain.
 */
PyAPI_FUNC(PyObject *) PyStackless_Schedule(PyObject *retval, int remove);
/*
 * retval = success  NULL = failure
 */
PyAPI_FUNC(PyObject *) PyStackless_Schedule_nr(PyObject *retval, int remove);
/*
 * retval = success  NULL = failure
 * retval == Py_UnwindToken: soft switched
 */

/*
 * get the number of runnable tasks, including the current one.
 */
PyAPI_FUNC(int) PyStackless_GetRunCount(void);
/* -1 = failure */

/*
 * get the currently running tasklet, that is, "yourself".
 */
PyAPI_FUNC(PyObject *) PyStackless_GetCurrent(void);

/*
 * get a unique integer ID for the current tasklet.
 *
 * Threadsafe.
 * This is useful for for example benchmarking code that
 * needs to get some sort of a stack identifier and must
 * not worry about the GIL being present and so on.
 * Notes:
 * 1) the "main" tasklet on each thread will have
 * the same id even if a proper tasklet has not been initialized
 * 2) IDs may get recycled for new tasklets.
 */
PyAPI_FUNC(unsigned long) PyStackless_GetCurrentId(void);

/*
 * run the runnable tasklet queue until all are done,
 * an uncaught exception occoured, or the timeout
 * has been met.
 * This function can only be called from the main tasklet.
 * During the run, main is suspended, but will be invoked
 * after the action. You will write your exception handler
 * here, since every uncaught exception will be directed
 * to main.
 * In case on a timeout (opcode count), the return value
 * will be the long-running tasklet, removed from the queue.
 * You might decide to kill it or to insert it again.
 * flags is interpreted as an OR of :
 * Py_WATCHDOG_THREADBLOCK:
 *   When set enables the old thread-blocking behaviour when
 *   we run out of tasklets on this thread and there are other
 *   Python(r) threads running.
 * Py_WATCHDOG_SOFT:
 *   Instead of interrupting a tasklet, we wait until the
 *   next tasklet scheduling moment to return.  Always returns
 *   Py_None, as everything is in order.
 * Py_WATCHDOG_IGNORE_NESTING:
 *   allows interrupts at all levels, effectively acting as
 *   though the "ignore_nesting" attribute were set on all
 *   tasklets.
 * Py_WATCHDOG_TIMEOUT:
 *   interprets 'timeout' as a total timeout, rather than a
 *   timeslice length.  The function will then attempt to
 *   interrupt execution 
 */
#define Py_WATCHDOG_THREADBLOCK         1
#define PY_WATCHDOG_SOFT                2
#define PY_WATCHDOG_IGNORE_NESTING      4
#define PY_WATCHDOG_TOTALTIMEOUT        8
PyAPI_FUNC(PyObject *) PyStackless_RunWatchdog(long timeout);
PyAPI_FUNC(PyObject *) PyStackless_RunWatchdogEx(long timeout, int flags);

/******************************************************

  Support for soft switchable extension functions: SLFunction

  These functions have been added on a provisional basis (See PEP 411).

 ******************************************************/

typedef PyObject *(slp_softswitchablefunc) (PyObject *retval,
        long *step, PyObject **ob1, PyObject **ob2, PyObject **ob3,
        long *n, void **any);

typedef struct {
    PyObject_HEAD
    slp_softswitchablefunc * sfunc;
    const char * name;
    const char * module_name;
} PyStacklessFunctionDeclarationObject;

PyAPI_DATA(PyTypeObject) PyStacklessFunctionDeclaration_Type;

#define PyStacklessFunctionDeclarationType_CheckExact(op) \
                (Py_TYPE(op) == &PyStacklessFunctionDeclaration_Type)

PyAPI_FUNC(PyObject *) PyStackless_CallFunction(
        PyStacklessFunctionDeclarationObject *sfd, PyObject *arg,
        PyObject *ob1, PyObject *ob2, PyObject *ob3, long n, void *any);

PyAPI_FUNC(int) PyStackless_InitFunctionDeclaration(
        PyStacklessFunctionDeclarationObject *sfd, PyObject *module, PyModuleDef *module_def);

/*

Macros for the "stackless protocol"
===================================

How to does Stackless Python decide, if a function may return an unwind-token.

There is one global variable _PyStackless_TRY_STACKLESS which is used
like an implicit parameter. Since we don't have a real parameter,
the flag is copied into the local variable "stackless" and cleared.
This is done by the STACKLESS_GETARG() macro, which should be added to
the top of the function's declarations.
The idea is to keep the chances to introduce error to the minimum.
A function can safely do some tests and return before calling
anything, since the flag is in a local variable.
Depending on context, this flag is propagated to other called
functions. They *must* obey the protocol. To make this sure,
the STACKLESS_ASSERT() macro has to be called after every such call.

Many internal functions have been patched to support this protocol.

STACKLESS_GETARG()

  Move the _PyStackless_TRY_STACKLESS flag into the local variable "stackless".

STACKLESS_PROMOTE_ALL()

  is used for cases where we know that the called function will take
  care of our object, and we need no test. For example, PyObject_Call
  and all other Py{Object,Function,CFunction}_*Call* functions use
  STACKLESS_PROMOTE_xxx, itself, so we don't need to check further.

STACKLESS_PROMOTE(func)

  If stackless was set and the function's type has set
  Py_TPFLAGS_HAVE_STACKLESS_CALL, then this flag will be
  put back into _PyStackless_TRY_STACKLESS, and we expect that the
  function handles it correctly.

STACKLESS_PROMOTE_FLAG(flag)

  is used for special cases, like PyCFunction objects. PyCFunction_Type
  says that it supports a stackless call, but the final action depends
  on the METH_STACKLESS flag in the object to be called. Therefore,
  PyCFunction_Call uses PROMOTE_FLAG(flags & METH_STACKLESS) to
  take care of PyCFunctions which don't care about it.

  Another example is the "next" method of iterators. To support this,
  the wrapperobject's type has the Py_TPFLAGS_HAVE_STACKLESS_CALL
  flag set, but wrapper_call then examines the wrapper descriptors
  flags if PyWrapperFlag_STACKLESS is set. "next" has it set.
  It also checks whether Py_TPFLAGS_HAVE_STACKLESS_CALL is set
  for the iterator's type.

STACKLESS_ASSERT()

  Make sure that _PyStackless_TRY_STACKLESS was cleared. This debug feature
  tries to ensure that no unexpected nonrecursive call can happen.

STACKLESS_RETRACT()

  Reset _PyStackless_TRY_STACKLESS. Rarely needed.

*/

#define STACKLESS_GETARG() \
    int stackless = (STACKLESS__GETARG_ASSERT, \
                     stackless = _PyStackless_TRY_STACKLESS, \
                     _PyStackless_TRY_STACKLESS = 0, \
                     stackless)

#define STACKLESS_PROMOTE_ALL() ((void)(_PyStackless_TRY_STACKLESS = stackless, NULL))

#define STACKLESS_PROMOTE_FLAG(flag) \
    (stackless ? (_PyStackless_TRY_STACKLESS = (flag)) : 0)

#define STACKLESS_RETRACT() (_PyStackless_TRY_STACKLESS = 0)

#define STACKLESS_ASSERT() assert(!_PyStackless_TRY_STACKLESS)

#else /* STACKLESS */
/* turn the stackless flag macros into dummies */
#define STACKLESS_GETARG() int stackless = 0
#define STACKLESS_PROMOTE_ALL() (stackless = 0)
#define STACKLESS_PROMOTE_FLAG(flag) (stackless = 0)
#define STACKLESS_RETRACT() assert(1)
#define STACKLESS_ASSERT() assert(1)
#endif

#define STACKLESS_PROMOTE_METHOD(obj, slot_name) \
    STACKLESS_PROMOTE_FLAG( \
        (Py_TYPE(obj)->tp_flags & Py_TPFLAGS_HAVE_STACKLESS_EXTENSION) && \
        Py_TYPE(obj)->tp_as_mapping && \
                Py_TYPE(obj)->tp_as_mapping->slpflags.slot_name)

#define STACKLESS_PROMOTE(obj) \
    STACKLESS_PROMOTE_FLAG( \
        Py_TYPE(obj)->tp_flags & Py_TPFLAGS_HAVE_STACKLESS_CALL)


#ifdef STACKLESS

/******************************************************

  debugging and monitoring functions

 ******************************************************/

/*
 * channel debugging.
 * The callable will be called on every send or receive.
 * Passing NULL removes the handler.
 *
 * Parameters of the callable:
 *     channel, tasklet, int sendflag, int willblock
 */
PyAPI_FUNC(int) PyStackless_SetChannelCallback(PyObject *callable);
/* -1 = failure */

/*
 * scheduler monitoring.
 * The callable will be called on every scheduling.
 * Passing NULL removes the handler.
 *
 * Parameters of the callable:
 *     from, to
 * When a tasklet dies, to is None.
 * After death or when main starts up, from is None.
 */
PyAPI_FUNC(int) PyStackless_SetScheduleCallback(PyObject *callable);
/* -1 = failure */


/*
 * scheduler monitoring with a faster interface.
 */
PyAPI_FUNC(void) PyStackless_SetScheduleFastcallback(slp_schedule_hook_func func);

/******************************************************

  other functions

 ******************************************************/


/*
 * Stack unwinding
 */
PyAPI_DATA(PyUnwindObject *) Py_UnwindToken;
#define STACKLESS_UNWINDING(obj) \
    ((PyObject *) (obj) == (PyObject *) Py_UnwindToken)

/******************************************************

  interface functions

 ******************************************************/

/*
 * Most of the above functions can be called both from "inside"
 * and "outside" stackless. "inside" means there should be a running
 * (c)frame on top which acts as the "main tasklet". The functions
 * do a check whether the main tasklet exists, and wrap themselves
 * if it is necessary.
 * The following routines are used to support this, and you may use
 * them as well if you need to make your specific functions always
 * available.
 */

/*
 * Run any callable as the "main" Python(r) function.
 */
PyAPI_FUNC(PyObject *) PyStackless_Call_Main(PyObject *func,
                                             PyObject *args, PyObject *kwds);

/*
 * Convenience: Run any method as the "main" Python(r) function.
 */
PyAPI_FUNC(PyObject *) PyStackless_CallMethod_Main(PyObject *o, char *name,
                                                   char *format, ...);

/*
 *convenience: Run any cmethod as the "main" Python(r) function.
 */
PyAPI_FUNC(PyObject *) PyStackless_CallCMethod_Main(
                    PyMethodDef *meth, PyObject *self, char *format, ...);


#endif /* STACKLESS */

#ifdef __cplusplus
}
#endif

#endif
