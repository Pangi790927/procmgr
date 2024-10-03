#ifndef CMDS_H
#define CMDS_H

#include "co_utils.h"
#include "procmgr.h"


/* trigger event for all that listen to it */
void cmds_trigger_event(pmgr_event_t *ev);

co::task_t co_cmds();

#endif
