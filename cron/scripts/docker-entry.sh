#!/bin/sh
set -e

rm -rf /var/spool/cron/crontabs && mkdir -m 0644 -p /var/spool/cron/crontabs
env | grep 'CRON' | while IFS= read -r line; do tmp=${line#*=} && echo "$tmp" >> /var/spool/cron/crontabs/CRON_STRINGS; done

chmod -R 0644 /var/spool/cron/crontabs

exec "$@"