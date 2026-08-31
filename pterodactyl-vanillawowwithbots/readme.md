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

**1. Build and push the image**

```bash
docker build -t ghcr.io/solipsist01/pterodactyl-vanillawow:latest .
docker push ghcr.io/solipsist01/pterodactyl-vanillawow:latest
```

Make sure `start.sh` and `entrypoint.sh` sit next to the `Dockerfile` in
the build context. To pin a specific `mangos-classic` branch/tag instead
of `master`:

```bash
docker build --build-arg MANGOS_BRANCH=some-tag -t ghcr.io/solipsist01/pterodactyl-vanillawow:latest .
```

**2. Import the egg**

Admin panel -> Nests -> Import Egg -> upload `egg-mangos-classic.json`.
The Docker image reference is already set to
`ghcr.io/solipsist01/pterodactyl-vanillawow:latest` - update it first if
you're using a different registry/tag.

**3. Create the server**

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

**4. Provide client data**

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
3. Place the resulting `maps/`, `vmaps/`, `dbc/`, and `mmaps/` folders at
   `/home/container/server/data/` (i.e. `/home/container/server/data/maps`,
   `.../vmaps`, etc.). `mangosd` actually looks for these relative to its
   own working directory, which differs slightly between the two eggs -
   `start.sh` automatically symlinks them into the right place either
   way, so `server/data/` is the one consistent location to upload to
   regardless of which variant you're running.
4. Restart the server.

**5. Create your first (GM) account**

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

## Updating world content

The image bakes in a snapshot of `classic-db`'s `Full_DB` dump at build
time. To pick up newer content later without rebuilding the image, open
a console/shell on the server and run the real, interactive installer
against the copy that ships alongside it:

```bash
cd /home/container/classic-db
./InstallFullDB.sh
```

It'll ask for your MySQL root password and walk you through its menu -
use `mangos`/`characters`/`realmd` as the database names and
`127.0.0.1` / socket `/home/container/mysql.sock` for connection
details.

## Self-healing on rebuild

`start.sh` re-applies core's `sql/updates/mangos/*.sql` migrations and
re-ensures databases/grants/config on every boot, not just at install.
That means rebuilding the image against a newer `mangos-classic` commit
and restarting an existing server is enough on its own to catch the
database up - no reinstall required.

## Playerbots (AI-controlled bot characters)

A fully separate, standalone setup adds support for CMaNGOS's
[Playerbots](https://github.com/cmangos/mangos-classic/blob/master/README_PLAYERBOT.txt) -
AI-controlled bot characters that roam the world, form parties, run
dungeons, and populate the auction house. Turns out this is a build
option on `mangos-classic` core itself (`-DBUILD_PLAYERBOTS=1`), not a
separate repo to clone - an earlier version of this setup mistakenly
cloned `cmangos/playerbots` instead, which isn't a standalone buildable
project (no `project()`/`cmake_minimum_required()` in its CMakeLists.txt
- it's meant to be pulled into a parent build, not built on its own).
This setup lives entirely in its own `playerbots/` subfolder, using the
same plain filenames as the vanilla setup - nothing shared, no filename
collisions:

```
playerbots/
  Dockerfile     - clones the same cmangos/mangos-classic as vanilla,
                   builds with the extra -DBUILD_PLAYERBOTS=1 flag,
                   stages playerbots' own SQL content and config template
  install.sh     - its own install script
  start.sh       - its own startup script
  entrypoint.sh  - its own entrypoint wrapper
```

Plus **`egg-mangos-classic-playerbots.json`** at the repo root - its own
egg, pointing at its own dedicated image
(`ghcr.io/solipsist01/pterodactyl-vanillawowwithbots`).

**Why a separate egg instead of one variable on the existing egg**:
`BUILD_PLAYERBOTS` is a CMake flag, so "with bots" vs. "without bots" is
still a build-time decision baked into which binary gets compiled, even
though it now uses the same source repo as vanilla. Pterodactyl only
allows one install container per egg, and install has to match whatever
binary the runtime image actually has - so a single egg can't safely
support both.

**Why a completely separate folder instead of one shared install.sh/
start.sh with `if` branches**: keeping the two variants fully independent
means a change or bug in one can't accidentally affect the other, and
each can be debugged on its own terms - useful given the playerbots side
is newer and less proven than the vanilla setup (see below).

**What genuinely *is* a runtime toggle**: whether bots are actually
spawned/active, via the `Enable Playerbots` variable
(`AiPlayerbot.Enabled` in its own config file) - change it and restart,
no reinstall needed. This build also adds `-DBUILD_AHBOT=1` (the Auction
House Bot, which populates the AH with items and buys/bids on player
listings) - toggled the same way via `Enable AHBot`
(`AuctionHouseBot.Seller.Enabled`/`.Buyer.Enabled` in `ahbot.conf`).
AHBot is a build option on `mangos-classic` core itself too, same as
playerbots - it could equally be added to the plain vanilla image by
adding the same flag there, this setup just bundles it alongside
playerbots since they're commonly used together. `Random Bot Count`
controls how many bots actually populate the world (sets both
`AiPlayerbot.MinRandomBots` and `AiPlayerbot.MaxRandomBots` to the same
value) - also synced on every restart.

**Setup**: same steps as the plain vanilla egg (build, push, import,
create server, provide client data), just from inside the `playerbots/`
folder:

```bash
cd playerbots
docker build -t ghcr.io/solipsist01/pterodactyl-vanillawowwithbots:latest .
docker push ghcr.io/solipsist01/pterodactyl-vanillawowwithbots:latest
```

**Honesty check**: this is meaningfully newer and less battle-tested than
the plain vanilla setup above, which only reached its current stable
state after a long debugging process. The first real build attempt did
catch a genuine bug in this setup (cloning the wrong repo entirely, fixed
above) - a reminder that everything below this point is still only as
verified as it's actually been built. The playerbots SQL/config paths
(`src/modules/PlayerBots/sql/{characters,world}/vanilla/`,
`aiplayerbot*.conf.dist`) are based on playerbots' own published README
instructions, not something verified end-to-end against a real build
here. Both `playerbots/install.sh` and `playerbots/start.sh` fail loudly
with a clear error (rather than silently skipping) if those paths turn
out to be wrong - watch the install console closely on first use. If it
fails while staging the playerbots SQL/config files during the Docker
build itself, run
`find /usr/src/mangos-classic/src/modules/PlayerBots -type d` in a throwaway
build (from inside `playerbots/`: `docker build --target builder -t debug . && docker run --rm -it debug bash`)
to see the real layout and adjust the paths in `playerbots/Dockerfile`.


## Notes

- Running the database inside the same container as the game server
  isn't best practice for a production/public realm (no independent
  backups, resource contention, single point of failure) - a reasonable
  tradeoff for a personal/self-contained setup, which is what this was
  built for.
- Running a private WoW server carries its own legal/ToS considerations
  independent of this egg, worth being aware of for your jurisdiction
  and use case.
- `mangosd.conf`/`realmd.conf` key names can shift between
  `mangos-classic` commits - if something looks off after a rebuild
  against a much newer commit, check
  `/home/container/server/etc/mangosd.conf` and `realmd.conf` directly.