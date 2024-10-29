#include "tasks.h"
#include "path_utils.h"
#include "cmds.h"

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
    co::sem_t start_sem;
};

using ptask_t = std::shared_ptr<pmgr_private_task_t>;

/* bucket for tasks that don't need to be running now */
static std::vector<ptask_t> dead_tasks;
static std::set<ptask_t> tokill_tasks;
static std::unordered_map<std::string, ptask_t> tasks;
static std::unordered_map<pid_t, ptask_t> pid2task;
static bool shutdown_flag = false;

extern char **environ;

static void trigger_event(pmgr_event_e type, std::string task_name, pid_t pid) {
    pmgr_event_t ev{
        .hdr {
            .size = sizeof(pmgr_event_t),
            .type = PMGR_MSG_EVENT_LOOP,
        },
        .ev_type = type,
        .task_pid = pid,
    };
    strcpy(ev.task_name, task_name.c_str());
    cmds_trigger_event(&ev);
}

static bool is_prefix(const std::string& prefix, const std::string& dst) {
    return dst.compare(0, prefix.size(), prefix) == 0;
}

/* This is kinda of a mess, I couldn't decide what things should do and it shows, TODO: revisit it */
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
    std::string progpath = argv[0];
    if (pwdself) {
        auto abs_path = path_get_abs(path_get_relative(argv[0]));
        std::size_t found = abs_path.find_last_of("/\\");
        pwd = abs_path.substr(0, found + 1);
        progpath = abs_path;
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
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
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

        /* I really, really, really, hate processes, soo, soo, much, I can't stress it enough, how
        much I hate them. For god knows what reason they inherit everything and they piss me off. */
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGHUP);
        sigaddset(&mask, SIGQUIT);
        sigaddset(&mask, SIGPWR);
        ASSERT_FN(sigprocmask(SIG_UNBLOCK, &mask, NULL));

        if (execvpe(progpath.c_str(), (char * const *)argv.data(), (char * const *)envp.data()) < 0) {
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
    if (task->o.state == PMGR_TASK_STATE_STOPPED) {
        DBG("No:stopped");
        return 0;
    }
    if (task->o.state == PMGR_TASK_STATE_STOPING) {
        DBG("No:stopping");
        return 0;
    }

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
        task->start_sem.rel();
        task->closed_sem = co::sem_t(0);
        pid2task[ret] = task;
        DBG("Started: %s[%ld]", task->o.task_name, task->o.pid);
        trigger_event(PMGR_EVENT_TASK_START, task->o.task_name, task->o.pid);
    }
    else if (HAS(tasks, task->o.task_name) && (task->o.flags & PMGR_TASK_FLAG_PERSIST)) {
        dead_tasks.push_back(task);
    }
}

/* start a task */
int tasks_start(const std::string& task_name) {
    if (shutdown_flag) {
        DBG("Can't do that, shuting down...");
        return -1;
    }
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
    if (shutdown_flag) {
        DBG("Can't do that, shuting down...");
        return -1;
    }
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
    task->o.hdr.type = PMGR_MSG_ADD;

    /* TODO: add flag to enable "run on add" */
    if (task->o.flags & PMGR_TASK_FLAG_AUTORUN)
        run_task(task);
    
    trigger_event(PMGR_EVENT_TASK_ADD, task->o.task_name, task->o.pid);
    return 0;
}

/* remove a task */
int tasks_rm(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        return -1;
    }
    auto task = tasks[task_name];
    trigger_event(PMGR_EVENT_TASK_RM, task->o.task_name, task->o.pid);
    task->start_sem.rel();
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

int tasks_get(pid_t pid, pmgr_task_t *task) {
    if (!HAS(pid2task, pid)) {
        DBG("Task pid[%d] doesn't exist", pid);
        return -1;
    }
    *task = pid2task[pid]->o;
    return 0;
}

int tasks_get(std::string name, pmgr_task_t *task) {
    if (!HAS(tasks, name)) {
        DBG("Task[%s] doesn't exist", name.c_str());
        return -1;
    }
    *task = tasks[name]->o;
    return 0;
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
                DBG("%d killed by signal %d", pid, WTERMSIG(wstat));
                closed_proc = true;
            }
            else if (WIFSTOPPED(wstat)) {
                DBG("%d stopped by signal %d", pid, WSTOPSIG(wstat));
            }
            else if (WIFCONTINUED(wstat)) {
                DBG("%d continued\n", pid);
            }

            if (closed_proc) {
                auto task = pid2task[pid];

                pid2task.erase(pid);
                task->closed_sem.rel();
                task->o.state = PMGR_TASK_STATE_STOPPED;
                DBG("Stopped: %s[%ld]", task->o.task_name, task->o.pid);
                trigger_event(PMGR_EVENT_TASK_STOP, task->o.task_name, task->o.pid);
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

co::task_t co_shutdown() {
    if (shutdown_flag) {
        DBG("Double shutdown!?");
        co_return -1;
    }
    shutdown_flag = true;
    ASSERT_COFN(co_await co_tasks_clear());
    co_return 0;
}

co::task_t co_tasks_clear() {
    auto copy_tasks = tasks;
    for (auto &[k, v] : copy_tasks) {
        ASSERT_COFN(co_await co_tasks_waitrm(k));
    }
    co_return 0;
}

co::task_t co_task_waitadd(pmgr_task_t *msg) {
    ASSERT_COFN(tasks_add(msg));
    auto task = tasks[msg->task_name];
    co_await task->start_sem;
    ASSERT_COFN(CHK_BOOL(task->o.pid != 0)); /* this would mean that the process was removed before
                                                starting up */
    co_return 0;
};

co::task_t co_tasks_waitstop(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        co_return -1;
    }
    auto task = tasks[task_name];
    if (task->o.state == PMGR_TASK_STATE_STOPPED)
        co_return 0;
    ASSERT_COFN(tasks_stop(task_name));
    co_await task->closed_sem;
    co_return 0;
}

co::task_t co_tasks_waitrm(const std::string& task_name) {
    if (!HAS(tasks, task_name)) {
        DBG("Task does not exist: %s", task_name.c_str());
        co_return -1;
    }
    auto task = tasks[task_name];
    task->removing = true;
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
