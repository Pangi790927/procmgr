#!/usr/bin/python3.8

import sys
sys.path.insert(0, '../../python-mod/')

import procmgr_py as pmgr
import asyncio
import time
import json
from munch import DefaultMunch

pmgr.install_crash_handler()

# Get all the proc manager defines
str_pmgr_defs = pmgr.get_defs()
pmgr_defs = DefaultMunch.fromDict(json.loads(str_pmgr_defs))

# we are called by pmgr so that is our parent dir
pmgr_dir = f"{pmgr.get_mod_dir()}/../"
pmgr.dbg(pmgr_dir)

async def sendmsg(fd, msg):
    await pmgr.write_msg(fd, json.dumps(msg))
    rsp = json.loads(await pmgr.read_msg(fd))
    if rsp["retval"] < 0:
        raise Exception("Failed to send message")

async def recvmsg(fd):
    return json.loads(await pmgr.read_msg(fd))

async def co_client():
    fd = await pmgr.connect(f"{pmgr_dir}/chanmgr.sock")
    conn_ev = {
        "hdr": { "type": pmgr_defs.pmgr_msg_type_e.PMGR_CHAN_REGISTER },
        "flags": pmgr_defs.pmgr_chan_flags_e.PMGR_CHAN_WAITC,
        "chan_name": "pyexamp.example"
    }
    await sendmsg(fd, conn_ev)

    while True:
        msg = await recvmsg(fd)
        pmgr.dbg(f"CLIENT: Py Recv Event: {msg}")

async def co_server():
    pmgr.dbg("SERVER: Started server")
    fd = await pmgr.connect(f"{pmgr_dir}/chanmgr.sock")
    conn_ev = {
        "hdr": { "type": pmgr_defs.pmgr_msg_type_e.PMGR_CHAN_REGISTER },
        "flags": pmgr_defs.pmgr_chan_flags_e.PMGR_CHAN_CREAT,
        "chan_name": "pyexamp.example"
    }
    await sendmsg(fd, conn_ev)

    msg = {
        "hdr": { "type": pmgr_defs.pmgr_msg_type_e.PMGR_CHAN_MESSAGE },
        "flags": pmgr_defs.pmgr_chan_flags_e.PMGR_CHAN_BCAST,
        "src_id": 0,
        "dst_id": 0,
        "contents": "This is the string content of the message",
    }

    while True:
        await sendmsg(fd, msg)
        await asyncio.sleep(1)

async def co_main():
    pmgr.dbg("Started main")
    t1 = asyncio.get_event_loop().create_task(co_server())
    t2 = asyncio.get_event_loop().create_task(co_client())

    await t1
    await t2
    pmgr.dbg("Done main")

asyncio.run(co_main())
pmgr.dbg("--Python exit for reasons?")
