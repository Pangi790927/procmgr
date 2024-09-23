#ifndef CFG_H
#define CFG_H

#include <string>

#include "procmgr.h"

#define CONFIG_PATH "procmgr.json"

struct config_t {
    std::string sock_path;
    int32_t sock_perm = 0;
    std::vector<pmgr_task_t> tasks;
};

int         cfg_read();
config_t    *cfg_get();

#endif
