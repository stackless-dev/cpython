
/* define USE_SLP_FALLBACK here if you want to test the SLP_FALLBACK mechanism */
#define ENABLE_SLP_FALLBACK
/* #define USE_SLP_FALLBACK */

#if defined USE_SLP_FALLBACK && !defined ENABLE_SLP_FALLBACK
#define ENABLE_SLP_FALLBACK
#endif

#ifndef USE_SLP_FALLBACK
#if   defined(_M_IX86)
#include "switch_x86_msvc.h" /* MS Visual Studio on X86 */
#elif defined(_M_X64)
#include "switch_x64_msvc.h" /* MS Visual Studio on X64 */
#elif defined(__GNUC__) && defined(__amd64__)
#include "switch_x86_64_gcc.h" /* gcc on amd64 */
#elif defined(__GNUC__) && defined(__i386__)
#include "switch_x86_gcc.h" /* gcc on X86 */
#else
#ifdef ENABLE_SLP_FALLBACK
#define USE_SLP_FALLBACK
#else
#error "Unsupported platform!"
#endif
#endif
#endif

#ifdef USE_SLP_FALLBACK
/* hope this is standard C */
#pragma message("fallback to stackless platform support. Switching is not thread-safe")
#include "platf_slp/slp_fallback.h"
#endif
