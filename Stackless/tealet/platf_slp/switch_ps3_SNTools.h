/*
 * Dummy transfer function for PS3
 * Sets us up to use the assembler, which is in switch_ps3_SNTools.asm
 */

#include <alloca.h>

#define STACK_REFPLUS 1
#define STACK_MAGIC 0 /* in the assembler, we grab the stack pointer directly */

#define EXTERNAL_ASM

/* use the c stack sparingly.  No initial gap, and invoke stack spilling at 16k */
#define CSTACK_GOODGAP 0
#define CSTACK_WATERMARK (16*1024/sizeof(intptr_t))