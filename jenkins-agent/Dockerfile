FROM jenkins/inbound-agent

USER root

RUN curl -sSL https://get.docker.com | sh

RUN apt --assume-yes update && \
    apt --assume-yes install python3 python3-dev python3-pip wget docker-compose curl rclone pigz
RUN pip3 install wheel requests graphyte docker

# ensure all builds runs with Buildkit
#ENV DOCKER_BUILDKIT=1
# enable full log of Buildkit builds
#ENV BUILDKIT_PROGRESS=plain
# enable Buildx integration with docker
#ENV DOCKER_CLI_EXPERIMENTAL=enabled
#
#ARG BUILDX_URL=https://github.com/docker/buildx/releases/download/v0.4.2/buildx-v0.4.2.linux-amd64#
#
#RUN mkdir -p $HOME/.docker/cli-plugins && \
#    wget -O $HOME/.docker/cli-plugins/docker-buildx $BUILDX_URL && \
#    chmod a+x $HOME/.docker/cli-plugins/docker-buildx