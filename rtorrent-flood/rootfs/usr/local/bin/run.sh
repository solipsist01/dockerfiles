#!/bin/sh

mkdir -p /data/torrents
mkdir -p /data/.watch
mkdir -p /data/.session

rm -f /data/.session/rtorrent.lock

if [ ! -f /data/.session/owned.lock ]; then
        chown -R $UID:$GID /data /home/torrent /tmp /usr/flood /flood-db /etc/s6.d && touch /data/.session/owned.lock
fi

if [ ${RTORRENT_SCGI} -ne 0 ]; then
    sed -i -e 's|^scgi_local.*$|scgi_port = 0.0.0.0:'${RTORRENT_SCGI}'|' /home/torrent/.rtorrent.rc
    sed -i -e 's|socket: true,|socket: false,|' -e 's|port: 5000,|port: '${RTORRENT_SCGI}',|' /usr/flood/config.js
fi

exec su-exec $UID:$GID /bin/s6-svscan /etc/s6.d
