#!/bin/bash
## Pterodactyl install script for Infinite Rails (runs in the install container as root,
## server files mounted at /mnt/server). Safe to re-run (Reinstall keeps world + config:
## SteamCMD only replaces the game files, InfiniteRails_Data/config.txt, Worlds/ etc. stay).
set -e
cd /mnt/server

echo "== SteamCMD =="
mkdir -p steamcmd
if [ ! -x steamcmd/steamcmd.sh ]; then
    curl -fsSL https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz | tar -xz -C steamcmd
fi

export HOME=/mnt/server
BRANCH_ARGS=""
[ -n "${STEAM_BRANCH}" ] && [ "${STEAM_BRANCH}" != "public" ] && BRANCH_ARGS="-beta ${STEAM_BRANCH}"

echo "== Infinite Rails Dedicated Server (app 5484540) =="
# app_info_update first: without it a fresh SteamCMD answers "Missing configuration"
# shellcheck disable=SC2086
./steamcmd/steamcmd.sh +@sSteamCmdForcePlatformType linux +force_install_dir /mnt/server \
    +login anonymous +app_info_update 1 +app_update 5484540 ${BRANCH_ARGS} validate +quit

# Steam client libs where Steamworks looks for them
mkdir -p .steam/sdk64
cp -f steamcmd/linux64/steamclient.so .steam/sdk64/ 2>/dev/null || true

[ -x InfiniteRails.x86_64 ] || { echo "!! InfiniteRails.x86_64 missing after install"; ls -la; exit 1; }
chmod +x InfiniteRails.x86_64
echo "== Install done =="
