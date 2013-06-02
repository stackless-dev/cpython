
static void *slp_switch(void *(*save_state)(void*, void*),
                        void *(*restore_state)(void*, void*),
                        void *extra)
{
  void *result;
  __asm__ volatile (
     "pushq %%rbp\n"
     "pushq %%rbx\n"       /* push the registers that may contain  */
     "pushq %%rsi\n"       /* some value that is meant to be saved */
     "pushq %%rdi\n"
     "pushq %%rcx\n"
     "pushq %%rdx\n"
     "pushq %%r8\n"
     "pushq %%r9\n"
     "pushq %%r10\n"
     "pushq %%r11\n"
     "pushq %%r12\n"
     "pushq %%r13\n"
     "pushq %%r14\n"
     "pushq %%r15\n"

     "movq %%rax, %%r12\n" /* save 'restore_state' for later */
     "movq %%rsi, %%r13\n" /* save 'extra' for later         */

                           /* arg 2: extra                       */
     "movq %%rsp, %%rdi\n" /* arg 1: current (old) stack pointer */
     "call *%%rcx\n"       /* call save_state()                  */

     "testb $1, %%al\n"        /* skip the rest if the return value is odd */
     "jnz 0f\n"

     "movq %%rax, %%rsp\n"     /* change the stack pointer */

     /* From now on, the stack pointer is modified, but the content of the
        stack is not restored yet.  It contains only garbage here. */

     "movq %%r13, %%rsi\n" /* arg 2: extra                       */
     "movq %%rax, %%rdi\n" /* arg 1: current (new) stack pointer */
     "call *%%r12\n"       /* call restore_state()               */

     /* The stack's content is now restored. */

     "0:\n"
     "popq %%r15\n"
     "popq %%r14\n"
     "popq %%r13\n"
     "popq %%r12\n"
     "popq %%r11\n"
     "popq %%r10\n"
     "popq %%r9\n"
     "popq %%r8\n"
     "popq %%rdx\n"
     "popq %%rcx\n"
     "popq %%rdi\n"
     "popq %%rsi\n"
     "popq %%rbx\n"
     "popq %%rbp\n"

     : "=a"(result)              /* output variables */
     : "a"(restore_state),       /* input variables  */
       "c"(save_state),
       "S"(extra)
     );
  return result;
}
