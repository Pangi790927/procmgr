#include "pymod.h"
#include "debug.h"
#include "json.h"

/* A python application that is a daemon can load this module to help it communicate with the rest
of the procmgr modules. This module will accomplish the task of translating messages from json to
procmgr_* and back.  */

static PyObject *example_function(PyObject *self, PyObject *args) {
    DBG_SCOPE();
    return NULL;
}

std::vector<PyMethodDef> module_methods = {
    PyMethodDef{"example_function", example_function, METH_VARARGS, "example function"},
};

int pymod_pre_init(std::vector<PyMethodDef> &methods, PyModuleDef *module_def) {
    for (auto m : module_methods)
        methods.push_back(m);
    return 0;
}

int pymod_post_init() {
    DBG_SCOPE();
    pymod_register_int_cbk = [](uint64_t, pymod_cbk_wp) {};
    pymod_register_str_cbk = [](std::string, pymod_cbk_wp) {};
    pymod_unregister_int_cbk = [](pymod_cbk_wp) {};
    pymod_unregister_str_cbk = [](pymod_cbk_wp) {};
    return 0;
}
