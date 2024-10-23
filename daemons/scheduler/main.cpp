#include <unistd.h> 
#include "procmgr.h"
#include "sys_utils.h"
#include "co_utils.h"
#include "pmgrch.h"

int main(int argc, char const *argv[])
{
    ASSERT_FN(pmgrch_init());

    DBG("SUB_SCHEDULER");
    /* TODO: This will run things at specific hours, seconds, days, etc. It should also have the
    option to be controlled from the network. But it's for some other time. */

    /* I'm a daemon so I will just exist... */
    while (true)
        sleep_s(1);
    return 0;
}
