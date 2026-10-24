# packwiz-web (Flask + packwiz)

Modrinth and CurseForge mods, plus your own uploaded jars.

One container that manages [packwiz](https://packwiz.infra.link/) modpacks and
hosts them. Update a mod once here; servers and Prism clients pick it up the
next time they start.

## URLs

| Path | Access | What |
|---|---|---|
| `/` | public | Player page: every pack with its Prism import link and mod list |
| `/packs/<pack>/pack.toml` | public | What packwiz-installer reads (servers and clients) |
| `/packs/<pack>.zip` | public | Ready-made Prism instance (bootstrap jar + pre-launch command) |
| `/jars/<sha256>/<file>.jar` | public | Self-hosted jars (uploads, and mirrored Modrinth mods) |
| `/packwiz-installer-bootstrap.jar` | public | The bootstrap jar, for servers |
| `/admin` | Cloudflare Access (`auth`) | The admin UI |

The pack files have to be public: packwiz-installer is a plain HTTP client and
can't get through an Access login. They only contain mod names, hashes and
Modrinth download links.

## Build and run

    docker build -t solipsist01/packwiz-web .
    docker push solipsist01/packwiz-web

Normally the GitHub workflow builds and pushes it. Then `compose/packwiz/docker-compose.yml`.
The `/data` volume holds `packs/<pack>/` (plain packwiz folders), `jars/` (self-hosted
jars, one copy per unique file), `settings/` (per-pack hosting mode) and the session `secret_key`.

## Settings (environment variables)

| Variable | Default | |
|---|---|---|
| `PUBLIC_URL` | the request's own URL | Base URL baked into Prism zips and into the download URLs of self-hosted jars. **Set it**: behind the tunnel the request URL is `http://`, which Java won't follow to https |
| `ACCESS_TEAM_DOMAIN` | *(off)* | e.g. `wenro.cloudflareaccess.com`. `/admin` then requires a valid Cloudflare Access JWT, so even if the Access app on `/admin` goes missing, the admin stays closed (403) |
| `CURSEFORGE_API_KEY` | *(off)* | From console.curseforge.com. Enables CurseForge search, update notices, mirroring and recognising uploaded CurseForge jars. Without it, CurseForge mods can still be added by link. In compose, write every `$` in the key as `$$` |
| `MAX_UPLOAD_MB` | `500` | Size limit per jar. Uploads go one file per request in 25 MB chunks, so Cloudflare's 100 MB request limit doesn't apply to the batch |
| `PACK_AUTHOR` | `packwiz-web` | Author written into new packs |
| `SECRET_KEY` | generated into `/data/secret_key` | Flask session key |

## Using it

**Admin:** create a pack (name, Minecraft version, loader; newest loader version
is picked), search Modrinth, click **Add**. Dependencies come along
automatically. **+ all Fabric packs** adds a mod to every pack with the same
loader. **Update all mods** / **Update all packs** / **Update everywhere** (per
shared mod) show what changed as `old.jar → new.jar`. Set a mod to `client` or
`server` so the other side skips it. **Copy** clones a pack as the start of a new one.

**Minecraft & loader version:** on the pack page, open *Minecraft & loader version*, pick the
Minecraft version, loader and loader version (empty = newest) and click **Change**. packwiz
first checks the combination in a scratch pack; nothing changes if it doesn't exist. Mods aren't
touched: after a Minecraft version change, update them (or remove the ones without a build).
Players' Prism instances follow automatically: packwiz-installer compares the instance's
`mmc-pack.json` with `pack.toml` on every start, shows the old and new versions and offers
**Update** (default). It rewrites the instance's loader/Minecraft version, but that launch still
uses the old one, so players start once more. The server needs a reinstall with the new loader:
packwiz-installer only does this for Prism/MultiMC instances.
Tip for 1.20.1: use Forge (47.4.x), not NeoForge 47.1.x. NeoForge for 1.20.1 is an old fork
that current 1.20.1 mods don't always load on.

**Search:** filtered on the pack's Minecraft version and loader. On 1.20.1 NeoForge, Forge
mods are included too (NeoForge 1.20.1 is a Forge fork and most mods there are only tagged Forge),
and Quilt packs include Fabric mods. Close matches without a build for the pack are listed
greyed out underneath, so a mod never just silently disappears.

**CurseForge:**
- With `CURSEFORGE_API_KEY`: a Modrinth/CurseForge switch next to the search box, update
  notices for CurseForge mods, and uploads are also matched by CurseForge fingerprint.
- Always (no key needed): paste a CurseForge link or slug under the search results. packwiz adds
  and updates CurseForge mods with its own built-in key.
- CurseForge mods have no download URL in the pack; packwiz-installer asks CurseForge for it.
- Some authors block third-party downloads. Those mods get a red **manual download** tag:
  players' launchers ask them to download the jar by hand, and a headless server install stops
  ("Update cancelled by user!"). Fix: download the jar from CurseForge and upload it here. It's
  recognised and replaces that mod, keeping the CurseForge link for update notices. Such mods
  are never mirrored.

**Jar hosting** (per pack):
- *Link to Modrinth/CurseForge* (default): mods download from Modrinth's CDN or via CurseForge.
- *Host here (mirror)*: after every add/update, Modrinth and CurseForge jars are downloaded,
  checked against their hash and served from `/jars/` (CurseForge needs the API key, and only
  where the author allows third-party downloads). Updates keep working (the link stays in the
  `[update]` section). Switching back points them at Modrinth/CurseForge again. Note that this publicly
  redistributes other people's mods; many mod licenses don't allow that.
- *Uploads* are always hosted here, in any mode. The mod name is read from the jar
  (`fabric.mod.json`, `quilt.mod.json`, `mods.toml`). Uploading a newer jar of a mod that's
  already in the pack replaces it and keeps its side.

**Uploading many jars:** pick as many as you like (45 is fine). The browser sends them one
file per request, 3 at a time, splitting anything over 25 MB into chunks, with a progress bar and
a per-file log. Every request is short, so neither Cloudflare's size limit nor its ~100 s timeout
is hit. Failed chunks are retried, and a chunk that arrives twice isn't stored twice. One final
request refreshes the packs, then the page reloads with a summary. Without JavaScript the
form still works as a plain (single-request) upload.

**Modrinth link and updates:**
- Every pack page checks Modrinth for updates (one batch request, by file hash) and shows
  `→ new version` with an **Update** button per mod; **Update all mods (n)** does them all.
  The overview shows the number of available updates per pack.
- An upload whose exact file is on Modrinth (sha512) or CurseForge (fingerprint) is **linked
  automatically**. If it's on both, it keeps the provider of the mod it replaces. Otherwise
  click **Link** next to it and pick the project (Modrinth or CurseForge search, or paste a
  Modrinth link/slug). A hand-linked custom build shows "Modrinth has X" / "CurseForge has X",
  since its own version is unknown.
- **Update** on an uploaded jar swaps it for the Modrinth/CurseForge version; from then on it's a normal
  Modrinth mod that follows the pack's hosting mode (side is kept). So **Update all** also
  replaces linked uploads; unlinked uploads are never touched.
- Uploaded jars carry `x-uploaded = true` in their `.pw.toml`. packwiz keeps that key on
  refresh and drops it when it updates the mod from Modrinth.

Jars no pack refers to anymore are deleted automatically.

**Players (Prism):** Add Instance → Import → paste `https://packwiz.ofnie.org/packs/<pack>.zip`.
Every launch runs packwiz-installer first and syncs the mods.

**Servers:** before starting the server, in the server folder:

    java -jar packwiz-installer-bootstrap.jar -g -s server https://packwiz.ofnie.org/packs/<pack>/pack.toml

## Notes

- There's no history or undo: removing a mod or updating is
  immediate, and clients/servers follow on their next start.
- Mods packwiz-installer has already installed but that were removed from the
  pack are deleted by the installer on the next start.
- All changes run under one lock, so two admins clicking at once is safe.
