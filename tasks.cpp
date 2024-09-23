#include "tasks.h"
#include "path_utils.h"

#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <grp.h>
#include <pwd.h>

#define START_DEAD_TIMER 30

struct pmgr_private_task_t {
    pmgr_task_t o;
    int dead_timer = 0;
    bool revive = false;
    bool removing = false;
    co::sem_t closed_sem;
};

using ptask_t = std::shared_ptr<pmgr_private_task_t>;

/* bucket for tasks that don't need to be running now */
static std::vector<ptask_t> dead_tasks;
static std::set<ptask_t> tokill_tasks;
static std::unordered_map<std::string, ptask_t> tasks;
static std::unordered_map<pid_t, ptask_t> pid2task;

extern char **environ;

static bool is_prefix(const std::string& prefix, const std::string& dst) {
    return dst.compare(0, prefix.size(), prefix) == 0;
}

static pid_t exec_task(ptask_t task) {
    bool cutstdio = task->o.flags & PMGR_TASK_FLAG_NOSTDIO;
    bool pwdself = task->o.flags & PMGR_TASK_FLAG_PWDSELF;

    std::string usr = task->o.task_usr;
    std::string grp = task->o.task_grp;

    int uid = getuid();
    int gid = getgid();

    if (usr != "") {
        struct passwd *up = getpwnam(usr.c_str());
        ASSERT_FN(CHK_PTR(up));
        uid = up->pw_uid;
    }
    if (grp != "") {
        struct group *gp = getgrnam(grp.c_str());
        ASSERT_FN(CHK_PTR(gp));
        gid = gp->gr_gid;
    }

    std::vector<std::string> args;
    ASSERT_FN(ssplit_args(task->o.task_path, args));
    if (args.size() == 0 || args[0] == "") {
        DBG("Invalid exec_str");
        return -1;
    }
    std::vector<const char *> argv;
    for (auto& a : args) {
        argv.push_back(a.c_str());
    }
    argv.push_back(NULL);

    std::string pwd = task->o.task_pwd;
    if (pwdself) {
        auto abs_path = path_get_abs(path_get_relative(argv[0]));
        std::size_t found = abs_path.find_last_of("/\\");
        pwd = abs_path.substr(0, found + 1);
    }

    std::vector<const char *> envp;
    bool has_pwd = pwd != "";
    std::string new_pwd = path_get_relative(pwd);
    std::string new_pwd_env = sformat("PWD=%s", new_pwd.c_str());
    for (char **env = environ; *env; env++) {
        if (has_pwd && is_prefix("PWD=", *env)) {
            envp.push_back(new_pwd_env.c_str());
        }
        else {
            envp.push_back(*env);
        }
    }
    envp.push_back(NULL);

    pid_t child_pid = fork();

    if (child_pid == 0) {
        if (has_pwd) {
            if (chdir(new_pwd.c_str()) < 0) {
                DBGE("Failed to change dir");
                kill(getpid(), SIGTERM);
                exit(-1);
            }
        }
        if (cutstdio) {
            close(0);
            close(1);
            close(2);
        }
        if (usr != "") {
            if (setgid(gid) < 0) {
                DBGE("Failed to change user");
                kill(getpid(), SIGTERM);
                exit(-1);
            }
        }
        if (grp != "") {
            if (setuid(uid) < 0) {
                DBGE("Failed to change group");
                kill(getpid(), SIGTERM);
                exit(-1);
            }
        }

        if (execvpe(argv[0], (char * const *)argv.data(), (char * const *)envp.data()) < 0) {
            DBG("Failed to run the process:");
            for (auto arg : argv) {
                DBGE(" > %s", arg);
            }
            kill(getpid(), SIGTERM);
            exit(-1);
        }
    }
    else {
        ASSERT_FN(child_pid);
        return child_pid;
    }
    return 0;
}

static int kill_task(ptask_t task, bool force) {
    if (task->o.state == PMGR_TASK_STATE_STOPPED)
        return 0;
    if (task->o.state == PMGR_TASK_STATE_STOPING)
        return 0;

    if (force) {
        kill(task->o.pid, SIGKILL);
        return 0;
    }
    else {
        ASSERT_FN(kill(task->o.pid, SIGTERM));

        task->o.state = PMGR_TASK_STATE_STOPING;
        task->dead_timer = START_DEAD_TIMER;
        tokill_tasks.insert(task);
        return 0;
    }
}

static void run_task(ptask_t task) {
    if (task->o.state == PMGR_TASK_STATE_RUNNING)
        return ;
    if (task->o.state == PMGR_TASK_STATE_STOPING)
        return ;
    if (task->removing)
        return ;
    pid_t ret = exec_task(task);
    if (ret > 0) {
        task->o.pid = ret;
        task->o.state = PMGR_TASK_STATE_RUNNING;
        task->closed_sem = co::sem_t(0);
        pid2task[ret] = task;
    }
    else if (HAS(tasks, task->o.task_name) && (task->o.flags & PMGR_TASK_FLAG_PERSIST)) {
        dead_tasks.push_back(task);
    }
}

/* start a task */
int tasks_start(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        return -1;
    }
    auto task = tasks[task_name];
    if (task->o.state == PMGR_TASK_STATE_STOPING) {
        task->revive = true;
        return 0;
    }
    else {
        run_task(tasks[task_name]);
        return 0;
    }
}

/* stop a task */
int tasks_stop(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        return -1;
    }
    ASSERT_FN(kill_task(tasks[task_name], false));
    return 0;
}

/* add a task */
int tasks_add(pmgr_task_t *msg) {
    bool valid = false;
    if (msg->list_terminator) {
        DBG("Can't add a terminator task...");
        return -1;
    }
    if (msg->state != PMGR_TASK_STATE_INIT) {
        DBG("Initialy task must be in the init state");
        return -1;
    }
    for (int i = 0; i < PMGR_MAX_TASK_NAME; i++)
        if (msg->task_name[i] == 0)
            valid = true;
    if (!valid) {
        DBG("Name is not terminated, abort")
        return -1;
    }
    valid = false;
    for (int i = 0; i < PMGR_MAX_TASK_PATH; i++)
        if (msg->task_path[i] == 0)
            valid = true;
    if (!valid) {
        DBG("Path is not terminated, abort")
        return -1;
    }
    if (msg->hdr.size != sizeof(pmgr_task_t)) {
        DBG("Invalid task size");
        return -1;
    }
    if (msg->flags & (~PMGR_TASK_FLAG_MASK)) {
        DBG("Unknown/Invalid task flags");
        return -1;
    }
    if (msg->p) {
        DBG("Message can't have internal struct used");
        return -1;
    }
    if (msg->pid) {
        DBG("Pid can't be set before running the task...");
        return -1;
    }
    if (HAS(tasks, msg->task_name)) {
        DBG("Task does already exist: %s", msg->task_name);
        return -1;
    }
    if (std::string(msg->task_name) == "" || std::string(msg->task_path) == "") {
        DBG("Task name[%s] and path[%s] must not be empty", msg->task_name, msg->task_path);
        return -1;
    }

    /* This is here just to do the check */
    std::vector<std::string> args;
    ASSERT_FN(ssplit_args(msg->task_path, args));

    auto task = std::make_shared<pmgr_private_task_t>();
    tasks[msg->task_name] = task;
    task->o = *msg;
    task->o.p = (intptr_t)task.get();

    /* TODO: add flag to enable "run on add" */
    run_task(task);
    return 0;
}

/* remove a task (Obs: it will die in max 30 seconds) */
int tasks_rm(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        return -1;
    }
    if (tasks[task_name]->o.state != PMGR_TASK_STATE_STOPPED) {
        DBG("Process must be stopped to remove");
        return -1;
    }
    tasks.erase(task_name);
    return 0;
}

/* return in 'list' the list of all tasks */
int tasks_list(std::vector<pmgr_task_t>& list) {
    for (auto &[name, task] : tasks) {
        list.push_back(task->o);
    }
    pmgr_task_t terminator {
        .hdr = {
            .size = sizeof(pmgr_task_t),
            .type = PMGR_MSG_REPLAY,
        },
        .list_terminator = true,
    };
    list.push_back(terminator);
    return 0;
}

bool tasks_exists(const std::string& task_name) {
    return HAS(tasks, task_name);
}

static co::task_t co_handle_procs(int sigfd) {
    while (true) {
        struct signalfd_siginfo  fdsi;
        ssize_t ret = co_await co::read(sigfd, &fdsi, sizeof(fdsi));

        ASSERT_ECOFN(ret);
        ASSERT_ECOFN(CHK_BOOL(ret == sizeof(fdsi)));

        if (fdsi.ssi_signo != SIGCHLD) {
            DBG("Unexpected signal...");
            continue ;
        }

        pid_t pid;
        int wstat;

        /* I hate starting and stopping processes with a passion */
        while ((pid = waitpid(-1, &wstat, WNOHANG)) > 0) {
            bool closed_proc = false;
            if (WIFEXITED(wstat)) {
                // DBG("%d exited, status=%d", pid, WEXITSTATUS(wstat));
                closed_proc = true;
            }
            else if (WIFSIGNALED(wstat)) {
                // DBG("%d killed by signal %d", pid, WTERMSIG(wstat));
                closed_proc = true;
            }
            else if (WIFSTOPPED(wstat)) {
                // DBG("%d stopped by signal %d", pid, WSTOPSIG(wstat));
            }
            else if (WIFCONTINUED(wstat)) {
                // DBG("%d continued\n", pid);
            }

            if (closed_proc) {
                auto task = pid2task[pid];

                pid2task.erase(pid);
                task->closed_sem.rel();
                task->o.state = PMGR_TASK_STATE_STOPPED;
                if ((task->o.flags & PMGR_TASK_FLAG_PERSIST) && !task->removing) {
                    dead_tasks.push_back(task);
                }
                if (task->revive && !task->removing) {
                    task->revive = false;
                    dead_tasks.push_back(task);
                }
            }
        }
    }

    co_return 0;
}

co::task_t co_tasks_clear() {
    auto copy_tasks = tasks;
    for (auto &[k, v] : copy_tasks) {
        ASSERT_COFN(co_await co_tasks_waitrm(k));
    }
    co_return 0;
}

co::task_t co_tasks_waitstop(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        co_return -1;
    }
    co_await tasks[task_name]->closed_sem;
    co_return 0;
}

co::task_t co_tasks_waitrm(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        co_return -1;
    }
    tasks[task_name]->removing = true;
    ASSERT_COFN(co_await co_tasks_waitstop(task_name))
    tasks.erase(task_name);
    co_return 0;
}

/* This coroutine handles tasks, their starting/stopping/etc. */
co::task_t co_tasks() {
    int sigfd;
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    ASSERT_ECOFN(sigprocmask(SIG_BLOCK, &mask, NULL));
    ASSERT_ECOFN(sigfd = signalfd(-1, &mask, SFD_CLOEXEC));

    co_await co::sched(co_handle_procs(sigfd));

    while (true) {
        co_await co::sleep_s(1);

        if (dead_tasks.size()) {
            auto save_dt = dead_tasks;
            dead_tasks.clear();
            for (auto &t : save_dt) {
                run_task(t);
            }
        }

        if (tokill_tasks.size()) {
            std::set<ptask_t> removed;
            for (auto t : tokill_tasks) {
                if (t->dead_timer-- < 0) {
                    kill_task(t, true);
                    removed.insert(t);
                }
            }
            for (auto r : removed)
                tokill_tasks.erase(r);
        }
        /* Check the status of all held processes */
        /* Start-Stop processes that need starting-stopping */
    }
}
