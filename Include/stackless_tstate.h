/* This include file is included from pystate.h only */

#ifndef Py_LIMITED_API

/*** addition to tstate ***/
#ifdef Py_DEBUG
#ifndef SLP_WITH_FRAME_REF_DEBUG
/* SLP_WITH_FRAME_REF_DEBUG must be either undefined, 1 or 2
 *  undefined: disable frame recerence count debugging
 *  1: normal frame recerence count debugging
 *  2: heavy (slow) frame recerence count debugging
 */
/* #define SLP_WITH_FRAME_REF_DEBUG 1 */
/* #define SLP_WITH_FRAME_REF_DEBUG 2 */
#endif
#endif

/* forward declarations */
struct _cstack;
struct _bomb;
struct _tasklet;
struct _ts;

typedef int (slp_schedule_hook_func) (struct _tasklet *from, struct _tasklet *to);

typedef struct {
    struct _cstack * cstack_chain;              /* the chain of all C-stacks of this interpreter. This is an uncounted/borrowed ref. */
    PyObject * reduce_frame_func;               /* a function used to pickle frames */
    PyObject * error_handler;                   /* the Stackless error handler */
    PyObject * channel_hook;                    /* the channel callback function */
    struct _bomb * mem_bomb;                    /* a permanent bomb to use for memory errors */
    PyObject * schedule_hook;                   /* the schedule callback function */
    slp_schedule_hook_func * schedule_fasthook; /* the fast C-only schedule_hook */
    struct _ts * initial_tstate;                /* recording the main thread state */
    uint8_t enable_softswitch;                  /* the flag which decides whether we try to use soft switching */
    uint8_t pickleflags;                        /* flags for pickling / unpickling */
} PyStacklessInterpreterState;

#ifdef Py_BUILD_CORE
#define SLP_INITIAL_TSTATE(tstate) \
    (assert(tstate), \
     assert((tstate)->interp->st.initial_tstate), \
     (tstate)->interp->st.initial_tstate)

#define SPL_INTERPRETERSTATE_NEW(interp)       \
    (interp)->st.cstack_chain = NULL;          \
    (interp)->st.reduce_frame_func = NULL;     \
    (interp)->st.error_handler = NULL;         \
    (interp)->st.channel_hook = NULL;          \
    (interp)->st.mem_bomb = NULL;              \
    (interp)->st.schedule_hook = NULL;         \
    (interp)->st.schedule_fasthook = NULL;     \
    (interp)->st.initial_tstate = NULL;        \
    (interp)->st.enable_softswitch = 1;        \
    (interp)->st.pickleflags = 0;

#define SPL_INTERPRETERSTATE_CLEAR(interp)     \
    (interp)->st.cstack_chain = NULL; /* uncounted ref */  \
    Py_CLEAR((interp)->st.reduce_frame_func);  \
    Py_CLEAR((interp)->st.error_handler);      \
    Py_CLEAR((interp)->st.mem_bomb);           \
    Py_CLEAR((interp)->st.channel_hook);       \
    Py_CLEAR((interp)->st.schedule_hook);      \
    (interp)->st.schedule_fasthook = NULL;     \
    (interp)->st.enable_softswitch = 1;        \
    (interp)->st.pickleflags = 0;

#endif /* #ifdef Py_BUILD_CORE */

struct _frame; /* Avoid including frameobject.h */

typedef struct _sts {
    /* "serial" is incremented each time we create a new stub.
     * (enter "stackless" from the outside)
     * and "serial_last_jump" indicates to which stub the current
     * stack belongs.  This is used to find out if a stack switch
     * is required when the main tasklet exits
     */
#ifdef have_long_long
    long_long serial;
    long_long serial_last_jump;
#else
    long serial;
    long serial_last_jump;
#endif
    /* the blueprint for new stacks */
    struct _cstack *initial_stub;
    /* the base address for hijacking stacks. XXX deprecating */
    intptr_t *cstack_base;
    /* stack overflow check and init flag */
    intptr_t *cstack_root;
    /* main tasklet */
    struct _tasklet *main;
    /* runnable tasklets */
    struct _tasklet *current;

    /* scheduling */
    long tick_counter;
    long tick_watermark;
    long interval;
    PyObject * (*interrupt) (void);    /* the fast scheduler */
    struct {
        PyObject *block_lock;                   /* to block the thread */
        int is_blocked;                         /* waiting to be unblocked */
        int is_idle;                            /* unblocked, but waiting for GIL */
    } thread;
    PyObject *del_post_switch;                  /* To decref after a switch */
    PyObject *interrupted;                      /* The interrupted tasklet in stackles.run() */
    PyObject *watchdogs;                        /* the stack of currently running watchdogs */
    PyObject *unwinding_retval;                 /* The return value during stack unwinding */
    Py_ssize_t frame_refcnt;                    /* The number of owned references to frames */
    int runcount;
    int nesting_level;                          /* number of nested interpreters */
    int switch_trap;                            /* if non-zero, switching is forbidden */
    uint8_t schedlock;                          /* trap recursive scheduling via callbacks */
    uint8_t runflags;                           /* flags for stackless.run() behaviour */
    uint8_t pickleflags;                        /* flags for pickling / unpickling */
#ifdef SLP_WITH_FRAME_REF_DEBUG
    struct _frame *next_frame;                  /* a ref counted copy of PyThreadState.frame */
#endif
} PyStacklessState;

#ifdef Py_BUILD_CORE

/* internal macro to temporarily disable soft interrupts */
#define PY_WATCHDOG_NO_SOFT_IRQ (1U<<7)

/* these macros go into pystate.c */
#ifdef SLP_WITH_FRAME_REF_DEBUG
#define __STACKLESS_PYSTATE_NEW_NEXT_FRAME tstate->st.next_frame = NULL;
#define __STACKLESS_PYSTATE_CLEAR_NEXT_FRAME Py_CLEAR(tstate->st.next_frame);
#else
#define __STACKLESS_PYSTATE_NEW_NEXT_FRAME
#define __STACKLESS_PYSTATE_CLEAR_NEXT_FRAME
#endif

#define __STACKLESS_PYSTATE_NEW \
    tstate->st.initial_stub = NULL; \
    tstate->st.serial = 0; \
    tstate->st.serial_last_jump = 0; \
    tstate->st.cstack_base = NULL; \
    tstate->st.cstack_root = NULL; \
    tstate->st.main = NULL; \
    tstate->st.current = NULL; \
    tstate->st.tick_counter = 0; \
    tstate->st.tick_watermark = 0; \
    tstate->st.interval = 0; \
    tstate->st.interrupt = NULL; \
    tstate->st.del_post_switch = NULL; \
    tstate->st.interrupted = NULL; \
    tstate->st.watchdogs = NULL; \
    tstate->st.unwinding_retval = NULL; \
    tstate->st.frame_refcnt = 0; \
    tstate->st.runcount = 0; \
    tstate->st.nesting_level = 0; \
    tstate->st.switch_trap = 0; \
    tstate->st.schedlock = 0; \
    tstate->st.runflags = 0; \
    tstate->st.pickleflags = tstate->interp->st.pickleflags; \
    __STACKLESS_PYSTATE_NEW_NEXT_FRAME


/* note that the scheduler knows how to zap. It checks if it is in charge
   for this tstate and then clears everything. This will not work if
   we use Py_CLEAR, since it clears the pointer before deallocating.
 */


void slp_kill_tasks_with_stacks(struct _ts *tstate);

#define __STACKLESS_PYSTATE_CLEAR \
    Py_CLEAR(tstate->st.initial_stub); \
    Py_CLEAR(tstate->st.del_post_switch); \
    Py_CLEAR(tstate->st.interrupted); \
    Py_CLEAR(tstate->st.watchdogs); \
    Py_CLEAR(tstate->st.unwinding_retval); \
    __STACKLESS_PYSTATE_CLEAR_NEXT_FRAME

#define STACKLESS_PYSTATE_NEW \
    __STACKLESS_PYSTATE_NEW \
    tstate->st.thread.block_lock = NULL; \
    tstate->st.thread.is_blocked = 0;\
    tstate->st.thread.is_idle = 0;

#define STACKLESS_PYSTATE_CLEAR \
    __STACKLESS_PYSTATE_CLEAR \
    Py_CLEAR(tstate->st.thread.block_lock); \
    tstate->st.thread.is_blocked = 0; \
    tstate->st.thread.is_idle = 0;

#endif /* #ifdef Py_BUILD_CORE */
#endif /* #ifndef Py_LIMITED_API */
