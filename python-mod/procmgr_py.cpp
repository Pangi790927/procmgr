#include "pymod.h"
#include "debug.h"
#include "json.h"
#include "time_utils.h"
#include "co_utils.h"
#include "thco.h"

#include <thread>

/* A python application that is a daemon can load this module to help it communicate with the rest
of the procmgr modules. This module will accomplish the task of translating messages from json to
procmgr_* and back.  */

enum awaiter_type_e {
    AWAITER_TYPE_SLEEP_10,
    AWAITER_TYPE_STOP_APP,
};

struct awaiter_t {
    awaiter_type_e type;
    PyObject *aw = NULL;
    uint64_t time_us = 0;
};

static std::thread event_loop_thread;
static thco_t2c_queue_t<awaiter_t> await_register_queue;
static std::vector<awaiter_t> awaiters;

/* Event registration (Python thread)
================================================================================================= */

static PyObject *example_awaitable(PyObject *self, PyObject *args) {
    DBG_SCOPE();
    PyObject *ctx;
    if (!PyArg_ParseTuple(args, "O", &ctx)) {
        DBG("Failed parse args");
        return NULL;
    }
    PyObject *aw = pymod_await_new(ctx);
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_SLEEP_10,
        .aw = aw,
        .time_us = get_time_us(),
    });

    return aw;
}

/* Event loop (Event loop thread)
================================================================================================= */

static int awake_awaiter(PyObject *aw, const std::string& strval, int64_t intval) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT_FN(pymod_await_trig(aw, strval, intval));
    PyGILState_Release(gstate);
    return 0;
}

co::task_t co_sleep_10(awaiter_t awaiter) {
    DBG_SCOPE();
    co_await co::sleep_s(10);
    awake_awaiter(awaiter.aw, "nomnom", 1102);
    co_return 0;
}

/* This corutine waits for the awaiters to come and dispatches them to different waiting coros */
co::task_t co_await_register() {
    while (true) {
        awaiter_t awaiter;
        co_await await_register_queue.pop(awaiter);

        switch (awaiter.type) {
            case AWAITER_TYPE_SLEEP_10: {
                DBG("AWAITER_TYPE_SLEEP_10");
                co_await co::sched(co_sleep_10(awaiter));
            }
            break;
            case AWAITER_TYPE_STOP_APP: {
                DBG("AWAITER_TYPE_STOP_APP");
                co_await co::force_stop(0);
            }
            break;
            default: {
                DBG("This can't happen");
            }
        }
    }
    co_return 0;
}

co::task_t co_event_loop() {
    co_await co::sched(co_await_register());
    while (true) {
        co_await co::sleep_s(3);
        DBG("looopapool");
    }
    co_return 0;
}

/* Function registration (Python thread:)
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

    event_loop_thread = std::thread([]{
        co::pool_t pool;

        pool.sched(co_event_loop());
        pool.run();
    });

    return 0;
}

void pymod_uninit() {
    DBG_SCOPE();
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_STOP_APP,
    });
    if (event_loop_thread.joinable())
        event_loop_thread.join();
}