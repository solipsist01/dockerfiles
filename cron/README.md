# cron

Dockerfile and scripts for creating image with Cron based on Alpine  

#### Environment variables:
make multiple environmental variables

-e CRON01=0 2 * * * python3 /scripts/somescript.py >> /config/somescript.log 2>&1
-e CRON02=0 2 * * * python3 /scripts/someotherscript.py >> /config/someotherscript.log 2>&1

#### Log files
Log file by default placed in /var/log/cron/cron.log 

#### Simple usage:
```
docker run --name="cron" -d \
-e CRON01=0 2 * * * python3 /scripts/somescript.py >> /config/somescript.log 2>&1 \
-e CRON02=0 2 * * * python3 /scripts/someotherscript.py >> /config/someotherscript.log 2>&1 \
-v /path/to/app/scripts:/scripts \
solipsist01/cron
```

#### Docker-Compose:
```
version: "3.5"
services:
  
  cron:
    container_name: cron
    image: solipsist01/cron
    environment:
      - TZ=Europe/Amsterdam
      #- CRON01=0 2 * * * python3 /workspace/Gemist/kijktest.py >> /config/kijk.log 2>&1
      #- CRON02=0 5 * * * python3 /workspace/Gemist/rtl.py >> /config/rtl.log 2>&1
    volumes:
      - /path/to/scripts:/scripts
      - /etc/localtime:/etc/localtime:ro
    networks:
      - ${NETWORK}
    restart: always

networks: 
  internal:
    name: ${NETWORK}
```