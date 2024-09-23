#define JSON_DIAGNOSTICS 1

#include <sys/un.h>

#include "debug.h"
#include "co_utils.h"
#include "procmgr.h"
#include "sys_utils.h"

#include "cmds.h"
#include "tasks.h"
#include "cfg.h"

/* TODO:
    - add channels
    - add librarry
    - add mutexes?
 */

co::task_t co_waitexit(int sigfd) {
    while (true) {
        struct signalfd_siginfo  fdsi;
        ssize_t ret = co_await co::read(sigfd, &fdsi, sizeof(fdsi));

        ASSERT_ECOFN(ret);
        ASSERT_ECOFN(CHK_BOOL(ret == sizeof(fdsi)));

        DBG("Quit");
        co_await co::force_stop(0);
    }
    co_return 0;
}

co::task_t co_main(std::function<std::string(int)> args) {
    int sigfd;
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    ASSERT_ECOFN(sigprocmask(SIG_BLOCK, &mask, NULL));
    ASSERT_ECOFN(sigfd = signalfd(-1, &mask, SFD_CLOEXEC));

    co_await co::sched(co_waitexit(sigfd));
    co_await co::sched(co_cmds());
    co_await co::sched(co_tasks());

    co_return 0;
};

int main(int argc, char const *argv[])
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++)
        args.push_back(argv[i]);
    auto arg = [&args](int i) {
        if (i >= 0 && i < args.size())
            return args[i];
        return std::string("");
    };
    std::string prog_name   = arg(0);
    std::string usage       = arg(1);

    ASSERT_FN(cfg_read());

    if (usage == "daemon" || usage == "d") {
        co::pool_t pool;

        for (auto &t : cfg_get()->tasks)
            ASSERT_FN(tasks_add(&t));

        pool.sched(co_main(arg));
        ASSERT_FN(pool.run());
    }
    else if (usage == "ctrl" || usage == "c") {
        int server_fd;

        if ((server_fd = pmgr_conn_socket(cfg_get()->sock_path.c_str())) < 0) {
            DBG("Can't connect to procmgr, exiting");
            return -1;
        }

        usage = arg(2);
        std::string task = arg(3);
        std::string path = arg(4);

        if (task.size() + 1 >= PMGR_MAX_TASK_NAME) {
            DBG("task name too long");
            return -1;
        }
        if (path.size() + 1 >= PMGR_MAX_TASK_PATH) {
            DBG("path too long");
            return -1;
        }

        if (usage == "start") {
            pmgr_task_name_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_name_t),
                    .type = PMGR_MSG_START,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "stop") {
            pmgr_task_name_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_name_t),
                    .type = PMGR_MSG_STOP,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "wstop") {
            pmgr_task_name_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_name_t),
                    .type = PMGR_MSG_WAITSTOP,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "add") {
            pmgr_task_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_t),
                    .type = PMGR_MSG_ADD,
                },
            };
            strcpy(msg.task_name, task.c_str());
            strcpy(msg.task_path, path.c_str());

            int flags = 0;
            for (int i = 5; i < args.size(); i++) {
                if (arg(i) == "-p") {
                    flags |= PMGR_TASK_FLAG_PERSIST;
                }
            }
            msg.flags = (pmgr_task_flags_e)flags;

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "rm") {
            pmgr_task_name_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_name_t),
                    .type = PMGR_MSG_RM,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "list") {
            pmgr_hdr_t msg{
                .size = sizeof(pmgr_hdr_t),
                .type = PMGR_MSG_LIST,
            };

            /* wait for response list */

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));

            while (true) {
                pmgr_task_t reply;
                ASSERT_FN(read_sz(server_fd, &reply, sizeof(reply)));

                if (reply.list_terminator)
                    break;

                DBG("TASK:[%s] -> PATH:[%s]", reply.task_name, reply.task_path);
            }
        }
        else if (usage == "load") {
            pmgr_hdr_t msg{
                .size = sizeof(pmgr_hdr_t),
                .type = PMGR_MSG_LOAD_CFG,
            };

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "clear") {
            pmgr_hdr_t msg{
                .size = sizeof(pmgr_hdr_t),
                .type = PMGR_MSG_CLEAR,
            };

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else {
            DBG("Invalid ctrl seq: %s", usage.c_str());
            close(server_fd);
            return -1;
        }

        pmgr_return_t retmsg;
        ASSERT_FN(read_sz(server_fd, &retmsg, sizeof(retmsg)));
        ASSERT_FN(retmsg.retval);

        close(server_fd);
    }
    else {
        DBG("Unknown usage type[%s]", usage.c_str());
        return -1;
    }
    /* code */
    return 0;
}
