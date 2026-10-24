"""packwiz-web - a small web UI around the packwiz CLI, plus the pack host.

Public (no login): the pack files under /packs/<slug>/ that packwiz-installer
reads, a Prism instance zip per pack, and an overview page for players.
Admin (/admin, behind Cloudflare Access): create/copy/delete packs, add mods
from Modrinth, change sides, remove and update mods, across one or all packs.

Every change goes through the packwiz binary, so the pack folders stay plain
packwiz packs. All changes run under one global file lock.
"""

import fcntl
import io
import json
import os
import re
import secrets
import shutil
import subprocess
import tomllib
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
BOOTSTRAP_JAR = BASE_DIR / "packwiz-installer-bootstrap.jar"

PUBLIC_URL = os.environ.get("PUBLIC_URL", "").rstrip("/")
PACK_AUTHOR = os.environ.get("PACK_AUTHOR", "packwiz-web")
# e.g. "wenro.cloudflareaccess.com". When set, /admin requires a valid Access JWT.
ACCESS_TEAM_DOMAIN = os.environ.get("ACCESS_TEAM_DOMAIN", "").strip().rstrip("/")
ACCESS_TEAM_DOMAIN = re.sub(r"^https?://", "", ACCESS_TEAM_DOMAIN)

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
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

PACKS_DIR.mkdir(parents=True, exist_ok=True)


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
        "mods": read_mods(slug),
    }


def read_mods(slug):
    mods = []
    root = PACKS_DIR / slug
    for meta_file in sorted(root.rglob("*.pw.toml")):
        with open(meta_file, "rb") as f:
            meta = tomllib.load(f)
        rel = meta_file.relative_to(root)
        mods.append({
            "stem": meta_file.name[:-len(".pw.toml")],
            "folder": str(rel.parent),
            "name": meta.get("name", meta_file.stem),
            "filename": meta.get("filename", ""),
            "side": meta.get("side", "both"),
            "mod_id": meta.get("update", {}).get("modrinth", {}).get("mod-id"),
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


def public_base():
    return PUBLIC_URL or request.url_root.rstrip("/")


def pack_url(slug):
    return f"{public_base()}/packs/{slug}/pack.toml"


# --- Running packwiz ----------------------------------------------------------

class PackwizError(Exception):
    pass


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
        raise PackwizError(f"packwiz {' '.join(args)} timed out")
    output = ANSI_RE.sub("", result.stdout + result.stderr).strip()
    if result.returncode != 0:
        tail = "\n".join(output.splitlines()[-8:])
        raise PackwizError(f"packwiz {' '.join(args)} failed:\n{tail}")
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
    if changes:
        flash("\n".join(changes), "ok")
    elif not errors:
        flash(nothing, "ok")


# --- Modrinth -----------------------------------------------------------------

def modrinth_get(path, params=None):
    url = f"{MODRINTH_API}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.load(resp)


def modrinth_search(query, loader, mc_version):
    facets = [["project_type:mod"], [f"versions:{mc_version}"]]
    if loader:
        facets.append([f"categories:{loader}"])
    data = modrinth_get("/search", {
        "query": query, "facets": json.dumps(facets), "limit": 20, "index": "relevance",
    })
    return data.get("hits", [])


_mc_versions_cache = []


def minecraft_versions():
    if not _mc_versions_cache:
        try:
            tags = modrinth_get("/tag/game_version")
            _mc_versions_cache.extend(t["version"] for t in tags if t.get("version_type") == "release")
        except OSError:
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
                g.user = None
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
    return render_template("admin_index.html", packs=packs, shared=shared, loaders=LOADERS,
                           mc_versions=minecraft_versions(), pack_url=pack_url)


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
        except PackwizError as e:
            shutil.rmtree(path, ignore_errors=True)
            flash(str(e), "error")
            return redirect(url_for("admin_index"))
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
    flash(f"Copied to '{name}'.", "ok")
    return redirect(url_for("admin_pack", slug=new_slug))


@admin_route("/admin/pack/<slug>/delete", methods=["POST"])
def admin_delete(slug):
    path = pack_dir(slug)
    name = read_pack(slug)["name"]
    with write_lock():
        shutil.rmtree(path)
    flash(f"Pack '{name}' deleted.", "ok")
    return redirect(url_for("admin_index"))


@admin_route("/admin/pack/<slug>")
def admin_pack(slug):
    pack_dir(slug)
    pack = read_pack(slug)
    query = request.args.get("q", "").strip()
    results, search_error = [], None
    if query:
        try:
            results = modrinth_search(query, pack["loader"], pack["minecraft"])
        except OSError as e:
            search_error = f"Modrinth search failed: {e}"
    installed = {m["mod_id"] for m in pack["mods"] if m["mod_id"]}
    siblings = [p for p in all_packs() if p["loader"] == pack["loader"] and p["slug"] != slug]
    return render_template("admin_pack.html", pack=pack, query=query, results=results,
                           search_error=search_error, installed=installed, siblings=siblings,
                           sides=SIDES, pack_url=pack_url(slug))


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
            except PackwizError as e:
                errors.append(f"{pack['name']}: {e}")
    flash_results(changes, errors, "Already in the pack.")
    return redirect(url_for("admin_pack", slug=slug, q=request.form.get("q", "")))


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
        except PackwizError as e:
            flash(str(e), "error")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/side", methods=["POST"])
def admin_side(slug):
    root = pack_dir(slug)
    stem, side = request.form.get("stem", ""), request.form.get("side", "")
    mods = {m["stem"]: m for m in read_mods(slug)}
    if stem not in mods or side not in SIDES:
        abort(400)
    meta_file = root / mods[stem]["folder"] / f"{stem}.pw.toml"
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
        except PackwizError as e:
            flash(str(e), "error")
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/pack/<slug>/update", methods=["POST"])
def admin_update_pack(slug):
    pack_dir(slug)
    changes, errors = [], []
    with write_lock():
        try:
            changes = run_and_report(read_pack(slug), "update", "--all")
        except PackwizError as e:
            errors.append(e)
    flash_results(changes, errors)
    return redirect(url_for("admin_pack", slug=slug))


@admin_route("/admin/update-all", methods=["POST"])
def admin_update_all():
    changes, errors = [], []
    with write_lock():
        for pack in all_packs():
            try:
                changes += run_and_report(pack, "update", "--all")
            except PackwizError as e:
                errors.append(f"{pack['name']}: {e}")
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))


@admin_route("/admin/update-mod", methods=["POST"])
def admin_update_mod():
    mod_id = request.form.get("mod_id", "")
    changes, errors = [], []
    with write_lock():
        for pack in all_packs():
            for mod in pack["mods"]:
                if mod["mod_id"] == mod_id:
                    try:
                        changes += run_and_report(pack, "update", mod["stem"])
                    except PackwizError as e:
                        errors.append(f"{pack['name']}: {e}")
    flash_results(changes, errors)
    return redirect(url_for("admin_index"))
