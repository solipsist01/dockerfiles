#!/bin/bash
set -e
source /opt/gow/bash-lib/utils.sh

gow_log "Starting Dolphin with DISPLAY=${DISPLAY}"
cd /home/retro/Applications
./Dolphin_Emulator.AppImage --appimage-extract-and-run
#pcsx2-qt