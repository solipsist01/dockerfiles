## Infinite Rails dedicated server - Pterodactyl image
##
## Thin layer on top of the standard Pterodactyl SteamCMD yolk (the same one other
## Steam eggs on this node use). The server itself is NOT in the image: the egg's
## install script downloads it with SteamCMD (tool app 5484540, anonymous login,
## Linux build) into /home/container, and start.sh updates it on every boot.
##
## What this layer adds:
##   - jq, so start.sh can keep InfiniteRails_Data/config.txt in step with the
##     panel (port = allocation, autoUpdate off, optional MOTD/login/creative)
##   - start.sh as the entrypoint. Unlike the yolk's own entrypoint it runs
##     "+app_info_update 1" before "+app_update" (without it, a fresh SteamCMD
##     fails on this app with "Missing configuration").
##
## Build & push (GitHub workflow pterodactyl-infiniterails does this):
##   docker build -t ghcr.io/solipsist01/pterodactyl-infiniterails:latest .
FROM ghcr.io/ptero-eggs/steamcmd:debian

USER root
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends jq \
    && rm -rf /var/lib/apt/lists/*

COPY start.sh /start.sh
RUN chmod +x /start.sh

USER container
ENV USER=container HOME=/home/container
WORKDIR /home/container

ENTRYPOINT ["/usr/bin/tini", "-g", "--"]
CMD ["/start.sh"]
