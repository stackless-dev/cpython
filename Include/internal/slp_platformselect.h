#ifndef STACKLESS_SLP_PLATFORM_SELECT_H
#define STACKLESS_SLP_PLATFORM_SELECT_H

/*
 * Platform Selection for Stackless
 */

#if   defined(MS_WIN32) && !defined(MS_WIN64) && defined(_M_IX86)
#include "internal/slp_switch_x86_msvc.h" /* MS Visual Studio on X86 */
#elif defined(MS_WIN64) && defined(_M_X64)
#include "internal/slp_switch_x64_msvc.h" /* MS Visual Studio on X64 */
#elif defined(__GNUC__) && defined(__i386__)
#include "internal/slp_switch_x86_unix.h" /* gcc on X86 */
#elif defined(__GNUC__) && defined(__amd64__)
#include "internal/slp_switch_amd64_unix.h" /* gcc on amd64 */
#elif defined(__GNUC__) && defined(__PPC__) && defined(__linux__)
#include "internal/slp_switch_ppc_unix.h" /* gcc on PowerPC */
#elif defined(__GNUC__) && defined(__ppc__) && defined(__APPLE__)
#include "internal/slp_switch_ppc_macosx.h" /* Apple MacOS X on PowerPC */
#elif defined(__GNUC__) && defined(sparc) && defined(sun)
#include "internal/slp_switch_sparc_sun_gcc.h" /* SunOS sparc with gcc */
#elif defined(__GNUC__) && defined(__s390__) && defined(__linux__)
#include "internal/slp_switch_s390_unix.h"   /* Linux/S390 */
#elif defined(__GNUC__) && defined(__s390x__) && defined(__linux__)
#include "internal/slp_switch_s390_unix.h"   /* Linux/S390 zSeries (identical) */
#elif defined(__GNUC__) && defined(__arm__) && defined(__thumb__)
#include "internal/slp_switch_arm_thumb_gcc.h" /* gcc using arm thumb */
#elif defined(__GNUC__) && defined(__arm32__)
#include "internal/slp_switch_arm32_gcc.h" /* gcc using arm32 */
#elif defined(__GNUC__) && defined(__mips__) && defined(__linux__)
#include "internal/slp_switch_mips_unix.h" /* MIPS */
#elif defined(SN_TARGET_PS3)
#include "internal/slp_switch_ps3_SNTools.h" /* Sony PS3 */
#endif

/* default definitions if not defined in above files */

/*
 * Call SLP_DO_NOT_OPTIMIZE_AWAY(pointer) to ensure that pointer will be
 * computed even post-optimization.  Use it for pointers that are computed but
 * otherwise are useless. The compiler tends to do a good job at eliminating
 * unused variables, and this macro fools it into thinking var is in fact
 * needed.
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
#endif

/* adjust slots to typical size of a few recursions on your system */

#ifndef SLP_CSTACK_SLOTS
#define SLP_CSTACK_SLOTS        1024
#endif

/* how many cstacks to cache at all */

#ifndef SLP_CSTACK_MAXCACHE
#define SLP_CSTACK_MAXCACHE     100
#endif

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

#define SLP_CSTACK_SAVE_NOW(tstate, stackvar) \
        ((tstate)->st.cstack_root != NULL ? \
         SLP_CSTACK_SUBTRACT((tstate)->st.cstack_root, \
         (intptr_t*)&(stackvar)) > SLP_CSTACK_WATERMARK : 1)

#endif
