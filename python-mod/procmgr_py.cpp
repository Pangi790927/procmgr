#include "pymod.h"
#include "debug.h"
#include "time_utils.h"
#include "co_utils.h"
#include "thco.h"
#include "json2pmgr.h"
#include "procmgr.h"

#include <thread>

/* A python application that is a daemon can load this module to help it communicate with the rest
of the procmgr modules. This module will accomplish the task of translating messages from json to
procmgr_* and back.  */

enum awaiter_type_e {
    AWAITER_TYPE_SLEEP_10,
    AWAITER_TYPE_OPEN_CONN,
    AWAITER_TYPE_CLOSE_CONN,
    AWAITER_TYPE_STOP_APP,
    AWAITER_TYPE_WRITE,
    AWAITER_TYPE_READ,
};

struct awaiter_t {
    awaiter_type_e type;
    PyObject *aw = NULL;
    uint64_t time_us = 0;
    int64_t chan_id;
    std::string conn_path;
    std::string json_str;
};

static std::thread event_loop_thread;
static thco_t2c_queue_t<awaiter_t> await_register_queue;
static std::vector<awaiter_t> awaiters;

/* Event loop (Event loop thread)
================================================================================================= */

struct chann_state_t {
    int fd;
};

static int64_t curr_alloc_id = 0;
static std::map<int64_t, std::shared_ptr<chann_state_t>> chan_state;

static int awake_awaiter_exception(PyObject *aw, PyObject *exc) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT_FN(pymod_await_trig(aw, NULL, exc));
    PyGILState_Release(gstate);
    return 0;
}

static int awake_awaiter(PyObject *aw, PyObject *obj) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT_FN(pymod_await_trig(aw, obj));
    PyGILState_Release(gstate);
    return 0;
}

static int awake_awaiter(PyObject *aw, const char *fmt, ...) {
    va_list list;
    va_start(list, fmt);

    PyGILState_STATE gstate = PyGILState_Ensure();
    ASSERT_FN(pymod_await_trig(aw, Py_VaBuildValue(fmt, list)));
    PyGILState_Release(gstate);

    va_end(list);
    return 0;
}

co::task_t co_sleep_10(awaiter_t awaiter) {
    DBG_SCOPE();
    co_await co::sleep_s(10);
    // ASSERT_COFN(awake_awaiter(awaiter.aw, "(sK)", "nomnom", 1102));
    ASSERT_COFN(awake_awaiter_exception(awaiter.aw, NULL));
    co_return 0;
}

co::task_t co_open_conn(awaiter_t awaiter) {
    /* Now it doesn't have any awaiting on the C++ part, later it will have for connecting to some
    channels that need a message before connecting to them */
    FnScope scope;
    scope([&awaiter]{ awake_awaiter_exception(awaiter.aw, NULL); });

    int fd;
    ASSERT_COFN(fd = pmgr_conn_socket(awaiter.conn_path.c_str()));
    scope([fd]{ close(fd); });

    int64_t chan_id = curr_alloc_id++;
    chan_state[chan_id] = std::shared_ptr<chann_state_t>(new chann_state_t{
        .fd = fd,
    });

    ASSERT_COFN(awake_awaiter(awaiter.aw, "K", chan_id));
    scope.disable();
    co_return 0;
}

co::task_t co_close_conn(awaiter_t awaiter) {
    FnScope scope;
    scope([&awaiter]{ awake_awaiter_exception(awaiter.aw, NULL); });

    ASSERT_COFN(CHK_BOOL(awaiter.chan_id >= 0));
    ASSERT_COFN(CHK_BOOL(HAS(chan_state, awaiter.chan_id)));

    auto &state = chan_state[awaiter.chan_id];
    co_await co::stopfd(state->fd);
    close(state->fd);
    chan_state.erase(awaiter.chan_id);

    ASSERT_COFN(awake_awaiter(awaiter.aw, "K", 0));

    scope.disable();
    co_return 0;
}

co::task_t co_write(awaiter_t awaiter) {
    FnScope scope;
    scope([&awaiter]{ awake_awaiter_exception(awaiter.aw, NULL); });

    ASSERT_COFN(CHK_BOOL(awaiter.chan_id >= 0));
    ASSERT_COFN(CHK_BOOL(HAS(chan_state, awaiter.chan_id)));

    auto &state = chan_state[awaiter.chan_id];

    std::shared_ptr<pmgr_hdr_t> msg;
    ASSERT_COFN(json2pmgr(awaiter.json_str, msg));
    ASSERT_COFN(co_await co::write_sz(state->fd, msg.get(), msg->size));

    scope.disable();
    co_return 0;
}

co::task_t co_read(awaiter_t awaiter) {
    FnScope scope;
    scope([&awaiter]{ awake_awaiter_exception(awaiter.aw, NULL); });

    ASSERT_COFN(CHK_BOOL(awaiter.chan_id >= 0));
    ASSERT_COFN(CHK_BOOL(HAS(chan_state, awaiter.chan_id)));

    auto &state = chan_state[awaiter.chan_id];

    int msg_len = 0;
    std::vector<uint8_t> msg_data(sizeof(pmgr_hdr_t));
    ASSERT_COFN(co_await co::read(state->fd, msg_data.data(), sizeof(pmgr_hdr_t)));

    auto hdr = (pmgr_hdr_t *)msg_data.data();
    ASSERT_COFN(CHK_BOOL(hdr->size >= sizeof(pmgr_hdr_t)));

    if (hdr->size > sizeof(pmgr_hdr_t)) {
        msg_data.resize(hdr->size);
        ASSERT_COFN(co_await co::read(state->fd,
                msg_data.data() + sizeof(pmgr_hdr_t), hdr->size - sizeof(pmgr_hdr_t)));
        hdr = (pmgr_hdr_t *)msg_data.data();
    }

    std::string json_str;
    ASSERT_COFN(json2pmgr(hdr, json_str));
    ASSERT_COFN(awake_awaiter(awaiter.aw, "s", json_str.c_str()));
 
    scope.disable();
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
            case AWAITER_TYPE_OPEN_CONN: {
                DBG("AWAITER_TYPE_OPEN_CONN");
                co_await co_open_conn(awaiter);
            }
            break;
            case AWAITER_TYPE_CLOSE_CONN: {
                DBG("AWAITER_TYPE_CLOSE_CONN");
                co_await co_close_conn(awaiter);
            };
            break;
            case AWAITER_TYPE_READ: {
                DBG("AWAITER_TYPE_READ");
                co_await co_read(awaiter);
            };
            break;
            case AWAITER_TYPE_WRITE: {
                DBG("AWAITER_TYPE_WRITE");
                co_await co_write(awaiter);
            };
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

/* In essence we have something like:
    channel/objects -> create integer and keep it in a set
    command/message -> send it to a channel/object as a json
                    <- recv it as a json in a channel and an int as an eventual error
    awaiters for callbacks and stuff 
*/

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

static PyObject *get_defs(PyObject *self, PyObject *args) {
    std::string defs;
    ASSERT_PYFN(json2pmgr_values(defs));
    return PyUnicode_FromString(defs.c_str());
}

static PyObject *connect(PyObject *self, PyObject *args) {
    const char *connstr; /* the unix socket that receives the messages */
    if (!PyArg_ParseTuple(args, "s", &connstr)) {
        DBG("Failed parse args");
        return NULL;
    }
    auto aw = pymod_await_new(NULL);
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_OPEN_CONN,
        .aw = aw,
        .conn_path = connstr,
    });
    return aw;
}

static PyObject *disconnect(PyObject *self, PyObject *args) {
    int64_t chan_id;
    if (!PyArg_ParseTuple(args, "K", &chan_id)) {
        DBG("Failed parse args");
        return NULL;
    }
    auto aw = pymod_await_new(NULL);
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_CLOSE_CONN,
        .aw = aw,
        .chan_id = chan_id,
    });
    return aw;
}

static PyObject *write_msg(PyObject *self, PyObject *args) {
    int64_t chan_id;
    const char *str_msg; /* TODO: accept jsons/dicts and serialize them ourselves */
    if (!PyArg_ParseTuple(args, "Ks", &chan_id, str_msg)) {
        DBG("Failed parse args");
        return NULL;
    }
    auto aw = pymod_await_new(NULL);
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_WRITE,
        .aw = aw,
        .chan_id = chan_id,
        .json_str = str_msg,
    });
    return aw;
}

static PyObject *read_msg(PyObject *self, PyObject *args) {
    int64_t chan_id;
    if (!PyArg_ParseTuple(args, "K", &chan_id)) {
        DBG("Failed parse args");
        return NULL;
    }
    auto aw = pymod_await_new(NULL);
    await_register_queue.push(awaiter_t{
        .type = AWAITER_TYPE_READ,
        .aw = aw,
        .chan_id = chan_id,
    });
    return aw;
}

std::vector<PyMethodDef> module_methods = {
    PyMethodDef{"example_awaitable", example_awaitable, METH_VARARGS, "doc:example awaitable"},
    PyMethodDef{"get_defs", get_defs, METH_VARARGS, "doc:get_defs"},
    PyMethodDef{"connect", connect, METH_VARARGS, "doc:connect"},
    PyMethodDef{"disconnect", disconnect, METH_VARARGS, "doc:disconnect"},
    PyMethodDef{"write_msg", write_msg, METH_VARARGS, "doc:write_msg"},
    PyMethodDef{"read_msg", read_msg, METH_VARARGS, "doc:read_msg"},
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