#include "Python.h"
#ifdef STACKLESS

#include "compile.h"
#include "frameobject.h"
#include "structmember.h"

#include "stackless_impl.h"
#include "pickling/prickelpit.h"

/* platform specific constants */
#include "platf/slp_platformselect.h"

#include "stackless_tealet.h"

/* Stackless extension for ceval.c */

/******************************************************

  Static Global Variables

*******************************************************/

/* the flag which decides whether we try to use soft switching */

int slp_enable_softswitch = 1;

/* compatibility mask for Psyco. It will be set to nonzero when
 * psyco-compiled code is run. Suppresses soft-switching.
 */
int slp_in_psyco = 0;

/*
 * flag whether the next call should try to be stackless.
 * The protocol is: This flag may be only set if the called
 * thing supports it. It doesn't matter whether it uses the
 * chance, but it *must* set it to zero before returning.
 * This flags in a way serves as a parameter that we don't have.
 */
int slp_try_stackless = 0;

/* the list of all tasklets of all threads */
PyTaskletObject *slp_tasklet_chain = NULL;


/******************************************************

  The C Stack

 ******************************************************/


/* this function will get called by PyStacklessEval_Fini */





PyObject *
slp_eval_frame(PyFrameObject *f)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *fprev = f->f_back;

    if (fprev == NULL && ts->st.main == NULL) {
        /* this is the initial frame.  From here we must
         * run the evaluation loop on a separate tealet, so that
         * all such tealets are equivalent and can jump back to main
         * when they exit.
         */
        PyObject *result;
        if (!ts->st.initial_stub) {
            if (slp_make_initial_stub(ts))
                return NULL;
        }
        ts->frame = f;
        result = slp_run_stub_from_main(ts);
        /* TODO: We could leave the stub around in the tstate and save
         * ourselves the effort of recreating it all the time
         */
        slp_destroy_initial_stub(ts);
        return result;
    }

    Py_INCREF(Py_None);
    return slp_frame_dispatch(f, fprev, 0, Py_None);
}

void slp_kill_tasks_with_stacks(PyThreadState *target_ts)
{
    PyThreadState *ts = PyThreadState_GET();
    int count = 0;

    /* a loop to kill tasklets on the local thread */
    while (1) {
        PyTealet_data *tdfirst = slp_tealet_list, *td;
        PyTaskletObject *t, *task;
        PyTaskletObject **chain;

        if (tdfirst == NULL)
            break;
        for (td = tdfirst; ; td = td->next) {
            if (count && td == tdfirst) {
                /* nothing found */
                return;
            }
            ++count;
            if (td->tasklet == NULL)
                continue;
            /* can't kill tasklets from other threads here */
            if (td->tasklet->tstate != ts)
                continue;
            /* Not killable, another thread's frameless main? */
            if (slp_get_frame(td->tasklet) == NULL)
                continue;
            break;
        }
        count = 0;
        t = td->tasklet;
        Py_INCREF(t); /* cs->task is a borrowed ref */

        /* We need to ensure that the tasklet 't' is in the scheduler
         * tasklet chain before this one (our main).  This ensures
         * that this one is directly switched back to after 't' is
         * killed.  The reason we do this this is because if another
         * tasklet is switched to, this is of course it being scheduled
         * and run.  Why we do not need to do this for tasklets blocked
         * on channels is that when they are scheduled to be run and
         * killed, they will be implicitly placed before this one,
         * leaving it to run next.
         */
        if (!t->flags.blocked && t != t->tstate->st.current) {
            PyTaskletObject *tmp;
            /* unlink from runnable queue if it wasn't previously remove()'d */
            if (t->next && t->prev) {
                task = t;
                chain = &task;
                SLP_CHAIN_REMOVE(PyTaskletObject, chain, task, next, prev);
            } else
                Py_INCREF(t); /* a new reference for the runnable queue */
            /* insert into the 'current' chain without modifying 'current' */
            tmp = t->tstate->st.current;
            chain = &tmp;
            task = t;
            SLP_CHAIN_INSERT(PyTaskletObject, chain, task, next, prev);
        }

        PyTasklet_Kill(t);
        PyErr_Clear();

        Py_DECREF(t);
    }

    /* and a separate simple loop to kill tasklets on foreign threads.
     * Since foreign tasklets are scheduled in their own good time,
     * there is no guarantee that they are actually dead when we
     * exit this function
     */
    {
        PyTealet_data *tdfirst = slp_tealet_list, *td;
        PyTaskletObject *t;
        
        if (tdfirst == NULL)
            return;
        count = 0;
        for (td = tdfirst; ; td = td->next) {
            if (count && td == tdfirst) {
                return;
            }
            if (td->tasklet == NULL)
                continue;
            t = td->tasklet;
            if (t->tstate == ts)
                continue; /* ignore this thread's tasklets */
            if (target_ts && t->tstate != target_ts)
                continue; /* want a specific thread */
            count++;
            Py_INCREF(t); /* cs->task is a borrowed ref */
            PyTasklet_Kill(t);
            PyErr_Clear();
            Py_DECREF(t);
        }
    }
}

void PyStackless_kill_tasks_with_stacks(int allthreads)
{
    PyThreadState *ts = PyThreadState_Get();

    if (ts->st.main == NULL) {
        /* TODO: Must destroy main and current afterwards, if required.
         * also, only do this is there is any work to be done.
         */
        if (initialize_main_and_current()) {
            PyObject *s = PyString_FromString("tasklet cleanup");
            PyErr_WriteUnraisable(s);
            Py_XDECREF(s);
            return;
        }
    }
    slp_kill_tasks_with_stacks(allthreads ? NULL : ts);
}


/* cstack spilling for recursive calls */
#if 0
/* re-enable stack spilling separately */

static PyObject *
eval_frame_callback(PyFrameObject *f, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyTaskletObject *cur = ts->st.current;
    PyCStackObject *cst;
    PyCFrameObject *cf = (PyCFrameObject *) f;
    intptr_t *saved_base;

    //make sure we don't try softswitching out of this callstack
    ts->st.nesting_level = cf->n + 1;
    ts->frame = f->f_back;

    //this tasklet now runs in this tstate.
    cst = cur->cstate; //The calling cstate
    cur->cstate = ts->st.initial_stub;
    Py_INCREF(cur->cstate);

    /* We must base our new stack from here, because otherwise we might find
     * ourselves in an infinite loop of stack spilling.
     */
    saved_base = ts->st.cstack_root;
    ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &f;

    /* pull in the right retval and tempval from the arguments */
    Py_DECREF(retval);
    retval = cf->ob1;
    cf->ob1 = NULL;
    TASKLET_SETVAL_OWN(cur, cf->ob2);
    cf->ob2 = NULL;

    retval = PyEval_EvalFrameEx_slp(ts->frame, exc, retval);
    ts->st.cstack_root = saved_base;

    /* store retval back into the cstate object */
    if (retval == NULL)
        retval = slp_curexc_to_bomb();
    if (retval == NULL)
        goto fatal;
    cf->ob1 = retval;

    /* jump back */
    Py_DECREF(cur->cstate);
    cur->cstate = cst;
    slp_transfer_return(cst);
    /* should never come here */
fatal:
    Py_DECREF(cf); /* since the caller won't do it */
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
        ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &f;
        retval = PyEval_EvalFrameEx_slp(f,exc, retval);
        ts->st.cstack_root = old;
        return retval;
    }
    if (ts->st.cstack_root == NULL) {
        /* this is a toplevel call.  Store the root of stack spilling */
        ts->st.cstack_root = STACK_REFPLUS + (intptr_t *) &f;
        retval = PyEval_EvalFrameEx_slp(f, exc, retval);
        /* and reset it.  We may reenter stackless at a completely different
         * depth later
         */
        return retval;
    }

    ts->frame = f;
    cf = slp_cframe_new(eval_frame_callback, 1);
    if (cf == NULL)
        return NULL;
    cf->n = ts->st.nesting_level;
    cf->ob1 = retval;
    /* store the tmpval here so that it won't get clobbered
     * by slp_run_tasklet()
     */
    TASKLET_CLAIMVAL(cur, &(cf->ob2));
    ts->frame = (PyFrameObject *) cf;
    cst = cur->cstate;
    cur->cstate = NULL;
    if (slp_transfer(&cur->cstate, NULL, cur) < 0)
        goto finally; /* fatal */
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
#endif

/******************************************************

  Generator re-implementation for Stackless

*******************************************************/

typedef struct {
    PyObject_HEAD
    /* The gi_ prefix is intended to remind of generator-iterator. */

    PyFrameObject *gi_frame;

    /* True if generator is being executed. */
    int gi_running;

    /* List of weak reference. */
    PyObject *gi_weakreflist;
} genobject;

/*
 * Note:
 * Generators are quite a bit slower in Stackless, because
 * we are jumping in and out so much.
 * I had an implementation with no extra cframe, but it
 * was not faster, but considerably slower than this solution.
 */

PyObject* gen_iternext_callback(PyFrameObject *f, int exc, PyObject *retval);

PyObject *
slp_gen_send_ex(PyGenObject *ob, PyObject *arg, int exc)
{
    STACKLESS_GETARG();
    genobject *gen = (genobject *) ob;
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *f = gen->gi_frame;
    PyFrameObject *stopframe = ts->frame;
    PyObject *retval;

    if (gen->gi_running) {
        PyErr_SetString(PyExc_ValueError,
                        "generator already executing");
        return NULL;
    }
    if (f==NULL || f->f_stacktop == NULL) {
        /* Only set exception if called from send() */
        if (arg && !exc)
            PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    if (f->f_back == NULL &&
        (f->f_back = (PyFrameObject *)
                 slp_cframe_new(gen_iternext_callback, 0)) == NULL)
        return NULL;

    if (f->f_lasti == -1) {
        if (arg && arg != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                            "can't send non-None value to a "
                            "just-started generator");
            return NULL;
        }
    } else {
        /* Push arg onto the frame's value stack */
        retval = arg ? arg : Py_None;
        Py_INCREF(retval);
        *(f->f_stacktop++) = retval;
    }

    /* XXX give the patch to python-dev */
    f->f_tstate = ts;
    /* Generators always return to their most recent caller, not
     * necessarily their creator. */
    Py_XINCREF(ts->frame);
    assert(f->f_back != NULL);
    assert(f->f_back->f_back == NULL);
    f->f_back->f_back = ts->frame;

    gen->gi_running = 1;

    f->f_execute = PyEval_EvalFrameEx_slp;

    /* make refcount compatible to frames for tasklet unpickling */
    Py_INCREF(f->f_back);

    Py_INCREF(gen);
    Py_XINCREF(arg);
    ((PyCFrameObject *) f->f_back)->ob1 = (PyObject *) gen;
    ((PyCFrameObject *) f->f_back)->ob2 = arg;
    Py_INCREF(f);
    ts->frame = f;

    retval = Py_None;
    Py_INCREF(retval);

    if (stackless)
        return STACKLESS_PACK(retval);
    return slp_frame_dispatch(f, stopframe, exc, retval);
}

PyObject*
gen_iternext_callback(PyFrameObject *f, int exc, PyObject *result)
{
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *cf = (PyCFrameObject *) f;
    genobject *gen = (genobject *) cf->ob1;
    PyObject *arg = cf->ob2;

    gen->gi_running = 0;
    /* make refcount compatible to frames for tasklet unpickling */
    Py_DECREF(f);

    /* Don't keep the reference to f_back any longer than necessary.  It
     * may keep a chain of frames alive or it could create a reference
     * cycle. */
    ts->frame = f->f_back;
    Py_XDECREF(f->f_back);
    f->f_back = NULL;

    f = gen->gi_frame;
    /* If the generator just returned (as opposed to yielding), signal
     * that the generator is exhausted. */
    if (result == Py_None && f->f_stacktop == NULL) {
        Py_DECREF(result);
        result = NULL;
        /* Set exception if not called by gen_iternext() */
        if (arg)
            PyErr_SetNone(PyExc_StopIteration);
        /* Stackless extra handling */
        /* are we awaited by a for_iter or called by next() ? */
        else if (ts->frame->f_execute != PyEval_EvalFrame_iter) {
            /* do the missing part of the next call */
            if (!PyErr_Occurred())
                PyErr_SetNone(PyExc_StopIteration);
        }
    }

    /* We hold references to things in the cframe, if we release it
       before we clear the references, they get incorrectly and
       prematurely free. */
    cf->ob1 = NULL;
    cf->ob2 = NULL;

    if (!result || f->f_stacktop == NULL) {
        /* generator can't be rerun, so release the frame */
        Py_DECREF(f);
        gen->gi_frame = NULL;
    }

    Py_DECREF(gen);
    Py_XDECREF(arg);
    return result;
}


/******************************************************

  Rebirth of software stack avoidance

*******************************************************/

static PyObject *
unwind_repr(PyObject *op)
{
    return PyString_FromString(
        "The invisible unwind token. If you ever should see this,\n"
        "please report the error to tismer@tismer.com"
    );
}

/* dummy deallocator, just in case */
static void unwind_dealloc(PyObject *op) {
}

static PyTypeObject PyUnwindToken_Type = {
    PyObject_HEAD_INIT(&PyUnwindToken_Type)
    0,
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
    NULL
};

PyUnwindObject *Py_UnwindToken = &unwind_token;

/*
    the frame dispatcher will execute frames and manage
    the frame stack until the "previous" frame reappears.
    The "Mario" code if you know that game :-)
 */

PyObject *
slp_frame_dispatch(PyFrameObject *f, PyFrameObject *stopframe, int exc, PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();

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
        retval = f->f_execute(f, exc, retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(retval);
        /* A soft switch is only complete here */
        Py_CLEAR(ts->st.del_post_switch);
        f = ts->frame;
        if (f == stopframe)
            break;
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

PyObject *
slp_frame_dispatch_top(PyObject *retval)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *f = ts->frame;

    if (f==NULL) return retval;

    while (1) {

        retval = f->f_execute(f, 0, retval);
        if (STACKLESS_UNWINDING(retval))
            STACKLESS_UNPACK(retval);
        /* A soft switch is only complete here */
        Py_CLEAR(ts->st.del_post_switch);
        f = ts->frame;
        if (f == NULL)
            break;
    }
    return retval;
}

/* Clear out the free list */

void
slp_stacklesseval_fini(void)
{
}

#endif /* STACKLESS */
