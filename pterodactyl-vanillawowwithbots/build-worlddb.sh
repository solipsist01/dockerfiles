#!/bin/bash
## Builds (or rebuilds) the world ("mangos") database from scratch, the
## same way upstream does it: by running classic-db's own InstallFullDB.sh
## in its headless "-World" mode instead of only importing the Full_DB
## snapshot.
##
## Why this exists: classic-db's Full_DB/*.sql.gz is only the BASE
## milestone (z2815, "Melting Pot v2" - years old). Everything since then
## lives in Updates/*.sql (~400 content updates) plus core-side data that
## the installer pulls straight from mangos-classic:
##   - sql/updates/mangos       core schema migrations newer than the DB
##   - sql/base/dbc/original_data + cmangos_fixes
##                              the spell_template table (and friends) -
##                              cmangos reads spell data from the DB, not
##                              Spell.dbc. Never importing this left the
##                              server on a stale spell_template from the
##                              old dump: the cause of spell power (and
##                              other spell effects) not working.
##   - sql/scriptdev2           script bindings
##   - sql/base/ahbot           AHBot GM command docs
## plus ACID and cmangos_custom.sql from classic-db itself. InstallFullDB.sh
## applies all of that in the right order, so we let it do the work.
##
## World DB is pure content (no accounts/characters live here), so
## dropping and rebuilding it is safe - characters/realmd are untouched.
##
## Usage: build-worlddb.sh <mysql-socket> <writable-workdir> <stamp-file>
##   env: DB_ROOT_PASSWORD, DB_PASSWORD
## Called from install.sh (fresh install/reinstall) and from start.sh
## (automatically, whenever the image was rebuilt with newer content).

SOCK="$1"
WORKDIR="$2"
STAMP_FILE="$3"
DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-changeme_root}"
DB_PASSWORD="${DB_PASSWORD:-mangos}"
CORE_PATH="/opt/mangos/core"
IMAGE_STAMP="$(cat /opt/mangos/BUILD_INFO 2>/dev/null || echo unknown)"

if [ -z "${SOCK}" ] || [ -z "${WORKDIR}" ] || [ -z "${STAMP_FILE}" ]; then
    echo "usage: $0 <mysql-socket> <writable-workdir> <stamp-file>"
    exit 2
fi

rootsql() { mysql --socket="${SOCK}" -u root -p"${DB_ROOT_PASSWORD}" "$@"; }

echo "==> Building world database from latest classic-db + core data"
echo "    image build: ${IMAGE_STAMP}"

echo "  - Recreating empty 'mangos' database..."
# Grants on mangos.* live in mysql.db and survive the DROP.
rootsql -e "DROP DATABASE IF EXISTS mangos; CREATE DATABASE mangos DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci;" || exit 1
rootsql mangos < "${CORE_PATH}/sql/base/mangos.sql" || { echo "!! base mangos.sql failed"; exit 1; }

echo "  - Preparing writable classic-db copy at ${WORKDIR}..."
rm -rf "${WORKDIR}"
cp -r /opt/mangos-db "${WORKDIR}" || exit 1

# Headless config for InstallFullDB.sh. Connects over TCP as the 'mangos'
# user (which exists for both localhost and 127.0.0.1, with ALL on all
# four databases). Also leaves a ready-to-use config behind if you ever
# want to run ./InstallFullDB.sh interactively from that folder.
cat > "${WORKDIR}/InstallFullDB.config" <<EOF
MYSQL_HOST="127.0.0.1"
MYSQL_PORT="3306"
MYSQL_USERNAME="mangos"
MYSQL_PASSWORD="${DB_PASSWORD}"
MYSQL_USERIP="localhost"
WORLD_DB_NAME="mangos"
REALM_DB_NAME="realmd"
CHAR_DB_NAME="characters"
LOGS_DB_NAME="classiclogs"
MYSQL_PATH="$(command -v mysql)"
MYSQL_DUMP_PATH="$(command -v mysqldump)"
CORE_PATH="${CORE_PATH}"
LOCALES="NO"
FORCE_WAIT="NO"
DEV_UPDATES="NO"
AHBOT="YES"
PLAYERBOTS_DB="NO"
EOF
chmod 600 "${WORKDIR}/InstallFullDB.config"

echo "  - Running InstallFullDB.sh -World (Full_DB + all content updates + core"
echo "    updates + spell/dbc data + scriptdev2 + ACID). Takes a few minutes;"
echo "    a heartbeat prints every 30s so the console doesn't look frozen."
(
    cd "${WORKDIR}" || exit 1
    TERM=dumb ./InstallFullDB.sh -World
) &
IFDB_PID=$!
(
    SECS=0
    while kill -0 "${IFDB_PID}" 2>/dev/null; do
        sleep 30
        SECS=$((SECS + 30))
        kill -0 "${IFDB_PID}" 2>/dev/null && echo "    ... world DB build still running (${SECS}s elapsed)"
    done
) &
HB_PID=$!
wait "${IFDB_PID}"
RC=$?
kill "${HB_PID}" 2>/dev/null
wait "${HB_PID}" 2>/dev/null

# The extracted Full_DB .sql is ~hundreds of MB and only needed during the
# build - the .gz stays, so InstallFullDB.sh can re-extract it any time.
rm -f "${WORKDIR}"/Full_DB/*.sql

if [ "${RC}" -ne 0 ]; then
    echo "!! InstallFullDB.sh -World FAILED (exit ${RC}) - see the 'FAILED!' line above"
    echo "!! for the exact file/statement. World DB is incomplete; not stamping it,"
    echo "!! so the next boot will retry the build."
    exit 1
fi

echo "  - Importing playerbots world SQL..."
for f in $(ls /opt/mangos/sql/playerbots/world/*.sql 2>/dev/null | sort); do
    OUT="$(rootsql mangos < "$f" 2>&1)" \
        && echo "      applied (world) $(basename "$f")" \
        || echo "      !! (world) $(basename "$f"): $(echo "${OUT}" | head -n1)"
done

VER="$(rootsql -N -e "SELECT version FROM mangos.db_version LIMIT 1" 2>/dev/null)"
REQ="$(rootsql -N -e "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='mangos' AND TABLE_NAME='db_version' AND COLUMN_NAME LIKE 'required_%'" 2>/dev/null)"
CONTENT="$(rootsql -N -e "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='mangos' AND TABLE_NAME='db_version' AND COLUMN_NAME LIKE 'content_%'" 2>/dev/null)"
echo "    World DB: ${VER}"
echo "    Schema:   ${REQ}   Content: ${CONTENT}"

echo "${IMAGE_STAMP}" > "${STAMP_FILE}"
echo "==> World database build complete."
exit 0
