/* interface to the stack switching routines used by stackless */
#ifndef _STACKLESS_TEALET_H
#define _STACKLESS_TEALET_H

#include <tealet/tealet.h>
#include <python.h>

/* a list of tealets. Currently these are suspended tealets with their
 * suspended tasklets indicated.
 */
typedef struct PyTealet_data {
    struct PyTealet_data *prev;
    struct PyTealet_data *next;
    struct PyTaskletObject *tasklet;
} PyTealet_data;

PyTealet_data *slp_tealet_list; /* head of the tealet list */

int slp_make_initial_stub(PyThreadState *ts);
void slp_destroy_initial_stub(PyThreadState *ts);
int slp_run_initial_stub(PyThreadState *ts, tealet_run_t func, void **arg);
int slp_run_stub_from_worker(PyThreadState *ts);
void slp_tealet_cleanup(PyThreadState *ts);

/* hard switching of tasklets */
int slp_transfer(PyThreadState *ts, tealet_t *cst, struct PyTaskletObject *prev);
int slp_transfer_with_exc(PyThreadState *ts, tealet_t *cst, struct PyTaskletObject *prev);
int slp_transfer_return(tealet_t *cst);

/* stack spilling */
int slp_cstack_save_now(PyThreadState *ts);

#endif