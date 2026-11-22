#!/bin/bash
## Runtime entrypoint for the Infinite Rails Pterodactyl egg.
## 1. update the server with SteamCMD (AUTO_UPDATE=1)
## 2. sync config.txt with the panel variables
## 3. run the egg's startup command (STARTUP, filled in by wings)
## Console commands typed in the panel go to the server's stdin, which is what
## the server reads its commands from (/save, /say, /exit, ...).

sleep 1
cd /home/container || exit 1
export TZ=${TZ:-UTC}

APP_ID=5484540
CFG=InfiniteRails_Data/config.txt

# ---- 1. update ----
if [ "${AUTO_UPDATE:-1}" = "1" ]; then
    if [ -x ./steamcmd/steamcmd.sh ]; then
        BRANCH_ARGS=""
        [ -n "${STEAM_BRANCH}" ] && [ "${STEAM_BRANCH}" != "public" ] && BRANCH_ARGS="-beta ${STEAM_BRANCH}"
        echo "[infiniterails] Updating server (app ${APP_ID}${STEAM_BRANCH:+, branch ${STEAM_BRANCH}})..."
        # shellcheck disable=SC2086
        ./steamcmd/steamcmd.sh +force_install_dir /home/container +login anonymous \
            +app_info_update 1 +app_update ${APP_ID} ${BRANCH_ARGS} +quit \
            || echo "[infiniterails] WARNING: SteamCMD update failed, starting the installed version"
    else
        echo "[infiniterails] WARNING: ./steamcmd/steamcmd.sh missing - reinstall the server to get it back"
    fi
fi

if [ ! -x ./InfiniteRails.x86_64 ]; then
    echo "[infiniterails] ERROR: InfiniteRails.x86_64 not found - run Reinstall in the panel"
    exit 1
fi

# ---- 2. config.txt ----
# The server writes a full config.txt on its first start; a missing key keeps its default,
# so before that a small file with only our keys is enough.
mkdir -p InfiniteRails_Data
[ -s "$CFG" ] || echo '{}' > "$CFG"
if jq empty "$CFG" 2>/dev/null; then
    tmp=$(mktemp)
    if jq --argjson port "${SERVER_PORT:-64888}" \
          --arg motd "${MOTD}" --arg login "${LOGIN_REQUIRED}" --arg creative "${CREATIVE_MODE}" \
          '.port = $port
           | .autoUpdate = false
           | (if $motd     != "" then .motd = $motd else . end)
           | (if $login    != "" then .loginRequired  = ($login    == "1" or $login    == "true") else . end)
           | (if $creative != "" then .isCreativeMode = ($creative == "1" or $creative == "true") else . end)' \
          "$CFG" > "$tmp"; then
        cat "$tmp" > "$CFG"
    fi
    rm -f "$tmp"
    echo "[infiniterails] config.txt: port=$(jq .port "$CFG") loginRequired=$(jq .loginRequired "$CFG") creative=$(jq .isCreativeMode "$CFG")"
else
    echo "[infiniterails] WARNING: $CFG is not valid JSON - left as is (the server will start on defaults)"
fi

# ---- 3. start ----
MODIFIED_STARTUP=$(echo "${STARTUP:-./InfiniteRails.x86_64 -batchmode -nographics -logFile -}" | sed -e 's/{{/${/g' -e 's/}}/}/g')
echo ":/home/container$ ${MODIFIED_STARTUP}"
eval exec ${MODIFIED_STARTUP}
