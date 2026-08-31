#!/bin/bash
## MaNGOS Classic + Playerbots - Pterodactyl install script
## Standalone script for the playerbots image variant (see
## playerbots/Dockerfile) - deliberately NOT shared with the plain
## vanilla egg's install.sh, so the two variants can be debugged and
## changed independently.
set -e

echo "=========================================="
echo " MaNGOS Classic + Playerbots - install"
echo "=========================================="

DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-changeme_root}"
DB_PASSWORD="${DB_PASSWORD:-mangos}"
REALM_NAME="${REALM_NAME:-MyMangosRealm}"
REALM_ADDRESS="${REALM_ADDRESS:-127.0.0.1}"
WORLD_PORT="${WORLD_PORT:-8085}"
SERVER_PORT="${SERVER_PORT:-3724}"
AI_PLAYERBOT_ENABLED="${AI_PLAYERBOT_ENABLED:-1}"
AHBOT_ENABLED="${AHBOT_ENABLED:-1}"
# Normalize to canonical 1/0: the panel's "boolean" validation rule
# (needed so the toggle switch UI actually works - see start.sh/README
# for why) accepts true/false/on/off/1/0 in various forms, but mangosd's
# own config file format only understands literal 1/0 - whatever the
# panel actually stores, this guarantees a clean value reaches the
# config file.
case "${AI_PLAYERBOT_ENABLED,,}" in
    1|true|yes|on)  AI_PLAYERBOT_ENABLED=1 ;;
    *)              AI_PLAYERBOT_ENABLED=0 ;;
esac
case "${AHBOT_ENABLED,,}" in
    1|true|yes|on)  AHBOT_ENABLED=1 ;;
    *)              AHBOT_ENABLED=0 ;;
esac
RANDOM_BOT_COUNT="${RANDOM_BOT_COUNT:-50}"

mkdir -p /mnt/server/server/etc /mnt/server/server/data /mnt/server/client
mkdir -p /mnt/server/mysql-data /mnt/server/mysql-run

echo "==> [1/5] Locating and copying default config files from the image..."
MANGOSD_DIST="$(find /opt/mangos -name 'mangosd.conf.dist' | head -n1)"
REALMD_DIST="$(find /opt/mangos -name 'realmd.conf.dist' | head -n1)"

if [ -z "${MANGOSD_DIST}" ] || [ -z "${REALMD_DIST}" ]; then
    echo "!! Could not find mangosd.conf.dist / realmd.conf.dist under /opt/mangos."
    echo "!! The image's cmake install layout may differ from what this script expects -"
    echo "!! run 'find /opt/mangos -iname \"*.conf.dist\"' in a shell to locate them and"
    echo "!! adjust this script."
    exit 1
fi

cp "${MANGOSD_DIST}" /mnt/server/server/etc/mangosd.conf
cp "${REALMD_DIST}"  /mnt/server/server/etc/realmd.conf

echo "==> Locating and copying the playerbots config template..."
AIPLAYERBOT_DIST="$(find /opt/mangos -iname 'aiplayerbot*.conf.dist' | head -n1)"
if [ -z "${AIPLAYERBOT_DIST}" ]; then
    echo "!! Could not find aiplayerbot*.conf.dist under /opt/mangos."
    echo "!! This means the playerbots build didn't stage its config template where"
    echo "!! this script expects it - run 'find /opt/mangos -iname \"*playerbot*\"' in"
    echo "!! a shell to see what's actually there and adjust playerbots/Dockerfile."
    exit 1
fi
cp "${AIPLAYERBOT_DIST}" /mnt/server/server/etc/aiplayerbot.conf

echo "==> Locating and copying the AHBot config template..."
# Unlike aiplayerbot.conf.dist (a separate module, hard-failed above if
# missing), ahbot.conf.dist.in lives directly in mangos-classic core
# (src/game/AuctionHouseBot/) - it's expected to be there regardless of
# the BUILD_AHBOT flag's exact effect on it, but treating this as a hard
# requirement is less certain than the playerbots case, so this warns
# and continues rather than failing the whole install if it's missing.
AHBOT_DIST="$(find /opt/mangos -iname 'ahbot.conf.dist' | head -n1)"
if [ -z "${AHBOT_DIST}" ]; then
    echo "!! Could not find ahbot.conf.dist under /opt/mangos - AHBot config won't be"
    echo "!! written. Run 'find /opt/mangos -iname \"*ahbot*\"' in a shell to check."
else
    cp "${AHBOT_DIST}" /mnt/server/server/etc/ahbot.conf
fi

echo "==> [2/5] Initializing MariaDB data directory..."
if [ -d /mnt/server/mysql-data/mysql ]; then
    echo "    (mysql-data already initialized from a previous attempt, skipping)"
else
    mariadb-install-db \
        --datadir=/mnt/server/mysql-data \
        --auth-root-authentication-method=normal \
        --skip-test-db \
        --user=root \
        > /mnt/server/mysql-install.log 2>&1
fi

echo "==> [3/5] Starting a temporary MariaDB instance to seed databases..."
# Clean up stale lock/pid/socket files before starting. A server that
# was crash-looping and got killed mid-write can leave these behind,
# which makes a fresh mysqld_safe refuse to start (or hang silently,
# writing nothing to its own log) even though nothing is actually still
# running.
echo "Running as: $(id)"
ls -ld /mnt/server/mysql-data 2>/dev/null
df -h /mnt/server 2>/dev/null
rm -f /mnt/server/mysql-run/mysqld.pid /mnt/server/mysql.sock
pkill -9 -f 'mariadbd.*mysql-data' 2>/dev/null || true
sleep 1
echo "Processes matching mysql/maria BEFORE start attempt:"
ps aux | grep -i "mysq\|maria" | grep -v grep || echo "  (none found)"

# Bypassing mysqld_safe's own log-file wrapper on purpose here: piping
# mariadbd's real stdout/stderr straight into this console via tee means
# we see whatever it actually says live, rather than trusting it
# successfully wrote to --log-error (which has come back completely
# empty on failure before, even when the underlying problem was real).
MARIADBD_BIN="$(command -v mariadbd || echo /usr/sbin/mariadbd)"
echo "DEBUG: about to exec: ${MARIADBD_BIN} --datadir=/mnt/server/mysql-data --socket=/mnt/server/mysql.sock --pid-file=/mnt/server/mysql-run/mysqld.pid --user=root --skip-networking=0 --bind-address=127.0.0.1 --port=3306"
echo "DEBUG: /mnt/server contents right now:"
ls -la /mnt/server 2>&1
echo "DEBUG: /mnt/server/mysql-data contents right now:"
ls -la /mnt/server/mysql-data 2>&1
echo "Starting MariaDB directly via ${MARIADBD_BIN} (live output below)..."
"${MARIADBD_BIN}" \
    --datadir=/mnt/server/mysql-data \
    --socket=/mnt/server/mysql.sock \
    --pid-file=/mnt/server/mysql-run/mysqld.pid \
    --user=root \
    --skip-networking=0 \
    --bind-address=127.0.0.1 \
    --port=3306 \
    2>&1 | tee -a /mnt/server/mysql-error.log &
MYSQL_PID=$!

# Up to 120s, not 60 - a datadir left mid-write by a killed process can
# genuinely need real InnoDB crash-recovery time on next start, not just
# a fixed "it usually boots fast" assumption.
UP=0
for i in $(seq 1 120); do
    if mysqladmin --socket=/mnt/server/mysql.sock ping >/dev/null 2>&1; then
        UP=1
        break
    fi
    sleep 1
done

if [ "${UP}" -ne 1 ]; then
    echo "!! MariaDB failed to start."
    echo "!! Processes matching mysql/maria still running:"
    ps aux | grep -i "mysq\|maria" | grep -v grep || echo "  (none found)"
    echo "!! Processes matching mysql/maria still running:"
    ps aux | grep -i "mysq\|maria" | grep -v grep || echo "  (none found)"
    echo "!! Contents of mysql-data (checking it isn't empty/corrupted):"
    ls -la /mnt/server/mysql-data 2>/dev/null | head -20
    exit 1
fi

if mysql --socket=/mnt/server/mysql.sock -u root -e "SELECT 1" >/dev/null 2>&1; then
    echo "    Setting initial MariaDB root password..."
    mysql --socket=/mnt/server/mysql.sock -u root -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '${DB_ROOT_PASSWORD}';"
else
    echo "    (root password already set from a previous attempt)"
fi

# Deliberately NOT gated behind the "is this a fresh install" check above:
# CREATE DATABASE IF NOT EXISTS / GRANT are safe to always re-run, and
# this needs to run even on a repeat install/reinstall of an
# already-initialized server - e.g. this is exactly what was missing
# before: mangosd also needs a 'logs' database that earlier versions of
# this script never created, and a plain reinstall on an
# already-password-set server would otherwise have skipped creating it
# forever.
echo "    Ensuring databases and mangos user exist..."
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" <<SQL
CREATE DATABASE IF NOT EXISTS realmd CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS characters CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS mangos CHARACTER SET utf8;
CREATE DATABASE IF NOT EXISTS classiclogs CHARACTER SET utf8;
CREATE USER IF NOT EXISTS 'mangos'@'localhost' IDENTIFIED BY '${DB_PASSWORD}';
CREATE USER IF NOT EXISTS 'mangos'@'127.0.0.1' IDENTIFIED BY '${DB_PASSWORD}';
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

echo "==> Importing base schema..."
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" realmd    < /opt/mangos/sql/base/realmd.sql
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" characters < /opt/mangos/sql/base/characters.sql
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos    < /opt/mangos/sql/base/mangos.sql
if [ -f /opt/mangos/sql/base/logs.sql ]; then
    mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" classiclogs < /opt/mangos/sql/base/logs.sql
fi

echo "==> [4/5] Copying classic-db repo (for optional later use) and importing the pre-built world content dump..."
cp -r /opt/mangos-db /mnt/server/classic-db

if [ ! -f /opt/mangos-db-dump/classic-db-full.sql ]; then
    echo "!! /opt/mangos-db-dump/classic-db-full.sql not found in the image - the world"
    echo "!! database will only have the empty base schema. Check the Docker build log."
else
    DUMP_SIZE="$(du -h /opt/mangos-db-dump/classic-db-full.sql | cut -f1)"
    echo "    Importing Full_DB snapshot (${DUMP_SIZE}) - this runs with no output for a"
    echo "    while, that's normal for a plain SQL import, not a hang. Printing a"
    echo "    heartbeat every 15s so the console doesn't look frozen:"

    mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos \
        < /opt/mangos-db-dump/classic-db-full.sql 2>/mnt/server/full_db_import_errors.log &
    IMPORT_PID=$!
    SECS=0
    while kill -0 "${IMPORT_PID}" 2>/dev/null; do
        sleep 15
        SECS=$((SECS + 15))
        echo "    ... still importing (${SECS}s elapsed)"
    done
    if wait "${IMPORT_PID}"; then
        echo "    Full_DB import finished with no reported errors."
    else
        echo "!! Full_DB import FAILED partway through - the mysql client stops at the"
        echo "!! first bad statement, so everything after it in the file was never"
        echo "!! applied. Error:"
        echo "----------------------------------------"
        cat /mnt/server/full_db_import_errors.log 2>/dev/null
        echo "----------------------------------------"
    fi

    # Concrete, unambiguous sanity check regardless of the above - counts
    # what's actually in the database right now.
    TABLE_COUNT="$(mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" -N -e \
        "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='mangos';" 2>/dev/null)"
    echo "    'mangos' database now has ${TABLE_COUNT:-0} table(s) after the Full_DB import."

    if [ -f /opt/mangos-db-dump/acid_classic.sql ]; then
        echo "    Importing ACID (creature AI scripts) on top..."
        mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos < /opt/mangos-db-dump/acid_classic.sql \
            || echo "!! ACID import reported errors above - non-fatal, core content is still in place"
    fi

    # The compiled mangosd binary is built from mangos-classic's master
    # branch, but the Full_DB dump above comes from classic-db's last
    # tagged release - which lags a bit behind core master. Core ships a
    # sql/updates/mangos/ folder of incremental migrations to bridge
    # exactly this gap; mangosd refuses to start against a world DB
    # that's missing them. We don't replicate the official installer's
    # precise "which revision am I on" detection here, so we just try
    # applying every update file in order - ones already folded into the
    # Full_DB snapshot will fail harmlessly (column/table already
    # exists) and are skipped over, the ones actually needed will apply.
    #
    # One mysql process per file (reverted from a batched single-session
    # version - both a --force-piped variant and a `source`-per-file
    # variant were tried; the former caused real schema corruption, the
    # latter didn't resolve a specific spell_template mismatch and made
    # it harder to see which exact file was responsible. Back to the
    # simple, well-understood version while that gets tracked down
    # separately - this reintroduces real risk of hitting a wings
    # install timeout on this step (~140 separate process spawns), worth
    # watching for.
    UPD_DIR="/opt/mangos/sql/updates/mangos"
    if [ -d "${UPD_DIR}" ]; then
        echo "    Applying core schema updates (bridges core master vs. last classic-db release)..."
        FAILCOUNT=0
        for f in $(ls "${UPD_DIR}"/*.sql 2>/dev/null | sort); do
            OUT="$(mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos < "$f" 2>&1)"
            if [ $? -eq 0 ]; then
                echo "      applied $(basename "$f")"
            else
                FAILCOUNT=$((FAILCOUNT + 1))
                echo "      skipped $(basename "$f"): ${OUT}"
            fi
        done
        echo "    (${FAILCOUNT} update file(s) skipped - normal if they were already"
        echo "    included in the Full_DB snapshot; check the messages above if"
        echo "    mangosd still reports a version mismatch after this install)"
    else
        echo "!! ${UPD_DIR} not found in image - mangosd may report a version mismatch on first start"
    fi
fi

echo "    Note: this loads a complete snapshot of classic-db as of the image build,"
echo "    not the very latest commits since that release. If you want to update to"
echo "    newer content later, /home/container/classic-db has the full repo including"
echo "    the real (interactive) InstallFullDB.sh - run it from a container shell:"
echo "      cd /home/container/classic-db && ./InstallFullDB.sh"

echo "==> Importing playerbots SQL content..."
# Imports playerbots' own characters-db and world-db SQL content, needed
# for the bot-enabled binary to function at all. This is separate from
# the "enabled or not" toggle - that's AiPlayerbot.Enabled in the config
# file below, applied every boot in start.sh. This import just
# makes the feature exist in the database; the config controls whether
# it's active.
if [ ! -d /opt/mangos/sql/playerbots ]; then
    echo "!! /opt/mangos/sql/playerbots not found in the image - this means the"
    echo "!! playerbots build didn't stage its SQL content where this script"
    echo "!! expects it. Run 'find /usr/src/mangos-classic/src/modules/PlayerBots -type d'"
    echo "!! in a throwaway build to see the real layout and adjust"
    echo "!! playerbots/Dockerfile."
    exit 1
fi
for f in $(ls /opt/mangos/sql/playerbots/characters/*.sql 2>/dev/null | sort); do
    mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" characters < "$f" \
        && echo "    applied (characters) $(basename "$f")" \
        || echo "    skipped (characters) $(basename "$f") - may already be applied"
done
for f in $(ls /opt/mangos/sql/playerbots/world/*.sql 2>/dev/null | sort); do
    mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" mangos < "$f" \
        && echo "    applied (world) $(basename "$f")" \
        || echo "    skipped (world) $(basename "$f") - may already be applied"
done

echo "==> [5/5] Writing config file values and initial realmlist entry..."
CONF_DIR="/mnt/server/server/etc"
sed -i "s#^LoginDatabaseInfo.*#LoginDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};realmd#" "${CONF_DIR}/realmd.conf" || true
sed -i "s#^RealmServerPort.*#RealmServerPort = ${SERVER_PORT}#" "${CONF_DIR}/realmd.conf" || true

sed -i "s#^LoginDatabaseInfo.*#LoginDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};realmd#" "${CONF_DIR}/mangosd.conf" || true
sed -i "s#^WorldDatabaseInfo.*#WorldDatabaseInfo     = 127.0.0.1;3306;mangos;${DB_PASSWORD};mangos#" "${CONF_DIR}/mangosd.conf" || true
sed -i "s#^CharacterDatabaseInfo.*#CharacterDatabaseInfo = 127.0.0.1;3306;mangos;${DB_PASSWORD};characters#" "${CONF_DIR}/mangosd.conf" || true
sed -i "s#^LogsDatabaseInfo.*#LogsDatabaseInfo      = 127.0.0.1;3306;mangos;${DB_PASSWORD};classiclogs#" "${CONF_DIR}/mangosd.conf" || true
sed -i "s#^WorldServerPort.*#WorldServerPort = ${WORLD_PORT}#" "${CONF_DIR}/mangosd.conf" || true
sed -i "s#^DataDir.*#DataDir = \"/home/container/server/data\"#" "${CONF_DIR}/mangosd.conf" || true

sed -i "s#^AiPlayerbot.Enabled.*#AiPlayerbot.Enabled = ${AI_PLAYERBOT_ENABLED}#" "${CONF_DIR}/aiplayerbot.conf" || true
# Optional leading '#' in the pattern: several published versions of this
# config ship MinRandomBots/MaxRandomBots commented out by default - this
# matches (and uncomments) either form.
sed -i "s@^#\{0,1\}AiPlayerbot.MinRandomBots.*@AiPlayerbot.MinRandomBots = ${RANDOM_BOT_COUNT}@" "${CONF_DIR}/aiplayerbot.conf" || true
sed -i "s@^#\{0,1\}AiPlayerbot.MaxRandomBots.*@AiPlayerbot.MaxRandomBots = ${RANDOM_BOT_COUNT}@" "${CONF_DIR}/aiplayerbot.conf" || true

if [ -f "${CONF_DIR}/ahbot.conf" ]; then
    # No dedicated "Enabled" boolean key exists in this AHBot
    # implementation - confirmed by reading the real deployed file
    # directly. The actual mechanism is AuctionHouseBot.Chance.Sell/
    # .Chance.Buy (0-100 probability of the bot acting) - 0 effectively
    # turns it off, matching what the startup banner's "Enabled"/
    # "Disabled" text reflects. An earlier version of this script
    # targeted AuctionHouseBot.Seller.Enabled/.Buyer.Enabled, which don't
    # exist in this file at all - the sed calls ran without error but
    # matched nothing, so the setting silently stayed at the file's
    # default regardless of this toggle.
    if [ "${AHBOT_ENABLED}" = "1" ]; then
        AHBOT_CHANCE=10
    else
        AHBOT_CHANCE=0
    fi
    sed -i "s#^AuctionHouseBot.Chance.Sell.*#AuctionHouseBot.Chance.Sell = ${AHBOT_CHANCE}#" "${CONF_DIR}/ahbot.conf" || true
    sed -i "s#^AuctionHouseBot.Chance.Buy.*#AuctionHouseBot.Chance.Buy = ${AHBOT_CHANCE}#" "${CONF_DIR}/ahbot.conf" || true
fi

echo "==> Setting initial realmlist entry..."
# Column set varies between mangos-classic schema revisions (e.g. some
# don't have localAddress/localSubnetMask) - try the fuller set first,
# fall back to the minimal columns every version has if that fails.
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" realmd -e "
INSERT INTO realmlist (id, name, address, localAddress, localSubnetMask, port, icon, timezone, allowedSecurityLevel, population, gamebuild)
VALUES (1, '${REALM_NAME}', '${REALM_ADDRESS}', '127.0.0.1', '255.255.255.0', ${WORLD_PORT}, 0, 1, 0, 0, 5875)
ON DUPLICATE KEY UPDATE name='${REALM_NAME}', address='${REALM_ADDRESS}', port=${WORLD_PORT};
" 2>/dev/null || \
mysql --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" realmd -e "
INSERT INTO realmlist (id, name, address, port)
VALUES (1, '${REALM_NAME}', '${REALM_ADDRESS}', ${WORLD_PORT})
ON DUPLICATE KEY UPDATE name='${REALM_NAME}', address='${REALM_ADDRESS}', port=${WORLD_PORT};
" || echo "!! Could not set realmlist entry - check the realmlist table schema manually"

echo "==> Shutting down temporary MariaDB instance..."
mysqladmin --socket=/mnt/server/mysql.sock -u root -p"${DB_ROOT_PASSWORD}" shutdown
wait "${MYSQL_PID}" 2>/dev/null || true

# NOTE: relocating mangosd/realmd to the writable volume (so their
# hardcoded ../etc/ config lookup resolves correctly - see start.sh for
# the full explanation) is NOT done here. It lives entirely in start.sh
# instead, which runs it unconditionally on every single boot before
# anything else - a fast, DB-independent operation (just file copies).
# That way the server can always start correctly regardless of whether
# install.sh made it all the way through every step (e.g. if a slow step
# like the Full_DB import or schema updates gets interrupted) - the same
# self-healing principle already used for databases/grants/config sync
# elsewhere in this setup.

echo ""
echo "=========================================="
echo " Base install complete."
echo "=========================================="
echo ""
echo "STILL REQUIRED before the world server will actually run correctly:"
echo "map / vmap / dbc / mmap data has to come from your own legally-owned"
echo "WoW 1.12.1 client - it can't be generated or downloaded here."
echo ""
echo "  1. Upload your client files into /home/container/client"
echo "  2. From a container shell, in /opt/mangos/bin, run the extractor"
echo "     tools (ad, vmap_extractor, vmap_assembler, mmaps_generator)"
echo "  3. Copy the resulting dbc/, maps/, vmaps/, mmaps/ into"
echo "     /home/container/server/data/"
echo ""
echo "See the README for the full walkthrough."
echo "=========================================="