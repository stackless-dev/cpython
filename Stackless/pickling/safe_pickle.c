#include "Python.h"
#ifdef STACKLESS

#include "core/stackless_tealet.h"

/* safe pickling */

typedef struct pickle_args
{
    PyThreadState *ts;
    tealet_t *return_to;
    int (*save)(PyObject *, PyObject *, int);
    PyObject *self;
    PyObject *args;
    int pers_save;
    int result;
} pickle_args;


static tealet_t *
pickle_callback(tealet_t *current, void *arg)
{
    pickle_args *args = (pickle_args*)arg;
    args->ts->st.nesting_level++; /* always hard-switch from this one now */
    args->result = (*args->save)(args->self, args->args, args->pers_save);
    args->ts->st.nesting_level--;
    return args->return_to;
}

int
slp_safe_pickling(int(*save)(PyObject *, PyObject *, int),
                  PyObject *self, PyObject *args, int pers_save)
{
    PyThreadState *ts = PyThreadState_GET();
    pickle_args *pargs = PyMem_MALLOC(sizeof(pickle_args));
    void *ppargs;
    int result;
    if (!pargs) {
        PyErr_NoMemory();
        return -1;
    }
    pargs->ts = ts;
    pargs->return_to = tealet_current(ts->st.tealet_main);
    pargs->save = save;
    pargs->self = self;
    pargs->args = args;
    pargs->pers_save = pers_save;

    ppargs = (void*)pargs;
    result = slp_run_initial_stub(ts, pickle_callback, &ppargs);
    if (result == 0)
        result = pargs->result;
    PyMem_FREE(pargs);
    return result;
}


/* safe unpickling is not needed */
#endif