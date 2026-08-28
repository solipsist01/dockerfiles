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
    && make -j"$(nproc)" \
    && make install


RUN mkdir -p /opt/mangos/sql \
    && cp -r /usr/src/mangos-classic/sql/base     /opt/mangos/sql/base \
    && cp -r /usr/src/mangos-classic/sql/archives /opt/mangos/sql/archives \
    && rm -rf /usr/src/classic-db/.git \
    && cp -r /usr/src/classic-db /opt/mangos-db


FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

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

COPY --from=builder /opt/mangos    /opt/mangos
COPY --from=builder /opt/mangos-db /opt/mangos-db


WORKDIR /home/container
CMD ["bash"]