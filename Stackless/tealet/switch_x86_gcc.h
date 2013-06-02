
static void *slp_switch(void *(*save_state)(void*, void*),
                        void *(*restore_state)(void*, void*),
                        void *extra)
{
  void *result;
  __asm__ volatile (
     "pushl %%ebp\n"
     "pushl %%ebx\n"       /* push the registers that may contain  */
     "pushl %%esi\n"       /* some value that is meant to be saved */
     "pushl %%edi\n"
     "pushl %%ecx\n"
     "pushl %%edx\n"

     "movl %%eax, %%esi\n" /* save 'restore_state' for later */
     "movl %%edx, %%edi\n" /* save 'extra' for later         */

     "movl %%esp, %%eax\n"

     "pushl %%edx\n"       /* arg 2: extra                       */
     "pushl %%eax\n"       /* arg 1: current (old) stack pointer */
     "call *%%ecx\n"       /* call save_state()                  */

     "testl $1, %%eax\n"       /* skip the rest if the return value is odd */
     "jnz 0f\n"

     "movl %%eax, %%esp\n"     /* change the stack pointer */

     /* From now on, the stack pointer is modified, but the content of the
        stack is not restored yet.  It contains only garbage here. */

     "pushl %%edi\n"       /* arg 2: extra                       */
     "pushl %%eax\n"       /* arg 1: current (new) stack pointer */
     "call *%%esi\n"       /* call restore_state()               */

     /* The stack's content is now restored. */

     "0:\n"
     "addl $8, %%esp\n"
     "popl %%edx\n"
     "popl %%ecx\n"
     "popl %%edi\n"
     "popl %%esi\n"
     "popl %%ebx\n"
     "popl %%ebp\n"

     : "=a"(result)              /* output variables */
     : "a"(restore_state),       /* input variables  */
       "c"(save_state),
       "d"(extra)
     );
  return result;
}
