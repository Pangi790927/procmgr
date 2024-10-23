#!/bin/bash

set -e

if [ "$EUID" -eq 0 ]
    then echo "Please don't run whole script as root"
    exit
fi

# TODO: install munch
# TODO: install python3.8
# TODO: install g++11
# TODO: install lldb

TARGET_IP="${1:-"192.168.1.2"}"
TARGET_USR="${2:-"ubuntu"}"
TARGET_PAS="${3:-password}" # don't take my example on implementing this

ssh "$TARGET_USR@$TARGET_IP" "rm -r procmgr_src"
rsync -a "../procmgr/" "$TARGET_USR@$TARGET_IP":procmgr_src

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl stop procmgr.service" || true
ssh "$TARGET_USR@$TARGET_IP" "make -C procmgr_src clean"
ssh "$TARGET_USR@$TARGET_IP" "make -C procmgr_src"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/daemons/chanmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/daemons/taskmon"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/daemons/scheduler"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/daemons/pyexamp"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/daemons/pmgrch"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr/python-mod"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/a.out /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/procmgr.json /usr/local/procmgr/"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/procmgr.service /etc/systemd/system/"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/daemons/chanmgr/chanmgr /usr/local/procmgr/daemons/chanmgr/chanmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/daemons/taskmon/taskmon /usr/local/procmgr/daemons/taskmon/taskmon"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/daemons/scheduler/scheduler /usr/local/procmgr/daemons/scheduler/scheduler"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/daemons/pyexamp/pyexamp.py /usr/local/procmgr/daemons/pyexamp/pyexamp.py"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/daemons/pmgrch/pmgrch.py /usr/local/procmgr/daemons/pmgrch/pmgrch.py"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/python-mod/procmgr_py.so /usr/local/procmgr/python-mod/procmgr_py.so"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S chown root:root /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S chmod +x /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S ln -s /usr/local/procmgr/procmgr /usr/bin/procmgr" || true
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S sysctl kernel.yama.ptrace_scope=0"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl enable procmgr.service" || true
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl start procmgr.service"
