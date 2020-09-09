FROM theiaide/theia-full

USER root

RUN apt-get --assume-yes update && apt-get --assume-yes install software-properties-common lsb-release 

RUN add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"

RUN apt-get --assume-yes install docker-ce-cli && pip install docker-compose