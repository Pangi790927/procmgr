#ifndef PROCMGR_H
#define PROCMGR_H

#include "misc_utils.h"

#define PMGR_MAX_TASK_NAME 128
#define PMGR_MAX_TASK_PATH 512

enum pmgr_msg_type_e : int {
    PMGR_MSG_STOP,
    PMGR_MSG_START,
    PMGR_MSG_ADD,
    PMGR_MSG_RM,
    PMGR_MSG_LIST,
    PMGR_MSG_RELOAD,
};

struct PACKED_STRUCT pmgr_hdr_t {
    int size;
    pmgr_msg_type_e type;
};

struct PACKED_STRUCT pmgr_msg_stop_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME];
};

struct PACKED_STRUCT pmgr_msg_start_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME];
};

struct PACKED_STRUCT pmgr_msg_add_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME];
    char task_path[PMGR_MAX_TASK_PATH];
};

struct PACKED_STRUCT pmgr_msg_rm_t {
    pmgr_hdr_t hdr;
    char task_name[PMGR_MAX_TASK_NAME];
};

struct PACKED_STRUCT pmgr_msg_list_t {
    pmgr_hdr_t hdr;
};

struct PACKED_STRUCT pmgr_msg_reload_t {
    pmgr_hdr_t hdr;
};

struct PACKED_STRUCT pmgr_msg_list_reply_t {
    pmgr_hdr_t hdr;
    int done;
    char task_name[PMGR_MAX_TASK_NAME];
    char task_path[PMGR_MAX_TASK_PATH];
};

#endif
