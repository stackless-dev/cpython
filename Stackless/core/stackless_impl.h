#ifndef STACKLESS_IMPL_H
#define STACKLESS_IMPL_H

#include "Python.h"

#ifdef STACKLESS

#ifdef __cplusplus
extern "C" {
#endif

#include "structmember.h"
#include "compile.h"
#include "frameobject.h"

#include "module/flextype.h"
#include "core/stackless_structs.h"
#include "pickling/prickelpit.h"

#include <tealet/tealet.h>

#undef STACKLESS_SPY
/*
 * if a platform wants to support self-inspection via _peek,
 * it must provide a function or macro CANNOT_READ_MEM(adr, len)
 * which allows to spy at memory without causing exceptions.
 * This would usually be done in place with the assembly macros.
 */

/********************************************************************
 *
 * This section defines/references stuff from stacklesseval.c
 *
 ********************************************************************/

/*** access to system-wide globals from stacklesseval.c ***/

PyAPI_DATA(int) slp_enable_softswitch;
PyAPI_DATA(int) slp_in_psyco;
PyAPI_DATA(int) slp_try_stackless;
PyAPI_DATA(PyTaskletObject *) slp_tasklet_chain;

#if 0
PyAPI_FUNC(PyCStackObject *) slp_cstack_new(PyCStackObject **cst,
                                            intptr_t *stackref,
                                            PyTaskletObject *task);
PyAPI_FUNC(size_t) slp_cstack_save(PyCStackObject *cstprev);
PyAPI_FUNC(void) slp_cstack_restore(PyCStackObject *cst);

PyAPI_FUNC(int) slp_transfer(PyCStackObject **cstprev, PyCStackObject *cst,
                             PyTaskletObject *prev);

#ifdef Py_DEBUG
PyAPI_FUNC(int) slp_transfer_return(PyCStackObject *cst);
#else
#define slp_transfer_return(cst) \
                slp_transfer(NULL, (cst), NULL)
#endif
#endif

PyAPI_FUNC(int) _PyStackless_InitTypes(void);
PyAPI_FUNC(void) _PyStackless_Init(void);

/* clean-up up at the end */

PyAPI_FUNC(void) slp_stacklesseval_fini(void);
PyAPI_FUNC(void) slp_scheduling_fini(void);
PyAPI_FUNC(void) slp_cframe_fini(void);

PyAPI_FUNC(void) PyStackless_Fini(void);

PyAPI_FUNC(void) PyStackless_kill_tasks_with_stacks(int allthreads);

/* the special version of eval_frame */
PyAPI_FUNC(PyObject *) slp_eval_frame(struct _frame *f);

/* the frame dispatcher */
PyAPI_FUNC(PyObject *) slp_frame_dispatch(PyFrameObject *f,
                                          PyFrameObject *stopframe, int exc,
                                          PyObject *retval);

/* the frame dispatcher for toplevel tasklets */
PyAPI_FUNC(PyObject *) slp_frame_dispatch_top(PyObject *retval);

/* the now exported eval_frame */
PyAPI_FUNC(PyObject *) PyEval_EvalFrame(struct _frame *f);
PyAPI_FUNC(PyObject *) PyEval_EvalFrameEx_slp(struct _frame *, int, PyObject *);

/* eval_frame with stack overflow, triggered there with a macro */
PyAPI_FUNC(PyObject *) slp_eval_frame_newstack(struct _frame *f, int throwflag, PyObject *retval);

/* the new eval_frame loop with or without value or resuming an iterator */
PyAPI_FUNC(PyObject *) PyEval_EvalFrame_value(struct _frame *f,  int throwflag,
                                              PyObject *retval);
PyAPI_FUNC(PyObject *) PyEval_EvalFrame_noval(struct _frame *f,  int throwflag,
                                              PyObject *retval);
PyAPI_FUNC(PyObject *) PyEval_EvalFrame_iter(struct _frame *f,  int throwflag,
                                             PyObject *retval);

/* rebirth of software stack avoidance */

typedef struct {
    PyObject_HEAD
    PyObject *tempval;
} PyUnwindObject;

PyAPI_DATA(PyUnwindObject *) Py_UnwindToken;

/* frame cloning both needed in tasklets and generators */

PyAPI_FUNC(struct _frame *) slp_clone_frame(struct _frame *f);
PyAPI_FUNC(struct _frame *) slp_ensure_new_frame(struct _frame *f);

/* exposing some hidden types */

PyAPI_DATA(PyTypeObject) PyGen_Type;
PyAPI_FUNC(PyObject *) PyGenerator_New(struct _frame *f);
PyAPI_FUNC(PyObject *) slp_gen_send_ex(PyGenObject *gen, PyObject *arg, int exc);
#define PyGenerator_Check(op) PyObject_TypeCheck(op, &PyGen_Type)

PyAPI_DATA(PyTypeObject) PyMethodDescr_Type;
PyAPI_DATA(PyTypeObject) PyClassMethodDescr_Type;

PyAPI_DATA(PyTypeObject) PyMethodWrapper_Type;
#define PyMethodWrapper_Check(op) PyObject_TypeCheck(op, &PyMethodWrapper_Type)

/* fast (release) and safe (debug) access to the unwind token and retval */

#ifdef Py_DEBUG

#define STACKLESS_PACK(retval) \
    (assert(Py_UnwindToken->tempval == NULL), \
     Py_UnwindToken->tempval = (retval), \
     (PyObject *) Py_UnwindToken)

#define STACKLESS_UNPACK(retval) \
    (assert(STACKLESS_UNWINDING(retval)), \
     retval = Py_UnwindToken->tempval, \
     Py_UnwindToken->tempval = NULL, retval)

#else

#define STACKLESS_PACK(retval) \
    (Py_UnwindToken->tempval = (retval), \
     (PyObject *) Py_UnwindToken)

#define STACKLESS_UNPACK(retval) \
    (retval = Py_UnwindToken->tempval, retval)

#endif

#define STACKLESS_UNWINDING(obj) \
    ((PyObject *) (obj) == (PyObject *) Py_UnwindToken)

/* macros for setting/resetting the stackless flag */

#define STACKLESS_POSSIBLE()                                        \
    (slp_enable_softswitch && !slp_in_psyco)

#define STACKLESS_GETARG() int stackless = (stackless = slp_try_stackless, \
                           slp_try_stackless = 0, stackless)

#define STACKLESS_PROMOTE(func) \
    (stackless ? slp_try_stackless = \
     (func)->ob_type->tp_flags & Py_TPFLAGS_HAVE_STACKLESS_CALL : 0)

#define STACKLESS_PROMOTE_FLAG(flag) \
    (stackless ? slp_try_stackless = (flag) : 0)

#define STACKLESS_PROMOTE_METHOD(obj, meth) \
    (obj->ob_type->tp_flags & Py_TPFLAGS_HAVE_STACKLESS_EXTENSION ? \
    slp_try_stackless = stackless & obj->ob_type->slpflags.meth : 0)

#define STACKLESS_PROMOTE_WRAPPER(wp) \
    (slp_try_stackless = stackless & wp->descr->d_slpmask)

#define STACKLESS_PROMOTE_ALL() (slp_try_stackless = stackless, NULL)

#define STACKLESS_PROPOSE(func)                                     \
    {                                                               \
    int stackless = STACKLESS_POSSIBLE();                       \
    STACKLESS_PROMOTE(func);                                    \
    }

#define STACKLESS_PROPOSE_FLAG(flag)                                \
    {                                                               \
    int stackless = STACKLESS_POSSIBLE();                       \
    STACKLESS_PROMOTE_FLAG(flag);                               \
    }

#define STACKLESS_PROPOSE_METHOD(obj, meth)                         \
    {                                                               \
    int stackless = STACKLESS_POSSIBLE();                       \
    STACKLESS_PROMOTE_METHOD(obj, meth);                        \
    }

#define STACKLESS_PROPOSE_ALL()                                     \
    slp_try_stackless = STACKLESS_POSSIBLE()

#define STACKLESS_RETRACT() slp_try_stackless = 0;

#define STACKLESS_ASSERT() assert(!slp_try_stackless)

/* this is just a tag to denote which methods are stackless */

#define STACKLESS_DECLARE_METHOD(type, meth)

/*

  How this works:
  There is one global variable slp_try_stackless which is used
  like an implicit parameter. Since we don't have a real parameter,
  the flag is copied into the local variable "stackless" and cleared.
  This is done by the GETARG() macro, which should be added to
  the top of the function's declarations.
  The idea is to keep the chances to introduce error to the minimum.
  A function can safely do some tests and return before calling
  anything, since the flag is in a local variable.
  Depending on context, this flag is propagated to other called
  functions. They *must* obey the protocol. To make this sure,
  the ASSERT() macro has to be called after every such call.

  Many internal functions have been patched to support this protocol.

  GETARG()

    move the slp_try_stackless flag into the local variable "stackless".

  PROMOTE(func)

    if stackless was set and the function's type has set
    Py_TPFLAGS_HAVE_STACKLESS_CALL, then this flag will be
    put back into slp_try_stackless, and we expect that the
    function handles it correctly.

  PROMOTE_FLAG(flag)

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

  PROMOTE_ALL()

    is used for cases where we know that the called function will take
    care of our object, and we need no test. For example, PyObject_Call
    uses PROMOTE, itself, so we don't need to check further.

  ASSERT()

    make sure that slp_try_stackless was cleared. This debug feature
    tries to ensure that no unexpected nonrecursive call can happen.

  Some functions which are known to be stackless by nature
  just use the PROPOSE macros. They do not care about prior state.
  Most of them are used in ceval.c and other contexts which are
  stackless by definition. All possible nonrecursive calls are
  initiated by these macros.

  Compatibility with Psyco:
  Psyco is not compatible with soft switching and will presumably
  never be. An extra flag 'slp_in_psyco' has been added, which
  overrides 'slp_enable_softswitch'. This flag will be set by Psyco
  whenever Psyco is calling into stackless.
  In the future, a new way of softswitching will be developed, which
  will be understood by Stackless and Psyco.

*/


/********************************************************************
 *
 * This section defines/references stuff from stacklessmodule.c
 *
 ********************************************************************/

/* generic ops for chained objects */

/*  Tasklets are in doubly linked lists. We support
    deletion and insertion */

#define SLP_CHAIN_INSERT(__objtype, __chain, __task, __next, __prev) \
do { \
    __objtype *l, *r; \
    assert((__task)->__next == NULL); \
    assert((__task)->__prev == NULL); \
    if (*(__chain) == NULL) { \
        (__task)->__next = (__task)->__prev = (__task); \
        *(__chain) = (__task); \
    } \
    else { \
        /* insert at end */ \
        r = *(__chain); \
        l = r->__prev; \
        l->__next = r->__prev = (__task); \
        (__task)->__prev = l; \
        (__task)->__next = r; \
    } \
} while(0)

#define SLP_CHAIN_REMOVE(__objtype, __chain, __task, __next, __prev) \
do { \
    __objtype *l, *r; \
    if (*(__chain) == NULL) { \
        (__task) = NULL; \
    } \
    else { \
        /* remove current */ \
        (__task) = *(__chain); \
        l = (__task)->__prev; \
        r = (__task)->__next; \
        l->__next = r; \
        r->__prev = l; \
        *(__chain) = r; \
        if (*(__chain)==(__task)) \
            *(__chain) = NULL;  /* short circuit */ \
        (__task)->__prev = NULL; \
        (__task)->__next = NULL; \
    } \
} while(0)

/* these versions operate on an embedded head, which channels use now */

#define SLP_HEADCHAIN_INSERT(__objtype, __chan, __task, __next, __prev) \
do { \
    __objtype *__head = (__objtype *) __chan; \
    assert((__task)->__next == NULL); \
    assert((__task)->__prev == NULL); \
    /* insert at end */ \
    (__task)->__prev = (__head)->__prev; \
    (__task)->__next = (__head); \
    (__head)->__prev->next = (__task); \
    (__head)->__prev = (__task); \
} while(0)

#define SLP_HEADCHAIN_REMOVE(__task, __next, __prev) \
do { \
    assert((__task)->__next != NULL); \
    assert((__task)->__prev != NULL); \
    /* remove at front */ \
    (__task)->__next->__prev = (__task)->prev; \
    (__task)->__prev->__next = (__task)->next; \
    (__task)->__next = (__task)->__prev = NULL; \
} while(0)

/* operations on chains */

PyAPI_FUNC(void) slp_current_insert(PyTaskletObject *task);
PyAPI_FUNC(void) slp_current_insert_after(PyTaskletObject *task);
PyAPI_FUNC(void) slp_current_uninsert(PyTaskletObject *task);
PyAPI_FUNC(PyTaskletObject *) slp_current_remove(void);
PyAPI_FUNC(void) slp_current_unremove(PyTaskletObject *task);
PyAPI_FUNC(void) slp_channel_insert(
                                    PyChannelObject *channel,
                                    PyTaskletObject *task,
                                    int dir, PyTaskletObject *next);
PyAPI_FUNC(PyTaskletObject *) slp_channel_remove(
                                    PyChannelObject *channel,
                                    PyTaskletObject *task,
                                    int *dir, PyTaskletObject **next);
PyAPI_FUNC(void) slp_channel_remove_slow(
                                    PyTaskletObject *task,
                                    PyChannelObject **u_chan,
                                    int *dir, PyTaskletObject **next);									

/* recording the main thread state */

PyAPI_DATA(PyThreadState *) slp_initial_tstate;

/* protecting soft-switched tasklets in other threads */

PyAPI_FUNC(int) slp_ensure_linkage(PyTaskletObject *task);

/* tasklet/scheduling operations */

PyAPI_FUNC(PyObject *) slp_tasklet_new(PyTypeObject *type, PyObject *args,
                                       PyObject *kwds);

PyAPI_FUNC(int) slp_schedule_task(
                                    PyObject **result,
                                    PyTaskletObject *prev,
                                    PyTaskletObject *next,
                                    int stackless,
                                    int *did_switch);

PyAPI_FUNC(void) slp_thread_unblock(PyThreadState *ts);

PyAPI_FUNC(int) initialize_main_and_current(void);

/* setting the tasklet's tempval, optimized for no change */

#define TASKLET_SETVAL(task, val) \
do { \
    if ((task)->tempval != (PyObject *) val) { \
        Py_INCREF(val); \
        TASKLET_SETVAL_OWN(task, val); \
    } \
} while(0)

/* ditto, without incref. Made no sense to optimize. */

#define TASKLET_SETVAL_OWN(task, val) \
do { \
    PyObject *hold = (task)->tempval; \
    assert(val != NULL); \
    (task)->tempval = (PyObject *) val; \
    Py_DECREF(hold); \
} while(0)

/* exchanging values with safety check */

#define TASKLET_SWAPVAL(prev, next) \
do { \
    PyObject *hold = (prev)->tempval; \
    assert((prev)->tempval != NULL); \
    assert((next)->tempval != NULL); \
    (prev)->tempval = (next)->tempval; \
    (next)->tempval = hold; \
} while(0)

/* Get the value and replace it with a None */

#define TASKLET_CLAIMVAL(task, val) \
do { \
    *(val) = (task)->tempval; \
    (task)->tempval = Py_None; \
    Py_INCREF(Py_None); \
} while(0)


/* exception handling */

PyAPI_FUNC(PyObject *) slp_make_bomb(PyObject *klass, PyObject *args, char *msg);
PyAPI_FUNC(PyObject *) slp_exc_to_bomb(PyObject *exc, PyObject *val, PyObject *tb);
PyAPI_FUNC(PyObject *) slp_curexc_to_bomb(void);
PyAPI_FUNC(PyObject *) slp_bomb_explode(PyObject *bomb);

/* tasklet startup */

PyAPI_FUNC(PyObject *) slp_run_tasklet(PyFrameObject *f);

/* handy abbrevations */

PyAPI_FUNC(PyObject *) slp_type_error(const char *msg);
PyAPI_FUNC(PyObject *) slp_runtime_error(const char *msg);
PyAPI_FUNC(PyObject *) slp_value_error(const char *msg);
PyAPI_FUNC(PyObject *) slp_null_error(void);

/* this seems to be needed for gcc */

/* Define NULL pointer value */

#undef NULL
#ifdef  __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif

#define TYPE_ERROR(str, ret) return (slp_type_error(str), ret)
#define RUNTIME_ERROR(str, ret) return (slp_runtime_error(str), ret)
#define VALUE_ERROR(str, ret) return (slp_value_error(str), ret)

PyAPI_FUNC(PyCFrameObject *) slp_cframe_new(PyFrame_ExecFunc *exec,
                                            unsigned int linked);
PyAPI_FUNC(PyCFrameObject *) slp_cframe_newfunc(PyObject *func,
                                                PyObject *args,
                                                PyObject *kwds,
                                                unsigned int linked);

PyAPI_FUNC(PyFrameObject *) slp_get_frame(PyTaskletObject *task);
PyAPI_FUNC(void) slp_check_pending_irq(void);
PyAPI_FUNC(int) slp_return_wrapper(PyObject *retval);
PyAPI_FUNC(int) slp_int_wrapper(PyObject *retval);
PyAPI_FUNC(int) slp_current_wrapper(int(*func)(PyTaskletObject*),
                                    PyTaskletObject *task);
PyAPI_FUNC(int) slp_resurrect_and_kill(PyObject *self,
                                       void(*killer)(PyObject *));

/* stackless pickling support */

PyAPI_FUNC(int) slp_safe_pickling(int(*save)(PyObject *, PyObject *, int),
                                  PyObject *self, PyObject *args,
                                  int pers_save);

/* debugging/monitoring */

typedef int (slp_schedule_hook_func) (PyTaskletObject *from,
                                       PyTaskletObject *to);
PyAPI_DATA(slp_schedule_hook_func) *_slp_schedule_fasthook;
PyAPI_DATA(PyObject* ) _slp_schedule_hook;
int slp_schedule_callback(PyTaskletObject *prev, PyTaskletObject *next);

/* macro for use when interrupting tasklets from watchdog */
#define TASKLET_NESTING_OK(task) \
    (ts->st.nesting_level == 0 || \
     (task)->flags.ignore_nesting || \
     (ts->st.runflags & PY_WATCHDOG_IGNORE_NESTING))


#include "stackless_api.h"

#else /* STACKLESS */

/* turn the stackless flag macros into dummies */

#define STACKLESS_GETARG() int stackless = 0
#define STACKLESS_PROMOTE(func) stackless = 0
#define STACKLESS_PROMOTE_FLAG(flag) stackless = 0
#define STACKLESS_PROMOTE_METHOD(obj, meth) stackless = 0
#define STACKLESS_PROMOTE_WRAPPER(wp) stackless = 0
#define STACKLESS_PROMOTE_ALL() stackless = 0
#define STACKLESS_PROPOSE(func) assert(1)
#define STACKLESS_PROPOSE_FLAG(flag) assert(1)
#define STACKLESS_PROPOSE_ALL() assert(1)
#define STACKLESS_RETRACT() assert(1)
#define STACKLESS_ASSERT() assert(1)

#define STACKLESS_DECLARE_METHOD(type, meth)

#endif /* STACKLESS */

#ifdef __cplusplus
}
#endif

#endif
