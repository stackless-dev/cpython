/*[clinic input]
preserve
[clinic start generated code]*/

#if defined(STACKLESS)

PyDoc_STRVAR(_stackless_tasklet_set_context__doc__,
"set_context($self, /, context)\n"
"--\n"
"\n"
"Set the context to be used while this tasklet runs.\n"
"\n"
"Every tasklet has a private context attribute. When the tasklet runs,\n"
"this context becomes the current context of the thread.\n"
"\n"
"This method raises RuntimeError, if the tasklet is bound to a foreign thread and is current or scheduled.\n"
"This method raises RuntimeError, if called from within Context.run().\n"
"This method returns the tasklet it is called on.");

#define _STACKLESS_TASKLET_SET_CONTEXT_METHODDEF    \
    {"set_context", (PyCFunction)(void(*)(void))_stackless_tasklet_set_context, METH_FASTCALL|METH_KEYWORDS, _stackless_tasklet_set_context__doc__},

static PyObject *
_stackless_tasklet_set_context_impl(PyTaskletObject *self, PyObject *context);

static PyObject *
_stackless_tasklet_set_context(PyTaskletObject *self, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"context", NULL};
    static _PyArg_Parser _parser = {"O!:set_context", _keywords, 0};
    PyObject *context;

    if (!_PyArg_ParseStackAndKeywords(args, nargs, kwnames, &_parser,
        &PyContext_Type, &context)) {
        goto exit;
    }
    return_value = _stackless_tasklet_set_context_impl(self, context);

exit:
    return return_value;
}

#endif /* defined(STACKLESS) */

#ifndef _STACKLESS_TASKLET_SET_CONTEXT_METHODDEF
    #define _STACKLESS_TASKLET_SET_CONTEXT_METHODDEF
#endif /* !defined(_STACKLESS_TASKLET_SET_CONTEXT_METHODDEF) */
/*[clinic end generated code: output=d6d26cc0dd503a03 input=a9049054013a1b77]*/
