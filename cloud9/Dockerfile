FROM kdelfour/cloud9-docker

RUN apt-get update && apt-get upgrade -y \
 && apt-get install -y python-dev python3-dev python-setuptools python3-setuptools mc locales libav-tools \
 && easy_install pip \
 && pip install requests youtube-dl flask \
 && apt-get autoremove -y && apt-get autoclean -y && apt-get clean -y \
 && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* \
 && npm cache clean --force \
 && git reset --hard

RUN locale-gen en_US.UTF-8

ENV LANG='en_US.UTF-8' LANGUAGE='en_US:en' LC_ALL='en_US.UTF-8'
