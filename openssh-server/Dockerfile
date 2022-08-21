FROM linuxserver/openssh-server:latest

RUN apk update && apk del openssh-server openssh-keygen openssh-client openssh-server-pam
RUN sed -i -e 's/v[[:digit:]]\..*\//edge\//g' /etc/apk/repositories
RUN OPENSSH_RELEASE=$(curl -s http://dl-cdn.alpinelinux.org/alpine/edge/main/x86_64/ | awk -F '(openssh-server-|.apk)' '/openssh-server.*.apk/ {print $2; exit}') \
    && apk add --no-cache openssh-server==${OPENSSH_RELEASE} openssh-sftp-server==${OPENSSH_RELEASE} openssh-server-pam==${OPENSSH_RELEASE} openssh-keygen==${OPENSSH_RELEASE}
COPY /sshd_config /etc/ssh/sshd_config
