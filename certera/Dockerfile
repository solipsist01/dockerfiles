FROM ubuntu:latest

RUN apt-get update && apt-get upgrade -y && \
    apt-get install wget unzip libcap2-bin -y && \
    mkdir ~/certera.io 

WORKDIR /root/certera.io

RUN wget -O certera.zip 'https://github.com/certera-io/certera/releases/download/2.1.5/certera-2.1.5-linux-x64.zip' && \
    unzip certera.zip && \
    chmod +x certera

COPY ./config.json /root/certera.io

ENV DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1

