FROM debian:10-slim

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get -y install libstdc++5 libstdc++5:i386 \
    && mkdir /app && mkdir /config

ADD mohaa_mini /app
WORKDIR /app
RUN chmod 777 /app/mohaa_lnxded && chmod 777 /app/init.sh

CMD /app/init.sh

EXPOSE 12300/udp
EXPOSE 12203/udp