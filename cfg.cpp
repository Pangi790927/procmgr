#include "cfg.h"

#include <fstream>

#include "debug.h"
#include "json.h"
#include "path_utils.h"

static config_t cfg;

int cfg_read() {
    using namespace nlohmann;
    auto _cfg = cfg;
    _cfg.tasks.clear();

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

        _cfg.sock_path = jcfg["sock_path"];

        for (auto &task : jcfg["tasks"]) {
            pmgr_task_t pt{
                .hdr = {
                    .size = sizeof(pmgr_task_t),
                    .type = PMGR_MSG_ADD,
                }
            };
            strcpy(pt.task_name, task["name"].get<std::string>().c_str());
            strcpy(pt.task_path, task["path"].get<std::string>().c_str());
            if (HAS(task, "pwd"))
                strcpy(pt.task_pwd, task["pwd"].get<std::string>().c_str());
            if (HAS(task, "user"))
                strcpy(pt.task_usr, task["user"].get<std::string>().c_str());
            if (HAS(task, "group"))
                strcpy(pt.task_grp, task["group"].get<std::string>().c_str());

            int32_t flags = 0;
            for (auto &flag : task["flags"]) {
                if (flag.get<std::string>() == "PERSIST") {
                    flags |= (int32_t)PMGR_TASK_FLAG_PERSIST;
                }
                else if (flag.get<std::string>() == "NOSTDIO") {
                    flags |= (int32_t)PMGR_TASK_FLAG_NOSTDIO;
                }
                else if (flag.get<std::string>() == "PWDSELF") {
                    flags |= (int32_t)PMGR_TASK_FLAG_PWDSELF;
                }
                else {
                    DBG("Unknown flag: %s", flag.get<std::string>().c_str());
                    return -1;
                }
            }
            pt.flags = (pmgr_task_flags_e)flags;
            _cfg.tasks.push_back(pt);
        }
    }
    catch (json::exception& e) {
        DBG("Config error: %s", e.what());
        return -1;
    }
    cfg = _cfg;
    return 0;
}

config_t *cfg_get() {
    return &cfg;
}