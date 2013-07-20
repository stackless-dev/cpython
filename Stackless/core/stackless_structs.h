#ifndef STACKLESS_STRUCTS_H
#define STACKLESS_STRUCTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* platform specific constants (mainly SEH stuff to store )*/
/* TODO: remove vestiges of old switching */
#include "platf/slp_platformselect.h"

#include "stackless_tealet.h"



/*** important structures: tasklet ***/


/***************************************************************************

    Tasklet Flag Definition
    -----------------------

    blocked:        The tasklet is either waiting in a channel for
                    writing (1) or reading (-1) or not blocked (0).
                    Maintained by the channel logic. Do not change.

    atomic:         If true, schedulers will never switch. Driven by
                    the code object or dynamically, see below.

    ignore_nesting: Allows auto-scheduling, even if nesting_level
                    is not zero.

    autoschedule:   The tasklet likes to be auto-scheduled. User driven.

    block_trap:     Debugging aid. Whenever the tasklet would be
                    blocked by a channel, an exception is raised.

    is_zombie:      This tasklet is almost dead, its deallocation has
                    started. The tasklet *must* die at some time, or the
                    process can never end.

    pending_irq:    If set, an interrupt was issued during an atomic
                    operation, and should be handled when possible.


    Policy for atomic/autoschedule and switching:
    ---------------------------------------------
    A tasklet switch can always be done explicitly by calling schedule().
    Atomic and schedule are concerned with automatic features.

    atomic  autoschedule

    1           any     Neither a scheduler nor a watchdog will
                    try to switch this tasklet.

    0           0       The tasklet can be stopped on desire, or it
                    can be killed by an exception.

    0           1       Like above, plus auto-scheduling is enabled.

    Default settings:
    -----------------
    All flags are zero by default.

 ***************************************************************************/

typedef struct PyTaskletObject_flags {
    int blocked: 2;
    unsigned int atomic: 1;
    unsigned int ignore_nesting: 1;
    unsigned int autoschedule: 1;
    unsigned int block_trap: 1;
    unsigned int is_zombie: 1;
    unsigned int pending_irq: 1;
} PyTaskletFlagStruc;


typedef struct PyTaskletObject {
    PyObject_HEAD
    struct PyTaskletObject *next;
    struct PyTaskletObject *prev;
    union {
        struct _frame *frame;
        struct _cframe *cframe;
    } f;
    PyObject *tempval;
    /* bits stuff */
    struct PyTaskletObject_flags flags;
    int recursion_depth;
    int nesting_level;
    PyThreadState *tstate;
    struct tealet_t *cstate;
    PyObject *def_globals;
    PyObject *tsk_weakreflist;
} PyTaskletObject;


/*** important structures: cstack ***/

typedef struct _cstack {
    PyObject_VAR_HEAD
    struct _cstack *next;
    struct _cstack *prev;
#ifdef have_long_long
    long_long serial;
#else
    long serial;
#endif
    struct PyTaskletObject *task;
    int nesting_level;
    PyThreadState *tstate;
#ifdef _SEH32
    DWORD exception_list; /* SEH handler on Win32 */
#endif
    tealet_t *tealet;
} PyCStackObject;


/*** important structures: bomb ***/

typedef struct _bomb {
    PyObject_HEAD
    PyObject *curexc_type;
    PyObject *curexc_value;
    PyObject *curexc_traceback;
} PyBombObject;

/*** important structures: channel ***/

/***************************************************************************

    Channel Flag Definition
    -----------------------


    closing:        When the closing flag is set, the channel does not
                    accept to be extended. The computed attribute
                    'closed' is true when closing is set and the
                    channel is empty.

    preference:     0    no preference, caller will continue
                    1    sender will be inserted after receiver and run
                    -1   receiver will be inserted after sender and run

    schedule_all:   ignore preference and always schedule the next task

    Default settings:
    -----------------
    All flags are zero by default.

 ***************************************************************************/

typedef struct _channel_flags {
    unsigned int closing: 1;
    int preference: 2;
    unsigned int schedule_all:1;
} PyChannelFlagStruc;

typedef struct _channel {
    PyObject_HEAD
    /* make sure that these fit tasklet's next/prev */
    struct PyTaskletObject *head;
    struct PyTaskletObject *tail;
    int balance;
    struct _channel_flags flags;
    PyObject *chan_weakreflist;
} PyChannelObject;


/*** important stuctures: cframe ***/

typedef struct _cframe {
    PyObject_VAR_HEAD
    struct _frame *f_back;      /* previous frame, or NULL */
    PyFrame_ExecFunc *f_execute;

    /*
     * the above part is compatible with frames.
     * Note that I have re-arranged some fields in the frames
     * to keep cframes as small as possible.
     */

    /* these can be used as the CFrame likes to */

    PyObject *ob1;
    PyObject *ob2;
    PyObject *ob3;
    long i, n;
    void *any1;
    void *any2;
} PyCFrameObject;


/*** important structures: slpmodule ***/

typedef struct _slpmodule {
    PyObject_HEAD
    PyObject *md_dict;
    /* the above is a copy of private PyModuleObject */
    PyTypeObject *__tasklet__;
    PyTypeObject *__channel__;
} PySlpModuleObject;


/*** associated type objects ***/

PyAPI_DATA(PyTypeObject) PyCFrame_Type;
#define PyCFrame_Check(op) ((op)->ob_type == &PyCFrame_Type)

PyAPI_DATA(PyTypeObject) PyCStack_Type;
#define PyCStack_Check(op) ((op)->ob_type == &PyCStack_Type)

PyAPI_DATA(PyTypeObject) PyBomb_Type;
#define PyBomb_Check(op) ((op)->ob_type == &PyBomb_Type)

PyAPI_DATA(PyTypeObject*) PyTasklet_TypePtr;
#define PyTasklet_Type (*PyTasklet_TypePtr)
#define PyTasklet_Check(op) PyObject_TypeCheck(op, PyTasklet_TypePtr)
#define PyTasklet_CheckExact(op) ((op)->ob_type == PyTasklet_TypePtr)

PyAPI_DATA(PyTypeObject*) PyChannel_TypePtr;
#define PyChannel_Type (*PyChannel_TypePtr)
#define PyChannel_Check(op) PyObject_TypeCheck(op, PyChannel_TypePtr)
#define PyChannel_CheckExact(op) ((op)->ob_type == PyChannel_TypePtr)

/*** these are in other bits of Python ***/
PyAPI_DATA(PyTypeObject) PyDictIterKey_Type;
PyAPI_DATA(PyTypeObject) PyDictIterValue_Type;
PyAPI_DATA(PyTypeObject) PyDictIterItem_Type;
PyAPI_DATA(PyTypeObject) PyListIter_Type;
PyAPI_DATA(PyTypeObject) PySetIter_Type;
PyAPI_DATA(PyTypeObject) PyRangeIter_Type;
PyAPI_DATA(PyTypeObject) PyTupleIter_Type;
PyAPI_DATA(PyTypeObject) PyEnum_Type;

/*** the Stackless module itself ***/
PyAPI_DATA(PyObject *) slp_module;

#ifdef __cplusplus
}
#endif

#endif
