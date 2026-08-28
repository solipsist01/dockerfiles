#!/bin/bash
## Runtime startup wrapper. Runs on every start/restart from the panel.
## - starts MariaDB
## - syncs the realmlist row with the current REALM_NAME / REALM_ADDRESS /
##   WORLD_PORT variables, so changing them in the panel and restarting is
##   all that's needed - no manual SQL required
## - starts realmd, then runs mangosd in the foreground (panel console)
cd /home/container || exit 1

DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-changeme_root}"
REALM_NAME="${REALM_NAME:-MyMangosRealm}"
REALM_ADDRESS="${REALM_ADDRESS:-127.0.0.1}"
WORLD_PORT="${WORLD_PORT:-8085}"
SERVER_PORT="${SERVER_PORT:-3724}"

mkdir -p /home/container/mysql-run

echo "Starting MariaDB..."
mysqld_safe \
    --datadir=/home/container/mysql-data \
    --socket=/home/container/mysql.sock \
    --pid-file=/home/container/mysql-run/mysqld.pid \
    --log-error=/home/container/mysql-error.log \
    --skip-networking=0 \
    --bind-address=127.0.0.1 \
    --port=3306 &

echo "Waiting for MariaDB..."
UP=0
for i in $(seq 1 60); do
    if mysqladmin --socket=/home/container/mysql.sock ping >/dev/null 2>&1; then
        UP=1
        break
    fi
    sleep 1
done

if [ "${UP}" -ne 1 ]; then
    echo "MariaDB did not come up in time. Error log:"
    cat /home/container/mysql-error.log 2>/dev/null
    exit 1
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
/opt/mangos/bin/realmd -c /home/container/server/etc/realmd.conf \
    > /home/container/realmd.log 2>&1 &

sleep 3

echo "Starting mangosd (world server)..."
exec /opt/mangos/bin/mangosd -c /home/container/server/etc/mangosd.conf