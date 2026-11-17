# OV Game server: webinterface + launcher/client-downloads + wereld-streaming + multiplayer (WebSocket).
#
# Alles wat nodig is om te bouwen staat in deze map (bijwerken vanuit de game: /workspace/ovgame/tools/sync_dockerfiles.sh):
#   server/                  de server (Go)
#   src/ + tools/build_windows.sh   de game, de launcher (mingw-w64) en de installer (NSIS)
# In de image zitten:
#   /usr/local/bin/ovserver
#   /opt/ovgame/client/OVGame.exe, OVLauncher.exe, OVGameSetup.exe
# De wereld en de rest van de clientdata zitten NIET in de image: mount de data-map van de game op /gamedata (ro).

# ---------- server (Go) ----------
FROM golang:1.23-alpine AS server
WORKDIR /build
COPY server/ ./
RUN go mod download && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /ovserver .

# ---------- Windows-client (mingw-w64) en installer (NSIS) ----------
FROM ubuntu:24.04 AS client
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 nsis \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY src/ ./src/
COPY tools/build_windows.sh ./tools/build_windows.sh
RUN sh tools/build_windows.sh /out && ls -la /out

# ---------- runtime ----------
FROM alpine:3.20
RUN apk add --no-cache ca-certificates tzdata \
    && adduser -D -H -u 1000 ovgame \
    && mkdir -p /data /gamedata /opt/ovgame/client \
    && chown ovgame:ovgame /data
COPY --from=server /ovserver /usr/local/bin/ovserver
COPY --from=client /out/OVGame.exe /out/OVLauncher.exe /out/OVGameSetup.exe /opt/ovgame/client/

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
