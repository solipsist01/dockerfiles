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
from concurrent.futures import ThreadPoolExecutor
import tempfile
import threading
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
MAX_UPLOAD_MB = int(os.environ.get("MAX_UPLOAD_MB", "4096"))
# CurseForge fingerprints are computed in Python (~0.1 s/MB); above this size we
# skip them so the last upload request stays well inside Cloudflare's timeout.
FINGERPRINT_MAX_MB = 256
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
CF_MINECRAFT = 432
# What can be searched and uploaded: folder in the pack, Modrinth project type,
# CurseForge class, and the upload file extension.
KINDS = {
    "mod": {"label": "Mods", "folder": "mods", "modrinth": "mod", "cf_class": 6, "ext": ".jar"},
    "resourcepack": {"label": "Resource packs", "folder": "resourcepacks", "modrinth": "resourcepack",
                     "cf_class": 12, "ext": ".zip"},
}
FOLDER_LABELS = {k["folder"]: k["label"] for k in KINDS.values()}
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


ALIASES_FILE = DATA_DIR / "aliases.json"


def load_aliases():
    """Old pack URLs (slugs) of renamed packs -> their current slug."""
    try:
        return json.loads(ALIASES_FILE.read_text())
    except (FileNotFoundError, ValueError):
        return {}


def save_aliases(aliases):
    ALIASES_FILE.write_text(json.dumps(aliases, indent=1))


def resolve_slug(slug):
    """The current slug for a pack URL, following renames."""
    aliases = load_aliases()
    seen = set()
    while slug in aliases and slug not in seen and not (PACKS_DIR / slug / "pack.toml").is_file():
        seen.add(slug)
        slug = aliases[slug]
    return slug


def slug_taken(slug):
    """A pack, or the old URL of a renamed pack, already uses this slug."""
    if (PACKS_DIR / slug).exists():
        return f"A pack called '{slug}' already exists."
    if slug in load_aliases():
        return f"'{slug}' is the old URL of a renamed pack (players still use it); pick another name."
    return None


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
        "stable": load_settings(slug)["stable"],
        "servers": load_settings(slug)["servers"],
        "servers_once": load_settings(slug)["servers_once"],
        "default_packs": load_settings(slug)["default_packs"],
        "options_extra": load_settings(slug)["options_extra"],
        "mem_min": load_settings(slug)["mem_min"],
        "mem_max": load_settings(slug)["mem_max"],
        "jvm_args": load_settings(slug)["jvm_args"],
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
            "pinned": bool(meta.get("pin", False)),
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
            "download_from": download_origin(url, download.get("mode", "")),
            # Path of the hosted file under /jars/, for the download link in the admin list.
            "jar_path": urllib.parse.urlparse(url).path if JAR_URL_RE.search(url) else None,
        })
    mods.sort(key=lambda m: m["name"].lower())
    return mods


def download_origin(url, mode):
    """Where players and servers get a mod's file: "here", or the remote site."""
    if JAR_URL_RE.search(url):
        return "here"
    if mode == "metadata:curseforge":
        return "CurseForge"
    host = urllib.parse.urlparse(url).hostname or ""
    if host.endswith("modrinth.com"):
        return "Modrinth"
    if host.endswith("forgecdn.net") or host.endswith("curseforge.com"):
        return "CurseForge"
    return host or "?"


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
    return {"mirror": bool(settings.get("mirror", False)), "stable": bool(settings.get("stable", False)),
            "servers": list(settings.get("servers", [])), "servers_once": bool(settings.get("servers_once", False)),
            "default_packs": list(settings.get("default_packs", [])), "options_extra": settings.get("options_extra", ""),
            "mem_min": int(settings.get("mem_min", 0)), "mem_max": int(settings.get("mem_max", 0)),
            "jvm_args": settings.get("jvm_args", ""),
            # Resource packs seen so far (stem -> project key); None until first looked at.
            "known_packs": settings.get("known_packs")}


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
    # Keyed by project where known, so a mod that moved to another file name still pairs up.
    return {m["key"] or m["stem"]: m["filename"] for m in read_mods(slug)}


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


def hash_file(path):
    """(sha256, sha512) of a file, read in blocks so big files don't fill memory."""
    sha256, sha512 = hashlib.sha256(), hashlib.sha512()
    with open(path, "rb") as f:
        while block := f.read(8 * 1024 * 1024):
            sha256.update(block)
            sha512.update(block)
    return sha256.hexdigest(), sha512.hexdigest()


def store_file(path, filename, sha256):
    """Move an uploaded file into the jar store (same disk, so no copying)."""
    filename = os.path.basename(filename)
    target = JARS_DIR / sha256 / filename
    if target.is_file():
        Path(path).unlink(missing_ok=True)
    else:
        target.parent.mkdir(exist_ok=True)
        os.replace(path, target)
    return sha256


def jar_url(sha, filename):
    return f"{public_base()}/jars/{sha}/{urllib.parse.quote(os.path.basename(filename))}"


def set_download(meta_file, url):
    """Point a .pw.toml at url; None means CurseForge metadata mode (no url)."""
    toml = meta_file.read_text()
    toml = re.sub(r'^(url|mode) = ".*"\n', "", toml, flags=re.M)
    line = f"url = {json.dumps(url)}" if url else 'mode = "metadata:curseforge"'
    toml = re.sub(r"^\[download\]\n", f"[download]\n{line}\n", toml, count=1, flags=re.M)
    meta_file.write_text(toml)


def fetch_jar(mod, source_url):
    """Download a mod's jar and check it against the hash packwiz recorded."""
    try:
        data = http_get(source_url, timeout=120)
    except OSError as e:
        raise ActionError(f"Downloading {mod['filename']} failed: {e}")
    if mod["hash_format"] in hashlib.algorithms_available:
        if hashlib.new(mod["hash_format"], data).hexdigest() != mod["hash"].lower():
            raise ActionError(f"Downloaded {mod['filename']} doesn't match its hash.")
    return data


def mirror_jar(mod, source_url):
    """Download a mod's jar, check it, store it; returns its sha256."""
    return store_jar(fetch_jar(mod, source_url), mod["filename"])


def modrinth_file_url(mod):
    """The Modrinth CDN url of a mod's current version, to stop mirroring it."""
    try:
        version = json.loads(http_get(f"{MODRINTH_API}/version/{mod['version_id']}"))
    except (OSError, ValueError) as e:
        raise ActionError(f"Looking up {mod['name']} on Modrinth failed: {e}")
    files = version.get("files", [])
    # The exact file first: an adopted upload may carry a different filename.
    match = next((f for f in files if f.get("hashes", {}).get(mod["hash_format"]) == mod["hash"].lower()), None)
    match = match or next((f for f in files if f["filename"] == mod["filename"]), None)
    match = match or next((f for f in files if f.get("primary")), files[0] if files else None)
    if not match:
        raise ActionError(f"Modrinth has no file for {mod['name']}.")
    return match["url"]


def sync_hosting(slug):
    """Point every mod's download url where the pack's hosting mode says."""
    mirror = load_settings(slug)["mirror"]
    changed, errors = False, []
    for mod in read_mods(slug):
        if can_go_remote(mod):
            # An upload that is a known Modrinth/CurseForge file is just that
            # mod: drop the upload mark so it follows the hosting mode. CurseForge
            # files whose author blocks third-party downloads stay ours.
            mod["path"].write_text(re.sub(r"^x-uploaded = true\n", "", mod["path"].read_text(), flags=re.M))
            mod["source"] = "mirrored"
            changed = True
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
            sync_options(slug)    # a resource pack update can change its file name
        except ActionError as e:
            errors.append(f"{slug}: {e}")
    collect_garbage()
    return errors


def safe_filename(name):
    name = os.path.basename(name.replace("\\", "/"))
    return re.sub(r"[^A-Za-z0-9._+-]", "_", name).lstrip(".")


def jar_metadata(path, filename):
    """Mod id and display name from the jar's loader metadata, if it has any."""
    try:
        jar = zipfile.ZipFile(path)
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


def is_exact_link(mod):
    """Linked to a known file (not just a project), so it can be fetched remotely."""
    return bool((mod["provider"] == "modrinth" and mod["version_id"]) or
                (mod["provider"] == "curseforge" and mod["cf_file_id"]))


_cf_remote_allowed = {}


def remote_allowed(mod):
    """Can players and servers fetch this file remotely without a manual download?

    Modrinth: always. CurseForge: only where the author allows third-party
    downloads, and we can only tell with an API key; when in doubt, no.
    """
    if mod["provider"] == "modrinth":
        return True
    if mod["provider"] != "curseforge" or not CURSEFORGE_API_KEY:
        return False
    key = (mod["cf_id"], mod["cf_file_id"])
    if key not in _cf_remote_allowed:
        try:
            _cf_remote_allowed[key] = curseforge_download_url(mod) is not None
        except ActionError:
            return False
    return _cf_remote_allowed[key]


def can_go_remote(mod):
    """An upload that is a known remote file, which players can fetch themselves."""
    return mod["source"] == "uploaded" and is_exact_link(mod) and remote_allowed(mod)


def adopt_linked_uploads(*packs):
    """Run the hosting sync for packs with uploads that should follow the hosting mode."""
    stale = [p["slug"] for p in packs if any(can_go_remote(m) for m in p["mods"])]
    if stale:
        with write_lock():
            for e in after_change(*stale):
                flash(e, "error")
    return stale


def write_upload_meta(meta_file, name, filename, side, sha256, sha512, link=None):
    """Write the .pw.toml of an uploaded jar, optionally linked to a project.

    link is (provider, project id, version/file id); the version is empty when
    the jar isn't a known file of that project. A jar that is a known file is
    written as a normal Modrinth/CurseForge mod, so it follows the pack's hosting
    mode. Any other jar gets x-uploaded: it can only be hosted here. packwiz keeps
    unknown keys on refresh and drops them when it updates the mod, so the mark
    lasts exactly as long as that jar is in use. sha512 lets Modrinth recognise it.
    """
    lines = [
        f"name = {json.dumps(name)}",
        f"filename = {json.dumps(filename)}",
        f'side = "{side}"',
    ]
    if not (link and link[2]):
        lines.append("x-uploaded = true")
    lines += [
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


def write_upload(slug, filename, sha256, sha512, jar_id, name, side, matches, folder="mods"):
    """Create (or replace) the .pw.toml for an uploaded jar; returns a message.

    matches are the (provider, project id, file id) links found for this exact
    file. A mod of the same project, or with the same name, is replaced, and
    keeps its provider when the file is on both sites.
    """
    pack_name = read_pack(slug)["name"]
    stem = slugify(jar_id or name or filename.rsplit(".", 1)[0]) or "upload"
    mods = [m for m in read_mods(slug) if m["folder"] == folder]
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
        meta_file = PACKS_DIR / slug / folder / f"{stem}.pw.toml"
        meta_file.parent.mkdir(exist_ok=True)
    # Otherwise keep the link of the mod it replaces, minus the (now wrong) version.
    link = match or (mod_link(existing, keep_version=False) if existing else None)
    write_upload_meta(meta_file, name or filename, filename, side, sha256, sha512, link)
    note = f"uploaded, matched on {PROVIDER_NAMES[match[0]]}" if match else "uploaded"
    if existing:
        return f"{pack_name}: {existing['filename']} → {filename} ({note})"
    return f"{pack_name}: added {filename} ({note})"


def find_file_matches(path, sha512):
    """(provider, project id, file id) for each site that has this exact file."""
    matches = []
    try:
        version = modrinth_version_by_hash(sha512)
        if version:
            matches.append(("modrinth", version["project_id"], version["id"]))
    except (OSError, ValueError):
        pass
    if CURSEFORGE_API_KEY and os.path.getsize(path) <= FINGERPRINT_MAX_MB * 1024 * 1024:
        try:
            found = curseforge_file_by_fingerprint(curseforge_fingerprint(Path(path).read_bytes()))
            if found:
                matches.append(("curseforge", found["modId"], found["id"]))
        except (OSError, ValueError, ActionError):
            pass
    return matches


def mod_jar_path(mod):
    """A copy of the file a mod uses right now, in the uploads folder.

    Stored here: copied from the jar store. Otherwise fetched from
    Modrinth/CurseForge. The caller moves or deletes it.
    """
    target = UPLOADS_DIR / f"{secrets.token_hex(16)}.part"
    hosted = JAR_URL_RE.search(mod["url"])
    if hosted:
        shutil.copyfile(JARS_DIR / hosted.group(1) / os.path.basename(mod["filename"]), target)
    else:
        target.write_bytes(mod_jar_bytes(mod))
    return target


def mod_jar_bytes(mod):
    """The jar a mod uses right now, fetched from Modrinth/CurseForge."""
    if mod["url"]:
        return fetch_jar(mod, mod["url"])
    if mod["provider"] == "curseforge" and CURSEFORGE_API_KEY:
        source_url = curseforge_download_url(mod)
        if source_url:
            return fetch_jar(mod, source_url)
    raise ActionError(f"Can't download {mod['filename']} from CurseForge here. "
                      "Download it from CurseForge and upload it instead.")


def set_side(meta_file, side):
    toml = meta_file.read_text()
    if re.search(r'^side = ".*"$', toml, flags=re.M):
        toml = re.sub(r'^side = ".*"$', f'side = "{side}"', toml, count=1, flags=re.M)
    else:
        toml = re.sub(r"^(filename = .*)$", rf'\1\nside = "{side}"', toml, count=1, flags=re.M)
    meta_file.write_text(toml)


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


def modrinth_search(query, facets, limit=20, kind="mod"):
    data = modrinth_get("/search", {
        "query": query, "facets": json.dumps([[f"project_type:{KINDS[kind]['modrinth']}"], *facets]),
        "limit": limit, "index": "relevance",
    })
    return data.get("hits", [])


def pack_facets(pack, kind="mod"):
    facets = [[f"versions:{pack['minecraft']}"]]
    loaders = compatible_loaders(pack["loader"], pack["minecraft"])
    if loaders and kind == "mod":
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


# Update checks ask Modrinth and CurseForge, which takes seconds; pages use the last result.
# A result is reused while the pack's mods are unchanged; once it is older than this, the
# page still gets it right away and a fresh check runs in the background.
UPDATE_CHECK_MAX_AGE = 15 * 60
_update_checks = {}   # slug -> (fingerprint, time, (updates, restricted))
_update_checks_lock = threading.Lock()
_update_checks_running = set()


def _update_fingerprint(pack):
    """What an update check depends on: the pack's versions and settings and each mod's file."""
    mods = tuple((m["stem"], m["provider"], m["version_id"], m["cf_file_id"], m["hash"], m["pinned"])
                 for m in pack["mods"])
    return pack["minecraft"], pack["loader"], pack.get("stable"), mods


def check_updates(pack, fresh=False):
    """Available updates per mod stem, and CurseForge mods that can't be mirrored.

    Served from the last check of this pack unless fresh is set or the mods changed since.
    """
    slug, fingerprint = pack["slug"], _update_fingerprint(pack)
    if not fresh:
        with _update_checks_lock:
            cached = _update_checks.get(slug)
        if cached and cached[0] == fingerprint:
            if time.time() - cached[1] > UPDATE_CHECK_MAX_AGE:
                _recheck_in_background(pack, fingerprint)
            updates, restricted = cached[2]
            return dict(updates), set(restricted)
    result = _check_updates_now(pack)
    with _update_checks_lock:
        _update_checks[slug] = (fingerprint, time.time(), result)
    return dict(result[0]), set(result[1])


def _recheck_in_background(pack, fingerprint):
    slug = pack["slug"]
    with _update_checks_lock:
        if slug in _update_checks_running:
            return
        _update_checks_running.add(slug)

    def run():
        try:
            result = _check_updates_now(pack)
            with _update_checks_lock:
                _update_checks[slug] = (fingerprint, time.time(), result)
        except (OSError, ValueError, ActionError):
            pass   # keep showing the last result; the next page view tries again
        finally:
            with _update_checks_lock:
                _update_checks_running.discard(slug)

    threading.Thread(target=run, daemon=True).start()


def _check_updates_now(pack):
    updates = modrinth_updates(pack)
    cf_updates, restricted = curseforge_updates(
        pack, [m for m in pack["mods"] if m["provider"] == "curseforge"])
    updates.update(cf_updates)
    # Pinned mods stay on the version that was picked for them.
    pinned = {m["stem"] for m in pack["mods"] if m["pinned"]}
    return {stem: u for stem, u in updates.items() if stem not in pinned}, restricted


def content_loaders(pack, mod=None):
    """Modrinth loaders to accept: the pack's for mods, "minecraft" for resource packs."""
    if mod is not None and mod["folder"] != "mods":
        return ["minecraft"]
    return compatible_loaders(pack["loader"], pack["minecraft"])


def modrinth_versions(pack, project_id, mod=None):
    """A project's versions for this pack's Minecraft version and loaders, newest first."""
    return modrinth_get(f"/project/{project_id}/version", {
        "loaders": json.dumps(content_loaders(pack, mod)),
        "game_versions": json.dumps([pack["minecraft"]])})


def newer_release(versions, current_id):
    """The newest release in versions if it's newer than the current version, else None."""
    release = next((v for v in versions if v["version_type"] == "release"), None)
    if not release or release["id"] == current_id:
        return None
    current = next((v for v in versions if v["id"] == current_id), None)
    # Don't offer an older release to someone who picked a newer beta on purpose.
    if current and current["date_published"] >= release["date_published"]:
        return None
    return release


def modrinth_updates(pack):
    """Newest compatible Modrinth version for each linked mod that isn't on it.

    With the pack's "stable releases only" setting, betas and alphas are skipped.
    """
    linked = [m for m in pack["mods"] if m["provider"] == "modrinth"]
    loaders = compatible_loaders(pack["loader"], pack["minecraft"])
    updates, unknown = {}, []
    if pack.get("stable"):
        # The batch endpoint can't filter on release type, so ask per project.
        with ThreadPoolExecutor(8) as pool:
            found = pool.map(lambda m: (m, modrinth_versions(pack, m["mod_id"], m)), linked)
            for mod, versions in found:
                release = newer_release(versions, mod["version_id"])
                if release:
                    updates[mod["stem"]] = release
        return updates
    by_hash = {m["hash"].lower(): m for m in linked if m["hash_format"] == "sha512" and m["version_id"]}
    if by_hash:
        latest = modrinth_post("/version_files/update", {
            "hashes": list(by_hash), "algorithm": "sha512",
            "loaders": loaders + ["minecraft"], "game_versions": [pack["minecraft"]],
        })
        for file_hash, mod in by_hash.items():
            if file_hash not in latest:
                unknown.append(mod)
            elif latest[file_hash]["id"] != mod["version_id"]:
                updates[mod["stem"]] = latest[file_hash]
    # Linked by hand, or a file Modrinth doesn't know: ask per project.
    unknown += [m for m in linked if m not in by_hash.values()]
    for mod in unknown:
        versions = modrinth_versions(pack, mod["mod_id"], mod)
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
                url=f"https://modrinth.com/{hit.get('project_type') or 'mod'}/{hit['slug']}", restricted=False,
                loaders=[c for c in hit.get("categories", []) if c in LOADERS])


def curseforge_search(query, pack=None, limit=20, kind="mod"):
    params = {"gameId": CF_MINECRAFT, "classId": KINDS[kind]["cf_class"], "searchFilter": query,
              "sortField": 2, "sortOrder": "desc", "pageSize": limit}
    if pack is None:
        return [curseforge_hit(m) for m in curseforge_request("/mods/search", params)["data"]]
    if kind != "mod":
        # Resource packs have no loader on CurseForge.
        found = curseforge_request("/mods/search", dict(params, gameVersion=pack["minecraft"]))
        return [curseforge_hit(m) for m in found["data"]]
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


def search(provider, query, pack=None, limit=20, kind="mod"):
    if provider == "curseforge":
        return curseforge_search(query, pack, limit, kind)
    facets = pack_facets(pack, kind) if pack else []
    return [modrinth_hit(h) for h in modrinth_search(query, facets, limit, kind)]


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


def curseforge_versions(pack, project_id, is_mod=True):
    """A project's files for this pack, newest first, shaped like Modrinth versions."""
    files = {}
    if not is_mod:
        found = curseforge_request(f"/mods/{int(project_id)}/files", {"gameVersion": pack["minecraft"], "pageSize": 50})
        files = {f["id"]: f for f in found["data"]}
    for loader in compatible_loaders(pack["loader"], pack["minecraft"]) if is_mod else []:
        if loader in CF_LOADER_TYPES:
            found = curseforge_request(f"/mods/{int(project_id)}/files", {
                "gameVersion": pack["minecraft"], "modLoaderType": CF_LOADER_TYPES[loader], "pageSize": 50})
            for f in found["data"]:
                files.setdefault(f["id"], f)
    types = {1: "release", 2: "beta", 3: "alpha"}
    return [{"id": f["id"], "version_number": f.get("displayName") or f["fileName"], "filename": f["fileName"],
             "version_type": types.get(f.get("releaseType"), "?"), "date_published": f.get("fileDate", "")}
            for f in sorted(files.values(), key=lambda f: f.get("fileDate", ""), reverse=True)]


def mod_versions(pack, mod):
    """The versions to choose from on a mod's edit page."""
    if mod["provider"] == "modrinth":
        return [{"id": v["id"], "version_number": v["version_number"], "filename": v["files"][0]["filename"] if v["files"] else "",
                 "version_type": v["version_type"], "date_published": v["date_published"]}
                for v in modrinth_versions(pack, mod["mod_id"], mod)]
    if mod["provider"] == "curseforge" and CURSEFORGE_API_KEY:
        return curseforge_versions(pack, mod["cf_id"], is_mod=mod["folder"] == "mods")
    return []


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
        # Resource packs have no loader on CurseForge.
        accepted = loaders if mod["folder"] == "mods" else {None}
        files = [f for f in project.get("latestFilesIndexes", [])
                 if f.get("gameVersion") == pack["minecraft"] and f.get("modLoader") in accepted
                 and (f.get("releaseType") == 1 or not pack.get("stable"))]
        if files:
            newest = max(files, key=lambda f: f["fileId"])
            # File ids only go up, so a lower one is older: never suggest going back.
            if newest["fileId"] > (mod["cf_file_id"] or 0):
                updates[mod["stem"]] = {"id": newest["fileId"], "version_number": newest["filename"],
                                        "name": newest["filename"]}
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
    lines = [
        "[General]",
        "ConfigVersion=1.3",
        "InstanceType=OneSix",
        f"name={ini_string(pack['name'])}",
        "iconKey=default",
        "OverrideCommands=true",
        f"PreLaunchCommand={ini_string(pre_launch)}",
    ]
    if pack["mem_max"]:
        lines += ["OverrideMemory=true", f"MaxMemAlloc={pack['mem_max']}",
                  f"MinMemAlloc={pack['mem_min'] or min(512, pack['mem_max'])}"]
    if pack["jvm_args"]:
        lines += ["OverrideJavaArgs=true", f"JvmArgs={ini_string(pack['jvm_args'])}"]
    instance_cfg = "\n".join(lines + [""])
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
    slug = resolve_slug(slug)
    pack_dir(slug)
    return send_file(prism_zip(read_pack(slug)), mimetype="application/zip",
                     as_attachment=True, download_name=f"{slug}.zip")


@app.route("/packs/<slug>/<path:filename>")
def pack_file(slug, filename):
    return send_from_directory(pack_dir(resolve_slug(slug)), filename, max_age=0)


@app.route("/jars/<sha>/<filename>")
def jar_file(sha, filename):
    if not SHA256_RE.match(sha):
        abort(404)
    # Content-addressed, so it never changes and can be cached for good. Sent as an attachment so a
    # click in the browser saves it under its own name (packwiz-installer doesn't care).
    return send_from_directory(JARS_DIR / sha, filename, max_age=31536000, as_attachment=True)


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


_asset_versions = {}


def asset_url(filename):
    """Static URL with a content hash, so a deploy reaches browsers and Cloudflare at once."""
    if filename not in _asset_versions:
        data = (Path(app.static_folder) / filename).read_bytes()
        _asset_versions[filename] = hashlib.sha256(data).hexdigest()[:10]
    return url_for("static", filename=filename, v=_asset_versions[filename])


app.jinja_env.globals["asset_url"] = asset_url


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
    if adopt_linked_uploads(*packs):
        packs = all_packs()
    shared = {}
    for pack in packs:
        for mod in pack["mods"]:
            if mod["key"]:
                entry = shared.setdefault(mod["key"], {"name": mod["name"], "packs": []})
                entry["packs"].append(pack["name"])
    shared = sorted((dict(e, key=k) for k, e in shared.items() if len(e["packs"]) > 1),
                    key=lambda e: e["name"].lower())
    def count_updates(pack):
        try:
            return len(check_updates(pack)[0])
        except (OSError, ValueError, ActionError):
            return None

    with ThreadPoolExecutor(max(1, len(packs))) as pool:
        update_counts = dict(zip((p["slug"] for p in packs), pool.map(count_updates, packs)))
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
        if slug_taken(slug):
            flash(slug_taken(slug), "error")
            return redirect(url_for("admin_index"))
        path.mkdir()
        try:
            packwiz(path, "init", "--name", name, "--author", PACK_AUTHOR, "--version", "1.0.0",
                    "--mc-version", mc, "--modloader", loader, f"--{loader}-latest")
        except ActionError as e:
            shutil.rmtree(path, ignore_errors=True)
            flash(str(e), "error")
            return redirect(url_for("admin_index"))
        save_settings(slug, dict(load_settings(slug), mirror=bool(request.form.get("mirror"))))
    flash(f"Pack '{name}' created.", "ok")
    return back_to_pack(slug)


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
        if slug_taken(new_slug):
            flash(slug_taken(new_slug), "error")
            return redirect(url_for("admin_index"))
        shutil.copytree(src, dst)
        toml = (dst / "pack.toml").read_text()
        toml = re.sub(r'^name = ".*"$', f"name = {json.dumps(name)}", toml, count=1, flags=re.M)
        (dst / "pack.toml").write_text(toml)
        save_settings(new_slug, load_settings(slug))
    flash(f"Copied to '{name}'.", "ok")
    return redirect(url_for("admin_pack", slug=new_slug))


@admin_route("/admin/pack/<slug>/rename", methods=["POST"])
def admin_rename(slug):
    """Rename a pack: the name only, or also its URL (keeping the old URL working, or not)."""
    path = pack_dir(slug)
    name = request.form.get("name", "").strip()[:100]
    if not name:
        flash("Give the pack a name.", "error")
        return back_to_pack(slug)
    url_mode = request.form.get("url", "keep")    # keep | alias (old URL keeps working) | move (old URL stops)
    if url_mode not in ("keep", "alias", "move"):
        abort(400)
    new_slug = slug if url_mode == "keep" else slugify(name)
    if not SLUG_RE.match(new_slug):
        flash("That name doesn't make a usable URL; use letters or digits.", "error")
        return back_to_pack(slug)
    with write_lock():
        own_old_url = load_aliases().get(new_slug) is not None and resolve_slug(new_slug) == slug
        if new_slug != slug and slug_taken(new_slug) and not own_old_url:
            flash(slug_taken(new_slug), "error")
            return back_to_pack(slug)
        toml = (path / "pack.toml").read_text()
        toml = re.sub(r'^name = ".*"$', f"name = {json.dumps(name)}", toml, count=1, flags=re.M)
        (path / "pack.toml").write_text(toml)
        if new_slug != slug:
            path.rename(PACKS_DIR / new_slug)
            settings = SETTINGS_DIR / f"{slug}.json"
            if settings.exists():
                settings.rename(SETTINGS_DIR / f"{new_slug}.json")
            aliases = load_aliases()
            if url_mode == "alias":
                # The old URL (and any older ones) now lead here.
                aliases = {old: (new_slug if new == slug else new) for old, new in aliases.items()}
                aliases[slug] = new_slug
            else:
                # A real rename: none of this pack's old URLs stay.
                aliases = {old: new for old, new in aliases.items() if new != slug}
            aliases.pop(new_slug, None)       # moving back to one of its own old URLs
            save_aliases(aliases)
    if new_slug != slug and url_mode == "alias":
        flash(f"Renamed to '{name}', new URL /packs/{new_slug}/. The old URL /packs/{slug}/ keeps working for "
              "servers and players that already use it.", "ok")
    elif new_slug != slug:
        flash(f"Renamed to '{name}', new URL /packs/{new_slug}/. The old URL /packs/{slug}/ no longer works: "
              "update servers (Pack URL) and have players re-import the Prism zip.", "ok")
    else:
        flash(f"Renamed to '{name}'. The URL stays /packs/{slug}/.", "ok")
    return redirect(url_for("admin_pack", slug=new_slug, tab="settings"))


@admin_route("/admin/pack/<slug>/delete", methods=["POST"])
def admin_delete(slug):
    path = pack_dir(slug)
    name = read_pack(slug)["name"]
    with write_lock():
        shutil.rmtree(path)
        (SETTINGS_DIR / f"{slug}.json").unlink(missing_ok=True)
        save_aliases({old: new for old, new in load_aliases().items() if new != slug})
        collect_garbage()
    flash(f"Pack '{name}' deleted.", "ok")
    return redirect(url_for("admin_index"))


@admin_route("/admin/pack/<slug>")
def admin_pack(slug):
    if resolve_slug(slug) != slug:
        return redirect(url_for("admin_pack", slug=resolve_slug(slug), **request.args))   # old bookmark
    pack_dir(slug)
    pack = read_pack(slug)
    if adopt_linked_uploads(pack):
        pack = read_pack(slug)
    if load_settings(slug)["known_packs"] is None:
        # Remember the resource packs already there, so only later ones get switched on.
        with write_lock():
            switch_on_new_resource_packs(slug)
    query = request.args.get("q", "").strip()
    provider = request.args.get("source", "modrinth")
    if provider != "curseforge" or not CURSEFORGE_API_KEY:
        provider = "modrinth"
    tab = request.args.get("tab", "mods")
    if tab not in TABS:
        tab = "mods"
    kind = TABS[tab] or "mod"
    results, others, search_error = [], [], None
    if query:
        try:
            results = search(provider, query, pack, kind=kind)
            # Close matches that have no build for this pack, so they don't just vanish.
            found = {hit["key"] for hit in results}
            others = [hit for hit in search(provider, query, limit=10, kind=kind) if hit["key"] not in found][:5]
        except (OSError, ValueError, ActionError) as e:
            search_error = f"{PROVIDER_NAMES[provider]} search failed: {e}"
    try:
        (updates, restricted), update_error = check_updates(pack), None
    except (OSError, ValueError, ActionError) as e:
        updates, restricted, update_error = {}, set(), f"Checking for updates failed: {e}"
    installed = {m["key"] for m in pack["mods"] if m["key"]}
    siblings = [p for p in all_packs() if p["loader"] == pack["loader"] and p["slug"] != slug]
    old_urls = sorted(old for old, new in load_aliases().items() if new == slug)
    return render_template("admin_pack.html", pack=pack, query=query, results=results, kind=kind, kinds=KINDS, tab=tab,
                           old_urls=old_urls,
                           folder_labels=FOLDER_LABELS,
                           others=others, search_error=search_error, installed=installed,
                           siblings=siblings, sides=SIDES, pack_url=pack_url(slug),
                           max_upload_mb=MAX_UPLOAD_MB, chunk_mb=UPLOAD_CHUNK_MB, updates=updates,
                           update_error=update_error, restricted=restricted, provider=provider,
                           cf_enabled=bool(CURSEFORGE_API_KEY), provider_names=PROVIDER_NAMES,
                           loaders=LOADERS, mc_versions=minecraft_versions())


def resolve_versions(mc, loader, loader_version):
    """The [versions] packwiz would write for this combination, checked by packwiz.

    Runs packwiz init in a scratch folder, so packwiz validates the Minecraft
    and loader version (and looks up "latest") exactly as it does for new packs.
    """
    version_flag = [f"--{loader}-latest"] if loader_version in ("", "latest") else [
        f"--{loader}-version", loader_version]
    with tempfile.TemporaryDirectory() as scratch:
        output = packwiz(Path(scratch), "init", "--name", "check", "--author", "check",
                         "--version", "1", "--mc-version", mc, "--modloader", loader, *version_flag)
        try:
            with open(Path(scratch) / "pack.toml", "rb") as f:
                return tomllib.load(f)["versions"]
        except (FileNotFoundError, KeyError):
            # packwiz exits 0 on these; its last line says what was wrong.
            raise ActionError(output.splitlines()[-1] if output else "packwiz couldn't create that combination.")


@admin_route("/admin/pack/<slug>/versions", methods=["POST"])
def admin_versions(slug):
    path = pack_dir(slug)
    mc = request.form.get("minecraft", "").strip()
    loader = request.form.get("loader", "")
    loader_version = request.form.get("loader_version", "").strip()
    if (not re.fullmatch(r"[0-9][0-9a-z.\-]*", mc) or loader not in LOADERS
            or not re.fullmatch(r"([0-9A-Za-z][0-9A-Za-z.+\-]*)?", loader_version)):
        flash("Fill in a Minecraft version and a loader; leave the loader version empty for the newest.", "error")
        return back_to_pack(slug)
    old = read_pack(slug)
    with write_lock():
        try:
            versions = resolve_versions(mc, loader, loader_version)
        except ActionError as e:
            flash(f"Not changed: {e}", "error")
            return back_to_pack(slug)
        block = "[versions]\n" + "".join(f"{k} = {json.dumps(v)}\n" for k, v in sorted(versions.items()))
        toml = (path / "pack.toml").read_text()
        toml = re.sub(r"^\[versions\]\n(?:[^\[\n].*\n?)*", block, toml, count=1, flags=re.M)
        (path / "pack.toml").write_text(toml)
        try:
            packwiz(slug, "refresh")
        except ActionError as e:
            flash(str(e), "error")
    new = read_pack(slug)
    flash(f"{new['name']} is now {new['loader_name']} {new['loader_version']} on Minecraft {new['minecraft']} "
          f"(was {old['loader_name']} {old['loader_version']} on {old['minecraft']}).\n"
          "Players' Prism instances follow automatically: on their next start packwiz-installer offers to "
          "update the loader (Update), then they start once more. Reinstall the server with the same loader; "
          "packwiz-installer only does this for Prism/MultiMC instances.", "ok")
    if new["minecraft"] != old["minecraft"]:
        flash("The Minecraft version changed: mods stay on their current files until you update them. "
              "Check the update column, and remove mods that have no build for the new version.", "error")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/hosting", methods=["POST"])
def admin_hosting(slug):
    pack_dir(slug)
    mirror = request.form.get("mirror") == "1"
    with write_lock():
        save_settings(slug, dict(load_settings(slug), mirror=mirror))
        errors = after_change(slug)
    for e in errors:
        flash(e, "error")
    if mirror:
        flash("Modrinth and CurseForge jars are now hosted here (where the author allows it).", "ok")
    else:
        flash("Mods download from Modrinth/CurseForge again. Uploaded jars stay hosted here.", "ok")
    return back_to_pack(slug)


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
                before, versions_before = {m["path"] for m in pack["mods"]}, file_versions(pack["slug"])
                packwiz(pack["slug"], *command)
                drop_duplicate_deps(pack["slug"], before, keep_key=key)
                for new in read_mods(pack["slug"]):
                    if new["path"] not in before and new["folder"] != "mods":
                        set_side(new["path"], "client")    # resource packs: servers don't need them
                packwiz(pack["slug"], "refresh")
                changes += describe_changes(pack["name"], versions_before, file_versions(pack["slug"]))
            except ActionError as e:
                errors.append(f"{pack['name']}: {e}")
        errors += after_change(*(p["slug"] for p in targets))
    flash_results(changes, errors, "Already in the pack.")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/add-link", methods=["POST"])
def admin_add_link(slug):
    """Add a CurseForge mod from its page link or slug; works without an API key."""
    pack_dir(slug)
    link = request.form.get("link", "").strip()
    if not (CF_URL_RE.match(link) or re.fullmatch(r"[a-z0-9][a-z0-9-]*", link)):
        flash("Paste a CurseForge mod link (https://www.curseforge.com/minecraft/mc-mods/...) or its slug.", "error")
        return back_to_pack(slug)
    changes, errors = [], []
    with write_lock():
        try:
            changes = run_and_report(read_pack(slug), "curseforge", "add", link)
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors, f"No CurseForge mod found for '{link}' (or it's already in the pack).")
    return back_to_pack(slug)


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


def process_jar(targets, original_name, path, side):
    """Store one uploaded jar and add it to the target packs; returns messages.

    Call under write_lock. Doesn't refresh: finish_upload does that once.
    """
    filename = safe_filename(original_name)
    if filename.lower().endswith(".jar"):
        jar_id, name = jar_metadata(path, filename)
        folder = "mods"
    elif filename.lower().endswith(".zip"):
        if not is_resource_pack(path):
            raise ActionError(f"{original_name}: not a resource pack (no pack.mcmeta in it).")
        jar_id, name, folder, side = None, filename[:-4], "resourcepacks", "client"
    else:
        raise ActionError(f"{original_name}: upload .jar mods or .zip resource packs.")
    sha256, sha512 = hash_file(path)
    matches = find_file_matches(path, sha512)
    store_file(path, filename, sha256)
    return [write_upload(t, filename, sha256, sha512, jar_id, name, side, matches, folder) for t in targets]


def is_resource_pack(path):
    try:
        return "pack.mcmeta" in zipfile.ZipFile(path).namelist()
    except zipfile.BadZipFile:
        return False


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
        return back_to_pack(slug)
    changes, errors = [], []
    with write_lock():
        for upload in files:
            path = UPLOADS_DIR / f"{secrets.token_hex(16)}.part"
            upload.save(path)
            try:
                changes += process_jar(targets, upload.filename, path, side)
            except ActionError as e:
                errors.append(e)
            finally:
                path.unlink(missing_ok=True)
        errors += finish_upload(targets)
    flash_results(changes, errors, "Nothing uploaded.")
    return back_to_pack(slug)


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
            messages = process_jar(targets, request.form.get("filename", ""), part, side)
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


TABS = {"mods": "mod", "resourcepacks": "resourcepack", "settings": None}


def tab_of(mod):
    return "resourcepacks" if mod["folder"] == "resourcepacks" else "mods"


def back_to_pack(slug, tab=None):
    """Back to the pack page, on the tab (and search) the form was sent from."""
    base = url_for("admin_pack", slug=slug)
    referrer = request.referrer or ""
    if tab is None and urllib.parse.urlparse(referrer).path == base:
        return redirect(referrer)
    return redirect(url_for("admin_pack", slug=slug, tab=tab) if tab else base)


def find_mod(slug, stem):
    mod = next((m for m in read_mods(slug) if m["stem"] == stem), None)
    if not mod:
        abort(404)
    return mod


@admin_route("/admin/pack/<slug>/link/<stem>")
def admin_link(slug, stem):
    pack_dir(slug)
    pack, mod = read_pack(slug), find_mod(slug, stem)
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
    try:
        versions, versions_error = mod_versions(pack, mod), None
    except (OSError, ValueError, ActionError) as e:
        versions, versions_error = [], f"Couldn't load the versions: {e}"
    return render_template("admin_link.html", pack=pack, mod=mod, query=query, results=results,
                           current=current, search_error=search_error, provider=provider,
                           cf_enabled=bool(CURSEFORGE_API_KEY), provider_names=PROVIDER_NAMES,
                           versions=versions, versions_error=versions_error,
                           current_version=mod["version_id"] or mod["cf_file_id"])


@admin_route("/admin/pack/<slug>/version/<stem>", methods=["POST"])
def admin_pick_version(slug, stem):
    """Put a mod on the version picked on its edit page (older, newer or a beta)."""
    pack_dir(slug)
    mod = find_mod(slug, stem)
    version_id = request.form.get("version_id", "")
    if not mod["provider"] or not re.fullmatch(r"[A-Za-z0-9]{1,16}", version_id):
        abort(400)
    changes, errors = [], []
    with write_lock():
        pack = read_pack(slug)
        before = file_versions(slug)
        try:
            if version_id != str(mod["version_id"] or mod["cf_file_id"]):
                install_version(slug, mod, {"id": version_id})
            new = next((m for m in read_mods(slug) if m["key"] == mod["key"]), None)
            if new:
                packwiz(slug, "pin", new["stem"])
            packwiz(slug, "refresh")
        except ActionError as e:
            errors.append(e)
        changes = describe_changes(pack["name"], before, file_versions(slug))
        if not errors:
            changes.append(f"{mod['name']} is pinned to this version: updates skip it until you unpin it.")
        errors += after_change(slug)
    flash_results(changes, errors, f"{mod['name']} was already on that version.")
    return back_to_pack(slug, tab_of(mod))


@admin_route("/admin/pack/<slug>/unpin", methods=["POST"])
def admin_unpin(slug):
    pack_dir(slug)
    mod = find_mod(slug, request.form.get("stem", ""))
    with write_lock():
        try:
            packwiz(slug, "unpin", mod["stem"])
            packwiz(slug, "refresh")
            flash(f"{mod['name']} is unpinned: it gets updates again.", "ok")
        except ActionError as e:
            flash(str(e), "error")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/servers", methods=["POST"])
def admin_servers(slug):
    pack_dir(slug)
    servers = []
    for i in range(int(request.form.get("rows", "0") or 0)):
        name = request.form.get(f"name_{i}", "").strip()[:64]
        address = request.form.get(f"address_{i}", "").strip()
        if not address or request.form.get(f"remove_{i}"):
            continue
        if not re.fullmatch(r"[A-Za-z0-9.\-_:\[\]]{1,255}", address):
            flash(f"'{address}' isn't a server address (host or host:port).", "error")
            return back_to_pack(slug)
        servers.append({"name": name or address, "address": address,
                        "accept_textures": bool(request.form.get(f"textures_{i}"))})
    with write_lock():
        save_settings(slug, dict(load_settings(slug), servers=servers,
                                 servers_once=request.form.get("once") == "1"))
        try:
            write_server_list(slug)
        except ActionError as e:
            flash(str(e), "error")
    flash(f"Server list saved ({len(servers)} server{'s' if len(servers) != 1 else ''}); "
          "players get it on their next start." if servers else "Server list removed from the pack.", "ok")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/defaults", methods=["POST"])
def admin_defaults(slug):
    pack_dir(slug)
    settings = load_settings(slug)
    packs = {m["stem"] for m in read_mods(slug) if m["folder"] == "resourcepacks"}
    if request.form.get("section") == "packs":
        chosen, extra = [stem for stem in request.form.getlist("pack") if stem in packs], settings["options_extra"]
    else:
        chosen, extra = settings["default_packs"], request.form.get("extra", "")[:4000]
    bad = [l for l in extra.splitlines() if l.strip() and not OPTION_LINE_RE.match(l.strip())]
    if bad:
        flash(f"Not an option line (expected key:value): {bad[0]}", "error")
        return back_to_pack(slug)
    with write_lock():
        save_settings(slug, dict(settings, default_packs=chosen, options_extra=extra))
        try:
            sync_options(slug)
        except ActionError as e:
            flash(str(e), "error")
    flash("Default settings saved. They apply to new installs (players without an options.txt yet).", "ok")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/memory", methods=["POST"])
def admin_memory(slug):
    """Java memory and arguments for the Prism instance in the pack's zip."""
    pack_dir(slug)
    try:
        mem_min = int(request.form.get("mem_min") or 0)
        mem_max = int(request.form.get("mem_max") or 0)
    except ValueError:
        flash("Memory must be a number of MB, e.g. 8192.", "error")
        return back_to_pack(slug)
    jvm_args = " ".join(request.form.get("jvm_args", "").split())[:1000]
    if mem_max and not 512 <= mem_max <= 262144 or mem_min < 0 or (mem_max and mem_min > mem_max):
        flash("Pick a maximum between 512 and 262144 MB, and a minimum no higher than the maximum.", "error")
        return back_to_pack(slug)
    with write_lock():
        save_settings(slug, dict(load_settings(slug), mem_min=mem_min if mem_max else 0, mem_max=mem_max,
                                 jvm_args=jvm_args))
    flash(f"Java settings saved ({f'{mem_max} MB' if mem_max else 'Prism default memory'}). "
          "They're in the Prism zip, so they apply to new imports.", "ok")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/stable", methods=["POST"])
def admin_stable(slug):
    pack_dir(slug)
    stable = request.form.get("stable") == "1"
    with write_lock():
        save_settings(slug, dict(load_settings(slug), stable=stable))
    flash("Updates now only offer stable releases." if stable else
          "Updates now offer the newest version, betas included.", "ok")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/link/<stem>", methods=["POST"])
def admin_link_save(slug, stem):
    pack_dir(slug)
    mod = find_mod(slug, stem)
    provider = request.form.get("provider", "")
    project_id = request.form.get("project_id", "")
    if project_id and not ((provider == "modrinth" and re.fullmatch(r"[A-Za-z0-9]{8}", project_id)) or
                           (provider == "curseforge" and re.fullmatch(r"[0-9]{1,10}", project_id))):
        abort(400)
    if project_id and mod["source"] != "uploaded":
        return relink_mod(slug, mod, provider, project_id)
    with write_lock():
        try:
            # Unlinking a Modrinth/CurseForge mod keeps its exact file as our own jar.
            path = mod_jar_path(mod)
        except ActionError as e:
            flash(str(e), "error")
            return back_to_pack(slug, tab_of(mod))
        sha256, sha512 = hash_file(path)
        link = None
        if project_id:
            # Keep the exact version when this jar is a known file of that project.
            wanted = project_key(provider, project_id)
            link = next((m for m in find_file_matches(path, sha512) if project_key(*m[:2]) == wanted),
                        (provider, project_id, ""))
        store_file(path, mod["filename"], sha256)
        write_upload_meta(mod["path"], mod["name"], mod["filename"], mod["side"], sha256, sha512, link)
        try:
            packwiz(slug, "refresh")
        except ActionError as e:
            flash(str(e), "error")
        errors = after_change(slug)
    for e in errors:
        flash(e, "error")
    if project_id:
        flash(f"{mod['name']} is linked to {PROVIDER_NAMES[provider]}. Updates show up on the pack page.", "ok")
    elif mod["source"] != "uploaded":
        flash(f"{mod['name']} is unlinked: {mod['filename']} is now hosted here and won't be updated.", "ok")
    else:
        flash(f"{mod['name']} is no longer linked.", "ok")
    return back_to_pack(slug, tab_of(mod))


def relink_mod(slug, mod, provider, project_id):
    """Take a Modrinth/CurseForge mod from another project instead, keeping its side."""
    wanted = project_key(provider, project_id)
    if wanted == mod["key"]:
        flash(f"{mod['name']} is already linked to that project.", "ok")
        return back_to_pack(slug, tab_of(mod))
    command = (["modrinth", "add", "--project-id", project_id] if provider == "modrinth"
               else ["curseforge", "add", "--addon-id", project_id])
    pack = read_pack(slug)
    changes, errors = [], []
    with write_lock():
        before = file_versions(slug)
        paths_before = {m["path"] for m in read_mods(slug)}
        try:
            packwiz(slug, *command)
            drop_duplicate_deps(slug, paths_before, keep_key=wanted)
            new = next((m for m in read_mods(slug) if m["key"] == wanted), None)
            if new and new["stem"] != mod["stem"]:
                packwiz(slug, "remove", mod["stem"])
            if new:
                set_side(new["path"], mod["side"])
            packwiz(slug, "refresh")
            changes = describe_changes(pack["name"], before, file_versions(slug))
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors, f"{mod['name']} now comes from {PROVIDER_NAMES[provider]}.")
    if changes:
        flash(f"{mod['name']} now comes from {PROVIDER_NAMES[provider]}.", "ok")
    return back_to_pack(slug, tab_of(mod))


def nbt_string(text):
    raw = text.encode("utf-8")
    return struct.pack(">H", len(raw)) + raw


def servers_dat(servers):
    """servers.dat as Minecraft writes it: uncompressed NBT {servers: [{name, ip, acceptTextures}]}."""
    out = b"\x0a" + nbt_string("")                                    # root compound
    out += b"\x09" + nbt_string("servers") + b"\x0a" + struct.pack(">i", len(servers))
    for server in servers:
        out += b"\x08" + nbt_string("name") + nbt_string(server["name"])
        out += b"\x08" + nbt_string("ip") + nbt_string(server["address"])
        if server.get("accept_textures"):
            out += b"\x01" + nbt_string("acceptTextures") + b"\x01"
        out += b"\x00"                                                  # end of this server
    return out + b"\x00"                                                # end of root


def write_server_list(slug):
    """Put the pack's server list in servers.dat (or remove it); call under write_lock.

    "Install once" sets packwiz's preserve flag: players' own servers survive,
    but later changes to the list only reach new installs.
    """
    settings = load_settings(slug)
    data = servers_dat(settings["servers"]) if settings["servers"] else None
    write_pack_file(slug, "servers.dat", data, preserve=settings["servers_once"])


def write_pack_file(slug, name, data, preserve=False):
    """Put a file in the pack root (or remove it when data is None); call under write_lock.

    preserve is packwiz's "install once": packwiz-installer only writes the
    file when the player doesn't have it yet.
    """
    target = PACKS_DIR / slug / name
    if data is None:
        target.unlink(missing_ok=True)
    else:
        target.write_bytes(data)
    packwiz(slug, "refresh")
    index = PACKS_DIR / slug / "index.toml"
    line = f"file = {json.dumps(name)}\n"

    def set_preserve(entry):
        # packwiz may put the flag anywhere in the entry, so drop it everywhere first.
        entry = re.sub(r"^preserve = .*\n", "", entry.group(0), flags=re.M)
        return entry.replace(line, line + "preserve = true\n") if preserve else entry

    toml = re.sub(r"\[\[files\]\]\n" + re.escape(line) + r"(?:(?!\[\[files\]\]).*\n?)*", set_preserve,
                  index.read_text())
    index.write_text(toml)
    packwiz(slug, "refresh")    # pack.toml carries the index hash


OPTION_LINE_RE = re.compile(r"^[A-Za-z0-9_.]+:.*$")


def options_txt(slug):
    """The default options.txt for new installs, or None if there's nothing to set.

    Minecraft fills in every option that's missing, so this only holds what the
    admin chose: extra lines plus the resource packs that start switched on.
    """
    settings = load_settings(slug)
    lines = [l.strip() for l in settings["options_extra"].splitlines()
             if OPTION_LINE_RE.match(l.strip()) and not l.strip().startswith(("resourcePacks:", "incompatibleResourcePacks:"))]
    packs = {m["stem"]: m for m in read_mods(slug) if m["folder"] == "resourcepacks"}
    chosen = [packs[stem] for stem in settings["default_packs"] if stem in packs]
    if chosen:
        # Later entries sit higher in Minecraft's list, so the first one chosen ends up on top.
        enabled = ["vanilla"] + [f"file/{m['filename']}" for m in reversed(chosen)]
        lines.append("resourcePacks:" + json.dumps(enabled, separators=(",", ":")))
    return ("\n".join(lines) + "\n").encode() if lines else None


def switch_on_new_resource_packs(slug):
    """Tick newly added resource packs for new players; they can be unticked after.

    A pack whose file got a new name (same project) keeps its old tick. The very
    first time a pack is looked at, its existing packs are only remembered.
    """
    settings = load_settings(slug)
    current = {m["stem"]: m["key"] for m in read_mods(slug) if m["folder"] == "resourcepacks"}
    known = settings["known_packs"]
    enabled = list(settings["default_packs"])
    if known is not None:
        gone = {stem: key for stem, key in known.items() if stem not in current}
        for stem, key in current.items():
            if stem in known:
                continue
            renamed = next((old for old, old_key in gone.items() if key and old_key == key), None)
            if renamed is None or renamed in enabled:
                enabled.append(stem)
        enabled = [stem for stem in enabled if stem in current]
    if known != current or enabled != settings["default_packs"]:
        save_settings(slug, dict(settings, known_packs=current, default_packs=enabled))


def sync_options(slug):
    """Keep options.txt in line with the settings and the current resource pack files."""
    switch_on_new_resource_packs(slug)
    wanted = options_txt(slug)
    target = PACKS_DIR / slug / "options.txt"
    current = target.read_bytes() if target.exists() else None
    if wanted != current:
        write_pack_file(slug, "options.txt", wanted, preserve=True)


def drop_duplicate_deps(slug, before, keep_key=None):
    """Remove projects that packwiz add just put in a second time.

    packwiz spots existing dependencies by file name, so a dependency already
    in the pack under another name (say, one named after an uploaded jar) gets
    added again. Those new copies go; keep_key is the project being added.
    """
    mods = read_mods(slug)
    old_keys = {m["key"] for m in mods if m["path"] in before and m["key"]}
    for mod in mods:
        if mod["path"] not in before and mod["key"] in old_keys and mod["key"] != keep_key:
            mod["path"].unlink()


def install_version(slug, mod, update):
    """Put a mod on exactly the version the update check found, keeping its side.

    packwiz update picks its own "latest", which can disagree with Modrinth (it
    prefers an older file that lists more loaders, for one), so we name the
    exact version instead. packwiz add writes to its own file name, so the copy
    under the old name is removed. Call under write_lock; refresh afterwards.
    """
    if mod["provider"] == "modrinth":
        command = ["modrinth", "add", "--project-id", mod["mod_id"], "--version-id", str(update["id"])]
    else:
        command = ["curseforge", "add", "--addon-id", str(mod["cf_id"]), "--file-id", str(update["id"])]
    before = {m["path"] for m in read_mods(slug)}
    packwiz(slug, *command)
    mods = read_mods(slug)
    new = next((m for m in mods if m["key"] == mod["key"] and m["path"] not in before), None)
    new = new or next((m for m in mods if m["path"] == mod["path"]), None)
    if new and new["path"] != mod["path"]:
        mod["path"].unlink(missing_ok=True)
    if new:
        set_side(new["path"], mod["side"])
    drop_duplicate_deps(slug, before, keep_key=mod["key"])


def apply_updates(pack, only=None):
    """Update the pack's mods (or just the stems in only) to what the check shows.

    Returns (changes, errors). Call under write_lock. Falls back to packwiz's
    own update if the check can't reach Modrinth/CurseForge.
    """
    slug = pack["slug"]
    try:
        updates, _ = check_updates(pack, fresh=True)
    except (OSError, ValueError, ActionError):
        args = ["update", *only] if only else ["update", "--all"]
        return run_and_report(pack, *args), []
    mods = {m["stem"]: m for m in pack["mods"]}
    before, errors = file_versions(slug), []
    for stem, update in updates.items():
        if only is not None and stem not in only:
            continue
        try:
            install_version(slug, mods[stem], update)
        except ActionError as e:
            errors.append(f"{pack['name']}: {mods[stem]['name']}: {e}")
    packwiz(slug, "refresh")
    return describe_changes(pack["name"], before, file_versions(slug)), errors


@admin_route("/admin/pack/<slug>/update-one", methods=["POST"])
def admin_update_one(slug):
    pack_dir(slug)
    stem = request.form.get("stem", "")
    if stem not in {m["stem"] for m in read_mods(slug)}:
        abort(400)
    changes, errors = [], []
    with write_lock():
        try:
            changes, errors = apply_updates(read_pack(slug), only={stem})
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors)
    return back_to_pack(slug)


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
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/remove-all", methods=["POST"])
def admin_remove_all(slug):
    """Remove every mod, or every resource pack, from a pack."""
    pack_dir(slug)
    folder = request.form.get("folder", "")
    if folder not in FOLDER_LABELS:
        abort(400)
    with write_lock():
        gone = [m for m in read_mods(slug) if m["folder"] == folder]
        for mod in gone:
            mod["path"].unlink(missing_ok=True)
        try:
            packwiz(slug, "refresh")
        except ActionError as e:
            flash(str(e), "error")
        for e in after_change(slug):     # cleans up stored files and options.txt
            flash(e, "error")
    flash(f"Removed all {FOLDER_LABELS[folder].lower()} ({len(gone)}).", "ok")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/side", methods=["POST"])
def admin_side(slug):
    pack_dir(slug)
    stem, side = request.form.get("stem", ""), request.form.get("side", "")
    mods = {m["stem"]: m for m in read_mods(slug)}
    if stem not in mods or side not in SIDES:
        abort(400)
    with write_lock():
        set_side(mods[stem]["path"], side)
        try:
            packwiz(slug, "refresh")
            flash(f"{mods[stem]['name']} is now '{side}'.", "ok")
        except ActionError as e:
            flash(str(e), "error")
    return back_to_pack(slug)


@admin_route("/admin/pack/<slug>/update", methods=["POST"])
def admin_update_pack(slug):
    pack_dir(slug)
    changes, errors = [], []
    with write_lock():
        try:
            changes, errors = apply_updates(read_pack(slug))
        except ActionError as e:
            errors.append(e)
        errors += after_change(slug)
    flash_results(changes, errors)
    return back_to_pack(slug)


@admin_route("/admin/update-all", methods=["POST"])
def admin_update_all():
    changes, errors = [], []
    with write_lock():
        packs = all_packs()
        for pack in packs:
            try:
                pack_changes, pack_errors = apply_updates(pack)
                changes, errors = changes + pack_changes, errors + pack_errors
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
                        pack_changes, pack_errors = apply_updates(pack, only={mod["stem"]})
                        changes, errors = changes + pack_changes, errors + pack_errors
                    except ActionError as e:
                        errors.append(f"{pack['name']}: {e}")
        errors += after_change(*touched)
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))
