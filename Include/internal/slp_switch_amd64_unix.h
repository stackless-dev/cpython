/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 24-Oct-11 Anselm Kruis <a.kruis at science-computing.de>
 *      Reworked based on the AMD64 ABI spec.
 * 26-Jul-10 Jeff Senn <senn at maya.com>
 *      Got this to work (rather than crash consistently)
 *      on OS-X 10.6 by adding more registers to save set.  
 *      Not sure what is the minimal set of regs, nor if 
 *      this is completely stable and works for all compiler 
 *      variations.
 * 01-Apr-04  Hye-Shik Chang    <perky at FreeBSD.org>
 *      Ported from i386 to amd64.
 * 24-Nov-02  Christian Tismer  <tismer at tismer.com>
 *      needed to add another magic constant to insure
 *      that f in slp_eval_frame(PyFrameObject *f)
 *      SLP_STACK_REFPLUS will probably be 1 in most cases.
 *      gets included into the saved stack area.
 * 17-Sep-02  Christian Tismer  <tismer at tismer.com>
 *      after virtualizing stack save/restore, the
 *      stack size shrunk a bit. Needed to introduce
 *      an adjustment SLP_STACK_MAGIC per platform.
 * 15-Sep-02  Gerd Woetzel       <gerd.woetzel at GMD.DE>
 *      slightly changed framework for spark
 * 31-Avr-02  Armin Rigo         <arigo at ulb.ac.be>
 *      Added ebx, esi and edi register-saves.
 * 01-Mar-02  Samual M. Rushing  <rushing at ironport.com>
 *      Ported from i386.
 */

#define SLP_USE_NATIVE_BITFIELD_LAYOUT 1

#define SLP_STACK_REFPLUS 1

#ifdef SLP_EVAL

/* #define SLP_STACK_MAGIC 3 */
/* the above works fine with gcc 2.96, but 2.95.3 wants this */
#define SLP_STACK_MAGIC 0


/* 
 * In order to switch the stack, we use the fact, that the compiler 
 * already knows how to preserve registers across function calls.
 *
 * The relevant AMD64 ABI specification precisely defines which registers
 * must be preserved and which registers may be modified. 
 * We use a gcc inline assembly feature to pretend that the inline 
 * assembly block modifies the registers to be preserved. As a result, 
 * the compiler emits code to preserve those registers.
 * 
 * The AMD64 ABI Draft 0.99.5, Figure 3.4 "Register usage"
 * defines the following registers as "Preserved across 
 * function calls":
 * %rbx         callee-saved register; optionally used as base pointer
 * %rsp         stack pointer
 * %rbp         callee-saved register; optional used as frame pointer
 * %r12 - %r15  callee-saved registers
 * %mxcsr       SSE2 control and status word
 * %fcw         x87 control word
 * 
 * The compiler always preserves the %rsp register across a function call.
 *
 * Depending on the usage of a frame pointer, which is optional
 * for the AMD64 ABI, the compiler already preserves the %rbp 
 * register. Unfortunately, we must not add "rbp" to the clobber list, if 
 * rbp is used as a frame pointer (won't compile). Therefore we save
 * rbp manually.
 *
 * For the other registers (except %mxcsr and %fcw) we tell the compiler, 
 * that we are going to clobber the registers. The compiler will then save the registers 
 * for us. (However the compiler gives no guarantee, when it will restore
 * the registers.) And the compiler only preserves those registers, that must
 * be preserved according to the calling convention. It does not preserve any other 
 * register, that may be modified during a function call. Therefore specifying additional 
 * registers has no effect at all. Take a look at the generated assembly code!
 * 
 * If we want more control, we need to preserve the
 * registers explicitly similar to  %mxcsr, %fcw and %rbp.
 * 
 */

#if 1
/* Registers marked as clobbered, minimum set according to the ABI spec. */
#define REGS_CLOBBERED "rbx", "r12", "r13", "r14", "r15"
#else
/* Maximum possible clobber list. It gives the same assembly code as the minimum list.
   If the ABI evolves, it might be necessary to add some of these registers */
#define REGS_CLOBBERED "memory", "rax", "rbx", "rcx", "rdx", "rsi", "rdi", \
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",                       \
    "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", \
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", \
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
#endif

static int
slp_switch(void)
{
    register long *stackref, stsizediff;
    int mxcsr; short x87cw;
#if STACKLESS_FRHACK
    __asm__ volatile (
        "fstcw %0\n\t"
        "stmxcsr %1\n\t"
        : "=m" (x87cw), "=m" (mxcsr) : : REGS_CLOBBERED );
#else
    void * rbp;
    __asm__ volatile (
        "fstcw %0\n\t"
        "stmxcsr %1\n\t"
        "movq %%rbp, %2\n\t"
        : "=m" (x87cw), "=m" (mxcsr), "=m" (rbp) : : REGS_CLOBBERED );
#endif
    __asm__ ("movq %%rsp, %0" : "=g" (stackref));
    {
        SLP_SAVE_STATE(stackref, stsizediff);
        __asm__ volatile (
            "addq %0, %%rsp\n\t"
            "addq %0, %%rbp\n\t"
            :
            : "r" (stsizediff)
            );
        SLP_RESTORE_STATE();
#if STACKLESS_FRHACK
        __asm__ volatile (
            "ldmxcsr %1\n\t"
            "fldcw %0\n\t"
            : : "m" (x87cw), "m" (mxcsr));
#else
        __asm__ volatile (
            "movq %2, %%rbp\n\t"
            "ldmxcsr %1\n\t"
            "fldcw %0\n\t"
            : : "m" (x87cw), "m" (mxcsr), "m" (rbp));
#endif
        return 0;
    }
}

#undef REGS_CLOBBERED
#endif
/*
 * further self-processing support
 */

/* 
 * if you want to add self-inspection tools, place them
 * here. See the x86_msvc for the necessary defines.
 * These features are highly experimental und not
 * essential yet.
 */
