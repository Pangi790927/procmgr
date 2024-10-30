#!/usr/bin/python3.8

import sys
sys.path.insert(0, '../../python-mod/')

import procmgr_py as pmgr
import asyncio
import time
import json
import socket
import threading
import os
from munch import DefaultMunch
import lldb

def write_crash_raport(target_pid, file_out):
    # Create a new debugger instance
    debugger = lldb.SBDebugger.Create()

    # When we step or continue, don't return from the function until the process
    # stops. Otherwise we would have to handle the process events ourselves which, while doable is
    # a little tricky.  We do this by setting the async mode to false.
    debugger.SetAsync (False)

    print(f"Creating a target for {target_pid}", file=file_out)
    target = debugger.CreateTarget('')

    if not target:
        print("Can't create target", file=file_out)
        return

    listener = lldb.SBListener('my.attach.listener')
    err = lldb.SBError()
    process = target.AttachToProcessWithID(listener, int(target_pid), err)
    if not process:
        print (f"can't connect to pid: {target_pid} err: {err}", file=file_out)
        return

    print(process, file=file_out)

    state = process.GetState()
    if not state:
        print("Failed to get process state", file=file_out)
        process.Continue()
        return

    if state != lldb.eStateStopped:
        print("Proc is not stopped", file=file_out)
        process.Continue()
        return

    print(state, file=file_out)

    for i in range(100):
        thread = process.GetThreadAtIndex(i)
        if not thread:
            break

        print (thread, file=file_out)

        for j in range(100):
            frame = thread.GetFrameAtIndex(j)

            if not frame:
                break

            print (frame, file=file_out)

    # done analysis
    process.Detach()

# Get all the proc manager defines
str_pmgr_defs = pmgr.get_defs()
pmgr_defs = DefaultMunch.fromDict(json.loads(str_pmgr_defs))

pmgr_lib_dir = pmgr.get_mod_dir()
sock_path = f"{pmgr_lib_dir}/../pmgrch.sock"
pmgr.dbg(f"sock_path: {sock_path}")

# create the socket inside the parent dir
if os.path.exists(sock_path):
    os.remove(sock_path)    

server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(sock_path)
server.listen(100)

def handle_crash(client, real_pid):
    try:
        pid = int.from_bytes(client.recv(4), "little")
        if real_pid != pid:
            pmgr.dbg("sneaky...")
            return
        data = client.recv(1)
        if len(data) == 1:
            pmgr.dbg("Crash received, will start to analyze it")

            exe_name = os.path.basename(os.path.realpath(f"/proc/{pid}/exe"))
            try:
                os.mkdir(exe_name)
            except:
                pass
            crash_file = f"./{exe_name}/{pid}.crash"

            with open(crash_file, "w") as f:
                write_crash_raport(pid, f)
            pmgr.dbg("crash done...")
            client.send(b'\x00')
    except Exception as e:
        pmgr.dbg(f"had exception: {e}...")
    client.close()

pmgr.dbg(f"waiting for connections...");
while True:
    conn, addr = server.accept()
    real_pid = conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED)
    pmgr.dbg(f"waiting for connections...");
    threading.Thread(target=handle_crash, args=(conn,real_pid)).start()

pmgr.dbg("Exit")

