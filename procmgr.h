#ifndef PROCMGR_H
#define PROCMGR_H

#include "misc_utils.h"
#include "debug.h"

#include <sys/un.h>
#include <sys/socket.h>

#define PMGR_MAX_TASK_NAME 128
#define PMGR_MAX_TASK_PATH 512

enum pmgr_msg_type_e : int32_t {
    /* --- Requests: --- */

    /* Starts a task if stopped */
    PMGR_MSG_START,

    /* Stops a task if started */
    PMGR_MSG_STOP,

    /* Stops a task if started and waits for it's stopping */
    PMGR_MSG_WAITSTOP,

    /* Adds a task */
    PMGR_MSG_ADD,

    /* Removes a task and kills it if it is running */
    PMGR_MSG_RM,

    /* As waitstop */
    PMGR_MSG_WAITRM,

    /* Returns a list of processes */
    PMGR_MSG_LIST,

    /* This loads the config from file, restarting those that exist already */
    PMGR_MSG_LOAD_CFG,

    /* This clears all processes */
    PMGR_MSG_CLEAR,

    /* --- Responses: ---  */

    /* Replay to the LIST command, multiple of those will be sent, the type is pmgr_task_t and
    the last one will have the 'done' field 0. */
    PMGR_MSG_REPLAY,

    /* Return value of the requested command. Negative values are errors */
    PMGR_MSG_RETVAL,
};

enum pmgr_task_state_e : int32_t {
    PMGR_TASK_STATE_INIT = 0,
    PMGR_TASK_STATE_STOPPED,
    PMGR_TASK_STATE_STOPING,
    PMGR_TASK_STATE_RUNNING,
};

enum pmgr_task_flags_e : int32_t {
    PMGR_TASK_FLAG_PERSIST = 1,

    PMGR_TASK_FLAG_MASK = 0b1,  /* This needs to be kept actualized */
};

struct PACKED_STRUCT pmgr_hdr_t {
    int32_t size;               /* size of the whole message */
    pmgr_msg_type_e type;
};

struct PACKED_STRUCT pmgr_task_name_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME]; /* identificator of a task */
};

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
