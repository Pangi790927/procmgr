#define JSON_DIAGNOSTICS 1

#include <fstream>
#include <sys/un.h>

#include "debug.h"
#include "co_utils.h"
#include "json.h"
#include "path_utils.h"
#include "procmgr.h"
#include "sys_utils.h"

#define CONFIG_PATH "procmgr.json"

struct config_t {
    std::string sock_path;
};

struct task_t {

};

static config_t cfg;

/* TODO: add global channel socket */

static int read_config() {
    using namespace nlohmann;
    auto cfg_path = path_get_relative(CONFIG_PATH);
    std::ifstream ifile(cfg_path.c_str());
    if (!ifile.good()) {
        DBG("Failed to open the config file");
        return -1;
    }
    try {
        std::string scfg((std::istreambuf_iterator<char>(ifile)),
                          std::istreambuf_iterator<char>());
        json jcfg = json::parse(scfg, nullptr, true, true);

        cfg.sock_path = jcfg["sock_path"];
    }
    catch (json::exception& e) {
        DBG("Config error: %s", e.what());
        return -1;
    }
    return 0;
}

static int open_socket() {
    int fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, cfg.sock_path.c_str());

    ASSERT_FN(fd = socket(AF_UNIX, SOCK_STREAM, 0));
    remove(sockaddr_un.sun_path);

    ASSERT_FN(bind(fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));

    return fd;
}

static int conn_socket() {
    int fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, cfg.sock_path.c_str());

    ASSERT_FN(fd = socket(AF_UNIX, SOCK_STREAM, 0));
       
    ASSERT_FN(connect(fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));
    return fd;
}

co::task_t read_msg(int fd, std::vector<uint8_t> &msg, int &len) {
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

co::task_t co_sock() {
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
        switch (hdr->type) {
            case PMGR_MSG_STOP: {
                auto msg = (pmgr_msg_stop_t *)hdr;
                DBG("STOP[%s]", msg->task_name);
            } break;
            case PMGR_MSG_START: {
                auto msg = (pmgr_msg_start_t *)hdr;
                DBG("START[%s]", msg->task_name);
            } break;
            case PMGR_MSG_ADD: {
                auto msg = (pmgr_msg_add_t *)hdr;
                DBG("ADD[%s:%s]", msg->task_name, msg->task_path);
            } break;
            case PMGR_MSG_RM: {
                auto msg = (pmgr_msg_rm_t *)hdr;
                DBG("RM[%s]", msg->task_name);
            } break;
            case PMGR_MSG_LIST: {
                auto msg = (pmgr_msg_list_t *)hdr;
                DBG("LIST");
            } break;
            case PMGR_MSG_RELOAD: {
                auto msg = (pmgr_msg_reload_t *)hdr;
                DBG("RELOAD");
            } break;
            default: {
                DBG("Invalid message received");
            }
        }
    }
    co_return 0;
};

co::task_t co_main(std::function<std::string(int)> args) {
    co_await co::sched(co_sock());

    while (true) {
        co_await co::sleep_s(1);
    }
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

    ASSERT_FN(read_config());

    if (usage == "daemon" || usage == "d") {
        DBG("Daemon...");
        co::pool_t pool;

        pool.sched(co_main(arg));
        ASSERT_FN(pool.run());
    }
    else if (usage == "ctrl" || usage == "c") {
        DBG("Ctrl...");
        int server_fd;

        if ((server_fd = conn_socket()) < 0) {
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
            pmgr_msg_start_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_start_t),
                    .type = PMGR_MSG_START,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "stop") {
            pmgr_msg_stop_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_stop_t),
                    .type = PMGR_MSG_STOP,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "add") {
            pmgr_msg_add_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_add_t),
                    .type = PMGR_MSG_ADD,
                },
            };
            strcpy(msg.task_name, task.c_str());
            strcpy(msg.task_path, path.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "rm") {
            pmgr_msg_rm_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_rm_t),
                    .type = PMGR_MSG_RM,
                },
            };
            strcpy(msg.task_name, task.c_str());

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "list") {
            pmgr_msg_list_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_list_t),
                    .type = PMGR_MSG_LIST,
                },
            };

            /* wait for response list */

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else if (usage == "reload") {
            pmgr_msg_reload_t msg{
                .hdr = {
                    .size = sizeof(pmgr_msg_reload_t),
                    .type = PMGR_MSG_RELOAD,
                },
            };

            ASSERT_FN(write_sz(server_fd, &msg, sizeof(msg)));
        }
        else {
            DBG("Invalid ctrl seq: %s", usage.c_str());
        }

        /* TOOD: option reading and do stuff */
        /* TODO: commands:
            - reload <daemon>
            - stop <daemon>
            - start <daeomon>
            - add <daemon> <daemon_path> [optional when]
            - rm <daemon>
            - list [optional level]
        */

        close(server_fd);
    }
    else {
        DBG("Unknown usage type[%s]", usage.c_str());
        return -1;
    }
    /* code */
    return 0;
}
