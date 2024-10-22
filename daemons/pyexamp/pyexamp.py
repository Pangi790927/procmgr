#!/usr/bin/python3

import sys
sys.path.insert(0, '../../python-mod/')

import procmgr_py as pmgr
import asyncio
import time
import json
from munch import DefaultMunch

# Get all the proc manager defines
str_pmgr_defs = pmgr.get_defs()
pmgr_defs = DefaultMunch.fromDict(json.loads(str_pmgr_defs))

# we are called by pmgr so that is our parent dir
pmgr_dir = pmgr.get_parent_dir()
print(pmgr_dir)

async def co_main():
    fd = await pmgr.connect(f"{pmgr_dir}/procmgr.sock")
    ev_start = {
        "hdr": { "type": pmgr_defs.pmgr_msg_type_e.PMGR_MSG_EVENT_LOOP, "size": 0 },
        "ev_type": 0, "ev_flags": 0, "task_pid": 0, "task_name": "",
    }
    await pmgr.write_msg(fd, json.dumps(ev_start))

    while True:
        msg = await pmgr.read_msg(fd)
        print(f"Py Recv Event: {msg}")

asyncio.run(co_main())
print("Python exit for reasons?")
