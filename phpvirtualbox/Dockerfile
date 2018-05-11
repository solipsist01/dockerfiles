FROM alpine

RUN apk add --no-cache bash nginx php7-fpm php7-cli php7-json php7-soap git \
    && apk add --no-cache --virtual build-dependencies wget unzip \
    && git clone https://github.com/phpvirtualbox/phpvirtualbox -b develop \
    && mkdir -p /var/www \
    && mv -v phpvirtualbox/*/* /var/www/ \
    && rm phpvirtualbox/ -R \
    && apk del build-dependencies \
    && echo "<?php return array(); ?>" > /var/www/config-servers.php \
    && echo "<?php return array(); ?>" > /var/www/config-override.php \
    && chown nobody:nobody -R /var/www 
    
COPY config.php /var/www/config.php
COPY nginx.conf /etc/nginx/nginx.conf
COPY servers-from-env.php /servers-from-env.php

EXPOSE 80

CMD php7 /servers-from-env.php && php-fpm7 && nginx
