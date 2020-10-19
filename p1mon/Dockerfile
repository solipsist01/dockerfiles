#FROM debian:buster-slim
FROM debian:buster

# Voeg p1mon en www-data user toe met juiste UID/GID
RUN addgroup --gid 1004 p1mon;addgroup -gid 997 gpio;useradd --gid 1004 --uid 1001 --create-home --password '$6$YrKO7PGalxElg00B$DhGh02AJO4gst7rA5YENOd5Y8zp/ksqvWnTzv2gZtq0C2GeuGPaI7Y7CW8NXS0N63LI3YlJPEl4/FZToYKnpS1' --groups dialout,sudo,www-data,gpio p1mon;adduser www-data p1mon

#RUN mkdir /var/log/p1mon;chown p1mon:p1mon /var/log/p1mon

# Tijdzone
RUN ln -fs /usr/share/zoneinfo/Europe/Amsterdam /etc/localtime;dpkg-reconfigure -f noninteractive tzdata

# Installeer missende packages	
RUN apt update;apt upgrade -y;apt install -y procps python3-venv python3-pip nginx-full php-fpm sqlite3 php-sqlite3 python3-cairo python-apt nano cron git logrotate libsodium-dev libffi-dev

# Installeer sudo met RUNLEVEL env var anders klaagt hij
RUN rm /etc/sudoers;RUNLEVEL=1 apt install -y sudo

# p1mon mag zonder wachtwoord alles met sudo uitvoeren
# www-data mag zonder wachtwoord alle p1mon scripts als user p1mon (met sudo -u p1mon) uitvoeren
RUN echo >>/etc/sudoers "p1mon ALL=(ALL) NOPASSWD: ALL";echo >>/etc/sudoers "www-data ALL=(p1mon) NOPASSWD: /p1mon/scripts/*"

# Installeer missende Python packages voor root
RUN pip3 install wheel setuptools asn1crypto bcrypt certifi cffi chardet colorzero cryptography dropbox entrypoints falcon future gpiozero idna iso8601 keyring keyrings.alt paho-mqtt pip psutil pycparser pycrypto PyGObject pyserial python-crontab python-dateutil pytz pyxdg PyYAML requests RPi.GPIO SecretStorage setuptools six spidev urllib3;SODIUM_INSTALL=system pip3 install PyNaCl

# De correcte PyCRC bestaat niet als pip module? Installeer apart van de source
RUN cd /tmp;git clone https://github.com/alexbutirskiy/PyCRC.git;cd PyCRC;python3 ./setup.py build;python3 ./setup.py install;cd ..;rm -rf PyCRC

# Kopieer settings, scripts etc voor p1mon, nginx en fpm-php en verwijder overbodige cron scripts
COPY . .
RUN rm /etc/cron.daily/apt-compat /etc/cron.daily/dpkg /etc/cron.daily/exim4-base /etc/cron.daily/passwd /etc/logrotate.d/alternatives /etc/logrotate.d/apt /etc/logrotate.d/dpkg /etc/logrotate.d/exim4-base /etc/logrotate.d/exim4-paniclog 

# Vervang diverse commando's / scripts:
# /sbin/init door een script dat 'start_all.sh' vertelt dat er een shutdown nodig is (reboot wordt ook als shutdown geinterpreteerd)
# /usr/local/sbin/mount door een script dat niks doet zodat mount geen foutmelding geeft
# p1monExec door shell script zodat het ook op andere architecturen dan arm werkt (in combinatie met de sudoers www-data antry)
RUN mv /p1mon/scripts/p1monExec /p1mon/scripts/p1monExec-orig;cp -a /opt/p1mon-mods/* /;chown -R p1mon:p1mon /p1mon;chown -R www-data:www-data /var/lib/nginx;chmod +s /bin/ping

# Installeer missende Python packages voor p1mon
USER p1mon
RUN pip3 install gunicorn

USER root

CMD [ "/start_all.sh" ]

