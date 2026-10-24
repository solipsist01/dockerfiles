"""packwiz-web - a small web UI around the packwiz CLI, plus the pack host.

Public (no login): the pack files under /packs/<slug>/ that packwiz-installer
reads, self-hosted jars under /jars/, a Prism instance zip per pack, and an
overview page for players.
Admin (/admin, behind Cloudflare Access): create/copy/delete packs, add mods
from Modrinth and CurseForge, upload jars, change sides, remove and update
mods, across one or all packs.

Jar hosting: uploaded jars are always served from /jars/. Modrinth mods link
to Modrinth's CDN and CurseForge mods are fetched from CurseForge by
packwiz-installer, unless the pack is set to mirror; then their jars are
downloaded, hash-checked and served from /jars/ as well (CurseForge only where
the author allows it). Jars are stored once per content (sha256) and removed
when no pack uses them anymore.

CurseForge: packwiz adds and updates CurseForge mods with its own built-in key.
Search, update notices, mirroring and recognising uploads need our own key in
CURSEFORGE_API_KEY; without it, CurseForge mods can still be added by link.

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
import struct
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
CURSEFORGE_API = os.environ.get("CURSEFORGE_API_URL", "https://api.curseforge.com/v1").rstrip("/")
CURSEFORGE_API_KEY = os.environ.get("CURSEFORGE_API_KEY", "").strip()
CF_MINECRAFT, CF_CLASS_MODS = 432, 6
CF_LOADER_TYPES = {"forge": 1, "fabric": 4, "quilt": 5, "neoforge": 6}
CF_LOADER_NAMES = {v: k for k, v in CF_LOADER_TYPES.items()}
CF_URL_RE = re.compile(r"^https://(www\.)?curseforge\.com/minecraft/[\w-]+/[\w-]+(/files/\d+)?/?$")
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
        update = meta.get("update", {})
        modrinth, curseforge = update.get("modrinth", {}), update.get("curseforge", {})
        provider = "modrinth" if modrinth else "curseforge" if curseforge else None
        download = meta.get("download", {})
        url = download.get("url", "")
        if meta.get("x-uploaded") or not provider:
            source = "uploaded"
        elif JAR_URL_RE.search(url):
            source = "mirrored"
        else:
            source = provider
        cf_id = curseforge.get("project-id")
        mods.append({
            "path": meta_file,
            "stem": meta_file.name[:-len(".pw.toml")],
            "folder": str(meta_file.relative_to(root).parent),
            "name": meta.get("name", meta_file.stem),
            "filename": meta.get("filename", ""),
            "side": meta.get("side", "both"),
            "provider": provider,
            "mod_id": modrinth.get("mod-id"),
            "version_id": modrinth.get("version"),
            "cf_id": cf_id,
            "cf_file_id": curseforge.get("file-id"),
            "key": project_key(provider, modrinth.get("mod-id") or cf_id),
            "page_url": project_page(provider, modrinth.get("mod-id") or cf_id),
            "url": url,
            "hash_format": download.get("hash-format", ""),
            "hash": download.get("hash", ""),
            "source": source,
        })
    mods.sort(key=lambda m: m["name"].lower())
    return mods


def project_key(provider, project_id):
    """One id for a project across providers, e.g. "mr:AANobbMI" or "cf:238222"."""
    if not provider or not project_id:
        return None
    return f"{'mr' if provider == 'modrinth' else 'cf'}:{project_id}"


def project_page(provider, project_id):
    if provider == "modrinth":
        return f"https://modrinth.com/project/{project_id}"
    if provider == "curseforge":
        return f"https://www.curseforge.com/projects/{project_id}"
    return None


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


def set_download(meta_file, url):
    """Point a .pw.toml at url; None means CurseForge metadata mode (no url)."""
    toml = meta_file.read_text()
    toml = re.sub(r'^(url|mode) = ".*"\n', "", toml, flags=re.M)
    line = f"url = {json.dumps(url)}" if url else 'mode = "metadata:curseforge"'
    toml = re.sub(r"^\[download\]\n", f"[download]\n{line}\n", toml, count=1, flags=re.M)
    meta_file.write_text(toml)


def mirror_jar(mod, source_url):
    """Download a mod's jar, check it against the hash packwiz recorded, store it."""
    try:
        data = http_get(source_url, timeout=120)
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
            elif hosted and mod["provider"] == "curseforge":
                url = None  # packwiz-installer gets it from CurseForge again
            elif hosted:
                url = modrinth_file_url(mod)
            elif mirror and mod["provider"] == "modrinth":
                url = jar_url(mirror_jar(mod, mod["url"]), mod["filename"])
            elif mirror and mod["provider"] == "curseforge" and CURSEFORGE_API_KEY:
                source_url = curseforge_download_url(mod)
                if not source_url:
                    continue  # the author doesn't allow it; it stays a CurseForge download
                url = jar_url(mirror_jar(mod, source_url), mod["filename"])
            else:
                continue
        except ActionError as e:
            errors.append(e)
            continue
        if url != (mod["url"] or None):
            set_download(mod["path"], url)
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


def write_upload_meta(meta_file, name, filename, side, sha256, sha512, link=None):
    """Write the .pw.toml of an uploaded jar, optionally linked to a project.

    link is (provider, project id, version/file id); the version may be empty
    when the jar isn't a known file of that project. x-uploaded marks the jar as
    ours: packwiz keeps unknown keys on refresh and drops them when it updates
    the mod from Modrinth or CurseForge, so the mark lasts exactly as long as the
    uploaded file is in use. sha512 lets Modrinth recognise it.
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
    if link and link[0] == "modrinth":
        lines += ["", "[update]", "[update.modrinth]",
                  f"mod-id = {json.dumps(link[1])}", f"version = {json.dumps(link[2] or '')}"]
    elif link and link[0] == "curseforge":
        lines += ["", "[update]", "[update.curseforge]",
                  f"file-id = {int(link[2] or 0)}", f"project-id = {int(link[1])}"]
    meta_file.write_text("\n".join(lines) + "\n")


def mod_link(mod, keep_version=True):
    if not mod["provider"]:
        return None
    if mod["provider"] == "modrinth":
        return ("modrinth", mod["mod_id"], mod["version_id"] if keep_version else "")
    return ("curseforge", mod["cf_id"], mod["cf_file_id"] if keep_version else 0)


def write_upload(slug, filename, sha256, sha512, jar_id, name, side, matches):
    """Create (or replace) the .pw.toml for an uploaded jar; returns a message.

    matches are the (provider, project id, file id) links found for this exact
    file. A mod of the same project, or with the same name, is replaced, and
    keeps its provider when the file is on both sites.
    """
    pack_name = read_pack(slug)["name"]
    stem = slugify(jar_id or name or filename.rsplit(".", 1)[0]) or "upload"
    mods = read_mods(slug)
    keys = {project_key(*m[:2]): m for m in matches}
    existing = next((m for m in mods if m["key"] in keys), None)
    match = keys[existing["key"]] if existing else None
    existing = existing or next((m for m in mods if m["stem"] == stem), None)
    if existing and not match:
        match = next((m for m in matches if m[0] == existing["provider"]), None)
    match = match or (matches[0] if matches else None)
    if existing:
        side = existing["side"]
        meta_file = existing["path"]
    else:
        meta_file = PACKS_DIR / slug / "mods" / f"{stem}.pw.toml"
        meta_file.parent.mkdir(exist_ok=True)
    # Otherwise keep the link of the mod it replaces, minus the (now wrong) version.
    link = match or (mod_link(existing, keep_version=False) if existing else None)
    write_upload_meta(meta_file, name or filename, filename, side, sha256, sha512, link)
    note = f"uploaded, matched on {PROVIDER_NAMES[match[0]]}" if match else "uploaded"
    if existing:
        return f"{pack_name}: {existing['filename']} → {filename} ({note})"
    return f"{pack_name}: added {filename} ({note})"


def find_file_matches(data, sha512):
    """(provider, project id, file id) for each site that has this exact file."""
    matches = []
    try:
        version = modrinth_version_by_hash(sha512)
        if version:
            matches.append(("modrinth", version["project_id"], version["id"]))
    except (OSError, ValueError):
        pass
    if CURSEFORGE_API_KEY:
        try:
            found = curseforge_file_by_fingerprint(curseforge_fingerprint(data))
            if found:
                matches.append(("curseforge", found["modId"], found["id"]))
        except (OSError, ValueError, ActionError):
            pass
    return matches


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
    """Available updates per mod stem, and CurseForge mods that can't be mirrored."""
    updates = modrinth_updates(pack)
    cf_updates, restricted = curseforge_updates(
        pack, [m for m in pack["mods"] if m["provider"] == "curseforge"])
    updates.update(cf_updates)
    return updates, restricted


def modrinth_updates(pack):
    """Newest compatible Modrinth version for each linked mod that isn't on it."""
    linked = [m for m in pack["mods"] if m["provider"] == "modrinth"]
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


# --- CurseForge ---------------------------------------------------------------

PROVIDER_NAMES = {"modrinth": "Modrinth", "curseforge": "CurseForge"}


def curseforge_request(path, params=None, body=None):
    if not CURSEFORGE_API_KEY:
        raise ActionError("No CurseForge API key set (CURSEFORGE_API_KEY).")
    url = f"{CURSEFORGE_API}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    headers = {"x-api-key": CURSEFORGE_API_KEY, "Accept": "application/json", "User-Agent": USER_AGENT}
    if body is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, json.dumps(body).encode() if body is not None else None, headers)
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp)


def curseforge_hit(mod):
    """A CurseForge mod in the same shape as a Modrinth search hit."""
    files = mod.get("latestFilesIndexes", [])
    versions = sorted({f["gameVersion"] for f in files},
                      key=lambda v: [int(x) if x.isdigit() else 0 for x in v.split(".")])
    return {
        "provider": "curseforge",
        "project_id": str(mod["id"]),
        "key": project_key("curseforge", mod["id"]),
        "title": mod["name"],
        "slug": mod["slug"],
        "description": mod.get("summary", ""),
        "icon_url": (mod.get("logo") or {}).get("thumbnailUrl"),
        "downloads": int(mod.get("downloadCount", 0)),
        "url": (mod.get("links") or {}).get("websiteUrl") or project_page("curseforge", mod["id"]),
        "versions": versions,
        "loaders": sorted({CF_LOADER_NAMES[f["modLoader"]] for f in files if f.get("modLoader") in CF_LOADER_NAMES}),
        "restricted": mod.get("allowModDistribution") is False,
    }


def modrinth_hit(hit):
    return dict(hit, provider="modrinth", key=project_key("modrinth", hit["project_id"]),
                url=f"https://modrinth.com/mod/{hit['slug']}", restricted=False,
                loaders=[c for c in hit.get("categories", []) if c in LOADERS])


def curseforge_search(query, pack=None, limit=20):
    params = {"gameId": CF_MINECRAFT, "classId": CF_CLASS_MODS, "searchFilter": query,
              "sortField": 2, "sortOrder": "desc", "pageSize": limit}
    if pack is None:
        return [curseforge_hit(m) for m in curseforge_request("/mods/search", params)["data"]]
    # One search per loader: the API's multi-loader filter gives poor results.
    hits = {}
    for loader in compatible_loaders(pack["loader"], pack["minecraft"]):
        if loader in CF_LOADER_TYPES:
            found = curseforge_request("/mods/search", dict(
                params, gameVersion=pack["minecraft"], modLoaderType=CF_LOADER_TYPES[loader]))
            for mod in found["data"]:
                hits.setdefault(mod["id"], mod)
    ranked = sorted(hits.values(), key=lambda m: -m.get("downloadCount", 0))[:limit]
    return [curseforge_hit(m) for m in ranked]


def search(provider, query, pack=None, limit=20):
    if provider == "curseforge":
        return curseforge_search(query, pack, limit)
    return [modrinth_hit(h) for h in modrinth_search(query, pack_facets(pack) if pack else [], limit)]


def curseforge_download_url(mod):
    """Where CurseForge serves a mod's file, or None if the author doesn't allow it."""
    try:
        return curseforge_request(f"/mods/{mod['cf_id']}/files/{mod['cf_file_id']}/download-url")["data"]
    except urllib.error.HTTPError as e:
        if e.code in (403, 404):
            return None
        raise ActionError(f"Asking CurseForge for {mod['name']} failed: {e}")
    except (OSError, ValueError) as e:
        raise ActionError(f"Asking CurseForge for {mod['name']} failed: {e}")


def curseforge_fingerprint(data):
    """CurseForge's file fingerprint: MurmurHash2 (seed 1) of the file minus whitespace."""
    data = data.translate(None, b"\t\n\r ")
    m, mask = 0x5BD1E995, 0xFFFFFFFF
    h = (1 ^ len(data)) & mask
    n = len(data) // 4 * 4
    for (k,) in struct.iter_unpack("<I", data[:n]):
        k = (k * m) & mask
        k ^= k >> 24
        k = (k * m) & mask
        h = ((h * m) & mask) ^ k
    rest = data[n:]
    if len(rest) == 3:
        h ^= rest[2] << 16
    if len(rest) >= 2:
        h ^= rest[1] << 8
    if rest:
        h ^= rest[0]
        h = (h * m) & mask
    h ^= h >> 13
    h = (h * m) & mask
    return h ^ (h >> 15)


def curseforge_file_by_fingerprint(fingerprint):
    found = curseforge_request(f"/fingerprints/{CF_MINECRAFT}", body={"fingerprints": [fingerprint]})
    matches = found["data"].get("exactMatches") or []
    return matches[0]["file"] if matches else None


def curseforge_project(project_id):
    return curseforge_hit(curseforge_request(f"/mods/{int(project_id)}")["data"])


def curseforge_updates(pack, mods):
    """Updates for CurseForge mods, plus the ones whose author forbids mirroring."""
    if not CURSEFORGE_API_KEY or not mods:
        return {}, set()
    found = curseforge_request("/mods", body={"modIds": [m["cf_id"] for m in mods], "filterPcOnly": True})
    info = {m["id"]: m for m in found["data"]}
    loaders = {CF_LOADER_TYPES[l] for l in compatible_loaders(pack["loader"], pack["minecraft"])
               if l in CF_LOADER_TYPES}
    updates, restricted = {}, set()
    for mod in mods:
        project = info.get(mod["cf_id"])
        if not project:
            continue
        if project.get("allowModDistribution") is False:
            restricted.add(mod["stem"])
        files = [f for f in project.get("latestFilesIndexes", [])
                 if f.get("gameVersion") == pack["minecraft"] and f.get("modLoader") in loaders]
        if files:
            newest = max(files, key=lambda f: f["fileId"])
            if newest["fileId"] != mod["cf_file_id"]:
                updates[mod["stem"]] = {"version_number": newest["filename"], "name": newest["filename"]}
    return updates, restricted


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
            if mod["key"]:
                entry = shared.setdefault(mod["key"], {"name": mod["name"], "packs": []})
                entry["packs"].append(pack["name"])
    shared = sorted((dict(e, key=k) for k, e in shared.items() if len(e["packs"]) > 1),
                    key=lambda e: e["name"].lower())
    update_counts = {}
    for pack in packs:
        try:
            update_counts[pack["slug"]] = len(check_updates(pack)[0])
        except (OSError, ValueError, ActionError):
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
    provider = request.args.get("source", "modrinth")
    if provider != "curseforge" or not CURSEFORGE_API_KEY:
        provider = "modrinth"
    results, others, search_error = [], [], None
    if query:
        try:
            results = search(provider, query, pack)
            # Close matches that have no build for this pack, so they don't just vanish.
            found = {hit["key"] for hit in results}
            others = [hit for hit in search(provider, query, limit=10) if hit["key"] not in found][:5]
        except (OSError, ValueError, ActionError) as e:
            search_error = f"{PROVIDER_NAMES[provider]} search failed: {e}"
    try:
        (updates, restricted), update_error = check_updates(pack), None
    except (OSError, ValueError, ActionError) as e:
        updates, restricted, update_error = {}, set(), f"Checking for updates failed: {e}"
    installed = {m["key"] for m in pack["mods"] if m["key"]}
    siblings = [p for p in all_packs() if p["loader"] == pack["loader"] and p["slug"] != slug]
    return render_template("admin_pack.html", pack=pack, query=query, results=results,
                           others=others, search_error=search_error, installed=installed,
                           siblings=siblings, sides=SIDES, pack_url=pack_url(slug),
                           max_upload_mb=MAX_UPLOAD_MB, chunk_mb=UPLOAD_CHUNK_MB, updates=updates,
                           update_error=update_error, restricted=restricted, provider=provider,
                           cf_enabled=bool(CURSEFORGE_API_KEY), provider_names=PROVIDER_NAMES)


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
        flash("Modrinth and CurseForge jars are now hosted here (where the author allows it).", "ok")
    else:
        flash("Mods download from Modrinth/CurseForge again. Uploaded jars stay hosted here.", "ok")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/add", methods=["POST"])
def admin_add(slug):
    pack_dir(slug)
    provider = request.form.get("provider", "modrinth")
    project_id = request.form.get("project_id", "")
    if provider == "modrinth" and re.fullmatch(r"[A-Za-z0-9]{8}", project_id):
        command = ["modrinth", "add", "--project-id", project_id]
    elif provider == "curseforge" and re.fullmatch(r"[0-9]{1,10}", project_id):
        command = ["curseforge", "add", "--addon-id", project_id]
    else:
        abort(400)
    key = project_key(provider, project_id)
    targets = [read_pack(slug)]
    if request.form.get("everywhere"):
        loader = targets[0]["loader"]
        targets += [p for p in all_packs() if p["loader"] == loader and p["slug"] != slug]
    changes, errors = [], []
    with write_lock():
        for pack in targets:
            if any(str(m["key"]) == key for m in pack["mods"]):
                continue
            try:
                changes += run_and_report(pack, *command)
            except ActionError as e:
                errors.append(f"{pack['name']}: {e}")
        errors += after_change(*(p["slug"] for p in targets))
    flash_results(changes, errors, "Already in the pack.")
    return redirect(url_for("admin_pack", slug=slug, q=request.form.get("q", ""), source=provider))


@admin_route("/admin/pack/<slug>/add-link", methods=["POST"])
def admin_add_link(slug):
    """Add a CurseForge mod from its page link or slug; works without an API key."""
    pack_dir(slug)
    link = request.form.get("link", "").strip()
    if not (CF_URL_RE.match(link) or re.fullmatch(r"[a-z0-9][a-z0-9-]*", link)):
        flash("Paste a CurseForge mod link (https://www.curseforge.com/minecraft/mc-mods/...) or its slug.", "error")
        return redirect(url_for("admin_pack", slug=slug))
    changes, errors = [], []
    with write_lock():
        try:
            changes = run_and_report(read_pack(slug), "curseforge", "add", link)
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors, f"No CurseForge mod found for '{link}' (or it's already in the pack).")
    return redirect(url_for("admin_pack", slug=slug))


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
    matches = find_file_matches(data, sha512)
    sha256 = store_jar(data, filename)
    return [write_upload(t, filename, sha256, sha512, jar_id, name, side, matches) for t in targets]


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
    provider = request.args.get("source", mod["provider"] or "modrinth")
    if provider != "curseforge" or not CURSEFORGE_API_KEY:
        provider = "modrinth"
    results, search_error, current = [], None, None
    try:
        if mod["provider"] == "modrinth":
            project = modrinth_get(f"/project/{mod['mod_id']}")
            current = {"title": project["title"], "description": project["description"],
                       "icon_url": project.get("icon_url"), "url": mod["page_url"], "key": mod["key"]}
        elif mod["provider"] == "curseforge":
            current = (curseforge_project(mod["cf_id"]) if CURSEFORGE_API_KEY else
                       {"title": f"CurseForge project {mod['cf_id']}", "description": "",
                        "icon_url": None, "url": mod["page_url"], "key": mod["key"]})
        if query:
            results = search(provider, query, pack)
            direct = modrinth_project(query) if provider == "modrinth" else None
            if direct and direct["id"] not in {hit["project_id"] for hit in results}:
                results.insert(0, modrinth_hit(dict(direct, project_id=direct["id"])))
    except (OSError, ValueError, ActionError) as e:
        search_error = f"Lookup failed: {e}"
    return render_template("admin_link.html", pack=pack, mod=mod, query=query, results=results,
                           current=current, search_error=search_error, provider=provider,
                           cf_enabled=bool(CURSEFORGE_API_KEY), provider_names=PROVIDER_NAMES)


@admin_route("/admin/pack/<slug>/link/<stem>", methods=["POST"])
def admin_link_save(slug, stem):
    pack_dir(slug)
    mod = uploaded_mod(slug, stem)
    provider = request.form.get("provider", "")
    project_id = request.form.get("project_id", "")
    if project_id and not ((provider == "modrinth" and re.fullmatch(r"[A-Za-z0-9]{8}", project_id)) or
                           (provider == "curseforge" and re.fullmatch(r"[0-9]{1,10}", project_id))):
        abort(400)
    with write_lock():
        data = uploaded_jar_bytes(mod)
        sha512 = hashlib.sha512(data).hexdigest()
        link = None
        if project_id:
            # Keep the exact version when this jar is a known file of that project.
            wanted = project_key(provider, project_id)
            link = next((m for m in find_file_matches(data, sha512) if project_key(*m[:2]) == wanted),
                        (provider, project_id, ""))
        write_upload_meta(mod["path"], mod["name"], mod["filename"], mod["side"],
                          hashlib.sha256(data).hexdigest(), sha512, link)
        try:
            packwiz(slug, "refresh")
        except ActionError as e:
            flash(str(e), "error")
    if project_id:
        flash(f"{mod['name']} is linked to {PROVIDER_NAMES[provider]}. Updates show up on the pack page.", "ok")
    else:
        flash(f"{mod['name']} is no longer linked.", "ok")
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
    key = request.form.get("key", "")
    changes, errors, touched = [], [], []
    with write_lock():
        for pack in all_packs():
            for mod in pack["mods"]:
                if key and mod["key"] == key:
                    touched.append(pack["slug"])
                    try:
                        changes += run_and_report(pack, "update", mod["stem"])
                    except ActionError as e:
                        errors.append(f"{pack['name']}: {e}")
        errors += after_change(*touched)
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))
