# Infinite Rails - Pterodactyl egg

Dedicated server for [Infinite Rails](https://infiniterails.com/). The server is a free
Steam tool app (**5484540**, "Infinite Rails Dedicated Server"). SteamCMD downloads it
with an anonymous login, so you need no Steam account, no Game Server Login Token and no
registration.

## Files

- `Dockerfile` - a thin layer over `ghcr.io/ptero-eggs/steamcmd:debian` that adds `jq` and `start.sh`.
- `start.sh` - runs on every boot:
  1. SteamCMD update (`AUTO_UPDATE=1`).
  2. Syncs `InfiniteRails_Data/config.txt`: the port is set to the allocation and `autoUpdate` is set to false.
     Login, creative mode and MOTD are written only if they are filled in.
  3. Starts the server.
- `install.sh` - the install script, also embedded in the egg. It fetches SteamCMD and the server into `/home/container`.
- `egg-infiniterails.json` - the egg to import.

## Notes

- **Port: UDP only.** The default is 64888.
- **Server updates:** the server's own `autoUpdate` is always forced off. It would shut the
  server down and restart it outside of wings. Updating is a restart instead (with
  `AUTO_UPDATE=1`), or a Reinstall.
- **Console:** panel console commands go to the server's stdin (`/save`, `/say ...`,
  `/setmode <player> admin`, `/help`). Stop sends `/exit`, which saves first.
- **Data:** everything lives in `InfiniteRails_Data/`: `config.txt`, `whitelist.txt`,
  `blacklist.txt`, `accounts.txt` and `Worlds/ServerWorld_<id>/`. A Reinstall leaves these alone.
- **Public server list:** add the server through the form at https://infiniterails.com/#servers
  (name, `ip:port`, description). This is optional; players can also connect directly by IP and port.
