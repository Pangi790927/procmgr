#ifndef JSON2PMGR_H
#define JSON2PMGR_H

#include "procmgr.h"

/* takes all definitions inside progmgr.h and serializes them as a json dictionary */
int json2pmgr_values(std::string &values_json);
int json2pmgr(std::string &src, std::shared_ptr<pmgr_hdr_t> &dst, int &target_fd);
int json2pmgr(pmgr_hdr_t *src, std::string &dst, int &target_fd);

#endif
