#!/bin/bash
## MaNGOS Classic + Playerbots - runtime startup wrapper.
## Standalone script for the playerbots image variant - deliberately NOT
## shared with the plain vanilla egg's start.sh.
## Runs on every start/restart from the panel.
## - starts MariaDB
## - syncs the realmlist row, AiPlayerbot.Enabled, and AHBot toggles with
##   the current panel variables, so changing them and restarting is all
##   that's needed - no manual SQL/config editing required
## - starts realmd, then runs mangosd in the foreground (panel console)
cd /home/container || exit 1

DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-changeme_root}"
REALM_NAME="${REALM_NAME:-MyMangosRealm}"
REALM_ADDRESS="${REALM_ADDRESS:-127.0.0.1}"
WORLD_PORT="${WORLD_PORT:-8085}"
SERVER_PORT="${SERVER_PORT:-3724}"
AI_PLAYERBOT_ENABLED="${AI_PLAYERBOT_ENABLED:-1}"
AHBOT_ENABLED="${AHBOT_ENABLED:-1}"

mkdir -p /home/container/mysql-run

echo "Running as: $(id)"
echo "mysql-data ownership/permissions:"
ls -ld /home/container/mysql-data 2>/dev/null
echo "Disk space:"
df -h /home/container 2>/dev/null

# Same stale-lock cleanup as install.sh - after a crash/kill, a leftover
# pid or socket file can make a fresh mysqld_safe refuse to start (or
# hang silently) even though nothing is actually still running.
# pkill here can only affect processes owned by this same (non-root)
# user - if a leftover mariadbd from install (which runs as root) is
# still holding the datadir lock, this pkill can't touch it and would
# silently fail (hence the check right below, before we even try to
# start a new one).
rm -f /home/container/mysql-run/mysqld.pid /home/container/mysql.sock
pkill -9 -f 'mariadbd.*mysql-data' 2>/dev/null || true
sleep 1
echo "Processes matching mysql/maria BEFORE start attempt:"
ps aux | grep -i "mysq\|maria" | grep -v grep || echo "  (none found)"

# Bypassing mysqld_safe's own log-file wrapper here on purpose: piping
# mariadbd's real stdout/stderr straight into this console via tee means
# we see whatever it actually says live, in the panel console itself,
# rather than trusting it successfully wrote to --log-error (which has
# been coming back completely empty even when startup fails - a sign
# something about that log-writing path itself may be the problem, not
# just mariadbd's actual startup logic).
MARIADBD_BIN="$(command -v mariadbd || echo /usr/sbin/mariadbd)"
echo "Starting MariaDB directly via ${MARIADBD_BIN} (live output below)..."
"${MARIADBD_BIN}" \
    --datadir=/home/container/mysql-data \
    --socket=/home/container/mysql.sock \
    --pid-file=/home/container/mysql-run/mysqld.pid \
    --skip-networking=0 \
    --bind-address=127.0.0.1 \
    --port=3306 \
    2>&1 | tee -a /home/container/mysql-error.log &

echo "Waiting for MariaDB..."
UP=0
for i in $(seq 1 120); do
    if mysqladmin --socket=/home/container/mysql.sock ping >/dev/null 2>&1; then
        UP=1
        break
    fi
    sleep 1
done

if [ "${UP}" -ne 1 ]; then
    echo "MariaDB did not come up in time."
    echo "Processes matching mysql/maria still running:"
    ps aux | grep -i "mysq\|maria" | grep -v grep || echo "  (none found)"
    exit 1
fi

# Ensure databases/user/grants exist on every boot, not just at install -
# CREATE DATABASE IF NOT EXISTS / GRANT are cheap and safe to always
# re-run. This is what actually closes the gap when the image gains a
# new requirement later (e.g. mangosd started requiring a 'logs'
# database that earlier versions of this image never created) - a
# rebuild + restart is now enough on its own, no reinstall needed.
mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" <<SQL 2>/dev/null
CREATE DATABASE IF NOT EXISTS realmd CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS characters CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS mangos CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS classiclogs CHARACTER SET utf8;
CREATE USER IF NOT EXISTS 'mangos'@'localhost' IDENTIFIED BY '${DB_PASSWORD:-mangos}';
CREATE USER IF NOT EXISTS 'mangos'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD:-mangos}';
GRANT ALL PRIVILEGES ON realmd.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON characters.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON mangos.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON classiclogs.* TO 'mangos'@'localhost';
GRANT ALL PRIVILEGES ON realmd.* TO 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON characters.* TO 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON mangos.* TO 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON classiclogs.* TO 'mangos'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL
if [ -f /opt/mangos/sql/base/logs.sql ]; then
    mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" -e "SELECT 1 FROM classiclogs.db_version LIMIT 1" >/dev/null 2>&1 \
        || mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" classiclogs < /opt/mangos/sql/base/logs.sql
fi

# Re-apply the same connection-string/port values install.sh writes, in
# case the config predates a fix like the logs database above (its
# LogsDatabaseInfo line wouldn't exist yet on an older install).
DB_PASSWORD="${DB_PASSWORD:-mangos}"
CONF_DIR="/home/container/server/etc"
if [ -f "${CONF_DIR}/realmd.conf" ]; then
    sed -i "s#^LoginDatabaseInfo.*#LoginDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};realmd#" "${CONF_DIR}/realmd.conf" || true
    sed -i "s#^RealmServerPort.*#RealmServerPort = ${SERVER_PORT}#" "${CONF_DIR}/realmd.conf" || true
fi
if [ -f "${CONF_DIR}/mangosd.conf" ]; then
    sed -i "s#^LoginDatabaseInfo.*#LoginDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};realmd#" "${CONF_DIR}/mangosd.conf" || true
    sed -i "s#^WorldDatabaseInfo.*#WorldDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};mangos#" "${CONF_DIR}/mangosd.conf" || true
    sed -i "s#^CharacterDatabaseInfo.*#CharacterDatabaseInfo = 127.0.0.1;3306;mangos;${DB_PASSWORD};characters#" "${CONF_DIR}/mangosd.conf" || true
    sed -i "s#^LogsDatabaseInfo.*#LogsDatabaseInfo      = 127.0.0.1;3306;mangos;${DB_PASSWORD};classiclogs#" "${CONF_DIR}/mangosd.conf" || true
    sed -i "s#^WorldServerPort.*#WorldServerPort = ${WORLD_PORT}#" "${CONF_DIR}/mangosd.conf" || true
fi

echo "Syncing playerbots config and content..."
if [ ! -f "${CONF_DIR}/aiplayerbot.conf" ]; then
    echo "!! ${CONF_DIR}/aiplayerbot.conf not found - was install.sh run"
    echo "!! successfully? Try Reinstall from the panel."
    exit 1
fi
sed -i "s#^AiPlayerbot.Enabled.*#AiPlayerbot.Enabled = ${AI_PLAYERBOT_ENABLED}#" "${CONF_DIR}/aiplayerbot.conf" || true

echo "Syncing AHBot config..."
if [ -f "${CONF_DIR}/ahbot.conf" ]; then
    sed -i "s#^AuctionHouseBot.Seller.Enabled.*#AuctionHouseBot.Seller.Enabled = ${AHBOT_ENABLED}#" "${CONF_DIR}/ahbot.conf" || true
    sed -i "s#^AuctionHouseBot.Buyer.Enabled.*#AuctionHouseBot.Buyer.Enabled = ${AHBOT_ENABLED}#" "${CONF_DIR}/ahbot.conf" || true
else
    echo "  (no ahbot.conf found - AHBot config wasn't written at install time)"
fi

if [ ! -d /opt/mangos/sql/playerbots ]; then
    echo "!! /opt/mangos/sql/playerbots not found in the image - this build doesn't"
    echo "!! actually have playerbots SQL content staged. Check playerbots/Dockerfile."
    exit 1
fi
for f in $(ls /opt/mangos/sql/playerbots/characters/*.sql 2>/dev/null | sort); do
    mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" characters < "$f" >/dev/null 2>&1 || true
done
for f in $(ls /opt/mangos/sql/playerbots/world/*.sql 2>/dev/null | sort); do
    mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos < "$f" >/dev/null 2>&1 || true
done

# Applying core's sql/updates/mangos/*.sql on every boot, not just at
# install, is deliberate: if the image gets rebuilt later against a
# newer mangos-classic commit, an already-installed server's world DB
# would otherwise stay pinned to whatever schema existed at its last
# install/reinstall and fail the same version-mismatch check forever.
# Doing this here means "rebuild the image, restart the server" is
# enough to catch the DB up - no manual SQL, no reinstall needed.
#
# Batched into a single mysql invocation with --force (see install.sh
# for the full reasoning) rather than ~140 separate ones - the per-file
# version added a real 1-3+ minutes to every single boot.
UPD_DIR="/opt/mangos/sql/updates/mangos"
if [ -d "${UPD_DIR}" ]; then
    echo "Checking for core schema updates..."
    UPD_START=$(date +%s)
    cat "${UPD_DIR}"/*.sql 2>/dev/null | \
        mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" --force mangos \
        > /home/container/schema_updates.log 2>&1
    UPD_ELAPSED=$(( $(date +%s) - UPD_START ))
    echo "  Done in ${UPD_ELAPSED}s."
fi

echo "Syncing realmlist (name=${REALM_NAME}, address=${REALM_ADDRESS}, port=${WORLD_PORT})..."
mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" realmd -e "
INSERT INTO realmlist (id, name, address, localAddress, localSubnetMask, port, icon, timezone, allowedSecurityLevel, population, gamebuild)
VALUES (1, '${REALM_NAME}', '${REALM_ADDRESS}', '127.0.0.1', '255.255.255.0', ${WORLD_PORT}, 0, 1, 0, 0, 5875)
ON DUPLICATE KEY UPDATE name='${REALM_NAME}', address='${REALM_ADDRESS}', port=${WORLD_PORT};
" 2>/dev/null || \
mysql --socket=/home/container/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" realmd -e "
INSERT INTO realmlist (id, name, address, port)
VALUES (1, '${REALM_NAME}', '${REALM_ADDRESS}', ${WORLD_PORT})
ON DUPLICATE KEY UPDATE name='${REALM_NAME}', address='${REALM_ADDRESS}', port=${WORLD_PORT};
" || echo "!! Could not sync realmlist entry - check the realmlist table schema manually"

echo "Starting realmd (login server)..."

# mangosd's DataDir sed replacement in install.sh doesn't appear to
# reliably match this config's actual line format (same category of
# issue as the earlier LogsDatabaseInfo mismatch) - it's been observed
# defaulting to "./" (relative to mangosd's working directory,
# /home/container) instead of the intended
# /home/container/server/data. Rather than keep fighting sed patterns
# against a config format that keeps surprising us, symlink the
# expected relative paths straight at the real data location - this
# works regardless of whatever DataDir actually ends up set to, and
# regardless of whether client data was uploaded before or after this
# runs (a symlink doesn't require its target to exist yet).
for d in maps vmaps dbc mmaps; do
    if [ ! -e "/home/container/${d}" ]; then
        ln -sfn "/home/container/server/data/${d}" "/home/container/${d}"
    fi
done

# ahbot.conf/aiplayerbot.conf lookups (../etc/, relative to the binary's
# own location) are handled by symlinks baked into the image at build
# time (see Dockerfile) pointing at the real, editable configs on this
# persistent volume - not by anything in this script. Running the
# original /opt/mangos binaries directly (not a runtime copy) means a
# rebuilt image is always what actually runs here, no stale executables
# left over from an older build.
/opt/mangos/bin/realmd -c /home/container/server/etc/realmd.conf \
    > /home/container/realmd.log 2>&1 &

sleep 3

echo "Starting mangosd (world server)..."
exec /opt/mangos/bin/mangosd -c /home/container/server/etc/mangosd.conf