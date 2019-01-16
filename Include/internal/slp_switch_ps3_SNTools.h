/*
 * Dummy transfer function for PS3
 * Sets us up to use the assembler, which is in switch_ps3_SNTools.asm
 */

#include <alloca.h>

#define SLP_STACK_REFPLUS 1
#define SLP_STACK_MAGIC 0 /* in the assembler, we grab the stack pointer directly */

#define SLP_EXTERNAL_ASM

/* use the c stack sparingly.  No initial gap, and invoke stack spilling at 16k */
#define SLP_CSTACK_GOODGAP 0
#define SLP_CSTACK_WATERMARK (16*1024/sizeof(intptr_t))