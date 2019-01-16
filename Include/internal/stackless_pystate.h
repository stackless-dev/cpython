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

/* This include file is included from internal/pystate.h only */

#include "internal/slp_platformselect.h"  /* for SLP_CSTACK_SLOTS */

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
