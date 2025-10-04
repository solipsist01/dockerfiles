FROM jenkins/inbound-agent:latest-alpine

USER root

RUN apk update && \
    apk add python3 python3-dev py3-pip ffmpeg tzdata mkvtoolnix \
    dcron curl wget ca-certificates build-base linux-headers \
    libffi-dev gettext curl gcc musl-dev libressl-dev perl unzip \
    tar pigz bash jq coreutils netcat-openbsd vnstat bc lm_sensors smartmontools py-cryptography \
    docker-cli openssl \
    && rm -rf /var/cache/apk/*

RUN curl https://rclone.org/uninstall.sh | bash

ENV CRYPTOGRAPHY_DONT_BUILD_RUST=1

RUN python3 -m pip install -U pip --break-system-packages && \
    pip install wheel requests youtube-dl pymkv graphyte holidays python-binance telethon ccxt docker ffmpeg-python --break-system-packages