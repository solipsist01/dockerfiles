FROM alpine:latest
 
RUN apk update && \
    apk add python3 python3-dev py3-pip ffmpeg tzdata mkvtoolnix \
    dcron curl wget ca-certificates build-base linux-headers \
    libffi-dev gettext curl gcc musl-dev libressl-dev perl rclone \
    tar pigz bash jq coreutils netcat-openbsd vnstat bc lm_sensors smartmontools py-cryptography \
    docker-cli openssl openssl-dev \
    && rm -rf /var/cache/apk/*

ENV CRYPTOGRAPHY_DONT_BUILD_RUST=1

RUN mkdir -p /var/log/cron \
&& mkdir -m 0644 -p /var/spool/cron/crontabs \
&& touch /var/log/cron/cron.log \
&& mkdir -m 0644 -p /etc/cron.d \
&& python3 -m pip install -U pip \
&& pip install wheel requests youtube-dl pymkv graphyte holidays python-binance telethon

COPY /scripts/* /

RUN chmod +x /docker-entry.sh && chmod +x /docker-cmd.sh

ENTRYPOINT ["/docker-entry.sh"]
CMD ["/docker-cmd.sh"]
