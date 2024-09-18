#include "debug.h"
#include "co_utils.h"
#include "json.h"

/* TODO: add global channel */

co::task_t co_main(std::function<std::string(int)> args) {
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

    /* TODO: read config */

    if (usage == "daemon") {
        DBG("Daemon...");
        co::pool_t pool;

        pool.sched(co_main(arg));
        ASSERT_FN(pool.run());
    }
    else if (usage == "ctrl") {
        DBG("Ctrl...");

        /* TOOD: option reading and do stuff */
        /* TODO: commands:
            - reload <daemon>
            - stop <daemon>
            - start <daeomon>
            - add <daemon> <daemon_path> [optional when]
            - rm <daemon>
            - list [optional level]
        */
    }
    else {
        DBG("Unknown usage type[%s]", usage.c_str());
        return -1;
    }
    /* code */
    return 0;
}
