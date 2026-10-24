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
| `/packwiz-installer-bootstrap.jar` | public | The bootstrap jar, for servers |
| `/admin` | Cloudflare Access (`auth`) | The admin UI |

The pack files have to be public: packwiz-installer is a plain HTTP client and
can't get through an Access login. They only contain mod names, hashes and
Modrinth download links.

## Build and run

    docker build -t solipsist01/packwiz-web .
    docker push solipsist01/packwiz-web

Then `compose/packwiz/docker-compose.yml`. The `/data` volume holds
`packs/<pack>/` (plain packwiz folders) and the session `secret_key`.

## Settings (environment variables)

| Variable | Default | |
|---|---|---|
| `PUBLIC_URL` | the request's own URL | Base URL baked into Prism zips and shown in the UI. Set it, because behind the tunnel the request URL is `http://` |
| `ACCESS_TEAM_DOMAIN` | *(off)* | e.g. `wenro.cloudflareaccess.com`. `/admin` then requires a valid Cloudflare Access JWT, so even if the Access app on `/admin` goes missing, the admin stays closed (403) |
| `PACK_AUTHOR` | `packwiz-web` | Author written into new packs |
| `SECRET_KEY` | generated into `/data/secret_key` | Flask session key |

## Using it

**Admin:** create a pack (name, Minecraft version, loader; newest loader version
is picked), search Modrinth, click **Add**. Dependencies come along
automatically. **+ all Fabric packs** adds a mod to every pack with the same
loader. **Update all mods** / **Update all packs** / **Update everywhere** (per
shared mod) show what changed as `old.jar → new.jar`. Set a mod to `client` or
`server` so the other side skips it. **Copy** clones a pack as the start of a new one.

**Players (Prism):** Add Instance → Import → paste `https://packwiz.ofnie.org/packs/<pack>.zip`.
Every launch runs packwiz-installer first and syncs the mods.

**Servers:** before starting the server, in the server folder:

    java -jar packwiz-installer-bootstrap.jar -g -s server https://packwiz.ofnie.org/packs/<pack>/pack.toml

## Notes

- Modrinth only. There's no history or undo: removing a mod or updating is
  immediate, and clients/servers follow on their next start.
- Mods packwiz-installer has already installed but that were removed from the
  pack are deleted by the installer on the next start.
- All changes run under one lock, so two admins clicking at once is safe.
