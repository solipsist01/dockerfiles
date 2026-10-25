# Pterodactyl egg: Minecraft (packwiz modpack)

A Minecraft server that follows a packwiz modpack. You give it the pack URL; the
Minecraft version, the loader (Forge, NeoForge, Fabric, Quilt) and the mods all
come from the pack.

## Import

Admin panel → Nests → Import Egg → `egg-packwiz-minecraft.json`. Create a server
with it and fill in **Pack URL**, e.g. `https://packwiz.ofnie.org/packs/mtr/pack.toml`.
Pick the Docker image with the right Java version:

| Minecraft | Image |
|---|---|
| 26.1 and newer | Java 25 |
| 1.20.5 – 1.21.x | Java 21 (default) |
| 1.18 – 1.20.4 | Java 17 |
| 1.17 | Java 16/17 |
| older | Java 8 |

## What happens

- **Install:** puts `packwiz-sync.sh` and `packwiz-installer-bootstrap.jar` on the server.
  (The installer image has no Java, so the loader comes on the first start.)
- **Every start**, before the server launches, `packwiz-sync.sh`:
  1. reads `[versions]` from the pack,
  2. installs that Minecraft + loader if it isn't installed yet or the pack changed it
     (Forge/NeoForge via their installer, Fabric via its server launcher, Quilt via its installer),
  3. warns in the console if the image's Java version doesn't fit that Minecraft version,
  4. syncs the server-side mods with packwiz-installer.

  If something fails (pack site down, download error), it says so in the console and the
  server starts with what it already has.
- Afterwards the folder has `unix_args.txt` (Forge/NeoForge) or `server.jar` (Fabric, Quilt,
  Forge before 1.17); the startup command launches whichever is there.
- `.packwiz-loader` remembers what's installed. Delete it to force a loader reinstall.

So changing the loader in packwiz-web (*Minecraft & loader version*) is enough: the next
server restart installs it. Players' Prism instances follow by themselves too.

## Changing the script

`packwiz-sync.sh` is the source; the egg embeds it. After editing it run
`python3 make_egg.py` and re-import the egg. Existing servers get the new script on a
reinstall (Settings → Reinstall Server; worlds and configs stay).

The startup command runs the script inside `$( … )`: the yolks entrypoint expands the
startup line with `eval echo` first, and only command substitutions run properly there.
