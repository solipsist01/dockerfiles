FROM theiaide/theia-python

USER root

RUN apt-get --assume-yes update && apt-get --assume-yes install curl software-properties-common lsb-release apt-transport-https ca-certificates gnupg-agent \
    && apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys 9578539176BAFBC6 7EA0A9C3F273FCD8

RUN curl -fsSL https://download.docker.com/linux/debian/gpg | apt-key add - \
    && add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/debian \
   $(lsb_release -cs) \
   stable"

RUN apt-get update && apt-get --assume-yes install docker-ce-cli && /usr/bin/python3 -m pip install -U pylint --user \
    && curl --silent "https://api.github.com/repos/docker/compose/releases/latest" | \
    grep '"tag_name":' | \
    sed -E 's/.*"([^"]+)".*/\1/' | \
    xargs -I {} curl -sL "https://github.com/docker/compose/releases/download/"{}'/docker-compose-Linux-x86_64' -o /usr/local/bin/docker-compose \
    && chmod +x /usr/local/bin/docker-compose
