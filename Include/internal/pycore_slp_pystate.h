/*
 * Copyright 2018 Anselm Kruis <anselm.kruis@atos.net>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/* This include file is included from pycore_pystate.h only */

/* how many cstacks to cache at all */
#ifndef SLP_CSTACK_MAXCACHE
#define SLP_CSTACK_MAXCACHE     100
#endif

/* adjust slots to typical size of a few recursions on your system */
#ifndef SLP_CSTACK_SLOTS
#define SLP_CSTACK_SLOTS        1024
#endif

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

#define SLP_INITIAL_TSTATE(tstate) \
    (assert(tstate), \
     assert((tstate)->interp->st.initial_tstate), \
     (tstate)->interp->st.initial_tstate)

#define SPL_INTERPRETERSTATE_NEW(interp)       \
    (interp)->st.enable_softswitch = 1;

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

/*
 * Stackless runtime state
 *
 * Initialized by
 * void slp_initialize(struct _stackless_runtime_state * state)
 * in stackless_util.c
 */
struct _stackless_runtime_state {
    /*
     * flag whether the next call should try to be stackless.
     * The protocol is: This flag may be only set if the called
     * thing supports it. It doesn't matter whether it uses the
     * chance, but it *must* set it to zero before returning.
     * This flags in a way serves as a parameter that we don't have.
     *
     * As long as the GIL is shared between sub-interpreters,
     * try_stackless can be a field in the runtime state.
     */
    int try_stackless;

    /* Used to manage free C-stack objects, see stacklesseval.c */
    int cstack_cachecount;
    struct _cstack *cstack_cache[SLP_CSTACK_SLOTS];

    /*
     * Used during a hard switch.
     */
    struct {
        struct _cstack **cstprev;
        struct _cstack *cst;
        struct _tasklet *prev;
    } transfer;
};

#ifdef Py_BUILD_CORE
void slp_initialize(struct _stackless_runtime_state *);
#endif /* #ifdef Py_BUILD_CORE */
