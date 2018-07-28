FROM debian:latest
ENV DEBIAN_FRONTEND noninteractive
ENV TERM xterm

RUN echo 'deb http://ppa.launchpad.net/privacyidea/privacyidea/ubuntu trusty main' >> /etc/apt/sources.list 
RUN apt-get update
RUN apt-get -y --force-yes install python-privacyidea privacyideaadm privacyidea-radius python-setuptools python-pylons \
    python-qrcode python-netaddr python-ldap python-pyrad python-yaml python-configobj python-repoze.who python-httplib2 \
    gnupg python-software-properties software-properties-common python-pip python-dev build-essential curl \
    python-crypto python-docutils python-repoze.who-plugins 
RUN pip install enum34

RUN privacyidea-create-pwidresolver-user -u admin -p test -i 1000 >> /etc/privacyidea/admin-users

EXPOSE 80 443 5347