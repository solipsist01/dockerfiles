FROM node:slim

ENV ARCH=amd64

RUN apt-get update \
  && apt-get install -y apt-utils locales \
  && apt-get install -y git curl tzdata libav-tools python-dev python3-dev python-setuptools python3-setuptools mc locales cron \
  && locale-gen en_US.UTF-8 \
  && curl -SLO "https://github.com/just-containers/s6-overlay/releases/download/v1.20.0.0/s6-overlay-${ARCH}.tar.gz" \
  && tar -xzf s6-overlay-amd64.tar.gz -C / \
  && tar -xzf s6-overlay-amd64.tar.gz -C /usr ./bin \
  && rm -rf s6-overlay-amd64.tar.gz \
  && useradd -u 911 -U -d /config -s /bin/false abc \
  && usermod -G users abc \
  && mkdir -p /app /config /defaults \
  && cd /tmp \
  && wget https://bootstrap.pypa.io/get-pip.py \
  && python2 get-pip.py && python3 get-pip.py \
  && pip2 install pytz requests

RUN mkdir -p /tmp/xmltv \
  && cd /tmp/xmltv \
  && wget https://github.com/tvgrabbers/DataTree/archive/master.zip \
  && unzip master.zip && rm master.zip \
  && cd DataTree-master \
  && ./setup.py install \
  && wget https://github.com/tvgrabbers/tvgrabpyAPI/archive/master.zip \
  && unzip master.zip && rm master.zip \
  && cd tvgrabpyAPI-master \
  && ./setup.py install \
  && cd /tmp \
  && rm -rf /tmp/xmltv \
  && tv_grab_nl3.py --configure \
  && echo '#!/usr/bin/with-contenv bash' > /etc/cont-init.d/50-copyconfig \
  && echo ' ' >> /etc/cont-init.d/50-copyconfig \
  && echo 'if [ ! -f /config/tv_grab_nl3_py.conf ]; then' >> /etc/cont-init.d/50-copyconfig \
  && echo '   cp /etc/tvgrabpyAPI/tv_grab_nl3_py.conf /config/tv_grab_nl3_py.conf' >> /etc/cont-init.d/50-copyconfig \
  && echo 'fi' >> /etc/cont-init.d/50-copyconfig \
  && apt-get clean \
  && rm -rf /tmp/* /var/lib/apt/lists/* /var/tmp/*
  
RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen \
  && dpkg-reconfigure --frontend=noninteractive locales \
  && update-locale LANG=en_US.UTF-8

ENV LANG en_US.UTF-8 

COPY rootfs /

RUN chmod +x /etc/services.d/cron/run

ENTRYPOINT [ "/init" ]
