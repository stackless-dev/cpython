/*
 * Platform Selection for Stackless Python
 */

#if   defined(_M_IX86)
#include "switch_x86_msvc.h" /* MS Visual Studio on X86 */
#elif defined(_M_X64)
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
#include "switch_s390_unix.h"	/* Linux/S390 */
#elif defined(__GNUC__) && defined(__s390x__) && defined(__linux__)
#include "switch_s390_unix.h"	/* Linux/S390 zSeries (identical) */
#elif defined(__GNUC__) && defined(__arm__) && defined(__thumb__)
#include "switch_arm_thumb_gcc.h" /* gcc using arm thumb */
#elif defined(__GNUC__) && defined(__arm32__)
#include "switch_arm32_gcc.h" /* gcc using arm32 */
#elif defined(__GNUC__) && defined(__mips__) && defined(__linux__)
#include "switch_mips_unix.h" /* MIPS */
#elif defined(SN_TARGET_PS3)
#include "switch_ps3_SNTools.h" /* Sony PS3 */
#else
#error "platform unsupported by stackless python"
#endif

/* default definitions if not defined in above files */

/* adjust slots to typical size of a few recursions on your system */

#ifndef CSTACK_SLOTS
#define CSTACK_SLOTS        1024
#endif

/* how many cstacks to cache at all */

#ifndef CSTACK_MAXCACHE
#define CSTACK_MAXCACHE     100
#endif

/* a good estimate how much the cstack level differs between
   initialisation and main python code. Not critical, but saves time.
   Note that this will vanish with the greenlet approach. */

#ifndef CSTACK_GOODGAP
#define CSTACK_GOODGAP      4096
#endif

/* stack size in pointer to trigger stack spilling */

#ifndef CSTACK_WATERMARK
#define CSTACK_WATERMARK 16384
#endif

/* define direction of stack growth */

#ifndef CSTACK_DOWNWARDS
#define CSTACK_DOWNWARDS 1   /* 0 for upwards */
#endif

/**************************************************************

  Don't change definitions below, please.

 **************************************************************/

#if CSTACK_DOWNWARDS == 1
#define CSTACK_COMPARE(a, b) (a) < (b)
#define CSTACK_SUBTRACT(a, b) (a) - (b)
#else
#define CSTACK_COMPARE(a, b) (a) > (b)
#define CSTACK_SUBTRACT(a, b) (b) - (a)
#endif

#define CSTACK_SAVE_NOW(tstate, stackvar) \
	((tstate)->st.cstack_root != NULL ? \
	 CSTACK_SUBTRACT((tstate)->st.cstack_root, \
	 (intptr_t*)&(stackvar)) > CSTACK_WATERMARK : 1)
