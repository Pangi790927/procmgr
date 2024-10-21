#include "cmds.h"
#include "cfg.h"
#include "procmgr.h"
#include "tasks.h"
#include "sys_utils.h"
#include "path_utils.h"

#include <sys/stat.h>

static int open_socket() {
    int fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, path_get_relative(cfg_get()->sock_path).c_str());

    ASSERT_FN(fd = socket(AF_UNIX, SOCK_STREAM, 0));
    ASSERT_FN(fcntl(fd, F_SETFD, FD_CLOEXEC));

    remove(sockaddr_un.sun_path);

    ASSERT_FN(bind(fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));
    ASSERT_FN(chmod(sockaddr_un.sun_path, cfg_get()->sock_perm));

    return fd;
}

static co::task_t read_msg(int fd, std::vector<uint8_t> &msg, int &len) {
    msg.resize(sizeof(int));
    ASSERT_COFN(CHK_BOOL(co_await co::read_sz(fd, msg.data(), sizeof(int)) == sizeof(int)));

    len = *(int *)msg.data();
    if (len < sizeof(pmgr_hdr_t)) {
        DBG("Invalid message size");
        co_return -1;
    }
    msg.resize(len);
    int rest = len - sizeof(int);
    ASSERT_COFN(CHK_BOOL(co_await co::read_sz(fd, msg.data() + sizeof(int), rest) == rest));
    co_return 0;
}

static void trigger_event(pmgr_event_e type) {
    pmgr_event_t ev{
        .hdr {
            .size = sizeof(pmgr_event_t),
            .type = PMGR_MSG_EVENT_LOOP,
        },
        .ev_type = type,
    };
    strcpy(ev.task_name, "");
    cmds_trigger_event(&ev);
}

#define VALIDATE_SIZE(hdr, type) { \
    if ((hdr)->size != sizeof(type)) { \
        DBG("Invalid size"); \
        co_return -1; \
    } \
}
static co::task_t co_do_cmd(int fd, pmgr_hdr_t *hdr) {
    switch (hdr->type) {
        case PMGR_MSG_STOP: {
            VALIDATE_SIZE(hdr, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("STOP[%s]", msg->task_name);
            ASSERT_COFN(tasks_stop(msg->task_name));
        } break;
        case PMGR_MSG_WAITSTOP: {
            VALIDATE_SIZE(hdr, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("WAITSTOP[%s]", msg->task_name);
            ASSERT_COFN(co_await co_tasks_waitstop(msg->task_name));
        } break;
        case PMGR_MSG_WAITRM: {
            VALIDATE_SIZE(hdr, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("WAITSTOP[%s]", msg->task_name);
            ASSERT_COFN(co_await co_tasks_waitrm(msg->task_name));
        } break;
        case PMGR_MSG_START: {
            VALIDATE_SIZE(hdr, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("START[%s]", msg->task_name);
            ASSERT_COFN(tasks_start(msg->task_name));
        } break;
        case PMGR_MSG_ADD: {
            VALIDATE_SIZE(hdr, pmgr_task_t);
            auto msg = (pmgr_task_t *)hdr;
            ASSERT_COFN(tasks_add(msg));
            DBG("ADDED[%s:%s]", msg->task_name, msg->task_path);
        } break;
        case PMGR_MSG_RM: {
            VALIDATE_SIZE(hdr, pmgr_task_name_t);
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("RM[%s]", msg->task_name);
            ASSERT_COFN(tasks_rm(msg->task_name));
        } break;
        case PMGR_MSG_LIST: {
            DBG("LIST");
            std::vector<pmgr_task_t> tasks;
            ASSERT_COFN(tasks_list(tasks)); /* contains terminator */
            for (auto &t : tasks) {
                ASSERT_COFN(write_sz(fd, &t, sizeof(t)));
            }
        } break;
        case PMGR_MSG_LOAD_CFG: {
            DBG("LOAD CFG");
            /* TODO: */
            trigger_event(PMGR_EVENT_CFG_RELOAD);
        } break;
        case PMGR_MSG_CLEAR: {
            DBG("CLEAR");
            ASSERT_COFN(co_await co_tasks_clear());
            trigger_event(PMGR_EVENT_CLEAR);
        } break;
        case PMGR_MSG_GET_PID: {
            VALIDATE_SIZE(hdr, pmgr_chann_identity_t);
            auto msg = (pmgr_chann_identity_t *)hdr;
            pmgr_task_t task{};
            ASSERT_COFN(tasks_get(msg->task_pid, &task));
            ASSERT_COFN(write_sz(fd, &task, sizeof(task)));
        }
        case PMGR_MSG_GET_NAME: {
            VALIDATE_SIZE(hdr, pmgr_chann_identity_t);
            auto msg = (pmgr_chann_identity_t *)hdr;
            pmgr_task_t task;
            bool has_ending = false;
            for (int i = 0; i < PMGR_MAX_TASK_NAME; i++)
                if (!msg->task_name[i]) {
                    has_ending = true;
                    break;
                }
            ASSERT_COFN(CHK_BOOL(has_ending));
            ASSERT_COFN(tasks_get(msg->task_name, &task));
            ASSERT_COFN(write_sz(fd, &task, sizeof(task)));
        }
        default: {
            DBG("Invalid message received");
        }
    }
    co_return 0;
}

static std::map<pid_t, std::set<int>>       pid2fd[32];
static std::map<std::string, std::set<int>> name2fd[32];

void cmds_trigger_event(pmgr_event_t *ev) {
    /* TODO: take this event and send it to all listeners */
    for (int i = 0; i < 32; i++) {
        if (ev->ev_type & (1 << i)) {
            if (HAS(pid2fd[i], ev->task_pid)) {
                for (int fd : pid2fd[i][ev->task_pid]) {
                    DBG("Send 1 to %d", fd);
                    write_sz(fd, ev, sizeof(*ev));
                }
            }
            if (HAS(name2fd[i], ev->task_name)) {
                for (int fd : name2fd[i][ev->task_name]) {
                    write_sz(fd, ev, sizeof(*ev));
                }
            }
            for (int fd : pid2fd[i][-1]) {
                write_sz(fd, ev, sizeof(*ev));
            }
            for (int fd : name2fd[i][""]) {
                write_sz(fd, ev, sizeof(*ev));
            }
        }
    }
}

co::task_t co_event_registration(int fd) {
    std::set<pid_t> pid_regs[32];
    std::set<std::string> name_regs[32];

    FnScope scope([fd, &pid_regs, &name_regs]{
        close(fd);
        for (int i = 0; i < 32; i++) {
            for (auto &pid : pid_regs[i])
                pid2fd[i][pid].erase(fd);
            for (auto &name : name_regs[i])
                name2fd[i][name].erase(fd);
        }
    });
    while (true) {
        pmgr_event_t ev_reg;
        int ret = co_await co::read_sz(fd, &ev_reg, sizeof(ev_reg));
        ASSERT_COFN(ret);
        if (ret == 0)
            break;

        pmgr_return_t retmsg {
            .hdr = {
                .size = sizeof(pmgr_return_t),
                .type = PMGR_MSG_RETVAL,
            },
            .retval = 0,
        };

        bool invalid_msg = true;
        for (int i = 0; i < PMGR_MAX_TASK_NAME; i++) {
            if (ev_reg.task_name[i] == '\0') {
                invalid_msg = false;
                break;
            }
        }
        if (invalid_msg) {
            DBG("received message was invalid, not null terminated name");
        }
        if (ev_reg.task_pid < -1) {
            DBG("received message was invalid, pid is smaller than -1");
            invalid_msg = true;
        }

        std::string name;
        pid_t pid;
        int32_t ev_type;
        if (!invalid_msg) {
            name = ev_reg.task_name;
            pid = ev_reg.task_pid;
            ev_type = ((int32_t)ev_reg.ev_type) & PMGR_EVENT_MASK;
        }

        if (invalid_msg) {
            retmsg.retval = -2;
        }
        else {
            DBG("fd:%d event registration for events: [%x], flags: [%x] pid: [%d] name: [%s]",
                    fd, ev_reg.ev_type, ev_reg.ev_flags, int(pid), name.c_str());
            for (int32_t i = 0; i < 32; i++) {
                if (ev_type & (1 << i)) {
                    if (ev_reg.ev_flags & PMGR_EVENT_FLAG_PID_FILTER) {
                        pid_regs[i].insert(pid);
                        pid2fd[i][pid].insert(fd);
                    }
                    if (ev_reg.ev_flags & PMGR_EVENT_FLAG_NAME_FILTER) {
                        name_regs[i].insert(name);
                        name2fd[i][name].insert(fd);
                    }
                }
            }
        }

        if (write_sz(fd, &retmsg, sizeof(retmsg)) < 0) {
            DBG("Failed to send return value");
            continue;
        }
    }
    DBG("conndone");
    co_return 0;
}

co::task_t co_cmds() {
    int usock_fd;

    ASSERT_ECOFN(usock_fd = open_socket());
    ASSERT_ECOFN(listen(usock_fd, 4096));

    DBG("Created socket, waiting clients");

    while (true) {
        DBG("usock_fd: %d", usock_fd);
        int remote_fd = co_await co::accept(usock_fd, NULL, NULL);
        ASSERT_ECOFN(remote_fd);

        /* TODO: maybe add some privilage checks here? */
        FnScope close_fd([remote_fd]{ close(remote_fd); });

        socklen_t cred_len;
        struct ucred ucred;

        cred_len = sizeof(struct ucred);
        if (getsockopt(remote_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &cred_len) == -1) {
            DBG("No ucred...");
            continue ;
        }

        DBG("Connected: path: [%s] pid: %d", path_pid_path(ucred.pid).c_str(), ucred.pid);

        int len = 0;
        std::vector<uint8_t> msg;
        if (co_await read_msg(remote_fd, msg, len) < 0) {
            DBG("Failed to receive request");
            continue ;
        }

        DBG("received message");

        auto hdr = (pmgr_hdr_t *)msg.data();
        if (hdr->type == PMGR_MSG_EVENT_LOOP) {
            close_fd.disable();
            co_await co::sched(co_event_registration(remote_fd));
            continue;
        }

        DBG("Got here?");

        /* TODO: return values are bad, only -1, should be more expresive */
        int retval = co_await co_do_cmd(remote_fd, hdr);
        pmgr_return_t retmsg {
            .hdr = {
                .size = sizeof(pmgr_return_t),
                .type = PMGR_MSG_RETVAL,
            },
            .retval = retval,
        };
        if (write_sz(remote_fd, &retmsg, sizeof(retmsg)) < 0) {
            DBG("Failed to send return value");
            continue;
        }
        if (retval < 0) {
            DBG("Failed to execute remote cmd");
            continue ;
        }
    }
    co_return 0;
};