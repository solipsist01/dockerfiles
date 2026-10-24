# packwiz-web (Flask + packwiz)

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

**Search:** filtered on the pack's Minecraft version and loader. On 1.20.1 NeoForge, Forge
mods are included too (NeoForge 1.20.1 is a Forge fork and most mods there are only tagged Forge),
and Quilt packs include Fabric mods. Close matches without a build for the pack are listed
greyed out underneath, so a mod never just silently disappears.

**Jar hosting** (per pack):
- *Link to Modrinth* (default): Modrinth mods download from Modrinth's CDN.
- *Host here (mirror)*: after every add/update, Modrinth jars are downloaded, checked against
  their hash and served from `/jars/`. Updates keep working (the Modrinth link stays in the
  `[update]` section). Switching back points them at Modrinth again. Note that this publicly
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
- An upload whose exact file is on Modrinth (sha512 lookup) is **linked automatically**.
  Otherwise click **Link** next to it and pick the project (search, or paste a Modrinth
  link/slug). A hand-linked custom build shows "Modrinth has X", since its own version is unknown.
- **Update** on an uploaded jar swaps it for the Modrinth version; from then on it's a normal
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

- Modrinth search only (plus uploads). There's no history or undo: removing a mod or updating is
  immediate, and clients/servers follow on their next start.
- Mods packwiz-installer has already installed but that were removed from the
  pack are deleted by the installer on the next start.
- All changes run under one lock, so two admins clicking at once is safe.
