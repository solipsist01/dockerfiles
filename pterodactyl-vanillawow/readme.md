# MaNGOS Classic (Vanilla WoW 1.12.1) - All-in-One Pterodactyl Egg

A self-contained CMaNGOS `mangos-classic` server for vanilla World of
Warcraft 1.12.1. MariaDB, the `realmd`/`characters`/`mangos`/`classiclogs`
databases, and the CMaNGOS `classic-db` world content all run in a single
container, built once as a Docker image rather than compiled per-server.

## How it's put together

- **`Dockerfile`** - multistage build. Stage 1 compiles `mangos-classic`
  from source and stages the SQL files plus the `classic-db` world
  content; stage 2 is the lean runtime image (MariaDB, runtime libraries,
  and the compiled output from stage 1 - no compiler toolchain).
- **`entrypoint.sh`** - baked into the image as the container's
  `ENTRYPOINT`. Runs whatever command it's given (what the Pterodactyl
  install step provides), and falls back to launching the game server via
  `start.sh` when given no arguments at all - so the image behaves
  correctly for both install and normal runtime regardless of exactly how
  the container is invoked.
- **`install.sh`** - runs once per server (and again on Reinstall).
  Copies config templates out of the image, initializes MariaDB, creates
  and seeds the databases, imports the world content, and writes
  `mangosd.conf`/`realmd.conf`. Safe to re-run.
- **`start.sh`** - runs on every boot. Starts MariaDB, self-heals
  databases/grants/config drift and any pending schema updates, syncs the
  realmlist entry to the current variables, then starts `realmd` and
  `mangosd`.
- **`egg-mangos-classic.json`** - the importable Pterodactyl egg, with
  the scripts above embedded and the Docker image reference already set.

## Setup

**1. Import the egg**

Admin panel -> Nests -> Import Egg -> upload `egg-mangos-classic.json`.
The Docker image reference is already set to
`ghcr.io/solipsist01/pterodactyl-vanillawow:latest` - update it first if
you're using a different registry/tag.

**2. Create the server**

- Assign **two allocations**: the primary one becomes the realm (login)
  port (3724 by default - what WoW clients expect), the second is the
  world server port. Pterodactyl doesn't auto-fill a second allocation
  into an egg variable, so set `World Server Port` to match whatever
  port you assigned as the second allocation.
- Set `Realm Public Address` to whatever players actually connect to.
- Change `MariaDB Root Password` and `MaNGOS DB User Password` from
  their defaults.
- Install. Since the binaries are already baked into the image, this is
  just database seeding - well under a minute.

**3 Provide client data**

Map, vmap, dbc, and mmap data has to be extracted from a real WoW 1.12.1
client - Blizzard's copyrighted software, so you'll need your own
legally-owned copy.

1. Upload your client files anywhere convenient (e.g.
   `/home/container/client`).
2. From a console/shell on the server, run the extractor tools, found at
   `/opt/mangos/bin/`:
   - `./ad` - extracts DBC and map data
   - `./vmap_extractor` then `./vmap_assembler vmaps vmaps_out` - vmaps
   - `./mmaps_generator` - pathfinding data (optional, recommended)
3. Place the resulting `maps/`, `vmaps/`, `dbc/`, and `mmaps/` folders
   directly at `/home/container/maps`, `/home/container/vmaps`,
   `/home/container/dbc`, and `/home/container/mmaps` - `mangosd` looks
   for them relative to its working directory by default.
4. Restart the server.

**4. Create your first (GM) account**

Once `mangosd` is running, its console is the panel's Console tab
directly - type:

```
account create <username> <password>
account set gmlevel <username> 3 -1
```

(`3` = full GM level, `-1` = applies to all realms.)

## Variables

| Variable | Purpose | Default |
|---|---|---|
| `MariaDB Root Password` | Password for MariaDB's `root` user | `changeme_root` |
| `MaNGOS DB User Password` | Password for the `mangos` MySQL user realmd/mangosd connect with | `mangos` |
| `Realm Name` | Shown in the client's realm list. Synced to the DB on every boot | `MyMangosRealm` |
| `Realm Public Address` | IP/hostname players connect to. Synced to the DB on every boot | `127.0.0.1` |
| `World Server Port` | Port `mangosd` listens on - must match your second allocation | `8085` |

Changing `Realm Name`, `Realm Public Address`, or `World Server Port` and
restarting is enough to take effect - no manual SQL required.


## Self-healing on rebuild

`start.sh` re-applies core's `sql/updates/mangos/*.sql` migrations and
re-ensures databases/grants/config on every boot, not just at install.
That means rebuilding the image against a newer `mangos-classic` commit
and restarting an existing server is enough on its own to catch the
database up - no reinstall required.
