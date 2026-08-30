#!/bin/bash
## Entrypoint wrapper for the playerbots image. Standalone - not shared
## with the plain vanilla image's entrypoint.sh (which points at a
## different start.sh). If invoked with an explicit command (as the
## install step does), run that. If invoked with no arguments at all (a
## plain `docker run <image>`, which is what the runtime container looks
## like), default to launching the game server.
if [ "$#" -gt 0 ]; then
    exec "$@"
else
    exec /opt/mangos/start.sh
fi