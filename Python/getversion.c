
/* Return the full version string. */

#include "Python.h"

#include "patchlevel.h"

#ifdef STACKLESS
        /* avoiding to recompile everything for Stackless all the time */
#include "stackless_version.h"

const char *
Py_GetVersion(void)
{
        static char version[250];
        PyOS_snprintf(version, sizeof(version), "%.80s Stackless %.80s (%.80s) %.80s",
                      PY_VERSION, STACKLESS_VERSION, Py_GetBuildInfo(), Py_GetCompiler());
        return version;
}

#else

const char *
Py_GetVersion(void)
{
    static char version[250];
    PyOS_snprintf(version, sizeof(version), "%.80s (%.80s) %.80s",
                  PY_VERSION, Py_GetBuildInfo(), Py_GetCompiler());
    return version;
}

#endif
