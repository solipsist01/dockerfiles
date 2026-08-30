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
## To build a specific branch/tag of mangos-classic instead of master:
##   docker build --build-arg MANGOS_BRANCH=some-tag -t ... .

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
    && git clone https://github.com/cmangos/classic-db.git classic-db

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
    CHAR_SQL_DIR="${BOTS_DIR}/sql/characters/vanilla"; \
    WORLD_SQL_DIR="${BOTS_DIR}/sql/world/vanilla"; \
    if [ ! -d "${CHAR_SQL_DIR}" ] || [ ! -d "${WORLD_SQL_DIR}" ]; then \
        echo "!! Expected SQL folders not found at ${CHAR_SQL_DIR} / ${WORLD_SQL_DIR}"; \
        echo "!! Actual layout under ${BOTS_DIR}:"; \
        find "${BOTS_DIR}" -maxdepth 4 -type d; \
        exit 1; \
    fi; \
    mkdir -p /opt/mangos/sql/playerbots/characters /opt/mangos/sql/playerbots/world; \
    cp "${CHAR_SQL_DIR}"/*.sql /opt/mangos/sql/playerbots/characters/; \
    cp "${WORLD_SQL_DIR}"/*.sql /opt/mangos/sql/playerbots/world/; \
    AIPB_DIST="$(find "${BOTS_DIR}/playerbot" -iname 'aiplayerbot*.conf.dist' 2>/dev/null | head -n1)"; \
    if [ -z "${AIPB_DIST}" ]; then \
        echo "!! No aiplayerbot*.conf.dist found under ${BOTS_DIR}/playerbot"; \
        find "${BOTS_DIR}/playerbot" -maxdepth 2 2>&1 || true; \
        exit 1; \
    fi; \
    cp "${AIPB_DIST}" /opt/mangos/aiplayerbot.conf.dist

# Stage the SQL files needed to seed the databases, next to the install
# prefix, so the runtime image gets everything without carrying the whole
# build tree (object files, .git, etc).
#
# NOTE on classic-db content import: InstallFullDB.sh in that repo is a
# genuinely interactive, menu-driven tool (prompts for the MySQL root
# password, numbered options) - it isn't something a config file can
# drive headlessly. Rather than automate its interactive flow, we take
# the simpler, reliable path: the repo also ships a complete, internally
# consistent pre-built world DB snapshot under Full_DB/*.sql.gz, which we
# extract here at build time and just import with a plain `mysql <` at
# install time. Trade-off: this pins content to whatever release was
# current when the image was built, rather than picking up incremental
# commits since the last release - the full classic-db repo (including
# InstallFullDB.sh) is still copied into the image if you want to run
# that interactively later for the latest content.
RUN mkdir -p /opt/mangos/sql \
    && cp -r /usr/src/mangos-classic/sql/base    /opt/mangos/sql/base \
    && cp -r /usr/src/mangos-classic/sql/updates /opt/mangos/sql/updates \
    && FULLDB_GZ="$(find /usr/src/classic-db/Full_DB -maxdepth 1 -iname 'ClassicDB_*.sql.gz' | head -n1)" \
    && if [ -z "${FULLDB_GZ}" ]; then echo "!! Could not find Full_DB/ClassicDB_*.sql.gz in classic-db - repo layout may have changed" && exit 1; fi \
    && mkdir -p /opt/mangos-db-dump \
    && gzip -dc "${FULLDB_GZ}" > /opt/mangos-db-dump/classic-db-full.sql \
    && ACID_SQL="$(find /usr/src/classic-db/ACID -maxdepth 1 -iname 'acid_classic.sql' 2>/dev/null | head -n1)" \
    && if [ -n "${ACID_SQL}" ]; then cp "${ACID_SQL}" /opt/mangos-db-dump/acid_classic.sql; fi \
    && rm -rf /usr/src/classic-db/.git \
    && cp -r /usr/src/classic-db /opt/mangos-db

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
    && sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/mangos          /opt/mangos
COPY --from=builder /opt/mangos-db       /opt/mangos-db
COPY --from=builder /opt/mangos-db-dump  /opt/mangos-db-dump
COPY start.sh /opt/mangos/start.sh
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /opt/mangos/start.sh /entrypoint.sh

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