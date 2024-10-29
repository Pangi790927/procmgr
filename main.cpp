#define JSON_DIAGNOSTICS 1

#include <sys/un.h>
#include <sys/stat.h>

#include "debug.h"
#include "co_utils.h"
#include "procmgr.h"
#include "sys_utils.h"

#include "cmds.h"
#include "tasks.h"
#include "cfg.h"
#include "path_utils.h"

/* TODO:
    - add a way to save runtime tasks, maybe in some sort of intermediary config?
        - maybe adding events would make this obsolete, as some sort of process could do this without
        breaking

    some generic daemons:
        - scheduler -> a daemon that has info about when things should run, that can receive tasks
                       from remote sources and works with the manager to keep those tasks running
                       also has a list of processes that is kept saved on disk

    some daemons that I know I want to move to this thing:
        - alarm clock
        - eve trader
        - temp monitor
        - connector with some sort of imgui interface
        - file syncer
        - server holder
        - watcher for all of those, so I know they work
        - some sort of updater or plugin installer (such that I can't break all other aps while
          installing a daemon)

    Better output: 
        1. Create a pipe for all outputs:
            - err fd
            - out fd
        2. Replace default outputs with those (dup2)
        3. Create coro to handle them as such:
            - have circular buffer of 1M
            - accept on un socket connections for each of those that consumes from pipes and
            sends to users
            - sends to users based on newline
            - have a cursor for each accepted process and a default at max buff end cursor for
            newcomers
*/

static int redir_old_out = -1;
static int redir_old_err = -1;
static int redir_read_end = -1;
static int redir_write_end = 1;
static std::atomic<bool> stop_redir_thread = false;
static std::atomic<bool> force_stop_redir_thread = false;

static std::thread redir_th;

/* don't want this dependent on the coro, mostly because if the coro is blocked, then no out gets
out */
int th_redirect_out() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGPWR);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL); /* we don't want to get those signals here, but in the
                                            handle bellow */

    FnScope scope;

    int file_fd = open(path_get_relative("procmgr.std-err-out").c_str(),
            O_CREAT | O_RDWR | O_CLOEXEC | O_TRUNC, 0666);

    auto errmsg = [&file_fd](const std::string &msg) {
        if (redir_old_out >= 0)
            dprintf(redir_old_out, "[th_redirect_out] %s %s[%d]\n",
                    msg.c_str(), strerror(errno), errno);
        if (file_fd >= 0)
            dprintf(      file_fd, "[th_redirect_out] %s %s[%d]\n",
                    msg.c_str(), strerror(errno), errno);
    };

    if (file_fd < 0) {
        errmsg("Failed to open file");
        return -1;
    }
    scope([file_fd]{ close(file_fd); });
    scope([]{
        /* This means that the handler didn't catch this up */
        if (force_stop_redir_thread)
            exit(-1);
    });

    char buff[1024];
    while (!stop_redir_thread) {
        int ret = read(redir_read_end, buff, sizeof(buff));
        if (ret < 0 /*&& !stop_redir_thread*/) {
            errmsg("Failed read input");
            return -1;
        }

        /* TODO: trim the output after writing too much */
        /* we know redirect out,err to a file and to the old out */
        if (redir_old_out >= 0) {
            if (write_sz(redir_old_out, buff, ret) < 0) {
                errmsg("Failed to write to old output");
                return -1;
            }
        }
        if (write_sz(file_fd, buff, ret) < 0) {
            errmsg("Failed to write to file");
            return -1;
        }
    }
    return 0;
}

void redir_stop() {
    sleep_ms(100);
    fflush(stdout);
    fflush(stderr);
    stop_redir_thread = true;
    printf("DONE SIGNAL\n");
    fflush(stderr);
    fflush(stdout);
    if (redir_th.joinable())
        redir_th.join();
    dprintf(redir_old_out, "[DONE REDIR]\n");
}

int init_redirect_out() {
    int redirect[2] = { -1, -1 };

    ASSERT_FN(pipe2(redirect, O_CLOEXEC));
    redir_read_end = redirect[0];
    int redir_write_end = redirect[1];

    fflush(stderr);
    fflush(stdout);

    /* we are also redirecting the output to the old output */
    ASSERT_FN(redir_old_out = fcntl(fileno(stdout), F_DUPFD_CLOEXEC, 0));
    ASSERT_FN(redir_old_err = fcntl(fileno(stderr), F_DUPFD_CLOEXEC, 0));

    if (dup2(redir_write_end, fileno(stdout)) < 0) {
        dprintf(redir_old_out, "Failed to dup stdout: %s\n", strerror(errno));
        return -1;
    }
    if (dup2(redir_write_end, fileno(stderr)) < 0)  {
        dprintf(redir_old_out, "Failed to dup stderr: %s\n", strerror(errno));
        return -1;
    }

    /* good boy chatgpt, it seems those change the buffering mode on a pipe, ffs */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    fflush(stdout);
    fflush(stderr);

    redir_th = std::thread(th_redirect_out);

    return 0;
}

co::task_t co_waitexit(int sigfd) {
    while (true) {
        struct signalfd_siginfo  fdsi;
        ssize_t ret = co_await co::read(sigfd, &fdsi, sizeof(fdsi));

        ASSERT_ECOFN(ret);
        ASSERT_ECOFN(CHK_BOOL(ret == sizeof(fdsi)));

        DBG("Quit");
        dprintf(redir_old_out, "QUIT-PRINT\n");
        co_await co_shutdown();
        co_await co::force_stop(0);
    }
    co_return 0;
}

co::task_t co_main(std::function<std::string(int)> args) {
    int sigfd;
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGPWR);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    ASSERT_ECOFN(sigprocmask(SIG_BLOCK, &mask, NULL));
    ASSERT_ECOFN(sigfd = signalfd(-1, &mask, SFD_CLOEXEC));

    co_await co::sched(co_waitexit(sigfd));
    co_await co::sched(co_cmds());
    co_await co::sched(co_tasks(redir_write_end));

    co_return 0;
};

int main(int argc, char const *argv[])
{
    umask(0);
    ASSERT_FN(init_redirect_out());
    FnScope scope([]{ redir_stop(); });

    DBG("This message, I should see it in logs");

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
    else {
        int server_fd;

        if ((server_fd = pmgr_conn_socket(path_get_relative(cfg_get()->sock_path).c_str())) < 0) {
            DBG("Can't connect to procmgr, exiting");
            return -1;
        }

        usage = arg(1);
        std::string task = arg(2);
        std::string path = arg(3);

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
        if (usage == "wstart") {
            pmgr_task_name_t msg{
                .hdr = {
                    .size = sizeof(pmgr_task_name_t),
                    .type = PMGR_MSG_WAITSTART,
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
            strcpy(msg.task_pwd, "");
            strcpy(msg.task_path, path.c_str());

            int flags = 0;
            for (int i = 4; i < args.size(); i++) {
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
    return 0;
}
