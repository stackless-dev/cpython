/*
 * this file was generated from the Python(r) C sources using the script
 * Stackless/core/extract_slp_methods.py .
 * please don't edit this output, but work on the script.
 */

typedef struct {
    void *type;
    size_t offset;
} _stackless_method;

#define MFLAG_IND 0x8000
#define MFLAG_OFS(meth) offsetof(PyMappingMethods, slpflags.meth)
#define MFLAG_OFS_IND(meth) MFLAG_OFS(meth) + MFLAG_IND

static _stackless_method _stackless_methtable[] = {
    /* from classobject.c */
    {&PyMethod_Type,            MFLAG_OFS(tp_call)},
    /* from descrobject.c */
    {&PyMethodDescr_Type,        MFLAG_OFS(tp_call)},
    {&PyClassMethodDescr_Type,    MFLAG_OFS(tp_call)},
    {&PyWrapperDescr_Type,        MFLAG_OFS(tp_call)},
    {&_PyMethodWrapper_Type,    MFLAG_OFS(tp_call)},
    /* from funcobject.c */
    {&PyFunction_Type,            MFLAG_OFS(tp_call)},
    /* from genobject.c */
    {&PyGen_Type,                MFLAG_OFS(tp_iternext)},
    {&_PyCoroWrapper_Type,        MFLAG_OFS(tp_iternext)},
    {&_PyAsyncGenASend_Type,    MFLAG_OFS(tp_iternext)},
    /* from methodobject.c */
    {&PyCFunction_Type,            MFLAG_OFS(tp_call)},
    /* from typeobject.c */
    {&PyType_Type,                MFLAG_OFS(tp_call)},
    /* from channelobject.c */
    {&PyChannel_Type,            MFLAG_OFS(tp_iternext)},
    {0, 0} /* sentinel */
};
