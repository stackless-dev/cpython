/*** addition to tstate ***/

typedef struct _sts {
    struct tealet_t *initial_stub;  /* stub for creating new tealets */
    struct tealet_t *tealet_main;   /* the current "main" tealet */
    /* main tasklet */
    struct PyTaskletObject *main;
    /* runnable tasklets */
    struct PyTaskletObject *current;
    int runcount;

    /* scheduling */
    long ticker;
    long interval;
    PyObject * (*interrupt) (void);    /* the fast scheduler */
    /* trap recursive scheduling via callbacks */
    int schedlock;
    int runflags;                               /* flags for stackless.run() behaviour */
#ifdef WITH_THREAD
    struct {
        PyObject *block_lock;                   /* to block the thread */
        int is_blocked;
    } thread;
#endif
    /* number of nested interpreters (1.0/2.0 merge) */
    int nesting_level;
    PyObject *del_post_switch;                  /* To decref after a switch */
    PyObject *interrupted;                      /* The interrupted tasklet in stackles.run() */
    int switch_trap;                            /* if non-zero, switching is forbidden */
} PyStacklessState;

/* internal macro to temporarily disable soft interrupts */
#define PY_WATCHDOG_NO_SOFT_IRQ (1<<31)

/* these macros go into pystate.c */
#define __STACKLESS_PYSTATE_NEW \
    tstate->st.initial_stub = NULL; \
    tstate->st.tealet_main = NULL; \
    tstate->st.ticker = 0; \
    tstate->st.interval = 0; \
    tstate->st.interrupt = NULL; \
    tstate->st.schedlock = 0; \
    tstate->st.main = NULL; \
    tstate->st.current = NULL; \
    tstate->st.runcount = 0; \
    tstate->st.nesting_level = 0; \
    tstate->st.runflags = 0; \
    tstate->st.del_post_switch = NULL; \
    tstate->st.interrupted = NULL; \
    tstate->st.switch_trap = 0;


/* note that the scheduler knows how to zap. It checks if it is in charge
   for this tstate and then clears everything. This will not work if
   we use Py_CLEAR, since it clears the pointer before deallocating.
 */

struct _ts; /* Forward */

void slp_kill_tasks_with_stacks(struct _ts *tstate);
void slp_tealet_cleanup(struct _ts *tstate);

#define __STACKLESS_PYSTATE_CLEAR \
    slp_kill_tasks_with_stacks(tstate); \
    Py_CLEAR(tstate->st.del_post_switch); \
    Py_CLEAR(tstate->st.interrupted); \
    slp_tealet_cleanup(tstate);

#ifdef WITH_THREAD

#define STACKLESS_PYSTATE_NEW \
    __STACKLESS_PYSTATE_NEW \
    tstate->st.thread.block_lock = NULL; \
    tstate->st.thread.is_blocked = 0;

#define STACKLESS_PYSTATE_CLEAR \
    __STACKLESS_PYSTATE_CLEAR \
    Py_CLEAR(tstate->st.thread.block_lock); \
    tstate->st.thread.is_blocked = 0;

#else

#define STACKLESS_PYSTATE_NEW __STACKLESS_PYSTATE_NEW
#define STACKLESS_PYSTATE_CLEAR __STACKLESS_PYSTATE_CLEAR

#endif
