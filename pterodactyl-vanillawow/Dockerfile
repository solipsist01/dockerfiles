FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        curl \
        pkg-config \
        libssl-dev \
        libmariadb-dev \
        libmariadb-dev-compat \
        libboost-all-dev \
        libreadline-dev \
        mariadb-server \
        mariadb-client \
        unzip \
        p7zip-full \
        locales \
        tzdata \
    && sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen \
    && locale-gen \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /home/container

CMD ["bash"]