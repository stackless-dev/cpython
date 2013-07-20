/* interface to the stack switching routines used by stackless */
#ifndef _STACKLESS_TEALET_H
#define _STACKLESS_TEALET_H

#include <tealet/tealet.h>
#include <python.h>

int slp_make_initial_stub(PyThreadState *ts);
void slp_destroy_initial_stub(PyThreadState *ts);
int slp_run_initial_stub(PyThreadState *ts, tealet_run_t func, void **arg);
PyObject * slp_run_stub_from_main(PyThreadState *ts);
int slp_run_stub_from_worker(PyThreadState *ts);

/* hard switching of tasklets */
int slp_transfer(PyThreadState *ts, tealet_t *cst, struct PyTaskletObject *prev);
int slp_transfer_with_exc(PyThreadState *ts, tealet_t *cst, struct PyTaskletObject *prev);
int slp_transfer_return(tealet_t *cst);


#endif