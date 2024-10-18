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
    PyObject *ctx;
    if (!PyArg_ParseTuple(args, "O", &ctx)) {
        DBG("Failed parse args");
        return NULL;
    }
    aw = pymod_await_new(ctx);
    lazy_awake = std::thread([]{
        DBG("sleeping");
        sleep_ms(10'000);
        DBG("done sleeping");

        /* Stupid complicated awake stuff */
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
        pymod_await_trig(aw, "nomnom", 10);
        PyGILState_Release(gstate);

        DBG("Result has been set");
    });
    lazy_awake.detach();
    return aw;
}

/* Registering the functions:
================================================================================================= */

std::vector<PyMethodDef> module_methods = {
    PyMethodDef{"example_awaitable", example_awaitable, METH_VARARGS, "example awaitable"},
};

int pymod_pre_init(std::vector<PyMethodDef> &methods, PyModuleDef *module_def) {
    DBG_SCOPE();
    for (auto m : module_methods)
        methods.push_back(m);
    return 0;
}

int pymod_post_init(PyObject *m) {
    DBG_SCOPE();
    pymod_register_int_cbk = [](uint64_t, pymod_cbk_wp) {
        DBG("Python registered an int callback");
    };
    pymod_register_str_cbk = [](std::string, pymod_cbk_wp) {
        DBG("Python registered a str callback");
    };
    pymod_unregister_int_cbk = [](pymod_cbk_wp) {
        DBG("Python unregistered an int callback");
    };
    pymod_unregister_str_cbk = [](pymod_cbk_wp) {
        DBG("Python unregistered a str callback");
    };
    return 0;
}
