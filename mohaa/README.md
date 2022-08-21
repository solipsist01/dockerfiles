# Mohaa Reborn 1.12

This image is around 143mb in size.

```
docker run \
--name=mohaa \
-d \
-p 12203:12203/udp \
-p 12300:12300/udp \
-v /your/location/config:/config \
--restart always \
solipsist01/mohaa
```

This container will spawn server.cfg at /your/location/config
Here you can change the server settings of the mohaa server

compose example:

```
version: "3.5"
services:
  mohaa:
    container_name: mohaa
    image: solipsist01/mohaa
    ports:
      - 12300:12300/udp
      - 12203:12203/udp
    environment:
      - TZ=${TZ}
    volumes:
      - /etc/localtime:/etc/localtime:ro
      - ${CONFIG}/mohaa:/config
    networks:
      - ${NETWORK}
    restart: ${RESTARTPOLICY}

networks: 
  mynetwork:
    name: ${NETWORK}

```

source of this container:
https://github.com/solipsist01/dockerfiles/tree/master/mohaa