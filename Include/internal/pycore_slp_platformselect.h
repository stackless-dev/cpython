#ifndef PYCORE_SLP_PLATFORMSELECT_H
#define PYCORE_SLP_PLATFORMSELECT_H

/*
 * Platform Selection for Stackless
 */

#if   defined(MS_WIN32) && !defined(MS_WIN64) && defined(_M_IX86)
#include "pycore_slp_switch_x86_msvc.h" /* MS Visual Studio on X86 */
#elif defined(MS_WIN64) && defined(_M_X64)
#include "pycore_slp_switch_x64_msvc.h" /* MS Visual Studio on X64 */
#elif defined(__GNUC__) && defined(__i386__)
#include "pycore_slp_switch_x86_unix.h" /* gcc on X86 */
#elif defined(__GNUC__) && defined(__amd64__)
#include "pycore_slp_switch_amd64_unix.h" /* gcc on amd64 */
#elif defined(__GNUC__) && defined(__PPC__) && defined(__linux__)
#include "pycore_slp_switch_ppc_unix.h" /* gcc on PowerPC */
#elif defined(__GNUC__) && defined(__ppc__) && defined(__APPLE__)
#include "pycore_slp_switch_ppc_macosx.h" /* Apple MacOS X on PowerPC */
#elif defined(__GNUC__) && defined(sparc) && defined(sun)
#include "pycore_slp_switch_sparc_sun_gcc.h" /* SunOS sparc with gcc */
#elif defined(__GNUC__) && defined(__s390__) && defined(__linux__)
#include "pycore_slp_switch_s390_unix.h"   /* Linux/S390 */
#elif defined(__GNUC__) && defined(__s390x__) && defined(__linux__)
#include "pycore_slp_switch_s390_unix.h"   /* Linux/S390 zSeries (identical) */
#elif defined(__GNUC__) && defined(__arm__) && defined(__thumb__)
#include "pycore_slp_switch_arm_thumb_gcc.h" /* gcc using arm thumb */
#elif defined(__GNUC__) && defined(__arm32__)
#include "pycore_slp_switch_arm32_gcc.h" /* gcc using arm32 */
#elif defined(__GNUC__) && defined(__mips__) && defined(__linux__)
#include "pycore_slp_switch_mips_unix.h" /* MIPS */
#elif defined(SN_TARGET_PS3)
#include "pycore_slp_switch_ps3_SNTools.h" /* Sony PS3 */
#endif

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

#endif  /* !STACKLESS_SLP_PLATFORM_SELECT_H */
