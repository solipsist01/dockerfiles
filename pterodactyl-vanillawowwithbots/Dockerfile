## MaNGOS Classic + Playerbots (vanilla 1.12.1) all-in-one Pterodactyl image
## Same as the base Dockerfile, clones the SAME cmangos/mangos-classic
## repo as vanilla, but builds with the extra -DBUILD_PLAYERBOTS=1 flag,
## adding AI-controlled bot characters. This is a SEPARATE image/egg from
## the plain vanilla one on purpose - Pterodactyl only allows one install
## container per egg, and the install step needs to match whatever
## binary the runtime image actually has, so the two variants can't
## safely share a single egg.
##
## Build & push before importing the (separate) playerbots egg:
##   cd playerbots
##   docker build -t ghcr.io/solipsist01/pterodactyl-vanillawowwithbots:latest .
##   docker push ghcr.io/solipsist01/pterodactyl-vanillawowwithbots:latest
##
## To build a specific branch/tag of mangos-classic / classic-db instead
## of master (keep them in step - classic-db master tracks core master):
##   docker build --build-arg MANGOS_BRANCH=some-tag --build-arg CLASSICDB_BRANCH=some-tag -t ... .

############################################
# Stage 1: builder
############################################
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        pkg-config \
        libssl-dev \
        libmariadb-dev \
        libmariadb-dev-compat \
        libboost-all-dev \
        libreadline-dev \
    && rm -rf /var/lib/apt/lists/*

ARG MANGOS_BRANCH=master
ARG CLASSICDB_BRANCH=master

# CORRECTED: clones the same cmangos/mangos-classic repo as the vanilla
# build - NOT a separate cmangos/playerbots repo. Playerbots turned out
# to be a build option on core itself (mangos-classic ships its own
# README_PLAYERBOT.txt and BUILD_PLAYERBOTS in cmake/options.cmake,
# alongside BUILD_AHBOT etc.) - "with bots" vs "without bots" is a
# build-time decision (-DBUILD_PLAYERBOTS=1), not a different source
# tree. What IS genuinely runtime-togglable (see install.sh/start.sh) is
# whether bots are actually spawned/active, via AiPlayerbot.Enabled in
# their own config file.
WORKDIR /usr/src
RUN git clone --recursive --branch "${MANGOS_BRANCH}" https://github.com/cmangos/mangos-classic.git mangos-classic \
    && git clone --depth 1 --branch "${CLASSICDB_BRANCH}" https://github.com/cmangos/classic-db.git classic-db

WORKDIR /usr/src/mangos-classic
RUN mkdir build && cd build && cmake .. \
        -DCMAKE_INSTALL_PREFIX=/opt/mangos \
        -DCMAKE_BUILD_TYPE=Release \
        -DPCH=1 \
        -DBUILD_EXTRACTORS=1 \
        -DBUILD_MYSQL=1 \
        -DBUILD_PLAYERBOTS=1 \
        -DBUILD_AHBOT=1 \
    && make -j"$(nproc)" \
    && make install

# Stage playerbots' own SQL content (characters-db and world-db additions,
# vanilla-specific subfolder) and its config template at predictable
# paths, so install.sh/start.sh can find them without guessing exact
# upstream paths. NOTE: exact folder layout here is based on playerbots'
# published README instructions, now at least confirmed to be the right
# base repo (this is a real mangos-classic checkout, not a separate fork)
# - but the exact subfolder names weren't verified end-to-end before this
# build. On failure this prints the real directory tree so the paths
# below can be corrected directly from that output.
RUN set -e; \
    BOTS_DIR="/usr/src/mangos-classic/src/modules/PlayerBots"; \
    if [ ! -d "${BOTS_DIR}" ]; then \
        echo "!! ${BOTS_DIR} doesn't exist - BUILD_PLAYERBOTS may not have"; \
        echo "!! pulled in a Bots module at this path. Directory tree of src/modules:"; \
        find /usr/src/mangos-classic/src/modules -maxdepth 3 -type d 2>&1 || echo "(src/modules doesn't exist at all)"; \
        exit 1; \
    fi; \
    CHAR_SQL_DIR="${BOTS_DIR}/sql/characters"; \
    WORLD_SQL_DIR="${BOTS_DIR}/sql/world/classic"; \
    WORLD_SQL_SHARED_DIR="${BOTS_DIR}/sql/world"; \
    if [ ! -d "${CHAR_SQL_DIR}" ] || [ ! -d "${WORLD_SQL_DIR}" ]; then \
        echo "!! Expected SQL folders not found at ${CHAR_SQL_DIR} / ${WORLD_SQL_DIR}"; \
        echo "!! Complete sql/ subtree under ${BOTS_DIR}:"; \
        find "${BOTS_DIR}/sql" -type d; \
        exit 1; \
    fi; \
    mkdir -p /opt/mangos/sql/playerbots/characters /opt/mangos/sql/playerbots/world; \
    cp "${CHAR_SQL_DIR}"/*.sql /opt/mangos/sql/playerbots/characters/; \
    cp "${WORLD_SQL_DIR}"/*.sql /opt/mangos/sql/playerbots/world/; \
    # Some content (bot dialogue text, RPG travel race data, table
    # indexes - confirmed via direct inspection of the real repo) lives
    # directly in sql/world/ itself, one level above the classic/
    # subfolder, not duplicated per-expansion since it's expansion-
    # agnostic. -maxdepth 1 so this only grabs files directly here, not
    # re-copying anything already pulled from classic/ above.
    find "${WORLD_SQL_SHARED_DIR}" -maxdepth 1 -iname '*.sql' -exec cp {} /opt/mangos/sql/playerbots/world/ \; ; \
    AIPB_DIST="$(find /opt/mangos -iname 'aiplayerbot*.conf.dist' 2>/dev/null | head -n1)"; \
    if [ -z "${AIPB_DIST}" ]; then \
        AIPB_DIST="$(find "${BOTS_DIR}/playerbot" -maxdepth 1 -iname 'aiplayerbot.conf.dist.in.classic' 2>/dev/null | head -n1)"; \
    fi; \
    if [ -z "${AIPB_DIST}" ]; then \
        AIPB_DIST="$(find "${BOTS_DIR}/playerbot" -maxdepth 1 -iname 'aiplayerbot.conf.dist.in.*' \
            ! -iname '*.tbc' ! -iname '*.wotlk' ! -iname '*.cata' ! -iname '*.mop' \
            2>/dev/null | head -n1)"; \
    fi; \
    if [ -z "${AIPB_DIST}" ]; then \
        echo "!! No matching aiplayerbot config template found (checked /opt/mangos and"; \
        echo "!! ${BOTS_DIR}/playerbot). All aiplayerbot* files under the latter:"; \
        find "${BOTS_DIR}/playerbot" -maxdepth 1 -iname 'aiplayerbot*' 2>&1 || true; \
        exit 1; \
    fi; \
    echo "Using playerbots config template: ${AIPB_DIST}"; \
    if echo "${AIPB_DIST}" | grep -q '\.in\.'; then \
        echo "NOTE: this is a raw .in source template, not a make-install-processed"; \
        echo "copy - if mangosd fails to parse aiplayerbot.conf later, check for"; \
        echo "unresolved @VARIABLE@ placeholders in it."; \
    fi; \
    cp "${AIPB_DIST}" /opt/mangos/aiplayerbot.conf.dist

# Stage everything needed to build the databases, without carrying the
# whole build tree (object files, .git, etc) into the runtime image.
#
# World DB: we no longer import only classic-db's Full_DB snapshot. That
# file is just the BASE milestone (z2815 "Melting Pot v2", years old) -
# all content since lives in classic-db's Updates/, and the spell data
# (spell_template, which cmangos reads from the DB instead of Spell.dbc)
# lives in core's sql/base/dbc/. Importing only Full_DB left the server
# on stale spell data - spell power etc. not applying. Instead, install
# runs classic-db's own InstallFullDB.sh in its headless "-World" mode
# (see build-worlddb.sh), which applies Full_DB + every content update +
# newer core updates + dbc/spell data + scriptdev2 + ACID in the order
# upstream intends. It needs a "core path" with core's sql/ tree and
# src/shared/revision_sql.h - staged at /opt/mangos/core below.
#
# /opt/mangos/sql/{base,updates} stay where they were (install.sh and
# start.sh reference them directly).
#
# BUILD_INFO records the exact core + classic-db commits baked into this
# image. start.sh compares it against what the world DB was last built
# from and rebuilds the world DB automatically when they differ - so the
# monthly CI rebuild + a restart is enough to pick up new content.
RUN set -e; \
    mkdir -p /opt/mangos/sql /opt/mangos/core/src/shared; \
    cp -r /usr/src/mangos-classic/sql/base    /opt/mangos/sql/base; \
    cp -r /usr/src/mangos-classic/sql/updates /opt/mangos/sql/updates; \
    cp -r /usr/src/mangos-classic/sql         /opt/mangos/core/sql; \
    cp /usr/src/mangos-classic/src/shared/revision_sql.h /opt/mangos/core/src/shared/; \
    for d in sql/updates/mangos sql/base/dbc/original_data sql/base/dbc/cmangos_fixes sql/scriptdev2; do \
        [ -d "/opt/mangos/core/${d}" ] || { echo "!! core ${d} missing - InstallFullDB.sh -World would refuse to run"; exit 1; }; \
    done; \
    [ -x /usr/src/classic-db/InstallFullDB.sh ] || { echo "!! classic-db InstallFullDB.sh missing/not executable - repo layout changed?"; exit 1; }; \
    ls /usr/src/classic-db/Full_DB/*.sql.gz >/dev/null 2>&1 || { echo "!! classic-db Full_DB/*.sql.gz missing - repo layout changed?"; exit 1; }; \
    CORE_REV="$(git -C /usr/src/mangos-classic rev-parse --short=12 HEAD)"; \
    CDB_REV="$(git -C /usr/src/classic-db rev-parse --short=12 HEAD)"; \
    echo "core=${CORE_REV} classic-db=${CDB_REV}" > /opt/mangos/BUILD_INFO; \
    echo "BUILD_INFO: $(cat /opt/mangos/BUILD_INFO)"; \
    rm -rf /usr/src/classic-db/.git; \
    cp -r /usr/src/classic-db /opt/mangos-db

############################################
# Stage 2: runtime
############################################
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

# NOTE on "lean": libboost-all-dev pulls in headers/static libs we don't
# strictly need at runtime, only the .so files matter. It's kept here for
# reliability (guarantees whatever boost components mangosd/realmd were
# linked against are present) rather than guessing exact runtime package
# names. Once you've got a server running, `ldd /opt/mangos/bin/mangosd |
# grep boost` will tell you exactly which libboost-*1.7x packages you
# actually need, and you can swap this line for just those to shrink the
# image further.
RUN apt-get update && apt-get install -y --no-install-recommends \
        mariadb-server \
        mariadb-client \
        libssl3 \
        libmariadb3 \
        libboost-all-dev \
        ca-certificates \
        locales \
        tzdata \
        bash \
        nano \
    && sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/mangos          /opt/mangos
COPY --from=builder /opt/mangos-db       /opt/mangos-db
COPY start.sh /opt/mangos/start.sh
COPY build-worlddb.sh /opt/mangos/build-worlddb.sh
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /opt/mangos/start.sh /opt/mangos/build-worlddb.sh /entrypoint.sh /opt/mangos-db/InstallFullDB.sh

# wings maps its own (non-root, often UID-less) user into the container at
# runtime, so we don't create/switch users here - that's wings' job.
WORKDIR /home/container

# IMPORTANT: this is an ENTRYPOINT, not just a CMD - relying on wings to
# override a bare `CMD ["bash"]` with the egg's startup command turned
# out not to happen reliably in practice, which is why this exists at
# all (see git history / README for the debugging story). It points at
# a small wrapper script rather than start.sh directly: the
# wrapper runs whatever command it's given (what the install step should
# provide via its own --entrypoint bash override) and only falls back to
# launching start.sh when invoked with no arguments at all -
# so the image behaves correctly for both install and runtime even if
# something about that entrypoint override doesn't take effect the way
# it's expected to.
ENTRYPOINT ["/entrypoint.sh"]