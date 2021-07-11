#include "Python.h"
#include <stddef.h> /* For ptrdiff_t */

#ifdef STACKLESS

#include "pycore_stackless.h"

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
    stackref += SLP_STACK_MAGIC; \
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

/*
 * Platform Selection for Stackless
 */
#define SLP_EVAL  /* enable code generation in the included header */

#ifndef SLP_NO_STACKMAN  /* defined by configure --without-stackman */
/* First, see if stackman an implementation without external
 * assembler, use that if possible
 */
#define STACKMAN_OPTIONAL
#include "stackman/stackman.h"
#endif  /* #ifndef SLP_NO_STACKMAN */
#if defined(STACKMAN_PLATFORM) && !defined(STACKMAN_EXTERNAL_ASM)
#include "switch_stackman.h"
#else  /* use traditional stackless switching */
#if   defined(MS_WIN32) && !defined(MS_WIN64) && defined(_M_IX86)
#include "switch_x86_msvc.h" /* MS Visual Studio on X86 */
#elif defined(MS_WIN64) && defined(_M_X64)
#include "switch_x64_msvc.h" /* MS Visual Studio on X64 */
#elif defined(__GNUC__) && defined(__i386__)
#include "switch_x86_unix.h" /* gcc on X86 */
#elif defined(__GNUC__) && defined(__amd64__)
#include "switch_amd64_unix.h" /* gcc on amd64 */
#elif defined(__GNUC__) && defined(__PPC__) && defined(__linux__)
#include "switch_ppc_unix.h" /* gcc on PowerPC */
#elif defined(__GNUC__) && defined(__ppc__) && defined(__APPLE__)
#include "switch_ppc_macosx.h" /* Apple MacOS X on PowerPC */
#elif defined(__GNUC__) && defined(sparc) && defined(sun)
#include "switch_sparc_sun_gcc.h" /* SunOS sparc with gcc */
#elif defined(__GNUC__) && defined(__s390__) && defined(__linux__)
#include "switch_s390_unix.h"   /* Linux/S390 */
#elif defined(__GNUC__) && defined(__s390x__) && defined(__linux__)
#include "switch_s390_unix.h"   /* Linux/S390 zSeries (identical) */
#elif defined(__GNUC__) && defined(__arm__) && defined(__thumb__)
#include "switch_arm_thumb_gcc.h" /* gcc using arm thumb */
#elif defined(__GNUC__) && defined(__arm32__)
#include "switch_arm32_gcc.h" /* gcc using arm32 */
#elif defined(__GNUC__) && defined(__mips__) && defined(__linux__)
#include "switch_mips_unix.h" /* MIPS */
#elif defined(SN_TARGET_PS3)
#include "switch_ps3_SNTools.h" /* Sony PS3 */
#endif
#ifndef STACKLESS
**********
If you see this error message,
your operating system is not supported yet.
Please provide an implementation of the switch_XXX.h
or disable the STACKLESS flag.
**********
#endif
#endif  /* use traditional stackless switching */

/* default definitions if not defined in above files */


/* a good estimate how much the cstack level differs between
   initialisation and main C-Python(r) code. Not critical, but saves time.
   Note that this will vanish with the greenlet approach. */

#ifndef SLP_CSTACK_GOODGAP
#define SLP_CSTACK_GOODGAP      4096
#endif

/* stack size in pointer to trigger stack spilling */

#ifndef SLP_CSTACK_WATERMARK
#define SLP_CSTACK_WATERMARK 16384
#endif

/* define direction of stack growth */

#ifndef SLP_CSTACK_DOWNWARDS
#define SLP_CSTACK_DOWNWARDS 1   /* 0 for upwards */
#endif


/*
 * Call SLP_DO_NOT_OPTIMIZE_AWAY(pointer) to ensure that pointer will be
 * computed even post-optimization.  Use it for pointers that are computed but
 * otherwise are useless. The compiler tends to do a good job at eliminating
 * unused variables, and this macro fools it into thinking var is in fact
 * needed.
 *
 * A platform specific include may provide its own definition of
 * SLP_DO_NOT_OPTIMIZE_AWAY and SLP_DO_NOT_OPTIMIZE_AWAY_DEFINITIONS.
 */
#ifndef SLP_DO_NOT_OPTIMIZE_AWAY

/* Code is based on Facebook folly
 * https://github.com/facebook/folly/blob/master/folly/Benchmark.h,
 * which has an Apache 2 license.
 */
#ifdef _MSC_VER

#pragma optimize("", off)

static inline void doNotOptimizeDependencySink(const void* p) {}

#pragma optimize("", on)

#define SLP_DO_NOT_OPTIMIZE_AWAY(pointer) doNotOptimizeDependencySink(pointer)
#define SLP_DO_NOT_OPTIMIZE_AWAY_DEFINITIONS /* empty */

#elif (defined(__GNUC__) || defined(__clang__))
/*
 * The "r" constraint forces the compiler to make datum available
 * in a register to the asm block, which means that it must have
 * computed/loaded it.
 */
#define SLP_DO_NOT_OPTIMIZE_AWAY(pointer) \
    do {__asm__ volatile("" ::"r"(pointer));} while(0)
#define SLP_DO_NOT_OPTIMIZE_AWAY_DEFINITIONS /* empty */
#else
/*
 * Unknown compiler
 */
#define SLP_DO_NOT_OPTIMIZE_AWAY(pointer) \
    do { slp_do_not_opimize_away_sink = ((void*)(pointer)); } while(0)
extern uint8_t* volatile slp_do_not_opimize_away_sink;
#define SLP_DO_NOT_OPTIMIZE_AWAY_DEFINITIONS uint8_t* volatile slp_do_not_opimize_away_sink;
#endif
#endif  /* #ifndef SLP_DO_NOT_OPTIMIZE_AWAY */

/**************************************************************

  Don't change definitions below, please.

 **************************************************************/

#if SLP_CSTACK_DOWNWARDS == 1
#define SLP_CSTACK_COMPARE(a, b) (a) < (b)
#define SLP_CSTACK_SUBTRACT(a, b) (a) - (b)
#else
#define SLP_CSTACK_COMPARE(a, b) (a) > (b)
#define SLP_CSTACK_SUBTRACT(a, b) (b) - (a)
#endif

/**************************************************************
 * End of definitions
 ***************************************************************/

SLP_DO_NOT_OPTIMIZE_AWAY_DEFINITIONS

#ifdef SLP_EXTERNAL_ASM
/* CCP addition: Make these functions, to be called from assembler.
 * The token include file for the given platform should enable the
 * SLP_EXTERNAL_ASM define so that this is included.
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
    PyThreadState *ts = _PyThreadState_GET();
    intptr_t probe;
    register ptrdiff_t needed = &probe - ts->st.cstack_base;
    /* in rare cases, the need might have vanished due to the recursion */
    if (needed > 0) {
        register void * stack_ptr_tmp = alloca(needed * sizeof(intptr_t));
        if (stack_ptr_tmp == NULL)
            return -1;
        /* hinder the compiler to optimise away
           stack_ptr_tmp and the alloca call.
           This happens with gcc 4.7.x and -O2 */
        SLP_DO_NOT_OPTIMIZE_AWAY(stack_ptr_tmp);
    }
    return slp_transfer(cstprev, cst, prev);
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
    PyThreadState *ts = _PyThreadState_GET();
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

/* This function returns -1 on error, 1 if a switch occurred and 0
 * if only a stack save was performed
 */
int
slp_transfer(PyCStackObject **cstprev, PyCStackObject *cst,
             PyTaskletObject *prev)
{
    PyThreadState *ts = _PyThreadState_GET();
    int result;
    /* Use a volatile pointer to prevent inlining of slp_switch().
     * See Stackless issue 183
     */
    static int (*volatile slp_switch_ptr)(void) = slp_switch;

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
    result = slp_switch_ptr();
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

int
slp_cstack_save_now(const PyThreadState *tstate, const void * pstackvar)
{
    assert(tstate);
    assert(pstackvar);
    if (tstate->st.cstack_root == NULL)
        return 1;
    return SLP_CSTACK_SUBTRACT(tstate->st.cstack_root, (const intptr_t*)pstackvar) > SLP_CSTACK_WATERMARK;
}

void
slp_cstack_set_root(PyThreadState *tstate, const void * pstackvar) {
    assert(tstate);
    assert(pstackvar);
    tstate->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *)pstackvar;
}

PyObject *
slp_cstack_set_base_and_goodgap(PyThreadState *tstate, const void * pstackvar, PyFrameObject *f) {
    intptr_t * stackref;
    assert(tstate);
    assert(pstackvar);
    tstate->st.cstack_root = SLP_STACK_REFPLUS + (intptr_t *)pstackvar;
    stackref = SLP_STACK_REFPLUS + (intptr_t *)pstackvar;
    if (tstate->st.cstack_base == NULL)
        tstate->st.cstack_base = stackref - SLP_CSTACK_GOODGAP;
    if (stackref > tstate->st.cstack_base)
        return climb_stack_and_eval_frame(f);  /* recursively calls slp_eval_frame(f) */
    return (void *)1;
}

#endif
