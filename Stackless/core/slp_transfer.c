
#include "Python.h"

#ifdef STACKLESS

#ifndef STACKLESS
**********
If you see this error message,
your operating system is not supported yet.
Please provide an implementation of the switch_XXX.h
or disable the STACKLESS flag.
**********
#endif

#include "stackless_impl.h"

/*
 * the following macros are spliced into the OS/compiler
 * specific code, in order to simplify maintenance.
 */

#define _cstprev (_PyRuntime.st.transfer.cstprev)
#define _cst (_PyRuntime.st.transfer.cst)
#define _prev (_PyRuntime.st.transfer.prev)


#define __return(x) return (x)

#define SLP_SAVE_STATE(stackref, stsizediff) \
    intptr_t stsizeb; \
    stackref += STACK_MAGIC; \
    if (_cstprev != NULL) { \
        if (slp_cstack_new(_cstprev, (intptr_t *)stackref, _prev) == NULL) __return(-1); \
        stsizeb = slp_cstack_save(*_cstprev); \
    } \
    else \
        stsizeb = (_cst->startaddr - (intptr_t *)stackref) * sizeof(intptr_t); \
    if (_cst == NULL) __return(0); \
    stsizediff = stsizeb - (Py_SIZE(_cst) * sizeof(intptr_t));

#define SLP_RESTORE_STATE() \
    if (_cst != NULL) { \
        slp_cstack_restore(_cst); \
    }

/* This define is no longer needed now? */
#define SLP_EVAL
#include "platf/slp_platformselect.h"

#ifdef EXTERNAL_ASM
/* CCP addition: Make these functions, to be called from assembler.
 * The token include file for the given platform should enable the
 * EXTERNAL_ASM define so that this is included.
 */

/* There are two cases where slp_save_state would return 0, the
 * first where there is no difference in where the stack pointer
 * should be from where it is now.  And the second where
 * SLP_SAVE_STATE returns without restoring because we are only
 * here to save.  The assembler routine needs to differentiate
 * between these, which is why we override the returns and flag
 * the low bit of the return value on early exit.
 */
#undef __return
#define __return(x) { exitcode = x; goto exit; }

intptr_t slp_save_state(intptr_t *stack){
    intptr_t exitcode;
    intptr_t diff;
    SLP_SAVE_STATE(stack, diff);
    return diff;
exit:
    /* hack: flag a problem by setting the value to odd */
    return exitcode | 1;
}

void slp_restore_state(void){
    SLP_RESTORE_STATE();
}

extern int slp_switch(void);

#endif

/* a write only variable used to prevent overly optimisation */
intptr_t *_dont_optimise_away_goobledigoobs;
static int
climb_stack_and_transfer(PyCStackObject **cstprev, PyCStackObject *cst,
                         PyTaskletObject *prev)
{
    /*
     * there are cases where we have been initialized
     * in some deep stack recursion, but later on we
     * need to switch from a higher stacklevel, and the
     * needed stack size becomes *negative* :-))
     */
    PyThreadState *ts = PyThreadState_GET();
    intptr_t probe;
    register ptrdiff_t needed = &probe - ts->st.cstack_base;
    /* in rare cases, the need might have vanished due to the recursion */
    register intptr_t *goobledigoobs;
    if (needed > 0) {
        goobledigoobs = alloca(needed * sizeof(intptr_t));
        if (goobledigoobs == NULL)
            return -1;
        /* hinder the compiler to optimise away 
           goobledigoobs and the alloca call. 
           This happens with gcc 4.7.x and -O2 */
        _dont_optimise_away_goobledigoobs = goobledigoobs;
    }
    return slp_transfer(cstprev, cst, prev);
}

/* This function returns -1 on error, 1 if a switch occurred and 0
 * if only a stack save was performed
 */
int
slp_transfer(PyCStackObject **cstprev, PyCStackObject *cst,
             PyTaskletObject *prev)
{
    PyThreadState *ts = PyThreadState_GET();
    int result;

    /* since we change the stack we must assure that the protocol was met */
    STACKLESS_ASSERT();
    SLP_ASSERT_FRAME_IN_TRANSFER(ts);

    if ((intptr_t *) &ts > ts->st.cstack_base)
        return climb_stack_and_transfer(cstprev, cst, prev);
    if (cst == NULL || Py_SIZE(cst) == 0)
        cst = ts->st.initial_stub;
    if (cst != NULL) {
        if (cst->tstate != ts) {
            PyErr_SetString(PyExc_SystemError,
                "bad thread state in transfer");
            return -1;
        }
        if (ts->st.cstack_base != cst->startaddr) {
            PyErr_SetString(PyExc_SystemError,
                "bad stack reference in transfer");
            return -1;
        }
        /*
         * if stacks are same and refcount==1, it must be the same
         * task. In this case, we would destroy the target before
         * switching. Therefore, we simply don't switch, just save.
         */
        if (cstprev && *cstprev == cst && Py_REFCNT(cst) == 1)
            cst = NULL;
    }
    _cstprev = cstprev;
    _cst = cst;
    _prev = prev;
    result = slp_switch();
    SLP_ASSERT_FRAME_IN_TRANSFER(ts);
    if (!result) {
        if (_cst) {
            /* record the context of the target stack.  Can't do it before the switch because
             * when saving the stack, the serial number is taken from serial_last_jump
             */
            ts->st.serial_last_jump = _cst->serial;

            /* release any objects that needed to wait until after the switch.
             * Note that it is important that this does not mess with the 
             * current tasklet's "tempval".  We store it here to be
             * absolutely sure.
             */
            if (ts->st.del_post_switch) {
                PyObject *tmp;
                TASKLET_CLAIMVAL(ts->st.current, &tmp);
                Py_CLEAR(ts->st.del_post_switch);
                TASKLET_SETVAL_OWN(ts->st.current, tmp);
            }
            result = 1;
        } else
            result = 0;
    } else
        result = -1;
    return result;
}

#ifdef Py_DEBUG
int
slp_transfer_return(PyCStackObject *cst)
{
    return slp_transfer(NULL, cst, NULL);
}
#endif

#endif
