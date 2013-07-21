
#include "stackless_tealet.h"

#include <python.h>
#include "frameobject.h"
#include "stackless_impl.h"

PyTealet_data *slp_tealet_list = NULL;

static void
slp_tealet_link(tealet_t *t, PyTaskletObject *tasklet)
{
    PyTealet_data *td = TEALET_EXTRA(t, PyTealet_data);
    if (slp_tealet_list) {
        td->next = slp_tealet_list;
        td->prev = td->next->prev;
        td->next->prev = td;
        td->prev->next = td;
    } else
        td->next = td->prev = td;
    slp_tealet_list = td;
    td->tasklet = tasklet;
}

static void
slp_tealet_unlink(tealet_t *t)
{
    PyTealet_data *td = TEALET_EXTRA(t, PyTealet_data);
    if (td->next != td) {
        slp_tealet_list = td->next;
        td->next->prev = td->prev;
        td->prev->next = td->next;
    } else
        slp_tealet_list = NULL;
}

/****************************************************************
 *Implement copyable tealet stubs by using a trampoline
 */
struct slp_stub_arg
{
    tealet_t *current;
    tealet_run_t run;
    void *runarg;
};

static tealet_t *slp_stub_main(tealet_t *current, void *arg)
{
    void *myarg = 0;
    /* the caller is in arg, return right back to him */
    tealet_switch((tealet_t*)arg, &myarg);
    /* now we are back, myarg should contain the arg to the run function.
     * We were possibly duplicated, so can't trust the original function args.
     */
    {
        struct slp_stub_arg sarg = *(struct slp_stub_arg*)myarg;
        tealet_free(sarg.current, myarg);
        return (sarg.run)(sarg.current, sarg.runarg);
    }
}

/* create a stub and return it */
tealet_t *slp_stub_new(tealet_t *t, size_t extrasize) {
    void *arg = (void*)tealet_current(t);
    return tealet_new(t, slp_stub_main, &arg, extrasize);
}

/* run a stub */
int slp_stub_run(tealet_t *stub, tealet_run_t run, void **parg)
{
    int result;
    void *myarg;
    
    /* we cannot pass arguments to a different tealet on the stack */
    struct slp_stub_arg *psarg = (struct slp_stub_arg*)tealet_malloc(stub, sizeof(struct slp_stub_arg));
    if (!psarg)
        return TEALET_ERR_MEM;
    psarg->current = stub;
    psarg->run = run;
    psarg->runarg = parg ? *parg : NULL;
    myarg = (void*)psarg;
    result = tealet_switch(stub, &myarg);
    if (result) {
        /* failure */
        tealet_free(stub, psarg);
        return result;
    }
    /* pass back the arg value from the switch */
    if (parg)
        *parg = myarg;
    return 0;
}

/* end of generic tealet stub code */

/* translate tealet errors into python errors */
static int
slp_tealet_error(int err)
{
    assert(err);
    if (err == TEALET_ERR_MEM)
        PyErr_NoMemory();
    assert(err == TEALET_ERR_DEFUNCT);
    PyErr_SetString(PyExc_RuntimeError, "Tealet corrupt");
    return -1;
}

/* the current mechanism is based on the generic callable stubs
 * above.  This can be simplified, TODO
 */
int
slp_make_initial_stub(PyThreadState *ts)
{
    if (ts->st.tealet_main == NULL) {
        tealet_alloc_t ta = {
            (tealet_malloc_t)&PyMem_Malloc,
            (tealet_free_t)&PyMem_Free,
            0};
        ts->st.tealet_main = tealet_initialize(&ta, sizeof(PyTealet_data));
        if (!ts->st.tealet_main)
            goto err;

    }
    ts->st.initial_stub = slp_stub_new(ts->st.tealet_main, sizeof(PyTealet_data));
    if (!ts->st.initial_stub)
        goto err;
    return 0;
err:
    PyErr_NoMemory();
    return -1;
}

/* clean up tealet state for this thread */
void slp_tealet_cleanup(PyThreadState *ts)
{
    if (ts->st.initial_stub)
        tealet_delete(ts->st.initial_stub);
    ts->st.initial_stub = NULL;
    if (ts->st.tealet_main) {
        assert(TEALET_IS_MAIN(ts->st.tealet_main));
        tealet_finalize(ts->st.tealet_main);
    }
    ts->st.initial_stub = NULL;
}

void
slp_destroy_initial_stub(PyThreadState *ts)
{
    tealet_delete(ts->st.initial_stub);
    ts->st.initial_stub = NULL;
}

/* The function that runs tasklet loop in a tealet */
static tealet_t *tasklet_stub_func(tealet_t *me, void *arg)
{
    PyThreadState *ts = PyThreadState_GET();
    PyFrameObject *f = ts->frame;
    PyObject *result;
    ts->frame = NULL;
    ts->st.nesting_level = 0;
    Py_CLEAR(ts->st.del_post_switch);
    result = slp_run_tasklet(f);
    
    /* this tealet is returning, which means that main is returning. Switch back
     * to the main tealet.  The result is passed to the target
     */
    tealet_exit(ts->st.tealet_main, (void*)result, TEALET_EXIT_DEFAULT);
    /* this should never fail */
    assert(0);
    return NULL;
}

/* Running a function in the top level stub.  If NULL is provided,
 * use the tasklet evaluation loop
 */

/* Running a function in the top level stub */
int
slp_run_initial_stub(PyThreadState *ts, tealet_run_t func, void **arg)
{
    tealet_t *stub;
    int result;
    stub = tealet_duplicate(ts->st.initial_stub, sizeof(PyTealet_data));
    if (!stub) {
        PyErr_NoMemory();
        return -1;
    }
    result = slp_stub_run(stub, func, arg);
    if (result)
        return slp_tealet_error(result);
    return 0;
}

/* run the top level loop from the main tasklet.  This invocation expects
 * a return value
 */
PyObject *
slp_run_stub_from_main(PyThreadState *ts)
{
    int result;
    void *arg;
    tealet_t *old_main;

    /* switch into a stub duplicate.  Run evaluation loop.  Then switch back.
     * Set the "main" to be us, so that a switch out of the tasklet_stub_func
     * lands us here
     */
    old_main = ts->st.tealet_main;
    ts->st.tealet_main = tealet_current(old_main);
    result = slp_run_initial_stub(ts, &tasklet_stub_func, &arg);
    ts->st.tealet_main = old_main;
    if (result)
        return NULL;
    return (PyObject*)arg;
}

/* call the top level loop as a means of startin a new such loop, hardswitching out
 * of a tasklet.  This invocation does not expect a return value
 */
int
slp_run_stub_from_worker(PyThreadState *ts)
{
    return slp_run_initial_stub(ts, &tasklet_stub_func, NULL);
}

/* hard switching of tasklets */

int
slp_transfer(PyThreadState *ts, tealet_t *cst, PyTaskletObject *prev)
{
    int result;
    int nesting_level;
    tealet_t *current;

    assert(prev->cstate == NULL);
    nesting_level = prev->tstate->st.nesting_level;
    prev->nesting_level = nesting_level;

    /* mark the old tasklet as having cstate, and link it in */
    current = tealet_current(ts->st.tealet_main);
    prev->cstate = current;
    slp_tealet_link(current, prev);

    if (cst) {
        /* make sure we are not trying to jump between threads */
        assert(TEALET_RELATED(ts->st.tealet_main, cst));
        result = tealet_switch(cst, NULL);
    } else {
        assert(TEALET_RELATED(ts->st.tealet_main, ts->st.initial_stub));
        result = slp_run_stub_from_worker(ts);
    }

    /* we are back, or failed, no cstate in tasklet */
    slp_tealet_unlink(current);
    prev->cstate = NULL;
    prev->tstate->st.nesting_level = nesting_level;
    if (!result) {
        if (ts->st.del_post_switch) {
            PyObject *tmp;
            TASKLET_CLAIMVAL(ts->st.current, &tmp);
            Py_CLEAR(ts->st.del_post_switch);
            TASKLET_SETVAL_OWN(ts->st.current, tmp);
        }
        return 0;
    }
    if (result == TEALET_ERR_MEM)
        PyErr_NoMemory();
    else
        PyErr_SetString(PyExc_RuntimeError, "Invalid tasklet");
    return -1;
}

int
slp_transfer_return(tealet_t *cst)
{
    int result = tealet_exit(cst, NULL, TEALET_EXIT_DEFAULT);
    if (result) {
        /* emergency switch back to main tealet */
        PyThreadState *ts = PyThreadState_GET();
        PyErr_SetString(PyExc_RuntimeError, "Invalid tealet");
        tealet_exit(ts->st.tealet_main, NULL, TEALET_EXIT_DEFAULT);
        /* emergency!.  Transfer to the _real_ main */
        tealet_exit(TEALET_MAIN(ts->st.tealet_main), NULL, TEALET_EXIT_DEFAULT);
    }
    assert(0); /* never returns */
    return 0;
}

int
slp_transfer_with_exc(PyThreadState *ts, tealet_t *cst, PyTaskletObject *prev)
{
    int tracing = ts->tracing;
    int use_tracing = ts->use_tracing;

    Py_tracefunc c_profilefunc = ts->c_profilefunc;
    Py_tracefunc c_tracefunc = ts->c_tracefunc;
    PyObject *c_profileobj = ts->c_profileobj;
    PyObject *c_traceobj = ts->c_traceobj;

    PyObject *exc_type = ts->exc_type;
    PyObject *exc_value = ts->exc_value;
    PyObject *exc_traceback = ts->exc_traceback;
    int ret;

    ts->exc_type = ts->exc_value = ts->exc_traceback = NULL;
    ts->c_profilefunc = ts->c_tracefunc = NULL;
    ts->c_profileobj = ts->c_traceobj = NULL;
    ts->use_tracing = ts->tracing = 0;

    /* note that trace/profile are set without ref */
    Py_XINCREF(c_profileobj);
    Py_XINCREF(c_traceobj);

    ret = slp_transfer(ts, cst, prev);

    ts->tracing = tracing;
    ts->use_tracing = use_tracing;

    ts->c_profilefunc = c_profilefunc;
    ts->c_tracefunc = c_tracefunc;
    ts->c_profileobj = c_profileobj;
    ts->c_traceobj = c_traceobj;
    Py_XDECREF(c_profileobj);
    Py_XDECREF(c_traceobj);

    ts->exc_type = exc_type;
    ts->exc_value = exc_value;
    ts->exc_traceback = exc_traceback;
    return ret;
}
