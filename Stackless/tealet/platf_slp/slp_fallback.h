/* slp_fallback.h
 * Implements a fallback mechanism where the stacless python switching code be used.
 * Stackless python switching code uses global variables to communicate and thus is not
 * thread safe. We do not attempt to provide a lock for those global variables here, it
 * is up to the user in each case to ensure that switching only occurs on one thread at
 * a time.
 */
 #include <stdint.h>


/* rename the slp_switch function that the slp headers will define */
#define slp_switch fallback_slp_switch

/* sttic vars to pass inforamtion around.  This is what makes this non-threadsafe */
typedef void *(*save_restore_t)(void*, void*);
static save_restore_t fallback_save_state=NULL, fallback_restore_state=NULL;
static void *fallback_extra=NULL;
static void *fallback_newstack = NULL;

/* call the provided save_satate method with its arguments.
 * compute a proper stack delta and store the result for later
 */
#define SLP_SAVE_STATE(stackref, stsizediff) do {\
	intptr_t stsizeb; \
	void *newstack; \
	stackref += STACK_MAGIC; \
    newstack = fallback_save_state(stackref, fallback_extra); \
	fallback_newstack = newstack; /* store this for restore_state */ \
	if ((intptr_t)newstack & 1) \
		return (int)newstack; \
    /* compute the delta expected by old switching code */ \
	stsizediff = ((intptr_t*) newstack - (intptr_t*)stackref) * sizeof(intptr_t); \
} while (0)

/* call the restore_state using the stored data */
#define SLP_RESTORE_STATE() do { \
	fallback_restore_state(fallback_newstack, fallback_extra); \
} while(0)

#define SLP_EVAL
#include "slp_platformselect.h"

/* This is a wrapper that takes care of setting the appropriate globals */
#undef slp_switch
static void *slp_switch(save_restore_t save_state,
                        save_restore_t restore_state,
                        void *extra)
{
	/* need to store the restore information on the stack */
	void *result;
	fallback_save_state = save_state;
	fallback_restore_state = restore_state;
	fallback_extra = extra;

	fallback_slp_switch();
	result = fallback_newstack;
	
    /* clearing them again is prudent */
	fallback_save_state = fallback_restore_state = NULL;
	fallback_extra = fallback_newstack = NULL;

	/* successful switch was indicated by save_state returning an even result */
	if (! ((intptr_t)result & 1))
		result = NULL;
	/* otherwise it is 1 or -1 */
	return result;
}