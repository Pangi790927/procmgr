#include "cmds.h"
#include "cfg.h"
#include "procmgr.h"
#include "tasks.h"
#include "sys_utils.h"

static int open_socket() {
    int fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, cfg_get()->sock_path.c_str());

    ASSERT_FN(fd = socket(AF_UNIX, SOCK_STREAM, 0));
    ASSERT_FN(fcntl(fd, F_SETFD, FD_CLOEXEC));
    remove(sockaddr_un.sun_path);

    ASSERT_FN(bind(fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));

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

static co::task_t co_do_cmd(int fd, pmgr_hdr_t *hdr) {
    switch (hdr->type) {
        case PMGR_MSG_STOP: {
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("STOP[%s]", msg->task_name);
            ASSERT_COFN(tasks_stop(msg->task_name));
        } break;
        case PMGR_MSG_WAITSTOP: {
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("WAITSTOP[%s]", msg->task_name);
            ASSERT_COFN(co_await co_tasks_waitstop(msg->task_name));
        } break;
        case PMGR_MSG_WAITRM: {
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("WAITSTOP[%s]", msg->task_name);
            ASSERT_COFN(co_await co_tasks_waitrm(msg->task_name));
        } break;
        case PMGR_MSG_START: {
            auto msg = (pmgr_task_name_t *)hdr;
            DBG("START[%s]", msg->task_name);
            ASSERT_COFN(tasks_start(msg->task_name));
        } break;
        case PMGR_MSG_ADD: {
            auto msg = (pmgr_task_t *)hdr;
            ASSERT_COFN(tasks_add(msg));
            DBG("ADDED[%s:%s]", msg->task_name, msg->task_path);
        } break;
        case PMGR_MSG_RM: {
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
        } break;
        case PMGR_MSG_CLEAR: {
            DBG("CLEAR");
            ASSERT_COFN(co_await co_tasks_clear());
        } break;
        default: {
            DBG("Invalid message received");
        }
    }
    co_return 0;
}

co::task_t co_cmds() {
    int usock_fd;

    ASSERT_ECOFN(usock_fd = open_socket());
    ASSERT_ECOFN(listen(usock_fd, 4096));

    DBG("Created socket, waiting clients");

    while (true) {
        int remote_fd = co_await co::accept(usock_fd, NULL, NULL);
        if (remote_fd <= 0) {
            DBG("Invalid connection");
            continue ;
        }
        /* TODO: maybe add some privilage checks here? */

        DBG("Client connected");

        int len = 0;
        std::vector<uint8_t> msg;
        if (co_await read_msg(remote_fd, msg, len) < 0) {
            DBG("Failed to receive request");
            continue ;
        }

        DBG("received message");

        auto hdr = (pmgr_hdr_t *)msg.data();

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