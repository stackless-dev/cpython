#ifndef Py_PRICKELPIT_H
#define Py_PRICKELPIT_H
#ifdef __cplusplus
extern "C" {
#endif
#ifdef STACKLESS

/******************************************************

  code object and frame pickling plugin

*******************************************************/

int slp_register_execute(PyTypeObject *t, char *name,
                                         PyFrame_ExecFunc *good,
                                         PyFrame_ExecFunc *bad);

int slp_find_execfuncs(PyTypeObject *type, PyObject *exec_name,
                                       PyFrame_ExecFunc **good,
                                       PyFrame_ExecFunc **bad);

PyObject * slp_find_execname(PyFrameObject *f, int *valid);

PyObject * slp_cannot_execute(PyFrameObject *f, const char *exec_name, PyObject *retval);

/* macros to define and use an invalid frame executor */

#define SLP_DEF_INVALID_EXEC(procname) \
static PyObject *\
cannot_##procname(PyFrameObject *f, int exc, PyObject *retval) \
{ \
        return slp_cannot_execute(f, #procname, retval); \
}

#define SLP_REF_INVALID_EXEC(procname) (cannot_##procname)


/* pickling of arrays with nulls */

PyObject * slp_into_tuple_with_nulls(PyObject **start, Py_ssize_t length);
/* creates a tuple of length+1 with the first element holding null markers */

Py_ssize_t slp_from_tuple_with_nulls(PyObject **start, PyObject *tup);
/* loads data from a tuple where the first element holds null markers.
   return value is the number of elements (length-1)
 */

/* flags */
#define SLP_PICKLEFLAGS_PRESERVE_TRACING_STATE (1U)
#define SLP_PICKLEFLAGS_PRESERVE_AG_FINALIZER  (1U<<1)
#define SLP_PICKLEFLAGS_RESET_AG_FINALIZER     (1U<<2)
#define SLP_PICKLEFLAGS__MAX_VALUE             ((1<<3)-1) /* must be a signed value */

/* helper functions for module dicts */

PyObject * slp_pickle_moduledict(PyObject *self, PyObject *args);
extern char slp_pickle_moduledict__doc__[];
PyObject * PyStackless_Pickle_ModuleDict(PyObject *pickler, PyObject *self);

/* initialization */

PyObject *slp_init_prickelpit(void);

/* pickle with stack spilling */
int slp_safe_pickling(int(*save)(PyObject *, PyObject *, int),
                      PyObject *self, PyObject *args,
                     int pers_save);


#endif /* STACKLESS */
#ifdef __cplusplus
}
#endif
#endif /* !Py_PRICKELPIT_H */
