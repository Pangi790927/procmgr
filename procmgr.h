#ifndef PROCMGR_H
#define PROCMGR_H

#include "misc_utils.h"
#include "debug.h"

#include <sys/un.h>
#include <sys/socket.h>

#define PMGR_MAX_TASK_NAME  128
#define PMGR_MAX_TASK_PATH  512
#define PMGR_MAX_TASK_USR   64
#define PMGR_MAX_TASK_GRP   64

/* maybe I will make it configurable later on */
#define PMGR_CHAN_TCP_PORT  7275
#define PMGR_CHAN_UN_NAME   "chanmgr.sock"
#define PMGR_CHAN_UN_PERM   0666

enum pmgr_msg_type_e : int32_t {
    /* --- Requests: --- */

    /* Starts a task if stopped */
    PMGR_MSG_START,

    /* Starts a task if started and waits for it's execution to happen(i.e. to get a pid) */
    PMGR_MSG_WAITSTART,

    /* Stops a task if started */
    PMGR_MSG_STOP,

    /* Stops a task if started and waits for it's stopping */
    PMGR_MSG_WAITSTOP,

    /* Adds a task */
    PMGR_MSG_ADD,

    /* Removes a task and starts it's killing process */
    PMGR_MSG_RM,

    /* Kills a task, waits for it to stop and after that it removes it */
    PMGR_MSG_WAITRM,

    /* Returns a list of processes */
    PMGR_MSG_LIST,

    /* This loads the config from file, restarting those that exist already */
    PMGR_MSG_LOAD_CFG,

    /* This clears all processes */
    PMGR_MSG_CLEAR,

    /* Ask for the information of a specific task (pmgr_chann_identity_t) */
    PMGR_MSG_GET_PID,
    PMGR_MSG_GET_NAME,

    /* Registers or unregisters for an event on a connection, the connection starts with a
    EVENT_LOOP event that is discarded */
    PMGR_MSG_EVENT_LOOP,
    PMGR_MSG_REGISTER_EVENT,
    PMGR_MSG_UNREGISTER_EVENT, /* TODO: this event needs implementing */

    /* --- Responses: ---  */

    /* Replay to the LIST command, multiple of those will be sent, the type is pmgr_task_t and
    the last one will have the 'done' field 0. */
    PMGR_MSG_REPLAY,

    /* Return value of the requested command. Negative values are errors */
    PMGR_MSG_RETVAL,

    /* --- Chann messages (see the chanmgr daemon) --- */

    /* all bellow end with a PMGR_MSG_RETVAL message */
    PMGR_CHAN_REGISTER, /* creates/connects to a channel  */
    PMGR_CHAN_IDENTITY, /* sends own identity to the channel (will be checked) */
    PMGR_CHAN_MESSAGE,  /* sends a message to someone */
    PMGR_CHAN_GET_IDENT,/* get's the identity of the client (pmgr_chann_msg_t) */
    PMGR_CHAN_ON_DISCON,/* asks the channel manager to be notified when a client disconnects */
    PMGR_CHAN_LIST,     /* asks for the list of connected clients up to this point */
    PMGR_CHAN_SELF,     /* asks about own id (pmgr_chann_msg_t, in dst) */
};

enum pmgr_task_state_e : int32_t {
    PMGR_TASK_STATE_INIT = 0,
    PMGR_TASK_STATE_STOPPED,
    PMGR_TASK_STATE_STOPING,
    PMGR_TASK_STATE_RUNNING,
};

enum pmgr_task_flags_e : int32_t {
    PMGR_TASK_FLAG_PERSIST = 1, /* whenever it stops, crashes, ends it will be restarted (1s delay) */
    PMGR_TASK_FLAG_NOSTDIO = 2, /* close sandard io (0, 1, 2) TODO: change this to redirect to null */
    PMGR_TASK_FLAG_PWDSELF = 4, /* working dir is inherited (works with abs paths) */
    PMGR_TASK_FLAG_AUTORUN = 8, /* runs when added to the tasks list */

    PMGR_TASK_FLAG_MASK = 0b1111,  /* This needs to be kept actualized */
};

enum pmgr_event_e : int32_t {
    PMGR_EVENT_TASK_START = 1,
    PMGR_EVENT_TASK_STOP  = 2,
    PMGR_EVENT_TASK_ADD   = 4,
    PMGR_EVENT_TASK_RM    = 8,
    PMGR_EVENT_CFG_RELOAD = 16,
    PMGR_EVENT_CLEAR      = 32,

    PMGR_EVENT_MASK = 0b111111, /* This needs to be kept actualized */
};

enum pmgr_event_flags_e : int32_t {
    PMGR_EVENT_FLAG_PID_FILTER = 1,     /* has to have this pid */
    PMGR_EVENT_FLAG_NAME_FILTER = 2,    /* has to have this name */

    PMGR_EVENT_FLAGS_MASK = 0b11, /* This needs to be kept actualized */
};

enum pmgr_chan_flags_e : int32_t {
    PMGR_CHAN_CREAT = 1,    /* creates channel if it doesn't exist */
    PMGR_CHAN_WAITC = 2,    /* wait for channel to be created */
    PMGR_CHAN_BCAST = 4,    /* broadcasts message inside the channel */
};

struct PACKED_STRUCT pmgr_hdr_t {
    int32_t size;               /* size of the whole message */
    pmgr_msg_type_e type;
};

/* message to register a channel */
struct PACKED_STRUCT pmgr_chann_t {
    pmgr_hdr_t          hdr;

    pmgr_chan_flags_e flags;
    char chan_name[PMGR_MAX_TASK_NAME];
};

/* message to send content */
struct PACKED_STRUCT pmgr_chann_msg_t {
    pmgr_hdr_t hdr;

    pmgr_chan_flags_e flags;
    int64_t src_id; /* the internal id of the sender (channel-wise) */
    int64_t dst_id; /* the internal id of the destination (channel-wise) */

    /* content: The rest of the message will stay here */
};

/* message to send identity of a connected client */
struct PACKED_STRUCT pmgr_chann_identity_t {
    pmgr_hdr_t          hdr;

    char                task_name[PMGR_MAX_TASK_NAME];
    int64_t             task_pid;
};

struct PACKED_STRUCT pmgr_event_t {
    pmgr_hdr_t          hdr;

    pmgr_event_e        ev_type;
    pmgr_event_flags_e  ev_flags;

    char                task_name[PMGR_MAX_TASK_NAME];
    int64_t             task_pid;
};

struct PACKED_STRUCT pmgr_task_name_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME]; /* identificator of a task */
};

/* TODO: fix too much useless copy and random dimensions */
struct PACKED_STRUCT pmgr_task_t {
    pmgr_hdr_t hdr;

    /* internal private pointer, not a smart pointer, as other languages don't have them */
    uint64_t p;

    uint64_t pid;

    pmgr_task_state_e state;
    pmgr_task_flags_e flags;

    int32_t list_terminator; /* This is a terminator for transfering lists of tasks */

    /* unique task name, the user MUST provide one */
    char task_name[PMGR_MAX_TASK_NAME];

    /* task power working directory */
    char task_pwd[PMGR_MAX_TASK_PATH];

    /* user and group */
    char task_usr[PMGR_MAX_TASK_USR];
    char task_grp[PMGR_MAX_TASK_GRP];

    /* the whole task, path and args included, this is placed last in this struct because we may
    want to make it expandable in the future */
    char task_path[PMGR_MAX_TASK_PATH];
};

struct PACKED_STRUCT pmgr_return_t {
    pmgr_hdr_t hdr;
    int32_t retval;
};

int pmgr_conn_socket(const char *sock_path);


/* IMPLEMENTATION:
================================================================================================= */

inline int pmgr_conn_socket(const char *sock_path) {
    int fd;
    struct sockaddr_un sockaddr_un = {0};

    sockaddr_un.sun_family = AF_UNIX;
    strcpy(sockaddr_un.sun_path, sock_path);

    ASSERT_FN(fd = socket(AF_UNIX, SOCK_STREAM, 0));
       
    ASSERT_FN(connect(fd, (struct sockaddr *) &sockaddr_un, sizeof(struct sockaddr_un)));
    return fd;
}

#endif
