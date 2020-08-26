FROM alpine:latest
 
RUN apk update && \
    apk add python3 py3-pip ffmpeg tzdata mkvtoolnix \
    dcron curl wget ca-certificates build-base linux-headers \
    libffi-dev gettext curl gcc musl-dev libressl-dev perl \
    && rm -rf /var/cache/apk/*

RUN mkdir -p /var/log/cron \
&& mkdir -m 0644 -p /var/spool/cron/crontabs \
&& touch /var/log/cron/cron.log \
&& mkdir -m 0644 -p /etc/cron.d \
&& pip install requests youtube-dl pymkv torpy

COPY /scripts/* /

ENTRYPOINT ["/docker-entry.sh"]
CMD ["/docker-cmd.sh"]
