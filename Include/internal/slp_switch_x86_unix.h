/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 06-Nov-16  Anselm Kruis <a.kruis@atos.net>
 *      Reworked based on the i386 ABI spec.
 * 24-Nov-02  Christian Tismer  <tismer@tismer.com>
 *      needed to add another magic constant to insure
 *      that f in slp_eval_frame(PyFrameObject *f)
 *      gets included into the saved stack area.
 *      SLP_STACK_REFPLUS will probably be 1 in most cases.
 * 17-Sep-02  Christian Tismer  <tismer@tismer.com>
 *      after virtualizing stack save/restore, the
 *      stack size shrunk a bit. Needed to introduce
 *      an adjustment SLP_STACK_MAGIC per platform.
 * 15-Sep-02  Gerd Woetzel       <gerd.woetzel@GMD.DE>
 *      slightly changed framework for spark
 * 31-Avr-02  Armin Rigo         <arigo@ulb.ac.be>
 *      Added ebx, esi and edi register-saves.
 * 01-Mar-02  Samual M. Rushing  <rushing@ironport.com>
 *      Ported from i386.
 */

#define SLP_USE_NATIVE_BITFIELD_LAYOUT 1

#define SLP_STACK_REFPLUS 1

#ifdef SLP_EVAL

#define SLP_STACK_MAGIC 0

/*
 * In order to switch the stack, we use the fact, that the compiler
 * already knows how to preserve registers accross function calls.
 *
 * The relevant i386 ABI specifigation pecisely defines which registers
 * must be preserved and which registers may be modified.
 * We use a gcc inline assembly feature to pretend that the inline
 * assembly block modifies the registers to be preserved. As a result,
 * the compiler emits code to preserve those registers.
 *
 * The "System V Application Binary Interface Intel386 Architecture Processor Supplment, Fourth Edition"
 * Section 3 chapter "Function Calling Sequence" states:
 *   All registers on the Intel386 are global and thus visible to both a calling and a
 *   called function. Registers %ebp, %ebx, %edi, %esi, and %esp "belong" to the cal-
 *   ling function. In other words, a called function must preserve these registers’
 *   values for its caller. Remaining registers ‘‘belong’’ to the called function.
 *
 * The compiler always preserves the %esp register accros a function call.
 *
 * Depending on the usage of a frame pointer, which is optional
 * for the i386 ABI, the compiler already preserves the %ebp
 * register. Unfortunately, we must not add "ebp" to the clobber list, if
 * ebp is used as a frame pointer (won't compile). Therefore we save
 * ebp manually.
 *
 * For the other registers we tell the compiler,
 * that we are going to clobber the registers. The compiler will then save the registers
 * for us. (However the compiler gives no guarantee, when it will restore
 * the registers.) And the compiler only preserves those registers, that must
 * be preserved according to the calling convention. It does not preserve any other
 * register, that may be modified during a function call. Therefore specifying additional
 * registers has no effect at all. Take a look at the generated assembly code!
 */

/* Registers marked as clobbered, minimum set according to the ABI spec. */
#define REGS_CLOBBERED "ebx", "edi", "esi"

static int
slp_switch(void)
{
    register int *stackref, stsizediff;
#if STACKLESS_FRHACK
    __asm__ volatile (
        ""
        : : : REGS_CLOBBERED );
#else
    void * ebp;
    __asm__ volatile (
        "movl %%ebp, %0\n\t"
        : "=m" (ebp) : : REGS_CLOBBERED );
#endif
    __asm__ ("movl %%esp, %0" : "=g" (stackref));
    {
        SLP_SAVE_STATE(stackref, stsizediff);
        __asm__ volatile (
            "addl %0, %%esp\n\t"
            "addl %0, %%ebp\n\t"
            :
            : "r" (stsizediff)
            );
        SLP_RESTORE_STATE();
#if ! STACKLESS_FRHACK
        __asm__ volatile (
            "movl %0, %%ebp\n\t"
            : : "m" (ebp) );
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
