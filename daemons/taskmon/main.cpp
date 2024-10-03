#include <unistd.h> 
#include "procmgr.h"
#include "sys_utils.h"
#include "co_utils.h"
#include "path_utils.h"

const char *ev2str(pmgr_event_e ev_type) {
    switch (ev_type) {
        case PMGR_EVENT_TASK_START: return "PMGR_EVENT_TASK_START";
        case PMGR_EVENT_TASK_STOP: return "PMGR_EVENT_TASK_STOP";
        case PMGR_EVENT_TASK_ADD: return "PMGR_EVENT_TASK_ADD";
        case PMGR_EVENT_TASK_RM: return "PMGR_EVENT_TASK_RM";
        case PMGR_EVENT_CFG_RELOAD: return "PMGR_EVENT_CFG_RELOAD";
        case PMGR_EVENT_CLEAR: return "PMGR_EVENT_CLEAR";
        default: return "PMGR_EVENT_[UNKNOWN]";
    }
}

co::task_t co_ev_recver(int fd) {
    pmgr_event_t evt;
    while (true) {
        ASSERT_ECOFN(co_await co::read_sz(fd, &evt, sizeof(evt)));
        DBG("Receivent: %x[%s] -> %s[%d]", evt.ev_type, ev2str(evt.ev_type),
                evt.task_name, (int)evt.task_pid);
    }
    co_return 0;
};


int main(int argc, char const *argv[])
{
    DBG("TASK_MONITOR");

    std::string cfg_str = path_pid_dir(getppid()) + "procmgr.json";
    std::string sock_str = path_pid_dir(getppid()) + "procmgr.sock";
    DBG("Parrent config: %s", (cfg_str).c_str());
    DBG("Parrent socket: %s", (sock_str).c_str());

    int fd;
    ASSERT_FN(fd = pmgr_conn_socket(sock_str.c_str()));
    FnScope scope([fd]{ close(fd); });

    pmgr_hdr_t evstart {
        .size = sizeof(pmgr_hdr_t),
        .type = PMGR_MSG_EVENT_LOOP,
    };
    ASSERT_FN(write_sz(fd, &evstart, sizeof(evstart)));

    DBG("sent evt start");
    pmgr_event_t ev_reg {
        .hdr = {
            .size = sizeof(pmgr_event_t),
            .type = PMGR_MSG_EVENT_LOOP,
        },
        .ev_type = PMGR_EVENT_MASK,
        .ev_flags = PMGR_EVENT_FLAGS_MASK,
        .task_pid = -1,
    };
    strcpy(ev_reg.task_name, "[none]");
    ASSERT_FN(write_sz(fd, &ev_reg, sizeof(ev_reg)));
    DBG("sent registration cmd");

    pmgr_return_t ret;
    ASSERT_FN(read_sz(fd, &ret, sizeof(ret)));
    ASSERT_FN(ret.retval);

    DBG("Now waiting for events...");

    co::pool_t pool;

    pool.sched(co_ev_recver(fd));
    pool.run();

    return 0;
}
