# syntax=docker/dockerfile:1.7
# OV Game server: webinterface + launcher/client-downloads + wereld-streaming + multiplayer (WebSocket).
#
# De broncode komt uit de ovgame-repo (git clone). In de image zitten:
#   /usr/local/bin/ovserver        de server (Go)
#   /opt/ovgame/client/OVGame.exe  de game (Windows, gebouwd met mingw-w64)
#   /opt/ovgame/client/OVLauncher.exe
# De wereld en de rest van de clientdata zitten NIET in de image: mount de data-map van de game op /gamedata (ro).
#
# Lokaal bouwen vanuit een werkmap in plaats van git:
#   docker buildx build --build-context src=/pad/naar/ovgame -t ovgame .
# Private repo: geef een token mee als secret (id github_token).

ARG OVGAME_REPO=solipsist01/ovgame
ARG OVGAME_REF=main

# ---------- broncode ----------
FROM alpine:3.20 AS fetch
ARG OVGAME_REPO
ARG OVGAME_REF
RUN apk add --no-cache git ca-certificates
RUN --mount=type=secret,id=github_token \
    set -e; \
    if [ -s /run/secrets/github_token ]; then \
        url="https://x-access-token:$(cat /run/secrets/github_token)@github.com/${OVGAME_REPO}.git"; \
    else \
        url="https://github.com/${OVGAME_REPO}.git"; \
    fi; \
    git clone --depth 1 --branch "${OVGAME_REF}" "$url" /src || \
        { git clone "$url" /src && git -C /src checkout "${OVGAME_REF}"; }; \
    git -C /src rev-parse --short HEAD > /src/.commit; \
    rm -rf /src/.git

FROM scratch AS src
COPY --from=fetch /src /

# ---------- server (Go) ----------
FROM golang:1.23-alpine AS server
WORKDIR /build
COPY --from=src /server/ ./
RUN go mod download && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /ovserver .

# ---------- Windows-client (mingw-w64) ----------
FROM ubuntu:24.04 AS client
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY --from=src /src/ ./src/
COPY --from=src /tools/build_windows.sh ./tools/build_windows.sh
RUN sh tools/build_windows.sh /out && ls -la /out

# ---------- runtime ----------
FROM alpine:3.20
RUN apk add --no-cache ca-certificates tzdata \
    && adduser -D -H -u 1000 ovgame \
    && mkdir -p /data /gamedata /opt/ovgame/client \
    && chown ovgame:ovgame /data
COPY --from=server /ovserver /usr/local/bin/ovserver
COPY --from=client /out/OVGame.exe /out/OVLauncher.exe /opt/ovgame/client/

ENV PORT=8080 \
    DATA=/data \
    GAME_DATA=/gamedata \
    CLIENT_BIN=/opt/ovgame/client \
    SERVER_NAME="OV Game server" \
    GAME_CLOCK=real \
    MAX_PLAYERS=32

USER ovgame
VOLUME /data
EXPOSE 8080
HEALTHCHECK --interval=60s --timeout=5s --start-period=30s \
    CMD wget -q -O /dev/null http://127.0.0.1:${PORT}/api/status || exit 1
ENTRYPOINT ["/usr/local/bin/ovserver"]
