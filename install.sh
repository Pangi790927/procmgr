#!/bin/bash

set -e

if [ "$EUID" -eq 0 ]
    then echo "Please don't run whole script as root"
    exit
fi

TARGET_IP="${1:-"192.168.1.2"}"
TARGET_USR="${2:-"ubuntu"}"
TARGET_PAS="${3:-password}" # don't take my example on implementing this

rsync -a "../procmgr/" "$TARGET_USR@$TARGET_IP":procmgr_src

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl stop procmgr.service" || true
ssh "$TARGET_USR@$TARGET_IP" "make -C procmgr_src clean"
ssh "$TARGET_USR@$TARGET_IP" "make -C procmgr_src"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S mkdir -p /usr/local/procmgr"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/a.out /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/procmgr.json /usr/local/procmgr/"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S cp procmgr_src/procmgr.service /etc/systemd/system/"

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S chown root:root /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S chmod +x /usr/local/procmgr/procmgr"
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S ln -s /usr/local/procmgr/procmgr /usr/bin/procmgr" || true

ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl enable procmgr.service" || true
ssh "$TARGET_USR@$TARGET_IP" "echo '$TARGET_PAS' | sudo -S systemctl start procmgr.service"
