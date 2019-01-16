#include "Python.h"
#ifdef STACKLESS

#include "compile.h"
#include "frameobject.h"
#include "structmember.h"

#include "internal/stackless_impl.h"
#include "internal/slp_prickelpit.h"

/* platform specific constants */
#include "internal/slp_platformselect.h"

/* Stackless extension for ceval.c */


/******************************************************

  The C Stack

 ******************************************************/



/* this function will get called by PyStacklessEval_Fini */
static void slp_cstack_cacheclear(void)
{
    int i;
    PyCStackObject *stack;

    for (i=0; i < SLP_CSTACK_SLOTS; i++) {
        while ((_PyRuntime.st.cstack_cache)[i] != NULL) {
            stack = (_PyRuntime.st.cstack_cache)[i];
            (_PyRuntime.st.cstack_cache)[i] = (PyCStackObject *) stack->startaddr;
            PyObject_Del(stack);
        }
    }
    (_PyRuntime.st.cstack_cachecount) = 0;
}

static void
cstack_dealloc(PyCStackObject *cst)
{
    PyThreadState * ts = PyThreadState_GET();
    ts->interp->st.cstack_chain = cst;
    SLP_CHAIN_REMOVE(PyCStackObject, &ts->interp->st.cstack_chain, cst, next,
                     prev);
#ifdef Py_REF_DEBUG
    PyObject_Del(cst);
#else
    if (Py_SIZE(cst) >= SLP_CSTACK_SLOTS) {
        PyObject_Del(cst);
    }
    else {
        if ((_PyRuntime.st.cstack_cachecount) >= SLP_CSTACK_MAXCACHE)
            slp_cstack_cacheclear();
        cst->startaddr = (intptr_t *) (_PyRuntime.st.cstack_cache)[Py_SIZE(cst)];
        (_PyRuntime.st.cstack_cache)[Py_SIZE(cst)] = cst;
        ++(_PyRuntime.st.cstack_cachecount);
    }
#endif
}


PyCStackObject *
slp_cstack_new(PyCStackObject **cst, intptr_t *stackref, PyTaskletObject *task)
{
    PyThreadState *ts;
    intptr_t *stackbase;
    ptrdiff_t size;

    ts = NULL;
    if (task && task->cstate) {
        /* usually a tasklet with a cstate has a valid
           tstate, but during shutdown (function slp_kill_tasks_with_stacks)
           the tstate may be NULL. The exact conditions are complicated. */
        ts = task->cstate->tstate;
    }
    if (ts == NULL) {
        ts = PyThreadState_GET();
    }

    stackbase = ts->st.cstack_base;
    size = stackbase - stackref;

    assert(size == 0 || ts == PyThreadState_GET());
    assert(size >= 0);

    if (*cst != NULL) {
        if ((*cst)->task == task)
            (*cst)->task = NULL;
        Py_DECREF(*cst);
    }
    if (size < SLP_CSTACK_SLOTS && ((*cst) = (_PyRuntime.st.cstack_cache)[size])) {
        /* take stack from cache */
        (_PyRuntime.st.cstack_cache)[size] = (PyCStackObject *) (*cst)->startaddr;
        --(_PyRuntime.st.cstack_cachecount);
        _Py_NewReference((PyObject *)(*cst));
    }
    else
        *cst = PyObject_NewVar(PyCStackObject, &PyCStack_Type, size);
    if (*cst == NULL) return NULL;

    (*cst)->startaddr = stackbase;
    (*cst)->next = (*cst)->prev = NULL;
    SLP_CHAIN_INSERT(PyCStackObject, &ts->interp->st.cstack_chain, *cst, next, prev);
    (*cst)->serial = ts->st.serial_last_jump;
    (*cst)->task = task;
    (*cst)->tstate = ts;
    (*cst)->nesting_level = ts->st.nesting_level;
#ifdef SLP_SEH32
    //save the SEH handler
    (*cst)->exception_list = 0;
#endif
    return *cst;
}

size_t
slp_cstack_save(PyCStackObject *cstprev)
{
    size_t stsizeb = Py_SIZE(cstprev) * sizeof(intptr_t);

    memcpy((cstprev)->stack, (cstprev)->startaddr -
                             Py_SIZE(cstprev), stsizeb);
#ifdef SLP_SEH32
    //save the SEH handler
    cstprev->exception_list = (DWORD)
                __readfsdword(FIELD_OFFSET(NT_TIB, ExceptionList));
    assert(cstprev->exception_list);
#endif
    return stsizeb;
}

void
#ifdef SLP_SEH32
#pragma warning(disable:4733) /* disable warning about modifying FS[0] */
#endif
slp_cstack_restore(PyCStackObject *cst)
{
    cst->tstate->st.nesting_level = cst->nesting_level;
    /* mark task as no longer responsible for cstack instance */
    cst->task = NULL;
    memcpy(cst->startaddr - Py_SIZE(cst), &cst->stack,
           Py_SIZE(cst) * sizeof(intptr_t));
#ifdef SLP_SEH32
    //restore the SEH handler
    assert(cst->exception_list);
    __writefsdword(FIELD_OFFSET(NT_TIB, ExceptionList), (DWORD)(cst->exception_list));
    #pragma warning(default:4733)
#endif
}


static char cstack_doc[] =
"A CStack object serves to save the stack slice which is involved\n\
during a recursive Python(r) call. It will also be used for pickling\n\
of program state. This structure is highly platform dependant.\n\
Note: For inspection, str() can dump it as a string.\
";

#if SIZEOF_VOIDP == SIZEOF_INT
#define T_ADDR T_UINT
#else
#define T_ADDR T_ULONG
#endif


static PyMemberDef cstack_members[] = {
    {"size", T_PYSSIZET, offsetof(PyCStackObject, ob_base.ob_size), READONLY},
    {"next", T_OBJECT, offsetof(PyCStackObject, next), READONLY},
    {"prev", T_OBJECT, offsetof(PyCStackObject, prev), READONLY},
    {"task", T_OBJECT, offsetof(PyCStackObject, task), READONLY},
    {"startaddr", T_ADDR, offsetof(PyCStackObject, startaddr), READONLY},
    {"nesting_level", T_INT, offsetof(PyCStackObject, nesting_level), READONLY},
    {0}
};

/* simple string interface for inspection */

static PyObject *
cstack_str(PyObject *o)
{
    PyCStackObject *cst = (PyCStackObject*)o;
    return PyUnicode_Decode((char*)&cst->stack,
        Py_SIZE(cst)*sizeof(cst->stack[0]),
        "latin_1", "strict");
}

PyTypeObject PyCStack_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "_stackless.cstack",
    sizeof(PyCStackObject),
    sizeof(PyObject *),
    (destructor)cstack_dealloc,         /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    0,                                  /* tp_call */
    (reprfunc)cstack_str,               /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    PyObject_GenericSetAttr,            /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                 /* tp_flags */
    cstack_doc,                         /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    0,                                  /* tp_methods */
    cstack_members,                     /* tp_members */
};


static int
make_initial_stub(void)
{
    PyThreadState *ts = PyThreadState_GET();
    int result;

    Py_CLEAR(ts->st.initial_stub);
    ts->st.serial_last_jump = ++ts->st.serial;
    result = slp_transfer(&ts->st.initial_stub, NULL, NULL);
    if (result < 0)
        return result;
    /*
     * from here, we always arrive with a compatible cstack
     * that also can be used by main, if it is running
     * in soft-switching mode.
     * To insure that, it was necessary to re-create the
     * initial stub for *every* run of a new main.
     * This will vanish with greenlet-like stack management.
     */

    return result;
}

static PyObject *
climb_stack_and_eval_frame(PyFrameObject *f)
{
    /*
     * a similar case to climb_stack_and_transfer,
     * but here we need to incorporate a gap in the
     * stack into main and keep this gap on the stack.
     * This way, initial_stub is always valid to be
     * used to return to the main c stack.
     */
    PyThreadState *ts = PyThreadState_GET();
    intptr_t probe;
    ptrdiff_t needed = &probe - ts->st.cstack_base;
    /* in rare cases, the need might have vanished due to the recursion */
    if (needed > 0) {
        register void * stack_ptr_tmp = alloca(needed * sizeof(intptr_t));
        if (stack_ptr_tmp == NULL)
            return NULL;
        /* hinder the compiler to optimise away
        stack_ptr_tmp and the alloca call.
        This happens with gcc 4.7.x and -O2 */
        SLP_DO_NOT_OPTIMIZE_AWAY(stack_ptr_tmp);
    }
    return slp_eval_frame(f);
}

static PyObject * slp_frame_dispatch_top(PyObject *retval);

static PyObject *
slp_run_tasklet(void)
{
    /* Note: this function does not return, if a sub-function
     * (slp_frame_dispatch_top or slp_tasklet_end) calls
     * slp_transfer_return(). Therefore, this function must not hold
     * any reference during the execution of these sub-functions.
     */
    PyThreadState *ts = PyThreadState_GET();
    PyObject *retval;

    SLP_ASSERT_FRAME_IN_TRANSFER(ts);
    if (ts->st.main == NULL) {
        PyFrameObject * f = SLP_CLAIM_NEXT_FRAME(ts);
        if (slp_initialize_main_and_current()) {
            Py_XDECREF(f);
            SLP_SET_CURRENT_FRAME(ts, NULL);
            return NULL;
        }
        SLP_STORE_NEXT_FRAME(ts, f);
        Py_XDECREF(f);
    }
    SLP_ASSERT_FRAME_IN_TRANSFER(ts);

    TASKLET_CLAIMVAL(ts->st.current, &retval);

    if (PyBomb_Check(retval))
        retval = slp_bomb_explode(retval);
    while (ts->st.main != NULL) {
        /* XXX correct condition? or current? */
        retval = slp_frame_dispatch_top(retval);
        retval = slp_tasklet_end(retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(ts, retval);
        /* if we softswitched out from the tasklet end */
        Py_CLEAR(ts->st.del_post_switch);
    }
    return retval;
}

PyObject * _Py_HOT_FUNCTION
slp_eval_frame(PyFrameObject *f)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *fprev = f->f_back;
    intptr_t * stackref;
    PyObject *retval;

    if (fprev == NULL && ts->st.main == NULL) {
        /* this is the initial frame, so mark the stack base */

        /*
         * careful, this caused me a major headache.
         * it is *not* sufficient to just check for fprev == NULL.
         * Reason: (observed with wxPython):
         * A toplevel frame is run as a tasklet. When its frame
         * is deallocated (in tasklet_end), a Python(r) object
         * with a __del__ method is destroyed. This __del__
         * will run as a toplevel frame, with f_back == NULL!
         */
        int returning;

        /* complete the interpreter state */
        if (ts->interp->st.initial_tstate == NULL) {
            ts->interp->st.initial_tstate = ts;
        }
        if (ts->interp->st.mem_bomb == NULL) {
            /* allocate the permanent bomb to use for mem errors */
            if ((ts->interp->st.mem_bomb = slp_new_bomb()) == NULL)
                return NULL;
        }

        /* mark the stack base */
        stackref = SLP_STACK_REFPLUS + (intptr_t *) &f;
        if (ts->st.cstack_base == NULL)
            ts->st.cstack_base = stackref - SLP_CSTACK_GOODGAP;
        if (stackref > ts->st.cstack_base) {
			PyCStackObject *cst;
            retval = climb_stack_and_eval_frame(f);
			cst = ts->st.initial_stub;
			/* might be NULL in OOM conditions */
			if (cst != NULL) {
                PyCStackObject *other;
                register int found = 0;
                assert(cst->startaddr == ts->st.cstack_base);
                for (other = cst->next; other != cst; other = other->next) {
                    if (Py_SIZE(other) != 0 && other->task != NULL && other->tstate == ts) {
                        assert(other->startaddr == ts->st.cstack_base);
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    Py_CLEAR(ts->st.initial_stub);
                    ts->st.cstack_base = NULL;
                    ts->st.cstack_root = NULL;
                }
            }
            return retval;
        }

        assert(SLP_CURRENT_FRAME(ts) == NULL);  /* else we would change the current frame */
        SLP_STORE_NEXT_FRAME(ts, f);
        returning = make_initial_stub();
        if (returning < 0)
            return NULL;
        /* returning will be 1 if we "switched back" to this stub, and 0
         * if this is the original call that just created the stub.
         * If the stub is being reused, the argument, i.e. the frame,
         * is in ts->frame
         */
        SLP_ASSERT_FRAME_IN_TRANSFER(ts);
        if (returning != 1) {
            assert(f == SLP_PEEK_NEXT_FRAME(ts));
        }
        assert(ts->interp->st.initial_tstate != NULL);
        assert(ts->interp->st.mem_bomb != NULL);
        return slp_run_tasklet();
    }
    assert(ts->interp->st.initial_tstate != NULL);
    assert(ts->interp->st.mem_bomb != NULL); /* see above */
    Py_INCREF(Py_None);
    Py_XINCREF(fprev);
    retval = slp_frame_dispatch(f, fprev, 0, Py_None);
    Py_XDECREF(fprev);
    return retval;
}

static void
get_current_main_and_watchdogs(PyThreadState *ts, PyObject *list)
{
    PyTaskletObject *t;

    assert(ts != PyThreadState_GET());  /* don't kill ourself */
    assert(PyList_CheckExact(list));

    /* kill watchdogs */
    if (ts->st.watchdogs && PyList_CheckExact(ts->st.watchdogs)) {
        Py_ssize_t i;
        /* we don't kill the "intterupt" slot, number 0 */
        for(i = PyList_GET_SIZE(ts->st.watchdogs) - 1; i > 0; i--) {
            PyObject *item = PyList_GET_ITEM(ts->st.watchdogs, i);
            assert(item && PyTasklet_Check(item));
            Py_INCREF(item);  /* it is a borrowed ref */
            PyList_Append(list, item);
            PyErr_Clear();
            Py_DECREF(item);
        }
    }
    /* kill main */
    t = ts->st.main;
    if (t != NULL) {
        Py_INCREF(t);  /* it is a borrowed ref */
        PyList_Append(list, (PyObject *)t);
        PyErr_Clear();
        Py_DECREF(t);
    }
    /* kill current */
    t = ts->st.current;
    if (t != NULL) {
        Py_INCREF(t);  /* it is a borrowed ref */
        PyList_Append(list, (PyObject *)t);
        PyErr_Clear();
        Py_DECREF(t);
    }
}

static void
kill_pending(PyObject *list)
{
    Py_ssize_t i, len;

    assert(list && PyList_CheckExact(list));

    len = PyList_GET_SIZE(list);
    for (i=0; i < len; i++) {
        PyTaskletObject *t = (PyTaskletObject *) PyList_GET_ITEM(list, i);
        assert(PyTasklet_Check(t));
        PyTasklet_KillEx(t, 1);
        PyErr_Clear();
        assert(len == PyList_GET_SIZE(list));
    }
}

static void
run_other_threads(PyObject **sleep, Py_ssize_t count)
{
    if (count == 0) {
        /* shortcut */
        return;
    }

    assert(sleep != NULL);
    if (*sleep == NULL) {
        /* get a reference to time.sleep() */
        PyObject *mod_time;
        assert(Py_IsInitialized());
        mod_time = PyImport_ImportModule("time");
        if (mod_time != NULL) {
            *sleep = PyObject_GetAttrString(mod_time, "sleep");
            Py_DECREF(mod_time);
        }
        if (*sleep == NULL) {
            PyErr_Clear();
        }
    }
    while(count-- > 0) {
        if (*sleep == NULL) {
            Py_BEGIN_ALLOW_THREADS
            Py_END_ALLOW_THREADS
        } else {
            PyObject *res = PyObject_CallFunction(*sleep, "(f)", (float)0.001);
            if (res != NULL) {
                Py_DECREF(res);
            } else {
                PyErr_Clear();
            }
        }
    }
}

/* a thread (or threads) is exiting.  After this call, no tasklet may
 * refer to target_ts, if target_ts != NULL.
 * Also inactivate all other threads during interpreter shut down (target_ts == NULL).
 * Later in the shutdown sequence Python clears the tstate structure. This
 * causes access violations, if another thread is still active.
 */
void slp_kill_tasks_with_stacks(PyThreadState *target_ts)
{
    PyThreadState *cts = PyThreadState_GET();
    PyInterpreterState * interp = cts->interp;
    int in_loop = 0;

    assert(target_ts == NULL || interp == target_ts->interp);
    if (target_ts == NULL || target_ts == cts) {
        /* Step I of III
         * A loop to kill tasklets on the current thread.
         *
         * Plan:
         *  - loop over all cstacks
         *  - if a cstack belongs to the current thread and is the
         *    cstack of a tasklet, eventually kill the tasklet. Then remove the
         *    tstate of all cstacks, which still belong to the killed tasklet.
         */
        while (1) {
            PyCStackObject *csfirst = interp->st.cstack_chain, *cs;
            PyTaskletObject *t;

            if (csfirst == NULL)
                goto other_threads;
            for (cs = csfirst; ; cs = cs->next) {
                if (in_loop && cs == csfirst) {
                    /* nothing found */
                    goto other_threads;
                }
                in_loop = 1;
                /* has tstate already been cleared or is it a foreign thread? */
                if (cs->tstate != cts)
                    continue;

                /* here we are looking for tasks only */
                if (cs->task == NULL)
                    continue;

                /* Do not damage the initial stub */
                assert(cs != cts->st.initial_stub);

                /* is it the current cstack of the tasklet */
                if (cs->task->cstate != cs)
                    continue;

                /* Do not damage the current tasklet of the current thread.
                 * Otherwise we fail to kill other tasklets.
                 * Unfortunately cts->st.current is only valid, if
                 * cts->st.main != NULL.
                 *
                 * Why? When the main tasklet ends,  the function
                 * tasklet_end(PyObject *retval) calls slp_current_remove()
                 * for the main tasklet. This call sets tstate->st.current to
                 * the next scheduled tasklet. Then tasklet_end() cleans up
                 * the main tasklet and returns.
                 */
                if (cts->st.main != NULL && cs->task == cts->st.current) {
                    continue;
                }

                break;
            }
            t = cs->task;
            Py_INCREF(t); /* cs->task is a borrowed ref */
            assert(cs == t->cstate);

            /* Is tasklet t already dead? */
            if (t->f.frame != NULL) {
                /* If a thread ends, the thread no longer has a main tasklet and
                 * the thread is not in a valid state. tstate->st.current is
                 * undefined. It may point to a tasklet, but the other fields in
                 * tstate have wrong values.
                 *
                 * Therefore we need to ensure, that t is not tstate->st.current.
                 * Convert t into a free floating tasklet. PyTasklet_Kill works
                 * for floating tasklets too.
                 */
                if (t->next && !t->flags.blocked) {
                    assert(t->prev);
                    slp_current_remove_tasklet(t);
                    assert(Py_REFCNT(t) > 1);
                    Py_DECREF(t);
                    assert(t->next == NULL);
                    assert(t->prev == NULL);
                }
                assert(t != cs->tstate->st.current);

                /* has the tasklet nesting_level > 0? The Stackles documentation
                 * specifies: "When a thread dies, only tasklets with a C-state are actively killed.
                 * Soft-switched tasklets simply stop."
                 */
                if ((cts->st.current == cs->task ? cts->st.nesting_level : cs->nesting_level) > 0) {
                    /* Is is hard switched. */
                    PyTasklet_Kill(t);
                    PyErr_Clear();
                }
            } /* already dead? */

            /* Now remove the tstate from all cstacks of tasklet t */
            csfirst = interp->st.cstack_chain;
            if (csfirst != NULL) {
                in_loop = 0;
                for (cs = csfirst; ; cs = cs->next) {
                    if (in_loop && cs == csfirst) {
                        /* nothing found */
                        break;
                    }
                    in_loop = 1;
                    if (cs->task == t) {
                        assert(cs->tstate == cts);
                        cs->tstate = NULL;
                    }
                }
            }
            Py_DECREF(t);
            in_loop = 0;
        } /* while(1) */
    } /* if(...) */

other_threads:
    /* Step II of III
     *
     * Kill tasklets on foreign threads:.
     * Since foreign tasklets are scheduled in their own good time,
     * there is no guarantee that they are actually dead when we
     * exit this function.  Therefore, we also can't clear their thread
     * states.  That will hopefully happen when their threads exit.
     */
    {
        PyCStackObject *cs;
        PyTaskletObject *t;
        PyObject *sleepfunc = NULL;
        Py_ssize_t count;
        PyObject *tasklet_list = PyList_New(0);
        if (tasklet_list == NULL) {
            PyErr_Clear();
        }

        /* Other threads, first pass: kill (pending) current, main and watchdog tasklets
         * Iterating over the threads requires the HEAD lock. In order to prevent dead locks,
         * we try to avoid interpreter recursions (= no Py_DECREF), while we hold the lock.
         */
        if (target_ts == NULL && tasklet_list) {
            PyThreadState *ts;
            count = 0;

            /* build a list of tasklets to be killed */
            SLP_HEAD_LOCK();
            for (ts = interp->tstate_head; ts != NULL; ts = ts->next) {
                if (ts != cts) {
                    /* Inactivate thread ts. In case the thread is active,
                     * it will be killed. If the thread is sleping, it
                     * continues to sleep.
                     */
                    count++;
                    get_current_main_and_watchdogs(ts, tasklet_list);
                }
            }
            SLP_HEAD_UNLOCK();

            /* kill the tasklets */
            kill_pending(tasklet_list);

            /* We must not release the GIL while we might hold the HEAD-lock.
             * Otherwise another thread (usually the thread of the killed tasklet)
             * could try to get the HEAD lock. The result would be a wonderful dead lock.
             * If target_ts is NULL, we know for sure, that we don't hold the HEAD-lock.
             */
            run_other_threads(&sleepfunc, count);
            /* The other threads might have modified the thread state chain, but fortunately we
             * are done with it.
             */
        } else if (target_ts != cts && tasklet_list) {
            get_current_main_and_watchdogs(target_ts, tasklet_list);
            kill_pending(tasklet_list);
            /* Here it is not safe to release the GIL. */
        }
        Py_XDECREF(tasklet_list);

        /* other threads, second pass: kill tasklets with nesting-level > 0 and
         * clear tstate if target_ts != NULL && target_ts != cts. */
        if (interp->st.cstack_chain == NULL) {
            Py_XDECREF(sleepfunc);
            goto current_main;
        }

        count = 0;
        in_loop = 0;
        /* build a tuple of all tasklets to be killed:
         * 1. count the tasklets
         * 2. alloc a tuple and record them
         * 3. kill them
         * Steps 1 and 2 must not run Python code (release the GIL), because another thread could
         * modify slp_cstack_chain.
         */
        for(cs = interp->st.cstack_chain; cs != interp->st.cstack_chain || in_loop == 0; cs = cs->next) {
            /* Count tasklets to be killed.
             * This loop body must not release the GIL
             */
            assert(cs);
            assert(cs->next);
            assert(cs->next->prev == cs);
            in_loop = 1;
            t = cs->task;
            if (t == NULL)
                continue;
            if (t->cstate != cs) {
                continue;  /* not the current cstate of the tasklet */
            }
            if (cs->tstate == NULL || cs->tstate == cts) {
                continue;  /* already handled */
            }
            if (target_ts != NULL && cs->tstate != target_ts) {
                continue;  /* we are not interested in this thread */
            }
            if (((cs->tstate && cs->tstate->st.current == t) ?
                cs->tstate->st.nesting_level : cs->nesting_level) > 0) {
                /* Kill only tasklets with nesting level > 0 */
                count++;
            }
        }
        assert(cs == interp->st.cstack_chain);
        if (count > 0) {
            PyObject *tasklets = PyTuple_New(count);
            if (NULL == tasklets) {
                PyErr_Print();
                return;
            }
            assert(cs == interp->st.cstack_chain);
            for(in_loop = 0, count=0; cs != interp->st.cstack_chain || in_loop == 0; cs = cs->next) {
                /* Record tasklets to be killed.
                 * This loop body must not release the GIL.
                 */
                assert(cs);
                assert(cs->next);
                assert(cs->next->prev == cs);
                in_loop = 1;
                t = cs->task;
                if (t == NULL)
                    continue;
                if (t->cstate != cs) {
                    continue;  /* not the current cstate of the tasklet */
                }
                if (cs->tstate == NULL || cs->tstate == cts) {
                    continue;  /* already handled */
                }
                if (target_ts != NULL && cs->tstate != target_ts) {
                    continue;  /* we are not interested in this thread */
                }
                if (((cs->tstate && cs->tstate->st.current == t) ?
                    cs->tstate->st.nesting_level : cs->nesting_level) > 0) {
                    /* Kill only tasklets with nesting level > 0 */
                    Py_INCREF(t);
                    assert(count < PyTuple_GET_SIZE(tasklets));
                    PyTuple_SET_ITEM(tasklets, count, (PyObject *)t); /* steals a reference to t */
                    count++;
                }
            }
            assert(count == PyTuple_GET_SIZE(tasklets));
            for (count = 0; count < PyTuple_GET_SIZE(tasklets); count++) {
                /* Kill the tasklets.
                 */
                t = (PyTaskletObject *)PyTuple_GET_ITEM(tasklets, count);
                cs = t->cstate;
                assert(cs);
                if (cs->tstate == NULL || cs->tstate == cts) {
                    continue;  /* already handled */
                }
                if (target_ts != NULL && cs->tstate != target_ts) {
                    continue;  /* we are not interested in this thread */
                }
                Py_INCREF(cs);
                if (((cs->tstate && cs->tstate->st.current == t) ?
                    cs->tstate->st.nesting_level : cs->nesting_level) > 0) {
                    /* Kill only tasklets with nesting level > 0
                     * We must check again, because killing one tasklet
                     * can change the state of other tasklets too.
                     */
                    PyTasklet_Kill(t);
                    PyErr_Clear();
                }
                if (target_ts != NULL) {
                    cs->tstate = NULL;
                }
                Py_DECREF(cs);
            }
            Py_DECREF(tasklets);
            if (target_ts == NULL) {
                /* We must not release the GIL while we might hold the HEAD-lock.
                 * Otherwise another thread (usually the thread of the killed tasklet)
                 * could try to get the HEAD lock. The result would be a wonderful dead lock.
                 * If target_ts is NULL, we know for sure, that we don't hold the HEAD-lock.
                 */
                run_other_threads(&sleepfunc, count);
            }
        }
        Py_XDECREF(sleepfunc);
    }

current_main:
    /* Step III of III
     *
     * Finally remove the thread state from all remaining cstacks.
     * In theory only cstacks of the main tasklet and the initial stub
     * should be left.
     */
    if (target_ts == NULL || target_ts == cts) {
        PyCStackObject *cs;

        if (interp->st.cstack_chain == NULL)
            return;
        in_loop = 0;
        for (cs = interp->st.cstack_chain; cs != interp->st.cstack_chain || in_loop == 0; cs = cs->next) {
            /* This loop body must not release the GIL. */
            assert(cs);
            assert(cs->next);
            assert(cs->next->prev == cs);
            in_loop = 1;
            /* has tstate already been cleared or is it a foreign thread? */
            if (target_ts == NULL || cs->tstate == cts) {
                cs->tstate = NULL;
            }
        } /* for(...) */
    } /* if(...) */
}

void PyStackless_kill_tasks_with_stacks(int allthreads)
{
    PyThreadState *ts = PyThreadState_Get();

    if (ts->st.main == NULL) {
        if (slp_initialize_main_and_current()) {
            PyObject *s = PyUnicode_FromString("tasklet cleanup");
            PyErr_WriteUnraisable(s);
            Py_XDECREF(s);
            return;
        }
    }
    slp_kill_tasks_with_stacks(allthreads ? NULL : ts);
}


/* cstack spilling for recursive calls */

static PyObject *
eval_frame_callback(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    PyCStackObject *cst;
    PyCFrameObject *cf = (PyCFrameObject *) f;
    intptr_t *saved_base;
    Py_ssize_t tmp;
    int in_transfer;

    /* make sure we don't try softswitching out of this callstack */
    ts->st.nesting_level = cf->n + 1;

    /* this tasklet now runs in this tstate. */
    cst = cur->cstate; /* The calling cstate */
    cur->cstate = ts->st.initial_stub;
    Py_INCREF(cur->cstate);

    /* We must base our new stack from here, because otherwise we might find
     * ourselves in an infinite loop of stack spilling.
     */
    saved_base = ts->st.cstack_root;
    ts->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *) &f;

    /* pull in the right retval and tempval from the arguments */
    Py_SETREF(retval, cf->ob1);
    cf->ob1 = NULL;
    TASKLET_SETVAL_OWN(cur, cf->ob2);
    cf->ob2 = NULL;

    f = f->f_back;
    SLP_SET_CURRENT_FRAME(ts, f);
    Py_XINCREF(f);
    /* 'f' is now a counted reference to 'cf->f_back'.
     * This keeps 'cf->f_back' alive during the following call to
     * PyEval_EvalFrameEx_slp(...).
     */
    in_transfer = SLP_IS_FRAME_IN_TRANSFER_AFTER_EXPR(ts, tmp,
        retval = PyEval_EvalFrameEx_slp(f, exc, retval));
    ts->st.cstack_root = saved_base;
    if (in_transfer) {
        Py_XDECREF(f);
        f = SLP_CLAIM_NEXT_FRAME(ts);  /* returns a new reference */
    }
    else {
        assert(f == SLP_CURRENT_FRAME(ts));
    }

    /* store retval back into the cstate object */
    if (retval == NULL)
        retval = slp_curexc_to_bomb();
    if (retval == NULL) {
        Py_XDECREF(f);
        goto fatal;
    }
    cf->ob1 = retval;

    /* jump back */
    Py_SETREF(cur->cstate, cst);
    SLP_STORE_NEXT_FRAME(ts, f);
    Py_XDECREF(f);

    SLP_FRAME_EXECFUNC_DECREF(cf);
    slp_transfer_return(cst);
    /* should never come here */
    assert(0);
fatal:
    SLP_STORE_NEXT_FRAME(ts, cf->f_back);
    return NULL;
}

PyObject *
slp_eval_frame_newstack(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    PyCFrameObject *cf = NULL;
    PyCStackObject *cst;

    if (cur == NULL || PyErr_Occurred()) {
        /* Bypass this during early initialization or if we have a pending
         * exception, such as the one set via gen_close().  Doing the stack
         * magic here will clear that exception.
         */
        intptr_t *old = ts->st.cstack_root;
        ts->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *) &f;
        retval = PyEval_EvalFrameEx_slp(f, exc, retval);
        ts->st.cstack_root = old;
        return retval;
    }
    if (ts->st.cstack_root == NULL) {
        /* this is a toplevel call.  Store the root of stack spilling */
        ts->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *) &f;
        retval = PyEval_EvalFrameEx_slp(f, exc, retval);
        /* and reset it.  We may reenter stackless at a completely different
         * depth later
         */
        ts->st.cstack_root = NULL;
        return retval;
    }

    SLP_SET_CURRENT_FRAME(ts, f);
    cf = slp_cframe_new(eval_frame_callback, 1);
    if (cf == NULL)
        return NULL;
    cf->n = ts->st.nesting_level;
    cf->ob1 = retval;
    /* store the tmpval here so that it won't get clobbered
     * by slp_run_tasklet()
     */
    TASKLET_CLAIMVAL(cur, &(cf->ob2));
    SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *) cf);
    cst = cur->cstate;
    cur->cstate = NULL;
    if (slp_transfer(&cur->cstate, NULL, cur) < 0)
        goto finally; /* fatal */
    SLP_ASSERT_FRAME_IN_TRANSFER(ts);
    Py_XDECREF(cur->cstate);

    retval = cf->ob1;
    cf->ob1 = NULL;
    if (PyBomb_Check(retval))
        retval = slp_bomb_explode(retval);
finally:
    Py_DECREF(cf);
    cur->cstate = cst;
    return retval;
}



/******************************************************

  Rebirth of software stack avoidance

*******************************************************/

static PyObject *
unwind_repr(PyObject *op)
{
    return PyUnicode_FromString(
        "The invisible unwind token. If you ever should see this,\n"
        "please report the error to https://bitbucket.org/stackless-dev/stackless/issues"
    );
}

/* dummy deallocator, just in case */
static void unwind_dealloc(PyObject *op) {
    assert(0);  /*should never be called*/
}

static PyTypeObject PyUnwindToken_Type = {
    PyVarObject_HEAD_INIT(&PyUnwindToken_Type, 0)
    "UnwindToken",
    0,
    0,
    (destructor)unwind_dealloc, /*tp_dealloc*/ /*should never be called*/
    0,                  /*tp_print*/
    0,                  /*tp_getattr*/
    0,                  /*tp_setattr*/
    0,                  /*tp_compare*/
    (reprfunc)unwind_repr, /*tp_repr*/
    0,                  /*tp_as_number*/
    0,                  /*tp_as_sequence*/
    0,                  /*tp_as_mapping*/
    0,                  /*tp_hash */
};

static PyUnwindObject unwind_token = {
    PyObject_HEAD_INIT(&PyUnwindToken_Type)
};

PyUnwindObject *Py_UnwindToken = &unwind_token;

/*
    the frame dispatcher will execute frames and manage
    the frame stack until the "previous" frame reappears.
    The "Mario" code if you know that game :-)
 */

PyObject * _Py_HOT_FUNCTION
slp_frame_dispatch(PyFrameObject *f, PyFrameObject *stopframe, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *first_frame = f;
    ++ts->st.nesting_level;

/*
    frame protocol:
    If a frame returns the Py_UnwindToken object, this
    indicates that a different frame will be run.
    Semantics of an appearing Py_UnwindToken:
    The true return value is in its tempval field.
    We always use the topmost tstate frame and bail
    out when we see the frame that issued the
    originating dispatcher call (which may be a NULL frame).
 */
    while (1) {
        retval = CALL_FRAME_FUNCTION(f, exc, retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(ts, retval);
        /* A soft switch is only complete here */
        Py_CLEAR(ts->st.del_post_switch);
        if (f == first_frame) {
            first_frame = NULL;
        } else {
            Py_DECREF(f);
        }
        f = SLP_CLAIM_NEXT_FRAME(ts);
        if (f == stopframe) {
            Py_XDECREF(f);
            break;
        }
        exc = 0;
    }
    --ts->st.nesting_level;
    /* see whether we need to trigger a pending interrupt */
    /* note that an interrupt handler guarantees current to exist */
    if (ts->st.interrupt != NULL &&
        ts->st.current->flags.pending_irq)
        slp_check_pending_irq();
    return retval;
}

static PyObject *
slp_frame_dispatch_top(PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *f = SLP_CLAIM_NEXT_FRAME(ts);

    if (f==NULL) return retval;

    while (1) {
        retval = CALL_FRAME_FUNCTION(f, 0, retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(ts, retval);
        Py_SETREF(f, SLP_CLAIM_NEXT_FRAME(ts));
        /* A soft switch is only complete here */
        Py_CLEAR(ts->st.del_post_switch);
        if (f == NULL)
            break;
    }
    return retval;
}

/* Clear out the free list */

void
slp_stacklesseval_fini(void)
{
    slp_cstack_cacheclear();
}

#endif /* STACKLESS */
