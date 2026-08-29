#!/bin/bash
## Entrypoint wrapper. If invoked with an explicit command (as the
## install step does: something like `bash -c "<install script>"`), run
## that command instead. If invoked with no arguments at all (a plain
## `docker run <image>`, which is what the runtime container looks
## like), default to launching the game server.
##
## Why this exists: a bare `ENTRYPOINT ["/opt/mangos/start.sh"]` only
## behaves correctly for install if the caller actually overrides it
## with something like `--entrypoint bash` - if that override doesn't
## take effect for whatever reason, start.sh would run unconditionally
## for every container spun from this image, install included, which
## would silently run the wrong script with no indication anything was
## wrong. This wrapper makes the image correct either way.
if [ "$#" -gt 0 ]; then
    exec "$@"
else
    exec /opt/mangos/start.sh
fi