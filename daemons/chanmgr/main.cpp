#include <unistd.h> 
#include <sys/stat.h>
#include "procmgr.h"
#include "sys_utils.h"
#include "co_utils.h"
#include "path_utils.h"

struct client_t;
struct channel_t;

using client_p = std::shared_ptr<client_t>;
using client_wp = std::weak_ptr<client_t>;
using channel_p = std::shared_ptr<channel_t>;
using channel_wp = std::weak_ptr<channel_t>;

static std::map<std::string, channel_wp> channels;
static std::map<std::string, std::set<client_wp, std::owner_less<client_wp>>> chan_waiters;

static std::vector <std::pair<client_wp, int64_t>> send_disconnects_vec;
static co::sem_t send_disconnects_sem;

struct channel_t {
    std::string name;
    std::map<int, client_wp> fd2client; /* fd to client */
    std::map<int64_t, client_wp> id2client; /* id to client */

    ~channel_t() {
        channels.erase(name);
    }
};

struct client_t {
    int64_t id;
    int     fd;
    pid_t   pid;

    channel_p ch;
    co::sem_t wait_ch_sem;
    co::sem_t write_sem{1};

    pmgr_task_t task = {};
    pmgr_chann_identity_t ident = {};

    std::set<client_wp, std::owner_less<client_wp>> discon_list;

    ~client_t() {
        if (ch) {
            ch->fd2client.erase(fd);
            ch->id2client.erase(id);
            close(fd);
        }
        for (auto _client : discon_list) {
            send_disconnects_vec.push_back({_client, id});
            send_disconnects_sem.rel();
        }
    }
};

static std::string parent_sock;
static std::string parent_dir;
static int64_t last_client_id = 1;
static int procmgr_fd = -1;

/* we use this to make sure that all our messages arive in one piece at our destination */
co::task_t write_msg(client_p client, const void *data, size_t len) {
    co_await client->write_sem;
    ASSERT_COFN(co_await co::write_sz(client->fd, data, len));
    client->write_sem.rel();
    co_return 0;
}

co::task_t co_send_disconnects() {
    DBG_SCOPE();
    while (true) {
        co_await send_disconnects_sem;

        auto veccpy = send_disconnects_vec;
        send_disconnects_vec.clear();

        for (auto [_client, id] : veccpy) {
            if (client_p client = _client.lock()) {
                pmgr_chann_msg_t msg{
                    .hdr = {
                        .size = sizeof(pmgr_chann_msg_t),
                        .type = PMGR_CHAN_ON_DISCON,
                    },
                    .dst_id = id,
                };
                ASSERT_COFN(co_await write_msg(client, &msg, sizeof(msg)));
            }
        }
    }
    co_return 0;
}

#define VALIDATE_SIZE(hdr, type) { \
    if ((hdr)->size != sizeof(type)) { \
        DBG("Invalid size"); \
        co_return -1; \
    } \
}
co::task_t co_client_messaging(client_p client) {
    pmgr_return_t retmsg{
        .hdr = {
            .size = sizeof(pmgr_return_t),
            .type = PMGR_MSG_RETVAL,
        },
        .retval = 0,
    };

    std::vector<uint8_t> msg_raw;
    channel_p chan;

    while (true) {
        msg_raw.resize(sizeof(int));
        ASSERT_COFN(co_await co::read_sz(client->fd, msg_raw.data(), sizeof(int)));
        chan = client->ch;
        if (!chan) {
            DBG("Received a message without a valid channel registration...");
            client->wait_ch_sem.rel();
            co_return 0;
        }

        /* check for a message limit? */
        int msg_len = *(int *)msg_raw.data();
        if (msg_len < sizeof(pmgr_hdr_t)) {
            DBG("Invalid message size");
            co_return -1;
        }
        msg_raw.resize(msg_len);
        int rest = msg_len - sizeof(int);
        ASSERT_COFN(CHK_BOOL(
                co_await co::read_sz(client->fd, msg_raw.data() + sizeof(int), rest) == rest));

        auto hdr = (pmgr_hdr_t *)msg_raw.data();
        /* we now have a valid message from our peer */
        retmsg.retval = 0;

        switch (hdr->type) {
            case PMGR_CHAN_REGISTER: {
                DBG("Can't register twice...");
                co_return -1;
            } break;
            case PMGR_CHAN_IDENTITY: {
                /* time to ask the parent... */
                VALIDATE_SIZE(hdr, pmgr_chann_identity_t);
                auto msg = (pmgr_chann_identity_t *)hdr;
                if (client->pid != msg->task_pid) {
                    DBG("Lied about pid");
                    co_return -1;
                }
                /* we only check for task identity if the task sent us a name, else we consider it
                an independent process and only check for the  */
                if (msg->task_name[0] != '\0') {
                    /* first ask about the task */
                    msg->hdr.type = PMGR_MSG_GET_PID;
                    ASSERT_COFN(co_await co::write_sz(procmgr_fd, msg, hdr->size));

                    /* second, receive the header and check if it's a message or a return value */
                    pmgr_task_t task;
                    ASSERT_COFN(co_await co::read_sz(procmgr_fd, &task, sizeof(task.hdr)));
                    if (task.hdr.type != PMGR_MSG_ADD) {
                        DBG("Failed to get task");
                        co_return -1;
                    }

                    /* third, read the whole task */
                    ASSERT_COFN(co_await co::read_sz(procmgr_fd, (uint8_t *)&task + sizeof(task.hdr),
                            sizeof(task) - sizeof(task.hdr)));
                    client->task = task; /* valid if header has PMGR_MSG_ADD */

                    if (strncmp(task.task_name, msg->task_name, PMGR_MAX_TASK_NAME) != 0) {
                        DBG("Lied about task name");
                        co_return -1;
                    }
                }
                /* validation done, we now keep the identity */
                client->ident = *msg;
            } break;
            case PMGR_CHAN_MESSAGE: {
                if (hdr->size < sizeof(pmgr_chann_msg_t)) {
                    DBG("Invalid size");
                    co_return -1;
                }
                auto msg = (pmgr_chann_msg_t *)hdr;

                if (msg->flags & PMGR_CHAN_BCAST) {
                    for (auto [_, dst_c] : chan->id2client) {
                        if (co_await write_msg(dst_c.lock(), msg, hdr->size) < 0) {
                            DBG("Failed to send message...");
                            retmsg.retval -= 1;
                        }
                    }
                }
                else {
                    if (HAS(chan->id2client, msg->dst_id)) {
                        auto dst_c = chan->id2client[msg->dst_id];
                        if (co_await write_msg(dst_c.lock(), msg, hdr->size) < 0) {
                            DBG("Failed to send message...");
                            retmsg.retval = -1;
                        }
                    }
                }
                /* message for the channel */
            } break;
            case PMGR_CHAN_SELF: {
                VALIDATE_SIZE(hdr, pmgr_chann_msg_t);
                auto msg = (pmgr_chann_msg_t *)hdr;
                msg->src_id = msg->dst_id = client->id;
                ASSERT_COFN(co_await write_msg(client, msg, hdr->size));
            } break;
            case PMGR_CHAN_GET_IDENT: {
                VALIDATE_SIZE(hdr, pmgr_chann_msg_t);
                auto msg = (pmgr_chann_msg_t *)hdr;
                pmgr_chann_identity_t ident = {};
                if (HAS(chan->id2client, msg->dst_id)) {
                    ident = chan->id2client[msg->dst_id].lock()->ident;
                }
                else {
                    retmsg.retval = -1;
                }
                ASSERT_COFN(co_await write_msg(client, &ident, sizeof(ident)));
            } break;
            case PMGR_CHAN_ON_DISCON: {
                VALIDATE_SIZE(hdr, pmgr_chann_msg_t);
                auto msg = (pmgr_chann_msg_t *)hdr;
                if (HAS(chan->id2client, msg->dst_id)) {
                    client_p dst_c = chan->id2client[msg->dst_id].lock();
                    dst_c->discon_list.insert(client);
                }
                else {
                    retmsg.retval = -1;
                }
            } break;
            case PMGR_CHAN_LIST: {
                for (auto &[id, _] : chan->id2client) {
                    pmgr_chann_msg_t msg {
                        .hdr = {
                            .size = sizeof(pmgr_chann_msg_t),
                            .type = PMGR_CHAN_LIST,
                        },
                        .dst_id = id,
                    };
                    ASSERT_COFN(co_await write_msg(client, &msg, sizeof(msg)));
                }
            } break;
            default: {
                DBG("Unknown message");
                co_return -1;
            }
        }

        /* maybe change it a bit, for example write how many broadcasts succeded, etc. */
        ASSERT_COFN(co_await write_msg(client, &retmsg, retmsg.hdr.size));
    }
    co_return 0;
}

co::task_t co_client_register(int fd, pid_t pid) {
    pmgr_chann_t regmsg;
    pmgr_return_t retmsg{
        .hdr = {
            .size = sizeof(pmgr_return_t),
            .type = PMGR_MSG_RETVAL,
        },
        .retval = 0,
    };
    int ret = co_await co::read_sz(fd, &regmsg, sizeof(regmsg)); /* TODO: timeo? */

    if (ret != sizeof(regmsg)) {
        DBG("Failed to read register message");
        co_return -1;
    }

    if (regmsg.hdr.type != PMGR_CHAN_REGISTER) {
        DBG("Invalid first message");
        co_return -1;
    }

    bool has_null = false;
    for (int i = 0; i < regmsg.chan_name[i]; i++)
        if (!regmsg.chan_name[i]) {
            has_null = true;
            break;
        }
    if (!has_null || !regmsg.chan_name[0]) {
        DBG("Invalid channel name");
        co_return -1;
    }

    client_p client = std::make_shared<client_t>(client_t{
        .id = last_client_id++,
        .fd = fd,
        .pid = pid,
    });

    co_await co::sched(co_client_messaging(client));
    channel_p chan;

    if (HAS(channels, regmsg.chan_name)) {
        chan = channels[regmsg.chan_name].lock();
    }
    if (!chan && (regmsg.flags & PMGR_CHAN_CREAT)) {
        chan = std::make_shared<channel_t>(channel_t{
            .name = regmsg.chan_name,
        });
        channels[regmsg.chan_name] = chan;
    }
    else if (!chan && (regmsg.flags & PMGR_CHAN_WAITC)) {
        chan_waiters[regmsg.chan_name].insert(client_wp(client));
        co_await client->wait_ch_sem;
    }
    else {
        DBG("Failed to get channel");
        co_await co::stopfd(client->fd); /* stop the reading */

        retmsg.retval = -1;
        ASSERT_COFN(co_await co::write_sz(fd, &retmsg, sizeof(retmsg)));
        co_return -1;
    }

    /* awake all the clients that where waiting for the channel to exist */
    for (auto _cp : chan_waiters[chan->name])
        if (client_p cp = _cp.lock())
            cp->wait_ch_sem.rel();
    chan_waiters.erase(chan->name);

    /* add the client to this channel */
    chan->fd2client[fd] = client;
    chan->id2client[fd] = client;
    client->ch = chan;

    /* notify that channel was aquired */
    retmsg.retval = 0;
    ASSERT_COFN(co_await co::write_sz(fd, &retmsg, sizeof(retmsg)));

    co_return 0;
}

co::task_t co_wait_unix() {
    std::string sock_path = parent_dir + PMGR_CHAN_UN_NAME;

    int server_fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, sock_path.c_str());

    ASSERT_ECOFN(server_fd = socket(AF_UNIX, SOCK_STREAM, 0));
    remove(sockaddr_un.sun_path);

    ASSERT_ECOFN(bind(server_fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));
    ASSERT_ECOFN(chmod(sockaddr_un.sun_path, PMGR_CHAN_UN_PERM));

    DBG("unix_server_fd: %d sock_path: %s", server_fd, sock_path.c_str());
    ASSERT_ECOFN(listen(server_fd, 4096));

    while (true) {
        int remote_fd;
        ASSERT_ECOFN(remote_fd = co_await CO_REG(co::accept(server_fd, NULL, NULL)));

        /* TODO: maybe add some privilage checks here? */
        socklen_t cred_len;
        struct ucred ucred;

        cred_len = sizeof(struct ucred);
        if (getsockopt(remote_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &cred_len) == -1) {
            DBG("No ucred...");
            close(remote_fd);
            continue;
        }

        DBG("Connected: path: [%s] pid: %d", path_pid_path(ucred.pid).c_str(), ucred.pid);

        co_await co::sched(co_client_register(remote_fd, ucred.pid));
    }
    co_return 0;
}

co::task_t co_wait_net() {
    int server_fd, opt = 1;
    auto addr = create_sa_ipv4(INADDR_ANY, PMGR_CHAN_TCP_PORT);
    socklen_t addrlen = sizeof(addr);

    ASSERT_ECOFN(server_fd = socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_ECOFN(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)));
    ASSERT_ECOFN(bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)));
    ASSERT_ECOFN(listen(server_fd, 5));

    DBG("IP Socket: %d", server_fd);

    while (true) {
        int remote_fd;
        ASSERT_ECOFN(remote_fd = co_await co::accept(server_fd,
                (struct sockaddr*)&addr, (socklen_t*)&addrlen));

        DBG("%s connected", sa2str(addr).c_str());
        /* Obs: can't get the pid of an remote process */

        co_await co::sched(co_client_register(remote_fd, -1));
    }
    co_return 0;
}

co::task_t co_main() {
    DBG_SCOPE();
    co_await co::sched(CO_REG(co_send_disconnects()));
    co_await co::sched(CO_REG(co_wait_net()));
    co_await co::sched(CO_REG(co_wait_unix()));
    co_return 0;
}

int main(int argc, char const *argv[])
{
    DBG("CHANNEL_MANAGER");
    parent_dir = path_pid_dir(getppid());
    parent_sock = parent_dir + "procmgr.sock";

    ASSERT_FN(procmgr_fd = pmgr_conn_socket(parent_sock.c_str()));

    DBG("post conn");

    co::pool_t pool;

    pool.sched(co_main());
    pool.run();

    /*
        - this creates a socket inside the main's root directory
        - this also listens to a port (maybe set inside the main program's cfg)
        - waits for channels to be created and gives each peer an integer id
        - dispatches messages, from wherever they may come
     */

    return 0;
}
