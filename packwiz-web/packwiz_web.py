"""packwiz-web - a small web UI around the packwiz CLI, plus the pack host.

Public (no login): the pack files under /packs/<slug>/ that packwiz-installer
reads, self-hosted jars under /jars/, a Prism instance zip per pack, and an
overview page for players.
Admin (/admin, behind Cloudflare Access): create/copy/delete packs, add mods
from Modrinth, upload jars, change sides, remove and update mods, across one
or all packs.

Jar hosting: uploaded jars are always served from /jars/. Modrinth mods link
to Modrinth's CDN, unless the pack is set to mirror; then their jars are
downloaded, hash-checked and served from /jars/ as well. Jars are stored once
per content (sha256) and removed when no pack uses them anymore.

Every pack change goes through the packwiz binary, so the pack folders stay
plain packwiz packs. All changes run under one global file lock.
"""

import fcntl
import hashlib
import io
import json
import os
import re
import secrets
import shutil
import subprocess
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from contextlib import contextmanager
from functools import wraps
from pathlib import Path

import jwt
from flask import (Flask, abort, flash, g, redirect, render_template, request,
                   send_file, send_from_directory, session, url_for)

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = Path(os.environ.get("PACKWIZ_DATA", str(BASE_DIR / "data")))
PACKS_DIR = DATA_DIR / "packs"
JARS_DIR = DATA_DIR / "jars"
SETTINGS_DIR = DATA_DIR / "settings"
UPLOADS_DIR = DATA_DIR / "uploads"
BOOTSTRAP_JAR = BASE_DIR / "packwiz-installer-bootstrap.jar"

PUBLIC_URL = os.environ.get("PUBLIC_URL", "").rstrip("/")
PACK_AUTHOR = os.environ.get("PACK_AUTHOR", "packwiz-web")
# e.g. "wenro.cloudflareaccess.com". When set, /admin requires a valid Access JWT.
ACCESS_TEAM_DOMAIN = os.environ.get("ACCESS_TEAM_DOMAIN", "").strip().rstrip("/")
ACCESS_TEAM_DOMAIN = re.sub(r"^https?://", "", ACCESS_TEAM_DOMAIN)
MAX_UPLOAD_MB = int(os.environ.get("MAX_UPLOAD_MB", "500"))
# The browser uploads one file per request, in chunks of this size, to stay
# far below Cloudflare's 100 MB request limit and its ~100 s timeout.
UPLOAD_CHUNK_MB = 25
MAX_FLASH_LINES = 30

LOADERS = {
    "fabric": "Fabric",
    "neoforge": "NeoForge",
    "forge": "Forge",
    "quilt": "Quilt",
}
SIDES = ("both", "client", "server")
MODRINTH_API = "https://api.modrinth.com/v2"
USER_AGENT = "packwiz-web (self-hosted packwiz admin)"
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,62}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
JAR_URL_RE = re.compile(r"/jars/([0-9a-f]{64})/")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

for _dir in (PACKS_DIR, JARS_DIR, SETTINGS_DIR, UPLOADS_DIR):
    _dir.mkdir(parents=True, exist_ok=True)


def _secret_key():
    path = DATA_DIR / "secret_key"
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        key = secrets.token_hex(32)
        path.write_text(key)
        return key


app = Flask(__name__)
app.secret_key = os.environ.get("SECRET_KEY") or _secret_key()
app.config["MAX_CONTENT_LENGTH"] = MAX_UPLOAD_MB * 1024 * 1024


class ActionError(Exception):
    pass


# --- Packs on disk ------------------------------------------------------------

def slugify(name):
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return slug[:63]


def pack_dir(slug):
    if not SLUG_RE.match(slug or ""):
        abort(404)
    path = PACKS_DIR / slug
    if not (path / "pack.toml").is_file():
        abort(404)
    return path


def read_pack(slug):
    path = PACKS_DIR / slug
    with open(path / "pack.toml", "rb") as f:
        meta = tomllib.load(f)
    versions = meta.get("versions", {})
    loader = next((l for l in LOADERS if l in versions), None)
    return {
        "slug": slug,
        "name": meta.get("name", slug),
        "minecraft": versions.get("minecraft", "?"),
        "loader": loader,
        "loader_name": LOADERS.get(loader, "Vanilla"),
        "loader_version": versions.get(loader, ""),
        "mirror": load_settings(slug)["mirror"],
        "mods": read_mods(slug),
    }


def read_mods(slug):
    mods = []
    root = PACKS_DIR / slug
    for meta_file in sorted(root.rglob("*.pw.toml")):
        with open(meta_file, "rb") as f:
            meta = tomllib.load(f)
        modrinth = meta.get("update", {}).get("modrinth", {})
        download = meta.get("download", {})
        url = download.get("url", "")
        if meta.get("x-uploaded") or not modrinth:
            source = "uploaded"
        elif JAR_URL_RE.search(url):
            source = "mirrored"
        else:
            source = "modrinth"
        mods.append({
            "path": meta_file,
            "stem": meta_file.name[:-len(".pw.toml")],
            "folder": str(meta_file.relative_to(root).parent),
            "name": meta.get("name", meta_file.stem),
            "filename": meta.get("filename", ""),
            "side": meta.get("side", "both"),
            "mod_id": modrinth.get("mod-id"),
            "version_id": modrinth.get("version"),
            "url": url,
            "hash_format": download.get("hash-format", ""),
            "hash": download.get("hash", ""),
            "source": source,
        })
    mods.sort(key=lambda m: m["name"].lower())
    return mods


def all_packs():
    packs = []
    for path in sorted(PACKS_DIR.iterdir()):
        if path.is_dir() and (path / "pack.toml").is_file():
            packs.append(read_pack(path.name))
    packs.sort(key=lambda p: p["name"].lower())
    return packs


def load_settings(slug):
    try:
        settings = json.loads((SETTINGS_DIR / f"{slug}.json").read_text())
    except (FileNotFoundError, ValueError):
        settings = {}
    return {"mirror": bool(settings.get("mirror", False))}


def save_settings(slug, settings):
    (SETTINGS_DIR / f"{slug}.json").write_text(json.dumps(settings))


def public_base():
    return PUBLIC_URL or request.url_root.rstrip("/")


def pack_url(slug):
    return f"{public_base()}/packs/{slug}/pack.toml"


# --- Running packwiz ----------------------------------------------------------

@contextmanager
def write_lock():
    with open(DATA_DIR / ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def packwiz(slug_or_path, *args):
    cwd = slug_or_path if isinstance(slug_or_path, Path) else PACKS_DIR / slug_or_path
    env = dict(os.environ, HOME="/tmp")
    try:
        result = subprocess.run(
            ["packwiz", "-y", *args], cwd=cwd, env=env, capture_output=True,
            text=True, timeout=600, stdin=subprocess.DEVNULL,
        )
    except subprocess.TimeoutExpired:
        raise ActionError(f"packwiz {' '.join(args)} timed out")
    output = ANSI_RE.sub("", result.stdout + result.stderr).strip()
    if result.returncode != 0:
        tail = "\n".join(output.splitlines()[-8:])
        raise ActionError(f"packwiz {' '.join(args)} failed:\n{tail}")
    return output


def file_versions(slug):
    return {m["stem"]: m["filename"] for m in read_mods(slug)}


def describe_changes(pack_name, before, after):
    lines = []
    for stem in sorted(after):
        if stem not in before:
            lines.append(f"{pack_name}: added {after[stem]}")
        elif before[stem] != after[stem]:
            lines.append(f"{pack_name}: {before[stem]} → {after[stem]}")
    for stem in sorted(set(before) - set(after)):
        lines.append(f"{pack_name}: removed {before[stem]}")
    return lines


def run_and_report(pack, *args):
    """Run packwiz on a pack and return the list of file changes it caused."""
    before = file_versions(pack["slug"])
    packwiz(pack["slug"], *args)
    return describe_changes(pack["name"], before, file_versions(pack["slug"]))


def flash_results(changes, errors, nothing="Nothing changed, everything is up to date."):
    for e in errors:
        flash(str(e), "error")
    if len(changes) > MAX_FLASH_LINES:
        changes = changes[:MAX_FLASH_LINES] + [f"… and {len(changes) - MAX_FLASH_LINES} more"]
    if changes:
        flash("\n".join(changes), "ok")
    elif not errors:
        flash(nothing, "ok")


# --- Jar hosting --------------------------------------------------------------

def http_get(url, timeout=15):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def store_jar(data, filename):
    """Store a jar under its sha256 and return the hash."""
    sha = hashlib.sha256(data).hexdigest()
    filename = os.path.basename(filename)
    target = JARS_DIR / sha / filename
    if not target.is_file():
        target.parent.mkdir(exist_ok=True)
        tmp = target.with_name(f".{filename}.tmp")
        tmp.write_bytes(data)
        tmp.rename(target)
    return sha


def jar_url(sha, filename):
    return f"{public_base()}/jars/{sha}/{urllib.parse.quote(os.path.basename(filename))}"


def set_download_url(meta_file, url):
    toml = meta_file.read_text()
    toml = re.sub(r'^url = ".*"$', f"url = {json.dumps(url)}", toml, count=1, flags=re.M)
    meta_file.write_text(toml)


def mirror_jar(mod):
    """Download a Modrinth mod's jar, check its hash and store it."""
    try:
        data = http_get(mod["url"], timeout=120)
    except OSError as e:
        raise ActionError(f"Downloading {mod['filename']} failed: {e}")
    if mod["hash_format"] in hashlib.algorithms_available:
        if hashlib.new(mod["hash_format"], data).hexdigest() != mod["hash"].lower():
            raise ActionError(f"Downloaded {mod['filename']} doesn't match its hash.")
    return store_jar(data, mod["filename"])


def modrinth_file_url(mod):
    """The Modrinth CDN url of a mod's current version, to stop mirroring it."""
    try:
        version = json.loads(http_get(f"{MODRINTH_API}/version/{mod['version_id']}"))
    except (OSError, ValueError) as e:
        raise ActionError(f"Looking up {mod['name']} on Modrinth failed: {e}")
    files = version.get("files", [])
    match = next((f for f in files if f["filename"] == mod["filename"]), None)
    match = match or next((f for f in files if f.get("primary")), files[0] if files else None)
    if not match:
        raise ActionError(f"Modrinth has no file for {mod['name']}.")
    return match["url"]


def sync_hosting(slug):
    """Point every mod's download url where the pack's hosting mode says."""
    mirror = load_settings(slug)["mirror"]
    changed, errors = False, []
    for mod in read_mods(slug):
        hosted = JAR_URL_RE.search(mod["url"])
        try:
            if hosted and (mod["source"] == "uploaded" or mirror):
                url = jar_url(hosted.group(1), mod["filename"])
            elif hosted:
                url = modrinth_file_url(mod)
            elif mirror and mod["mod_id"]:
                url = jar_url(mirror_jar(mod), mod["filename"])
            else:
                continue
        except ActionError as e:
            errors.append(e)
            continue
        if url != mod["url"]:
            set_download_url(mod["path"], url)
            changed = True
    if changed:
        packwiz(slug, "refresh")
    return errors


def collect_garbage():
    """Delete stored jars that no pack refers to anymore."""
    used = set()
    for meta_file in PACKS_DIR.glob("*/**/*.pw.toml"):
        used.update(JAR_URL_RE.findall(meta_file.read_text()))
    for jar_dir in JARS_DIR.iterdir():
        if jar_dir.name not in used:
            shutil.rmtree(jar_dir, ignore_errors=True)


def after_change(*slugs):
    """Bring hosting in line after packwiz ran; returns errors to report."""
    errors = []
    for slug in dict.fromkeys(slugs):
        try:
            errors += [f"{read_pack(slug)['name']}: {e}" for e in sync_hosting(slug)]
        except ActionError as e:
            errors.append(f"{slug}: {e}")
    collect_garbage()
    return errors


def safe_filename(name):
    name = os.path.basename(name.replace("\\", "/"))
    return re.sub(r"[^A-Za-z0-9._+-]", "_", name).lstrip(".")


def jar_metadata(data, filename):
    """Mod id and display name from the jar's loader metadata, if it has any."""
    try:
        jar = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        raise ActionError(f"{filename} is not a valid jar.")
    names = set(jar.namelist())
    try:
        if "fabric.mod.json" in names:
            meta = json.loads(jar.read("fabric.mod.json").decode("utf-8", "replace"), strict=False)
            return meta.get("id"), meta.get("name")
        if "quilt.mod.json" in names:
            meta = json.loads(jar.read("quilt.mod.json").decode("utf-8", "replace"))["quilt_loader"]
            return meta.get("id"), meta.get("metadata", {}).get("name")
        for toml_name in ("META-INF/neoforge.mods.toml", "META-INF/mods.toml"):
            if toml_name in names:
                mods = tomllib.loads(jar.read(toml_name).decode("utf-8", "replace")).get("mods", [])
                if mods:
                    name = mods[0].get("displayName", "")
                    return mods[0].get("modId"), None if "${" in name else name
    except (ValueError, KeyError, AttributeError, TypeError):
        pass
    return None, None


def write_upload_meta(meta_file, name, filename, side, sha256, sha512, mod_id=None, version_id=""):
    """Write the .pw.toml of an uploaded jar, optionally linked to a Modrinth project.

    x-uploaded marks the jar as ours: packwiz keeps unknown keys on refresh and
    drops them when it updates the mod from Modrinth, so the mark lasts exactly
    as long as the uploaded file is in use. sha512 lets Modrinth recognise it.
    """
    lines = [
        f"name = {json.dumps(name)}",
        f"filename = {json.dumps(filename)}",
        f'side = "{side}"',
        "x-uploaded = true",
        "",
        "[download]",
        f"url = {json.dumps(jar_url(sha256, filename))}",
        'hash-format = "sha512"',
        f'hash = "{sha512}"',
    ]
    if mod_id:
        lines += ["", "[update]", "[update.modrinth]",
                  f"mod-id = {json.dumps(mod_id)}", f"version = {json.dumps(version_id or '')}"]
    meta_file.write_text("\n".join(lines) + "\n")


def write_upload(slug, filename, sha256, sha512, jar_id, name, side, match):
    """Create (or replace) the .pw.toml for an uploaded jar; returns a message."""
    pack_name = read_pack(slug)["name"]
    stem = slugify(jar_id or name or filename.rsplit(".", 1)[0]) or "upload"
    existing = next((m for m in read_mods(slug) if m["stem"] == stem), None)
    if existing:
        side = existing["side"]
        meta_file = existing["path"]
    else:
        meta_file = PACKS_DIR / slug / "mods" / f"{stem}.pw.toml"
        meta_file.parent.mkdir(exist_ok=True)
    if match:
        mod_id, version_id = match["project_id"], match["id"]
    elif existing and existing["mod_id"]:
        # A new build of a mod that was linked by hand: keep the link.
        mod_id, version_id = existing["mod_id"], ""
    else:
        mod_id, version_id = None, ""
    write_upload_meta(meta_file, name or filename, filename, side, sha256, sha512, mod_id, version_id)
    note = "uploaded, matched on Modrinth" if match else "uploaded"
    if existing:
        return f"{pack_name}: {existing['filename']} → {filename} ({note})"
    return f"{pack_name}: added {filename} ({note})"


def uploaded_jar_bytes(mod):
    hosted = JAR_URL_RE.search(mod["url"])
    if not hosted:
        raise ActionError(f"{mod['name']} has no stored jar.")
    return (JARS_DIR / hosted.group(1) / os.path.basename(mod["filename"])).read_bytes()


# --- Modrinth -----------------------------------------------------------------

def modrinth_get(path, params=None):
    url = f"{MODRINTH_API}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    return json.loads(http_get(url))


def compatible_loaders(loader, mc_version):
    """Loaders whose mods run on this pack (packwiz accepts these too)."""
    if loader == "quilt":
        return ["quilt", "fabric"]
    if loader == "neoforge" and mc_version == "1.20.1":
        # NeoForge on 1.20.1 is a Forge fork; most mods there are only tagged forge.
        return ["neoforge", "forge"]
    return [loader] if loader else []


def modrinth_post(path, body):
    req = urllib.request.Request(f"{MODRINTH_API}{path}", json.dumps(body).encode(), {
        "User-Agent": USER_AGENT, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp)


def modrinth_search(query, facets, limit=20):
    data = modrinth_get("/search", {
        "query": query, "facets": json.dumps([["project_type:mod"], *facets]),
        "limit": limit, "index": "relevance",
    })
    return data.get("hits", [])


def pack_facets(pack):
    facets = [[f"versions:{pack['minecraft']}"]]
    loaders = compatible_loaders(pack["loader"], pack["minecraft"])
    if loaders:
        facets.append([f"categories:{l}" for l in loaders])
    return facets


def modrinth_version_by_hash(sha512):
    """The Modrinth version this exact file belongs to, or None."""
    try:
        return modrinth_get(f"/version_file/{sha512}", {"algorithm": "sha512"})
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def modrinth_project(slug_or_url):
    """Look up a project from a Modrinth URL, slug or id; None if it isn't one."""
    m = re.search(r"modrinth\.com/(?:mod|project|plugin|datapack)/([\w.-]+)", slug_or_url)
    slug = m.group(1) if m else slug_or_url.strip()
    if not re.fullmatch(r"[\w.-]+", slug):
        return None
    try:
        return modrinth_get(f"/project/{slug}")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def check_updates(pack):
    """Newest compatible Modrinth version for each linked mod that isn't on it."""
    linked = [m for m in pack["mods"] if m["mod_id"]]
    loaders = compatible_loaders(pack["loader"], pack["minecraft"])
    updates, unknown = {}, []
    by_hash = {m["hash"].lower(): m for m in linked if m["hash_format"] == "sha512" and m["version_id"]}
    if by_hash:
        latest = modrinth_post("/version_files/update", {
            "hashes": list(by_hash), "algorithm": "sha512",
            "loaders": loaders, "game_versions": [pack["minecraft"]],
        })
        for file_hash, mod in by_hash.items():
            if file_hash not in latest:
                unknown.append(mod)
            elif latest[file_hash]["id"] != mod["version_id"]:
                updates[mod["stem"]] = latest[file_hash]
    # Linked by hand, or a file Modrinth doesn't know: ask per project.
    unknown += [m for m in linked if m not in by_hash.values()]
    for mod in unknown:
        versions = modrinth_get(f"/project/{mod['mod_id']}/version", {
            "loaders": json.dumps(loaders), "game_versions": json.dumps([pack["minecraft"]])})
        if versions and versions[0]["id"] != mod["version_id"]:
            updates[mod["stem"]] = versions[0]
    return updates


_mc_versions_cache = []


def minecraft_versions():
    if not _mc_versions_cache:
        try:
            tags = modrinth_get("/tag/game_version")
            _mc_versions_cache.extend(t["version"] for t in tags if t.get("version_type") == "release")
        except (OSError, ValueError):
            return []
    return _mc_versions_cache


# --- Prism instance zip -------------------------------------------------------

def mmc_pack_components(pack):
    mc = pack["minecraft"]
    loader, version = pack["loader"], pack["loader_version"]
    components = [{"uid": "net.minecraft", "version": mc, "important": True}]
    if loader == "fabric":
        components += [{"uid": "net.fabricmc.intermediary", "version": mc},
                       {"uid": "net.fabricmc.fabric-loader", "version": version}]
    elif loader == "quilt":
        components += [{"uid": "net.fabricmc.intermediary", "version": mc},
                       {"uid": "org.quiltmc.quilt-loader", "version": version}]
    elif loader == "neoforge":
        components.append({"uid": "net.neoforged", "version": version})
    elif loader == "forge":
        components.append({"uid": "net.minecraftforge", "version": version})
    return components


def ini_string(value):
    # Prism reads instance.cfg with QSettings; a bare value loses its inner quotes.
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def prism_zip(pack):
    pre_launch = f'"$INST_JAVA" -jar packwiz-installer-bootstrap.jar {pack_url(pack["slug"])}'
    instance_cfg = "\n".join([
        "[General]",
        "ConfigVersion=1.3",
        "InstanceType=OneSix",
        f"name={ini_string(pack['name'])}",
        "iconKey=default",
        "OverrideCommands=true",
        f"PreLaunchCommand={ini_string(pre_launch)}",
        "",
    ])
    mmc_pack = {"components": mmc_pack_components(pack), "formatVersion": 1}
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("instance.cfg", instance_cfg)
        z.writestr("mmc-pack.json", json.dumps(mmc_pack, indent=2))
        z.write(BOOTSTRAP_JAR, ".minecraft/packwiz-installer-bootstrap.jar")
    buf.seek(0)
    return buf


# --- Public pages -------------------------------------------------------------

@app.route("/")
def index():
    return render_template("index.html", packs=all_packs(), pack_url=pack_url,
                           base=public_base())


@app.route("/packs/<slug>.zip")
def download_prism(slug):
    pack_dir(slug)
    return send_file(prism_zip(read_pack(slug)), mimetype="application/zip",
                     as_attachment=True, download_name=f"{slug}.zip")


@app.route("/packs/<slug>/<path:filename>")
def pack_file(slug, filename):
    return send_from_directory(pack_dir(slug), filename, max_age=0)


@app.route("/jars/<sha>/<filename>")
def jar_file(sha, filename):
    if not SHA256_RE.match(sha):
        abort(404)
    # Content-addressed, so it never changes and can be cached for good.
    return send_from_directory(JARS_DIR / sha, filename, max_age=31536000)


@app.route("/packwiz-installer-bootstrap.jar")
def bootstrap_jar():
    return send_file(BOOTSTRAP_JAR, as_attachment=True)


# --- Admin: auth and CSRF -----------------------------------------------------

_jwks_client = None


def access_email():
    """Validate the Cloudflare Access JWT; return the user's email or None."""
    global _jwks_client
    token = request.headers.get("Cf-Access-Jwt-Assertion", "")
    if not token:
        return None
    if _jwks_client is None:
        _jwks_client = jwt.PyJWKClient(f"https://{ACCESS_TEAM_DOMAIN}/cdn-cgi/access/certs")
    try:
        key = _jwks_client.get_signing_key_from_jwt(token)
        claims = jwt.decode(token, key.key, algorithms=["RS256"],
                            issuer=f"https://{ACCESS_TEAM_DOMAIN}",
                            options={"verify_aud": False})
    except jwt.PyJWTError:
        return None
    return claims.get("email") or claims.get("sub") or "service token"


def csrf_token():
    if "csrf" not in session:
        session["csrf"] = secrets.token_hex(16)
    return session["csrf"]


app.jinja_env.globals["csrf_token"] = csrf_token


def admin_route(rule, **options):
    def decorator(view):
        @wraps(view)
        def wrapped(*args, **kwargs):
            if ACCESS_TEAM_DOMAIN:
                g.user = access_email()
                if not g.user:
                    abort(403)
            else:
                g.user = request.headers.get("Cf-Access-Authenticated-User-Email")
            if request.method == "POST":
                sent = request.form.get("csrf", "")
                if not sent or not secrets.compare_digest(sent, session.get("csrf", "")):
                    abort(400, "Form expired, go back and try again.")
            return view(*args, **kwargs)
        return app.route(rule, **options)(wrapped)
    return decorator


# --- Admin: pages and actions -------------------------------------------------

@admin_route("/admin")
def admin_index():
    packs = all_packs()
    shared = {}
    for pack in packs:
        for mod in pack["mods"]:
            if mod["mod_id"]:
                entry = shared.setdefault(mod["mod_id"], {"name": mod["name"], "packs": []})
                entry["packs"].append(pack["name"])
    shared = sorted((dict(e, mod_id=k) for k, e in shared.items() if len(e["packs"]) > 1),
                    key=lambda e: e["name"].lower())
    update_counts = {}
    for pack in packs:
        try:
            update_counts[pack["slug"]] = len(check_updates(pack))
        except (OSError, ValueError):
            update_counts[pack["slug"]] = None
    return render_template("admin_index.html", packs=packs, shared=shared, loaders=LOADERS,
                           mc_versions=minecraft_versions(), pack_url=pack_url,
                           update_counts=update_counts)


@admin_route("/admin/new", methods=["POST"])
def admin_new():
    name = request.form.get("name", "").strip()
    mc = request.form.get("minecraft", "").strip()
    loader = request.form.get("loader", "")
    slug = slugify(name)
    if not SLUG_RE.match(slug) or not re.match(r"^[0-9][0-9a-z.\-]*$", mc) or loader not in LOADERS:
        flash("Fill in a name, a Minecraft version and a loader.", "error")
        return redirect(url_for("admin_index"))
    path = PACKS_DIR / slug
    with write_lock():
        if path.exists():
            flash(f"A pack called '{slug}' already exists.", "error")
            return redirect(url_for("admin_index"))
        path.mkdir()
        try:
            packwiz(path, "init", "--name", name, "--author", PACK_AUTHOR, "--version", "1.0.0",
                    "--mc-version", mc, "--modloader", loader, f"--{loader}-latest")
        except ActionError as e:
            shutil.rmtree(path, ignore_errors=True)
            flash(str(e), "error")
            return redirect(url_for("admin_index"))
        save_settings(slug, {"mirror": bool(request.form.get("mirror"))})
    flash(f"Pack '{name}' created.", "ok")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/copy", methods=["POST"])
def admin_copy(slug):
    src = pack_dir(slug)
    name = request.form.get("name", "").strip()
    new_slug = slugify(name)
    if not SLUG_RE.match(new_slug):
        flash("Give the copy a name.", "error")
        return redirect(url_for("admin_index"))
    dst = PACKS_DIR / new_slug
    with write_lock():
        if dst.exists():
            flash(f"A pack called '{new_slug}' already exists.", "error")
            return redirect(url_for("admin_index"))
        shutil.copytree(src, dst)
        toml = (dst / "pack.toml").read_text()
        toml = re.sub(r'^name = ".*"$', f"name = {json.dumps(name)}", toml, count=1, flags=re.M)
        (dst / "pack.toml").write_text(toml)
        save_settings(new_slug, load_settings(slug))
    flash(f"Copied to '{name}'.", "ok")
    return redirect(url_for("admin_pack", slug=new_slug))


@admin_route("/admin/pack/<slug>/delete", methods=["POST"])
def admin_delete(slug):
    path = pack_dir(slug)
    name = read_pack(slug)["name"]
    with write_lock():
        shutil.rmtree(path)
        (SETTINGS_DIR / f"{slug}.json").unlink(missing_ok=True)
        collect_garbage()
    flash(f"Pack '{name}' deleted.", "ok")
    return redirect(url_for("admin_index"))


@admin_route("/admin/pack/<slug>")
def admin_pack(slug):
    pack_dir(slug)
    pack = read_pack(slug)
    query = request.args.get("q", "").strip()
    results, others, search_error = [], [], None
    if query:
        try:
            results = modrinth_search(query, pack_facets(pack))
            # Close matches that have no build for this pack, so they don't just vanish.
            found = {hit["project_id"] for hit in results}
            others = [hit for hit in modrinth_search(query, [], limit=10)
                      if hit["project_id"] not in found][:5]
        except (OSError, ValueError) as e:
            search_error = f"Modrinth search failed: {e}"
    try:
        updates, update_error = check_updates(pack), None
    except (OSError, ValueError) as e:
        updates, update_error = {}, f"Checking Modrinth for updates failed: {e}"
    installed = {m["mod_id"] for m in pack["mods"] if m["mod_id"]}
    siblings = [p for p in all_packs() if p["loader"] == pack["loader"] and p["slug"] != slug]
    return render_template("admin_pack.html", pack=pack, query=query, results=results,
                           others=others, search_error=search_error, installed=installed,
                           siblings=siblings, sides=SIDES, pack_url=pack_url(slug),
                           max_upload_mb=MAX_UPLOAD_MB, chunk_mb=UPLOAD_CHUNK_MB, updates=updates,
                           update_error=update_error)


@admin_route("/admin/pack/<slug>/hosting", methods=["POST"])
def admin_hosting(slug):
    pack_dir(slug)
    mirror = request.form.get("mirror") == "1"
    with write_lock():
        save_settings(slug, {"mirror": mirror})
        errors = after_change(slug)
    for e in errors:
        flash(e, "error")
    if mirror:
        flash("Modrinth jars are now hosted here.", "ok")
    else:
        flash("Modrinth mods link to Modrinth again. Uploaded jars stay hosted here.", "ok")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/add", methods=["POST"])
def admin_add(slug):
    pack_dir(slug)
    project_id = request.form.get("project_id", "")
    if not re.match(r"^[A-Za-z0-9]{8}$", project_id):
        abort(400)
    targets = [read_pack(slug)]
    if request.form.get("everywhere"):
        loader = targets[0]["loader"]
        targets += [p for p in all_packs() if p["loader"] == loader and p["slug"] != slug]
    changes, errors = [], []
    with write_lock():
        for pack in targets:
            if any(m["mod_id"] == project_id for m in pack["mods"]):
                continue
            try:
                changes += run_and_report(pack, "modrinth", "add", "--project-id", project_id)
            except ActionError as e:
                errors.append(f"{pack['name']}: {e}")
        errors += after_change(*(p["slug"] for p in targets))
    flash_results(changes, errors, "Already in the pack.")
    return redirect(url_for("admin_pack", slug=slug, q=request.form.get("q", "")))


def upload_targets(slug):
    """The pack plus the "also add to" packs (same loader) from the upload form."""
    side = request.form.get("side", "both")
    if side not in SIDES:
        abort(400)
    loader = read_pack(slug)["loader"]
    also = request.form.getlist("also")
    targets = [slug] + [p["slug"] for p in all_packs()
                        if p["slug"] in also and p["loader"] == loader and p["slug"] != slug]
    return targets, side


def process_jar(targets, original_name, data, side):
    """Store one uploaded jar and add it to the target packs; returns messages.

    Call under write_lock. Doesn't refresh: finish_upload does that once.
    """
    filename = safe_filename(original_name)
    if not filename.lower().endswith(".jar"):
        raise ActionError(f"{original_name}: only .jar files can be uploaded.")
    jar_id, name = jar_metadata(data, filename)
    sha512 = hashlib.sha512(data).hexdigest()
    try:
        match = modrinth_version_by_hash(sha512)
    except (OSError, ValueError):
        match = None
    sha256 = store_jar(data, filename)
    return [write_upload(t, filename, sha256, sha512, jar_id, name, side, match) for t in targets]


def finish_upload(targets):
    """Refresh the packs after uploads; call under write_lock. Returns errors."""
    errors = []
    for t in targets:
        try:
            packwiz(t, "refresh")
        except ActionError as e:
            errors.append(e)
    return errors + after_change(*targets)


@admin_route("/admin/pack/<slug>/upload", methods=["POST"])
def admin_upload(slug):
    """Plain form upload, for when JavaScript is off; fine for a few small jars."""
    pack_dir(slug)
    targets, side = upload_targets(slug)
    files = [f for f in request.files.getlist("jars") if f.filename]
    if not files:
        flash("Pick one or more .jar files first.", "error")
        return redirect(url_for("admin_pack", slug=slug))
    changes, errors = [], []
    with write_lock():
        for upload in files:
            try:
                changes += process_jar(targets, upload.filename, upload.read(), side)
            except ActionError as e:
                errors.append(e)
        errors += finish_upload(targets)
    flash_results(changes, errors, "Nothing uploaded.")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/upload/chunk", methods=["POST"])
def admin_upload_chunk(slug):
    """Receive one chunk of one file; the last chunk stores and adds the jar.

    Chunks carry their byte offset, so a retried chunk that already arrived
    is recognised and not appended twice.
    """
    pack_dir(slug)
    upload_id = request.form.get("upload_id", "")
    chunk = request.files.get("chunk")
    try:
        offset, last = int(request.form["offset"]), request.form["last"] == "1"
    except (KeyError, ValueError):
        abort(400)
    if not re.fullmatch(r"[0-9a-f]{32}", upload_id) or chunk is None or offset < 0:
        abort(400)
    data = chunk.read()
    part = UPLOADS_DIR / f"{upload_id}.part"
    have = part.stat().st_size if part.is_file() else 0
    if have == offset:
        if offset + len(data) > MAX_UPLOAD_MB * 1024 * 1024:
            part.unlink(missing_ok=True)
            return {"ok": False, "message": f"bigger than {MAX_UPLOAD_MB} MB"}, 413
        with open(part, "ab") as f:
            f.write(data)
    elif have != offset + len(data):
        return {"ok": False, "message": "upload got out of step, try this file again"}, 409
    if not last:
        return {"ok": True}
    targets, side = upload_targets(slug)
    try:
        with write_lock():
            messages = process_jar(targets, request.form.get("filename", ""), part.read_bytes(), side)
    except ActionError as e:
        return {"ok": False, "message": str(e)}
    finally:
        part.unlink(missing_ok=True)
    return {"ok": True, "message": "\n".join(messages)}


@admin_route("/admin/pack/<slug>/upload/finish", methods=["POST"])
def admin_upload_finish(slug):
    pack_dir(slug)
    targets, _ = upload_targets(slug)
    with write_lock():
        errors = finish_upload(targets)
    # Leftovers of uploads that were abandoned halfway.
    for part in UPLOADS_DIR.glob("*.part"):
        if time.time() - part.stat().st_mtime > 86400:
            part.unlink(missing_ok=True)
    uploaded = int(request.form.get("uploaded", "0") or 0)
    failed = request.form.getlist("failed")
    flash_results([f"{uploaded} jar{'s' if uploaded != 1 else ''} uploaded."] if uploaded else [],
                  failed[:MAX_FLASH_LINES] + errors, "Nothing uploaded.")
    return {"ok": True}


def uploaded_mod(slug, stem):
    mod = next((m for m in read_mods(slug) if m["stem"] == stem), None)
    if not mod or mod["source"] != "uploaded":
        abort(404)
    return mod


@admin_route("/admin/pack/<slug>/link/<stem>")
def admin_link(slug, stem):
    pack_dir(slug)
    pack, mod = read_pack(slug), uploaded_mod(slug, stem)
    query = request.args.get("q", mod["name"]).strip()
    results, search_error, current = [], None, None
    try:
        if mod["mod_id"]:
            current = modrinth_get(f"/project/{mod['mod_id']}")
        if query:
            results = modrinth_search(query, pack_facets(pack))
            direct = modrinth_project(query)
            if direct and direct["id"] not in {hit["project_id"] for hit in results}:
                results.insert(0, dict(direct, project_id=direct["id"]))
    except (OSError, ValueError) as e:
        search_error = f"Modrinth lookup failed: {e}"
    return render_template("admin_link.html", pack=pack, mod=mod, query=query,
                           results=results, current=current, search_error=search_error)


@admin_route("/admin/pack/<slug>/link/<stem>", methods=["POST"])
def admin_link_save(slug, stem):
    pack_dir(slug)
    mod = uploaded_mod(slug, stem)
    project_id = request.form.get("project_id", "")
    if project_id and not re.match(r"^[A-Za-z0-9]{8}$", project_id):
        abort(400)
    with write_lock():
        data = uploaded_jar_bytes(mod)
        sha512 = hashlib.sha512(data).hexdigest()
        version_id = ""
        if project_id:
            try:
                match = modrinth_version_by_hash(sha512)
            except (OSError, ValueError):
                match = None
            if match and match["project_id"] == project_id:
                version_id = match["id"]
        write_upload_meta(mod["path"], mod["name"], mod["filename"], mod["side"],
                          hashlib.sha256(data).hexdigest(), sha512, project_id, version_id)
        try:
            packwiz(slug, "refresh")
        except ActionError as e:
            flash(str(e), "error")
    if project_id:
        flash(f"{mod['name']} is linked to Modrinth. Updates show up on the pack page.", "ok")
    else:
        flash(f"{mod['name']} is no longer linked to Modrinth.", "ok")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/update-one", methods=["POST"])
def admin_update_one(slug):
    pack_dir(slug)
    stem = request.form.get("stem", "")
    if stem not in {m["stem"] for m in read_mods(slug)}:
        abort(400)
    changes, errors = [], []
    with write_lock():
        try:
            changes = run_and_report(read_pack(slug), "update", stem)
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors)
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/remove", methods=["POST"])
def admin_remove(slug):
    pack_dir(slug)
    stem = request.form.get("stem", "")
    mods = {m["stem"]: m for m in read_mods(slug)}
    if stem not in mods:
        abort(400)
    with write_lock():
        try:
            packwiz(slug, "remove", stem)
            flash(f"Removed {mods[stem]['name']}.", "ok")
        except ActionError as e:
            flash(str(e), "error")
        collect_garbage()
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/side", methods=["POST"])
def admin_side(slug):
    pack_dir(slug)
    stem, side = request.form.get("stem", ""), request.form.get("side", "")
    mods = {m["stem"]: m for m in read_mods(slug)}
    if stem not in mods or side not in SIDES:
        abort(400)
    meta_file = mods[stem]["path"]
    with write_lock():
        toml = meta_file.read_text()
        if re.search(r'^side = ".*"$', toml, flags=re.M):
            toml = re.sub(r'^side = ".*"$', f'side = "{side}"', toml, count=1, flags=re.M)
        else:
            toml = re.sub(r"^(filename = .*)$", rf'\1\nside = "{side}"', toml, count=1, flags=re.M)
        meta_file.write_text(toml)
        try:
            packwiz(slug, "refresh")
            flash(f"{mods[stem]['name']} is now '{side}'.", "ok")
        except ActionError as e:
            flash(str(e), "error")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/update", methods=["POST"])
def admin_update_pack(slug):
    pack_dir(slug)
    changes, errors = [], []
    with write_lock():
        try:
            changes = run_and_report(read_pack(slug), "update", "--all")
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors)
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/update-all", methods=["POST"])
def admin_update_all():
    changes, errors = [], []
    with write_lock():
        packs = all_packs()
        for pack in packs:
            try:
                changes += run_and_report(pack, "update", "--all")
            except ActionError as e:
                errors.append(f"{pack['name']}: {e}")
        errors += after_change(*(p["slug"] for p in packs))
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))


@admin_route("/admin/update-mod", methods=["POST"])
def admin_update_mod():
    mod_id = request.form.get("mod_id", "")
    changes, errors, touched = [], [], []
    with write_lock():
        for pack in all_packs():
            for mod in pack["mods"]:
                if mod["mod_id"] == mod_id:
                    touched.append(pack["slug"])
                    try:
                        changes += run_and_report(pack, "update", mod["stem"])
                    except ActionError as e:
                        errors.append(f"{pack['name']}: {e}")
        errors += after_change(*touched)
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))
