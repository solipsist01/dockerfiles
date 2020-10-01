FROM theiaide/theia-full

USER root

RUN apt-get --assume-yes update && apt-get --assume-yes install software-properties-common lsb-release \
    && apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 9578539176BAFBC6 7EA0A9C3F273FCD8

RUN add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"

RUN apt-get --assume-yes install docker-ce-cli && pip install docker-compose && /usr/bin/python3 -m pip install -U pylint --user