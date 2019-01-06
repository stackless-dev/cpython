#ifndef STACKLESS_IMPL_H
#define STACKLESS_IMPL_H

#include "Python.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef STACKLESS

#if defined(Py_BUILD_CORE) && !defined(SLP_BUILD_CORE)
#define SLP_BUILD_CORE
#endif

#include "stackless_api.h"

#ifdef SLP_BUILD_CORE

#ifdef Py_BUILD_CORE
#include "internal/pystate.h"  /* for _PyRuntime */
#endif
#include "pickling/prickelpit.h"

#undef STACKLESS_SPY
/*
 * if a platform wants to support self-inspection via _peek,
 * it must provide a function or macro SLP_CANNOT_READ_MEM(adr, len)
 * which allows to spy at memory without causing exceptions.
 * This would usually be done in place with the assembly macros.
 */

/*
 * Macros used to extract bit-field values from an integer in a portable
 * way.
 */
#define SLP_EXTRACT_BITS_(integer, bits, offset) \
    (((integer) & (((1U << (bits))-1U) << (offset))) >> (offset))

#define SLP_SET_BITFIELD(NAME_PREFIX, bitfield_struct, integer, member) \
    (bitfield_struct).member = SLP_EXTRACT_BITS_((integer), NAME_PREFIX ## _BITS_ ## member, NAME_PREFIX ## _OFFSET_ ## member)

#define SLP_PREPARE_BITS_(value, offset) \
    (((unsigned int)(value)) << (offset))

#define SLP_GET_BITFIELD(NAME_PREFIX, bitfield_struct, member) \
    SLP_PREPARE_BITS_((bitfield_struct).member, NAME_PREFIX ## _OFFSET_ ## member)

/*
 * About the reference counting of frames and C-frames
 *
 * The reference counting of frames follows the rules for Python objects
 * as documented in the Python C-API documentation. The only exception is
 * that the reference in PyThreadState.frame is a borrowed (uncounted)
 * reference. No difference to regular C-Python up to this point.
 * (New since Stackless 2.7.13. In previous versions frames ate their own
 * reference when returning.)
 *
 * But what happens during stack unwinding and stack switching? Both
 * operations transfer the frame to be executed next up to the eval-frame loop
 * or to the new stack. Without special provisions the ref-count of this frame
 * could drop to zero during unwinding/switching. To avoid this, the "transfer
 * mechanism" itself owns a reference. It is created by the macro
 * SLP_STORE_NEXT_FRAME(tstate, frame). The macro
 * SLP_CLAIM_NEXT_FRAME(tstate) removes the reference from the "tranfer
 * mechanism", stores the frame in 'tstate->frame' and returns a new reference
 * to the frame.
 * To enforce the sequent execution of SLP_STORE_NEXT_FRAME() and
 * SLP_CLAIM_NEXT_FRAME(), SLP_STORE_NEXT_FRAME() invalidates tstate->frame,
 * if SLP_WITH_FRAME_REF_DEBUG is defined.
 *
 * There are a few additional macros, which shall be used to access the members
 * of tstate. They all add debug code in debug builds, but not in release builds:
 *
 * SLP_ASSERT_FRAME_IN_TRANSFER(tstate)
 *
 *  Assert, that a frame is in transfer. (Because frame transfers can be nested,
 *  if a Py_DECREF causes a recursive invocation of the interpreter, it is not
 *  possible to assert, that no frame is in transfer.)
 *
 *
 * SLP_IS_FRAME_IN_TRANSFER_AFTER_EXPR(tstate, py_ssize_t_tmpvar, expr)
 *
 *  This macro executes 'expr' and returns 1 if 'expr' started a frame transfer.
 *  Otherwise, the macro returns 0. 'py_ssize_t_tmpvar' is a temporary Py_ssize_t
 *  variable.
 *
 *
 * SLP_CURRENT_FRAME_IS_VALID(tstate)
 *
 *  Returns 1 if 'tstate->frame' is valid. To be used in assertions and debug code.
 *
 *
 * SLP_SET_CURRENT_FRAME(tstate, frame)
 *
 *  Set the current frame (tstate->frame) to frame.
 *
 *
 * SLP_STORE_NEXT_FRAME(tstate, frame)
 *
 *  Store a new reference to the (next) frame in the transfer mechanism and
 *  invalidate the current frame.
 *
 *
 * SLP_CLAIM_NEXT_FRAME(tstate)
 *
 *  Removes the reference to the next frame from the "tranfer mechanism",
 *  stores the frame in 'tstate->frame' and returns a new reference to the frame.
 *
 *
 * SLP_CURRENT_FRAME(tstate)
 *
 *  Return a borrowed reference to the current frame (tstate->frame). Assert
 *  that the current frame is valid.
 *
 *
 * SLP_PEEK_NEXT_FRAME(tstate)
 *
 *  Return a borrowed reference to the next frame, if a frame transfer is in
 *  progress. Otherwise, return a borrowed reference to the current frame.
 *
 *
 * The following two macros deal with the frame execution functions
 * 'frame->f_execute':
 *
 * CALL_FRAME_FUNCTION(frame, exc, retval)
 *
 *  Execute the frame->f_execute(frame, exc, retval). In debug builds,
 *  the macro performs a few sanity checks.
 *
 *
 * SLP_FRAME_EXECFUNC_DECREF(cframe)
 *
 *  If a frame execution function hard switches the stack (using
 *  slp_transfer_return()) instead of returning to the caller, pending
 *  Py_DECREF()-calls for references hold by the C-functions on the stack
 *  won't be executed. This would cause reference leaks.
 *  To avoid such leaks, the frame execution function must call
 *  SLP_FRAME_EXECFUNC_DECREF(cframe) before it calls slp_transfer_return().
 *  The macro SLP_FRAME_EXECFUNC_DECREF(cframe) performs the required
 *  'Py_DECREF's.
 *
 *
 * The following macro is used to eventually restore 'tstate->frame'
 * during a frame transfer.
 *
 * SLP_WITH_VALID_CURRENT_FRAME(expr)
 *
 *  Execute 'expr' with a valid 'tstate->frame'.
 *  Used in _Py_Dealloc() to eventually restore 'tstate->frame'. Only defined
 *  if Py_TRACE_REFS is defined.
 */

/* The macro SLP_WITH_FRAME_REF_DEBUG gets eventually defined in
 * stackless_tstate.h. It enables reference debugging for frames and C-frames.
 */
#ifdef SLP_WITH_FRAME_REF_DEBUG
#ifndef Py_TRACE_REFS
#error "SLP_WITH_FRAME_REF_DEBUG requires Py_TRACE_REFS"
/* The SLP_WITH_FRAME_REF_DEBUG mode sets 'tstate->frame' to the invalid
 * address 1, in order to prevent any usage of the frame during a frame
 * transfer.
 * A Py_DECREF can lead to a recursive invocation of the Python interpreter
 * during frame transfers. Therefore, it is necessary to restore
 * 'tstate->frame', if a reference counter drops to zero. This is done by
 * patching _Py_Dealloc(), which is only available, if Py_TRACE_REFS is
 * defined.
 */
#endif

#define _SLP_FRAME_IN_TRANSFER(tstate) \
    ((tstate)->frame == (PyFrameObject *)((Py_uintptr_t) 1))

#define _SLP_FRAME_STATE_IS_CONSISTENT(tstate) \
    (_SLP_FRAME_IN_TRANSFER(tstate) == (tstate)->st.frame_refcnt)

#define SLP_ASSERT_FRAME_IN_TRANSFER(tstate) \
    (assert(_SLP_FRAME_STATE_IS_CONSISTENT(tstate)), \
    assert(_SLP_FRAME_IN_TRANSFER(tstate)))

#define SLP_IS_FRAME_IN_TRANSFER_AFTER_EXPR(tstate, py_ssize_t_tmpvar, expr) \
    ((py_ssize_t_tmpvar) = (tstate)->st.frame_refcnt, \
    assert(_SLP_FRAME_STATE_IS_CONSISTENT(tstate)), \
    assert(!_SLP_FRAME_IN_TRANSFER(tstate)), \
    assert((py_ssize_t_tmpvar) == 0), \
    (void)(expr), \
    assert((tstate)->st.frame_refcnt >= 0), \
    assert(_SLP_FRAME_IN_TRANSFER(tstate) == (tstate)->st.frame_refcnt), \
    (tstate)->st.frame_refcnt > 0)

#define SLP_CURRENT_FRAME_IS_VALID(tstate) \
    (!_SLP_FRAME_IN_TRANSFER(tstate) && ((tstate)->frame == NULL || Py_REFCNT((tstate)->frame) > 0))

#define SLP_SET_CURRENT_FRAME(tstate, frame_) \
    (assert(_SLP_FRAME_STATE_IS_CONSISTENT(tstate)), \
    assert(SLP_CURRENT_FRAME_IS_VALID(tstate)), \
    (void)((tstate)->frame = (frame_)), \
    assert(_SLP_FRAME_STATE_IS_CONSISTENT(tstate)), \
    assert(SLP_CURRENT_FRAME_IS_VALID(tstate)))

void slp_store_next_frame(PyThreadState *tstate, PyFrameObject *frame);
#define SLP_STORE_NEXT_FRAME(tstate, frame_) \
    slp_store_next_frame(tstate, frame_)

PyFrameObject * slp_claim_next_frame(PyThreadState *tstate);
#define SLP_CLAIM_NEXT_FRAME(tstate) \
    slp_claim_next_frame(tstate)

#define SLP_CURRENT_FRAME(tstate) \
    (assert(SLP_CURRENT_FRAME_IS_VALID(tstate)), (tstate)->frame)

#define SLP_PEEK_NEXT_FRAME(tstate) \
    (assert(_SLP_FRAME_STATE_IS_CONSISTENT(tstate)), \
    (_SLP_FRAME_IN_TRANSFER(tstate) ? (tstate)->st.next_frame : SLP_CURRENT_FRAME(tstate)))

#if (SLP_WITH_FRAME_REF_DEBUG == 2)
/* extra heavy debugging */
#define SLP_FRAME_EXECFUNC_DECREF(cf) \
    do { \
        if (cf) { \
            assert(PyCFrame_Check(cf)); \
            assert(Py_REFCNT(cf) >= 2); \
            Py_DECREF(cf); /* the eval_frame loop */ \
            Py_DECREF(cf); /* wrap_call_frame */ \
        } \
    } while (0)

PyObject * slp_wrap_call_frame(PyFrameObject *frame, int exc, PyObject *retval);
#define CALL_FRAME_FUNCTION(frame_, exc, retval) \
    slp_wrap_call_frame(frame_, exc, retval)

#endif  /* #if (SLP_WITH_FRAME_REF_DEBUG == 2) */

/* Access the thread state without a check for NULL */
#define _SLP_PyThreadState_Current ((PyThreadState*)_Py_atomic_load_relaxed(&_PyThreadState_Current))

/* Used in _Py_Dealloc() to eventually restore tstate->frame */
#define SLP_WITH_VALID_CURRENT_FRAME(expr) \
    do { \
        if (_SLP_PyThreadState_Current != NULL && \
            (assert(_SLP_FRAME_STATE_IS_CONSISTENT(_SLP_PyThreadState_Current)), \
            _SLP_FRAME_IN_TRANSFER(_SLP_PyThreadState_Current))) \
        { \
            PyThreadState * ts = _SLP_PyThreadState_Current; \
            PyFrameObject * frame = SLP_CLAIM_NEXT_FRAME(ts); \
            (void)(expr); \
            assert(frame == NULL || Py_REFCNT(frame) >= 1); \
            assert(_SLP_FRAME_STATE_IS_CONSISTENT(ts)); \
            assert(SLP_CURRENT_FRAME(ts) == frame); \
            SLP_STORE_NEXT_FRAME(ts, frame); \
            Py_XDECREF(frame); \
        } else { \
            (void)(expr); \
            assert(_SLP_PyThreadState_Current == NULL || _SLP_FRAME_STATE_IS_CONSISTENT(_SLP_PyThreadState_Current)); \
            assert(_SLP_PyThreadState_Current == NULL || !_SLP_FRAME_IN_TRANSFER(_SLP_PyThreadState_Current)); \
        } \
    } while (0)

#else /* #ifdef SLP_WITH_FRAME_REF_DEBUG */

#define SLP_ASSERT_FRAME_IN_TRANSFER(tstate) \
    assert((tstate)->st.frame_refcnt > 0)

#define SLP_IS_FRAME_IN_TRANSFER_AFTER_EXPR(tstate, py_ssize_t_tmpvar, expr) \
    ((py_ssize_t_tmpvar) = (tstate)->st.frame_refcnt, \
    assert((py_ssize_t_tmpvar) >= 0), \
    (void)(expr), \
    assert((tstate)->st.frame_refcnt >= (py_ssize_t_tmpvar)), \
    (tstate)->st.frame_refcnt > (py_ssize_t_tmpvar))

#define SLP_CURRENT_FRAME_IS_VALID(tstate) \
    ((tstate)->frame == NULL || Py_REFCNT((tstate)->frame) > 0)

#define SLP_SET_CURRENT_FRAME(tstate, frame_) \
    (assert((tstate)->st.frame_refcnt >= 0), \
    assert(SLP_CURRENT_FRAME_IS_VALID(tstate)), \
    (void)((tstate)->frame = (frame_)), \
    assert(SLP_CURRENT_FRAME_IS_VALID(tstate)))

#define SLP_STORE_NEXT_FRAME(tstate, frame_) \
    do { \
        assert((tstate)->st.frame_refcnt >= 0); \
        SLP_SET_CURRENT_FRAME((tstate), (frame_)); \
        Py_XINCREF((tstate)->frame); \
        (tstate)->st.frame_refcnt++; \
    } while (0)

#define SLP_CLAIM_NEXT_FRAME(tstate) \
    (assert((tstate)->st.frame_refcnt > 0), \
    (tstate)->st.frame_refcnt--, \
    (tstate)->frame)

#define SLP_CURRENT_FRAME(tstate) \
    (assert(SLP_CURRENT_FRAME_IS_VALID(tstate)), (tstate)->frame)

#define SLP_PEEK_NEXT_FRAME(tstate) \
    ((tstate)->frame)

#ifdef Py_TRACE_REFS
#define SLP_WITH_VALID_CURRENT_FRAME(expr) \
    do { \
        (void)(expr); \
    } while (0)

#endif  /* #ifdef Py_TRACE_REFS */

#endif /* #ifdef SLP_WITH_FRAME_REF_DEBUG */

#ifndef SLP_FRAME_EXECFUNC_DECREF

#define SLP_FRAME_EXECFUNC_DECREF(cf) \
    do{ \
        if (cf) { \
            assert(PyCFrame_Check(cf)); \
            assert(Py_REFCNT(cf) >= 1); \
            Py_DECREF(cf); /* the eval_frame loop */ \
        } \
    } while (0)

#define CALL_FRAME_FUNCTION(frame_, exc, retval) \
     (assert((frame_) && (frame_)->f_execute), \
     ((frame_)->f_execute((frame_), (exc), (retval))))

#endif

/********************************************************************
 *
 * This section defines/references stuff from stacklesseval.c
 *
 ********************************************************************/

/*** access to system-wide globals from stacklesseval.c ***/

PyCStackObject * slp_cstack_new(PyCStackObject **cst, intptr_t *stackref, PyTaskletObject *task);
size_t slp_cstack_save(PyCStackObject *cstprev);
void slp_cstack_restore(PyCStackObject *cst);

int slp_transfer(PyCStackObject **cstprev, PyCStackObject *cst, PyTaskletObject *prev);

#ifdef Py_DEBUG
int slp_transfer_return(PyCStackObject *cst);
#else
#define slp_transfer_return(cst) \
                slp_transfer(NULL, (cst), NULL)
#endif

PyAPI_FUNC(int) _PyStackless_InitTypes(void);
PyAPI_FUNC(void) _PyStackless_Init(void);

/* clean-up up at the end */

void slp_stacklesseval_fini(void);
void slp_scheduling_fini(void);
void slp_cframe_fini(void);

void PyStackless_Fini(void);

void PyStackless_kill_tasks_with_stacks(int allthreads);

/* the special version of eval_frame */
PyObject * slp_eval_frame(struct _frame *f);

/* the frame dispatcher */
PyObject * slp_frame_dispatch(PyFrameObject *f,
                              PyFrameObject *stopframe, int exc,
                              PyObject *retval);

/* the now exported eval_frame */
PyAPI_FUNC(PyObject *) PyEval_EvalFrameEx_slp(struct _frame *, int, PyObject *);

/* eval_frame with stack overflow, triggered there with a macro */
PyObject * slp_eval_frame_newstack(struct _frame *f, int throwflag, PyObject *retval);

/* the new eval_frame loop with or without value or resuming an iterator
   or setting up or cleaning up a with block */
PyObject * slp_eval_frame_value(struct _frame *f,  int throwflag, PyObject *retval);
PyObject * slp_eval_frame_noval(struct _frame *f,  int throwflag, PyObject *retval);
PyObject * slp_eval_frame_iter(struct _frame *f,  int throwflag, PyObject *retval);
PyObject * slp_eval_frame_setup_with(struct _frame *f,  int throwflag, PyObject *retval);
PyObject * slp_eval_frame_with_cleanup(struct _frame *f,  int throwflag, PyObject *retval);
PyObject * slp_eval_frame_yield_from(struct _frame *f,  int throwflag, PyObject *retval);
/* other eval_frame functions from module/scheduling.c */
PyObject * slp_restore_tracing(PyFrameObject *f, int exc, PyObject *retval);
/* other eval_frame functions from Objects/typeobject.c */
PyObject * slp_tp_init_callback(PyFrameObject *f, int exc, PyObject *retval);
/* functions related to pickling */
PyObject * slp_reduce_frame(PyFrameObject * frame);

/* frame cloning both needed in tasklets and generators */

struct _frame * slp_clone_frame(struct _frame *f);
struct _frame * slp_ensure_new_frame(struct _frame *f);


/* access to the current watchdog tasklet */
PyTaskletObject * slp_get_watchdog(PyThreadState *ts, int interrupt);


/* access to the unwind token and retval */

#define STACKLESS_PACK(tstate, retval) \
    (assert((tstate)->st.unwinding_retval == NULL), \
     (tstate)->st.unwinding_retval = (retval), \
     (PyObject *) Py_UnwindToken)

#define STACKLESS_UNPACK(tstate, retval) \
    ((void)(assert(STACKLESS_UNWINDING(retval)), \
     retval = (tstate)->st.unwinding_retval, \
     (tstate)->st.unwinding_retval = NULL, retval))

#if 0 /* defined in stackless_api.h */
#define STACKLESS_UNWINDING(obj) \
    ((PyObject *) (obj) == (PyObject *) Py_UnwindToken)
#endif

/* an arbitrary positive number */
#define STACKLESS_UNWINDING_MAGIC 0x7fedcba9

#define STACKLESS_RETVAL(tstate, obj) \
    (STACKLESS_UNWINDING(obj) ? (tstate)->st.unwinding_retval : (obj))

#define STACKLESS_ASSERT_UNWINDING_VALUE_IS_NOT(tstate, obj, val) \
    assert(!STACKLESS_UNWINDING(obj) || (((tstate)->st.unwinding_retval) != (val)))

/* macros for setting/resetting the stackless flag */

#ifdef _PyStackless_TRY_STACKLESS
#undef _PyStackless_TRY_STACKLESS
#endif
#define _PyStackless_TRY_STACKLESS (_PyRuntime.st.try_stackless)

#define STACKLESS_POSSIBLE(tstate) \
    ((tstate)->interp->st.enable_softswitch && (tstate)->st.unwinding_retval == NULL)

#ifdef STACKLESS__GETARG_ASSERT
#undef STACKLESS__GETARG_ASSERT
#endif
#define STACKLESS__GETARG_ASSERT \
    assert(SLP_CURRENT_FRAME_IS_VALID(PyThreadState_GET()))

/* descr must be of type PyWrapperDescrObject, but this type is undocumented.
 * Therefore this macro is in stackless_impl.h and not in stackless_api.h
 */
#define STACKLESS_PROMOTE_WRAPPER(descr) \
    STACKLESS_PROMOTE_FLAG((descr)->d_slpmask)

#define STACKLESS_PROPOSE(tstate, func) {int stackless = STACKLESS_POSSIBLE(tstate); \
                 STACKLESS_PROMOTE(func);}

#define STACKLESS_PROPOSE_FLAG(tstate, flag) {int stackless = STACKLESS_POSSIBLE(tstate); \
                      STACKLESS_PROMOTE_FLAG(flag);}

#define STACKLESS_PROPOSE_METHOD(tstate, obj, meth) {int stackless = STACKLESS_POSSIBLE(tstate); \
                 STACKLESS_PROMOTE_METHOD(obj, meth);}

#define STACKLESS_PROPOSE_ALL(tstate) _PyStackless_TRY_STACKLESS = STACKLESS_POSSIBLE(tstate)

/* This can be set to 1 to completely disable the augmentation of
 * type info with stackless property.  For debugging.
 */
#define STACKLESS_NO_TYPEINFO 0

#if !STACKLESS_NO_TYPEINFO
/* This macro changes to NULL, if STACKLESS is not defined. */
#define SLP_TP_AS_MAPPING(tp_as_mapping) (&(tp_as_mapping))
#else
#define SLP_TP_AS_MAPPING(tp_as_mapping) 0
#endif

/*

  How this works:
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

    move the _PyStackless_TRY_STACKLESS flag into the local variable "stackless".

  STACKLESS_PROMOTE_ALL()

    is used for cases where we know that the called function will take
    care of our object, and we need no test. For example, PyObject_Call
    and all other Py{Object,Function,CFunction}_*Call* functions use
    STACKLESS_PROMOTE_xxx, itself, so we don't need to check further.

  STACKLESS_PROMOTE(func)

    if stackless was set and the function's type has set
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

    make sure that _PyStackless_TRY_STACKLESS was cleared. This debug feature
    tries to ensure that no unexpected nonrecursive call can happen.

  Some functions which are known to be stackless by nature
  just use the STACKLESS_PROPOSE_xxx macros. They do not care about prior
  state. Most of them are used in ceval.c and other contexts which are
  stackless by definition. All possible nonrecursive calls are
  initiated by these macros.

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

void slp_current_insert(PyTaskletObject *task);
void slp_current_insert_after(PyTaskletObject *task);
void slp_current_uninsert(PyTaskletObject *task);
PyTaskletObject * slp_current_remove(void);
void slp_current_remove_tasklet(PyTaskletObject *task);
void slp_current_unremove(PyTaskletObject *task);
void slp_channel_insert(PyChannelObject *channel,
                        PyTaskletObject *task,
                        int dir, PyTaskletObject *next);
PyTaskletObject * slp_channel_remove(PyChannelObject *channel,
                                     PyTaskletObject *task,
                                     int *dir, PyTaskletObject **next);
void slp_channel_remove_slow(PyTaskletObject *task,
                             PyChannelObject **u_chan,
                             int *dir, PyTaskletObject **next);

/* protecting soft-switched tasklets in other threads */
int slp_ensure_linkage(PyTaskletObject *task);

/* tasklet/scheduling operations */
PyObject * slp_tasklet_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
PyObject * slp_tasklet_end(PyObject *retval);

int slp_schedule_task(PyObject **result,
                      PyTaskletObject *prev,
                      PyTaskletObject *next,
                      int stackless,
                      int *did_switch);

void slp_thread_unblock(PyThreadState *ts);

int slp_initialize_main_and_current(void);

/* setting the tasklet's tempval, optimized for no change */

#define TASKLET_SETVAL(task, val) \
do { \
    if ((task)->tempval != (PyObject *) val) { \
        Py_INCREF(val); \
        TASKLET_SETVAL_OWN(task, val); \
    } \
} while(0)

/* ditto, without incref. Made no sense to optimize. */
#if defined(__GNUC__) && defined(__STDC__) && (__STDC_VERSION__ >= 199901L)
#define SLP_DISABLE_GCC_W_ADDRESS \
            _Pragma("GCC diagnostic push") \
            _Pragma("GCC diagnostic ignored \"-Waddress\"")
#define SLP_RESTORE_WARNINGS \
            _Pragma("GCC diagnostic pop")
#else
#define SLP_DISABLE_GCC_W_ADDRESS /**/
#define SLP_RESTORE_WARNINGS /**/
#endif

#define TASKLET_SETVAL_OWN(task, val) \
do { \
    PyObject *hold = (task)->tempval; \
    SLP_DISABLE_GCC_W_ADDRESS /* suppress warning, if val == Py_NONE */ \
    assert(val != NULL); \
    SLP_RESTORE_WARNINGS \
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

PyObject * slp_make_bomb(PyObject *klass, PyObject *args, char *msg);
PyObject * slp_exc_to_bomb(PyObject *exc, PyObject *val, PyObject *tb);
PyObject * slp_curexc_to_bomb(void);
PyObject * slp_nomemory_bomb(void);
PyObject * slp_bomb_explode(PyObject *bomb);
int slp_init_bombtype(void);
PyObject * slp_get_obj_for_exc_state(_PyErr_StackItem *exc_info);
PyBombObject * slp_new_bomb(void);

/* handy abbrevations */

PyObject * slp_type_error(const char *msg);
PyObject * slp_runtime_error(const char *msg);
PyObject * slp_value_error(const char *msg);
PyObject * slp_null_error(void);

#define TYPE_ERROR(str, ret) return (slp_type_error(str), ret)
#define RUNTIME_ERROR(str, ret) return (slp_runtime_error(str), ret)
#define VALUE_ERROR(str, ret) return (slp_value_error(str), ret)

PyCFrameObject * slp_cframe_new(PyFrame_ExecFunc *exec,
                                unsigned int linked);
PyCFrameObject * slp_cframe_newfunc(PyObject *func,
                                    PyObject *args,
                                    PyObject *kwds,
                                    unsigned int linked);

PyFrameObject * slp_get_frame(PyTaskletObject *task);
void slp_check_pending_irq(void);
int slp_return_wrapper(PyObject *retval);
int slp_return_wrapper_hard(PyObject *retval);
int slp_int_wrapper(PyObject *retval);
int slp_current_wrapper(int(*func)(PyTaskletObject*),
                        PyTaskletObject *task);

/* stackless pickling support */
PyObject * slp_coro_wrapper_reduce(PyObject *o, PyTypeObject * wrapper_type);
PyObject * slp_coro_wrapper_new(PyCoroObject *gen);
PyObject * slp_coro_wrapper_setstate(PyObject *self, PyObject *args);
int slp_async_gen_init_hooks(PyAsyncGenObject *o);
PyObject * slp_async_gen_asend_reduce(PyObject *o, PyTypeObject * wrapper_type);
PyObject * slp_async_gen_asend_new(PyAsyncGenObject *gen);
PyObject * slp_async_gen_asend_setstate(PyObject *self, PyObject *args);
PyObject * slp_async_gen_athrow_reduce(PyObject *o, PyTypeObject * wrapper_type);
PyObject * slp_async_gen_athrow_new(PyAsyncGenObject *gen);
PyObject * slp_async_gen_athrow_setstate(PyObject *self, PyObject *args);

/* debugging/monitoring */

int slp_schedule_callback(PyTaskletObject *prev, PyTaskletObject *next);

int slp_prepare_slots(PyTypeObject*);

Py_tracefunc slp_get_sys_profile_func(void);
Py_tracefunc slp_get_sys_trace_func(void);
int slp_encode_ctrace_functions(Py_tracefunc c_tracefunc, Py_tracefunc c_profilefunc);
PyTaskletTStateStruc * slp_get_saved_tstate(PyTaskletObject *task);

PyObject * slp_get_channel_callback(void);

/* macro for use when interrupting tasklets from watchdog */
#define TASKLET_NESTING_OK(task) \
    (ts->st.nesting_level == 0 || \
     (task)->flags.ignore_nesting || \
     (ts->st.runflags & PY_WATCHDOG_IGNORE_NESTING))

/* Interpreter shutdown and thread state access */
PyObject * slp_getthreads(PyObject *self);
void slp_head_lock(void);
void slp_head_unlock(void);
#define SLP_HEAD_LOCK() slp_head_lock()
#define SLP_HEAD_UNLOCK() slp_head_unlock()

long slp_parse_thread_id(PyObject *thread_id, unsigned long *id);

#endif /* #ifdef SLP_BUILD_CORE */

#else /* STACKLESS */

#ifdef SLP_BUILD_CORE
/* turn the stackless flag macros into dummies */

#define SLP_PEEK_NEXT_FRAME(tstate) \
    ((tstate)->frame)

#define STACKLESS_PROPOSE(tstate, func) assert(1)
#define STACKLESS_PROPOSE_FLAG(tstate, flag) assert(1)
#define STACKLESS_PROPOSE_ALL(tstate) assert(1)
#define STACKLESS_PROPOSE_METHOD(tstate, obj, meth) assert(1)

#define STACKLESS_RETVAL(tstate, obj) (obj)
#define STACKLESS_ASSERT_UNWINDING_VALUE_IS_NOT(tstate, obj, val) assert(1)

#define SLP_TP_AS_MAPPING(tp_as_mapping) 0

#define slp_initialize(s) /* empty */

#endif /* #ifdef SLP_BUILD_CORE */
#endif /* STACKLESS */

#ifdef __cplusplus
}
#endif

#endif
