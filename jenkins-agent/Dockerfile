FROM moonbuggy2000/jenkins-agent:inbound-agent-debianslim

USER root

RUN curl -sSL https://get.docker.com | sh

RUN apt update && \
    apt install python3 python3-dev py3-pip

RUN python3 -m pip install -U pip && \
    pip install wheel requests graphyte docker