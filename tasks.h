#ifndef TASKS_H
#define TASKS_H

#include "co_utils.h"
#include "procmgr.h"

int tasks_start(const std::string& task_name);
int tasks_stop(const std::string& task_name);
int tasks_add(pmgr_task_t *task);
int tasks_rm(const std::string& task_name);
int tasks_list(std::vector<pmgr_task_t>& list);

bool tasks_exists(const std::string& task_name);

co::task_t co_tasks();
co::task_t co_tasks_clear();
co::task_t co_tasks_waitstop(const std::string& task_name);
co::task_t co_tasks_waitrm(const std::string& task_name);

#endif
