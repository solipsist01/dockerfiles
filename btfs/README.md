Multi-arch build for go-btfs

```
version: "3.5"
services:
  btfs:
    container_name: btfs
    image: solipsist01/go-btfs:latest
    ports:
      - 4001:4001/tcp
      - 4001:4001/udp
    environment:
      - TZ=europe/amsterdam
      - ENABLE_WALLET_REMOTE=true
    volumes:
      - /your/storage/location/:/data/btfs/
      - /etc/localtime:/etc/localtime:ro
    labels:
      - "traefik.http.routers.btfs.entrypoints=websecure"
      - "traefik.http.routers.btfs.tls.certresolver=cloudflare"
      - "traefik.http.services.btfs.loadbalancer.server.port=5001"
    networks:
      - dockernetwork
    restart: always

networks: 
  dockernetwork:
    name: dockernetwork

```

for more info, see: https://github.com/TRON-US/go-btfs
