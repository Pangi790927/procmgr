#include "pymod.h"
#include "debug.h"
#include "json.h"
#include "time_utils.h"

#include <thread>

/* A python application that is a daemon can load this module to help it communicate with the rest
of the procmgr modules. This module will accomplish the task of translating messages from json to
procmgr_* and back.  */

static PyObject *aw;
static std::thread lazy_awake;

static PyObject *example_awaitable(PyObject *self, PyObject *args) {
    DBG_SCOPE();
    if (aw) {
        DBG("In this test we can't call await twice");
        return NULL;
    }
    aw = pymod_await_new(NULL);
    // aw = pyawaitable_new();
    // if (aw == NULL) {
    //     DBG("Failed to create awaitable");
    //     return NULL;
    // }
    // lazy_awake = std::thread([]{
    //     DBG("sleeping");
    //     sleep_ms(10'000);
    //     DBG("done sleeping");
    //     PyObject *value = PyLong_FromLong(42);
    //     if (value == NULL) {
    //         DBG("Failed to create a new value");
    //     }

    //     // if (pyawaitable_set_result(aw, value) < 0)
    //     // {
    //     //     DBG("Failed to set the result");
    //     //     Py_DECREF(value);
    //     // }

    //     DBG("Result has been set");
    //     Py_DECREF(value);
    // });
    return aw;
}

/* Registering the functions:
================================================================================================= */

std::vector<PyMethodDef> module_methods = {
    PyMethodDef{"example_awaitable", example_awaitable, METH_O, "example awaitable"},
};

int pymod_pre_init(std::vector<PyMethodDef> &methods, PyModuleDef *module_def) {
    DBG_SCOPE();
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
