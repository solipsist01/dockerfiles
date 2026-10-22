"""Startpage - Flask/SQLite port of the original PHP startpage.

The HTML output deliberately mirrors the PHP version (no doctype, same tables,
same attributes) so the page renders exactly as before. What changed is
everything underneath: parameterized SQL, auto-escaped output, CSRF tokens on
every form, salted password hashes and server-side permission checks.
"""

import hashlib
import hmac
import os
import re
import secrets
import sqlite3
from pathlib import Path

import click
from flask import Flask, g, render_template, request, session
from werkzeug.security import check_password_hash, generate_password_hash

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = os.environ.get("STARTPAGE_DB", str(BASE_DIR / "data" / "startpage.db"))


def _env_bool(name, default):
    return os.environ.get(name, "1" if default else "0").strip().lower() in ("1", "true", "yes", "on")


# --- Configuration (was config.php) -------------------------------------------
CFG = {
    "title": os.environ.get("STARTPAGE_TITLE", "Startpage"),
    "description": os.environ.get("STARTPAGE_DESCRIPTION", "Description"),
    "keywords": os.environ.get("STARTPAGE_KEYWORDS", "Keywords"),
    "stylesheet": os.environ.get("STARTPAGE_STYLESHEET", "startpage.css"),
    "header": os.environ.get("STARTPAGE_HEADER", ""),
}
DEFAULT_COLUMN_COUNT = int(os.environ.get("STARTPAGE_COLUMNS", "3"))
COMBINE_CATEGORY_CHILDS = _env_bool("STARTPAGE_COMBINE_CHILDS", True)

ROOT_CATEGORY = 1  # the "No Parent" pseudo-category, as in the original schema

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS categories (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT NOT NULL,
    parent INTEGER NOT NULL DEFAULT 1,
    user   INTEGER NOT NULL,
    count  INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS links (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT NOT NULL,
    link     TEXT NOT NULL,
    category INTEGER NOT NULL,
    user     INTEGER NOT NULL,
    count    INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS variables (
    name  TEXT PRIMARY KEY,
    value TEXT
);
CREATE INDEX IF NOT EXISTS idx_categories_user_parent ON categories(user, parent);
CREATE INDEX IF NOT EXISTS idx_links_user_category ON links(user, category);
INSERT OR IGNORE INTO categories (id, name, parent, user, count) VALUES (1, 'root', 1, 0, 0);
INSERT OR IGNORE INTO variables (name, value) VALUES ('version', '2');
INSERT OR IGNORE INTO variables (name, value) VALUES ('default_user', '');
INSERT OR IGNORE INTO variables (name, value) VALUES ('admin_user', '');
"""


def connect(path=None):
    path = path or DB_PATH
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    db.row_factory = sqlite3.Row
    return db


DEFAULT_ADMIN = os.environ.get("STARTPAGE_ADMIN_USER", "admin")
DEFAULT_ADMIN_PASSWORD = os.environ.get("STARTPAGE_ADMIN_PASSWORD", "admin")


def ensure_schema(path=None):
    """Create the tables, and on an empty database a first account (admin/admin
    unless overridden) that is both the admin and the page anonymous visitors see."""
    db = connect(path)
    with db:
        db.executescript(SCHEMA)
        created = db.execute(
            "INSERT INTO users (username, password) SELECT ?, ? "
            "WHERE NOT EXISTS (SELECT 1 FROM users)",
            (DEFAULT_ADMIN, generate_password_hash(DEFAULT_ADMIN_PASSWORD)),
        ).rowcount
        if created:
            db.execute("UPDATE variables SET value = ? WHERE name IN ('admin_user', 'default_user') "
                       "AND (value IS NULL OR value = '')", (DEFAULT_ADMIN,))
    db.close()
    if created:
        print(f"Startpage: created first account '{DEFAULT_ADMIN}'. Change its password!", flush=True)


def _load_secret_key():
    key = os.environ.get("SECRET_KEY")
    if key:
        return key
    key_file = Path(DB_PATH).parent / ".secret_key"
    key_file.parent.mkdir(parents=True, exist_ok=True)
    if not key_file.exists():
        # Write to a temp file, then link it into place atomically, so several
        # workers starting at once all end up with the same key.
        tmp = key_file.with_name(f".secret_key.{os.getpid()}")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(secrets.token_hex(32))
        try:
            os.link(tmp, key_file)
        except FileExistsError:
            pass
        finally:
            tmp.unlink(missing_ok=True)
    return key_file.read_text().strip()


app = Flask(__name__, static_folder="static", static_url_path="")
app.secret_key = _load_secret_key()
app.config.update(
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE="Lax",
    SESSION_COOKIE_SECURE=_env_bool("SESSION_COOKIE_SECURE", False),
)
app.jinja_env.trim_blocks = True
app.jinja_env.lstrip_blocks = True

ensure_schema()


# --- Database helpers -----------------------------------------------------------
def get_db():
    if "db" not in g:
        g.db = connect()
    return g.db


@app.teardown_appcontext
def _close_db(exc):
    db = g.pop("db", None)
    if db is not None:
        db.close()


def q(sql, *args):
    return get_db().execute(sql, args).fetchall()


def q1(sql, *args):
    return get_db().execute(sql, args).fetchone()


def x(sql, *args):
    return get_db().execute(sql, args)


def get_dbvar(name):
    row = q1("SELECT value FROM variables WHERE name = ?", name)
    return row["value"] if row and row["value"] is not None else ""


# --- Request helpers --------------------------------------------------------------
def input_var(name, default=""):
    """POST first, then GET, then default - like get_input_var() in helpers.php."""
    value = request.form.get(name, "")
    if value:
        return value
    value = request.args.get(name, "")
    if value:
        return value
    return default


def to_int(value):
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return None


def csrf_token():
    token = session.get("csrf")
    if not token:
        token = secrets.token_urlsafe(32)
        session["csrf"] = token
    return token


def csrf_ok():
    expected = session.get("csrf", "")
    given = request.form.get("csrf", "")
    return request.method == "POST" and bool(expected) and hmac.compare_digest(expected, given)


app.jinja_env.globals["csrf_token"] = csrf_token


# --- Security helpers ---------------------------------------------------------------
_MD5_RE = re.compile(r"^[0-9a-f]{32}$")


def hash_password(password):
    return generate_password_hash(password)


def verify_password(stored, given):
    """Returns (ok, is_legacy_md5). Legacy MD5 hashes from the PHP version still work
    once and are upgraded on the next successful login."""
    if _MD5_RE.match(stored or ""):
        digest = hashlib.md5(given.encode("utf-8")).hexdigest()
        return hmac.compare_digest(stored, digest), True
    try:
        return check_password_hash(stored, given), False
    except ValueError:
        return False, False


_SAFE_SCHEMES = {"http", "https", "ftp", "ftps", "sftp", "ssh", "mailto", "news",
                 "irc", "ircs", "telnet", "rdp", "vnc", "smb", "file", "tel"}
_SCHEME_RE = re.compile(r"^([a-z][a-z0-9+.\-]*):")


def safe_link(url):
    """Refuse javascript:, data:, vbscript: and other script-capable URLs."""
    cleaned = re.sub(r"[\x00-\x20\x7f]", "", url).lower()
    match = _SCHEME_RE.match(cleaned)
    return match is None or match.group(1) in _SAFE_SCHEMES


def user_category_exists(category_id, user_id):
    return q1("SELECT 1 FROM categories WHERE id = ? AND user = ? AND id != ?",
              category_id, user_id, ROOT_CATEGORY) is not None


def valid_parent(parent_id, user_id):
    return parent_id == ROOT_CATEGORY or user_category_exists(parent_id, user_id)


def would_create_cycle(category_id, new_parent, user_id):
    """True if new_parent is category_id itself or one of its descendants."""
    seen = set()
    current = new_parent
    while current is not None and current != ROOT_CATEGORY and current not in seen:
        if current == category_id:
            return True
        seen.add(current)
        row = q1("SELECT parent FROM categories WHERE id = ? AND user = ?", current, user_id)
        current = row["parent"] if row else None
    return False


# --- Actions (the "submit*" handlers from index.php) ---------------------------------
DATA_ACTIONS = {
    "submitaddcategory", "submiteditcategory", "submitdelcategory",
    "submitaddlink", "submiteditlink", "submitdellink",
    "submitadduser", "submitdeluser", "submitpassworduser",
    "submitchangepassword",
}


def handle_action(action, user_id, admin):
    msgs = []

    if action == "submitaddcategory":
        name = request.form.get("c_name", "")
        parent = to_int(request.form.get("c_parent", ""))
        if not name or parent is None:
            msgs.append("Failure: All values should be entered.")
        elif not valid_parent(parent, user_id):
            msgs.append("Failure: addcategory.")
        else:
            x("INSERT INTO categories (name, parent, user) VALUES (?, ?, ?)", name, parent, user_id)

    elif action == "submiteditcategory":
        cid = to_int(request.form.get("c_id", ""))
        name = request.form.get("c_name", "")
        parent = to_int(request.form.get("c_parent", ""))
        if cid is None or not name or parent is None:
            msgs.append("Failure: All values should be entered.")
        elif cid == parent or would_create_cycle(cid, parent, user_id):
            msgs.append("Failure: Cannot become own parent.")
        elif not valid_parent(parent, user_id):
            msgs.append("Failure: editcategory.")
        else:
            x("UPDATE categories SET name = ?, parent = ? WHERE id = ? AND user = ?",
              name, parent, cid, user_id)

    elif action == "submitdelcategory":
        cid = to_int(request.form.get("c_id", ""))
        parent = to_int(request.form.get("c_parent", ""))
        if cid is None or parent is None:
            msgs.append("Failure: Not all values are given.")
        elif parent == cid or not valid_parent(parent, user_id):
            msgs.append("Failure: delcategory.")
        else:
            x("DELETE FROM links WHERE category = ? AND user = ?", cid, user_id)
            x("DELETE FROM categories WHERE id = ? AND user = ?", cid, user_id)
            x("UPDATE categories SET parent = ? WHERE parent = ? AND user = ?", parent, cid, user_id)

    elif action in ("submitaddlink", "submiteditlink"):
        name = request.form.get("c_name", "")
        link = request.form.get("c_link", "").strip()
        category = to_int(request.form.get("c_category", ""))
        lid = to_int(request.form.get("c_id", ""))
        tag = "addlink" if action == "submitaddlink" else "editlink"
        if not name or not link or category is None or (tag == "editlink" and lid is None):
            msgs.append("Failure: All values should be entered.")
        elif not safe_link(link):
            msgs.append("Failure: This kind of link is not allowed.")
        elif not user_category_exists(category, user_id):
            msgs.append(f"Failure: {tag}.")
        elif tag == "addlink":
            x("INSERT INTO links (name, link, category, user) VALUES (?, ?, ?, ?)",
              name, link, category, user_id)
        else:
            x("UPDATE links SET name = ?, link = ?, category = ? WHERE id = ? AND user = ?",
              name, link, category, lid, user_id)

    elif action == "submitdellink":
        lid = to_int(request.form.get("c_id", ""))
        if lid is None:
            msgs.append("Failure: ID not given.")
        else:
            x("DELETE FROM links WHERE id = ? AND user = ?", lid, user_id)

    elif action == "submitadduser":
        if not admin:
            msgs.append("Failure: No admin rights.")
        else:
            username = request.form.get("c_username", "")
            pw1 = request.form.get("c_password1", "")
            pw2 = request.form.get("c_password2", "")
            if not username or not pw1 or not pw2:
                msgs.append("Failure: All values should be entered.")
            elif pw1 != pw2:
                msgs.append("Failure: Passwords are not equal.")
            elif q1("SELECT 1 FROM users WHERE username = ?", username):
                msgs.append("Failure: adduser.")
            else:
                x("INSERT INTO users (username, password) VALUES (?, ?)", username, hash_password(pw1))

    elif action == "submitdeluser":
        if not admin:
            msgs.append("Failure: No admin rights.")
        else:
            uid = to_int(request.form.get("c_id", ""))
            if uid is None:
                msgs.append("Failure: Not all values are given.")
            elif uid == user_id:
                msgs.append("Failure: You cannot delete your own account.")
            else:
                x("DELETE FROM links WHERE user = ?", uid)
                x("DELETE FROM categories WHERE user = ?", uid)
                x("DELETE FROM users WHERE id = ?", uid)

    elif action == "submitpassworduser":  # admin sets someone's password
        if not admin:
            msgs.append("Failure: No admin rights.")
        else:
            uid = to_int(request.form.get("c_id", ""))
            pw1 = request.form.get("c_password1", "")
            pw2 = request.form.get("c_password2", "")
            if uid is None or not pw1 or not pw2:
                msgs.append("Failure: All values should be entered.")
            elif pw1 != pw2:
                msgs.append("Failure: Passwords are not equal.")
            elif not q1("SELECT 1 FROM users WHERE id = ?", uid):
                msgs.append("Failure: passworduser.")
            else:
                x("UPDATE users SET password = ? WHERE id = ?", hash_password(pw1), uid)
                msgs.append("Password is changed.")

    elif action == "submitchangepassword":  # any user changes their own password
        current = request.form.get("c_password", "")
        pw1 = request.form.get("c_password1", "")
        pw2 = request.form.get("c_password2", "")
        row = q1("SELECT password FROM users WHERE id = ?", user_id)
        if not current or not pw1 or not pw2:
            msgs.append("Failure: All values should be entered.")
        elif pw1 != pw2:
            msgs.append("Failure: Passwords are not equal.")
        elif not row or not verify_password(row["password"], current)[0]:
            msgs.append("Failure: Current password is wrong.")
        else:
            x("UPDATE users SET password = ? WHERE id = ?", hash_password(pw1), user_id)
            msgs.append("Password is changed.")

    get_db().commit()
    return msgs


# --- Page builders ----------------------------------------------------------------
def build_columns(show_id, numcol):
    """Same column-balancing algorithm as startpage-main.php."""
    columns = [[] for _ in range(numcol)]
    counts = [0] * numcol

    def print_category(cat, col, seen):
        if cat["id"] in seen:  # guard against parent loops in old data
            return
        seen = seen | {cat["id"]}
        columns[col].append({"kind": "header", "id": cat["id"], "name": cat["name"]})
        links = q("SELECT id, name, link, count FROM links WHERE category = ? AND user = ? "
                  "ORDER BY count DESC, id", cat["id"], show_id)
        for link in links:
            columns[col].append({"kind": "link", "id": link["id"], "name": link["name"],
                                 "link": link["link"], "category": cat["id"]})
        counts[col] += 1 + len(links)
        if COMBINE_CATEGORY_CHILDS:
            for child in q("SELECT id, name, count FROM categories WHERE parent = ? AND user = ? "
                           "AND id != ? ORDER BY count DESC, id", cat["id"], show_id, ROOT_CATEGORY):
                print_category(child, col, seen)

    if COMBINE_CATEGORY_CHILDS:
        parents = q("SELECT id, name, count FROM categories WHERE id != ? AND parent = ? AND user = ? "
                    "ORDER BY count DESC, id", ROOT_CATEGORY, ROOT_CATEGORY, show_id)
    else:
        parents = q("SELECT id, name, count FROM categories WHERE id != ? AND user = ? "
                    "ORDER BY count DESC, id", ROOT_CATEGORY, show_id)

    for row in parents:
        lowest = counts.index(min(counts))
        print_category(row, lowest, frozenset())
        columns[lowest].append({"kind": "spacer"})
        counts[lowest] += 1

    return columns


def page_main(ctx):
    ctx["columns"] = build_columns(ctx["show_id"], max(1, DEFAULT_COLUMN_COUNT))
    return "main.html"


def page_login(ctx):
    return "login.html"


def page_category(ctx):
    user_id = ctx["user_id"]
    mode, label = "add", "Add"
    c_id, c_name, c_parent = "", "", ""
    req_mode = request.args.get("mode", "")
    category = to_int(request.args.get("category", ""))
    if req_mode in ("del", "edit") and category is not None:
        mode, label = req_mode, ("Delete" if req_mode == "del" else "Edit")
        row = q1("SELECT id, name, parent FROM categories WHERE id = ? AND user = ?", category, user_id)
        if row:
            c_id, c_name, c_parent = row["id"], row["name"], row["parent"]
    options = q("SELECT id, name FROM categories WHERE id != ? AND id != ? AND user = ? ORDER BY id",
                c_id if c_id != "" else -1, ROOT_CATEGORY, user_id)
    ctx.update(mode=mode, label=label, c_id=c_id, c_name=c_name,
               c_parent=str(c_parent), options=options)
    return "category.html"


def page_link(ctx):
    user_id = ctx["user_id"]
    mode, label = "add", "Add"
    c_id, c_name, c_link, c_category = "", "", "", ""
    req_mode = request.args.get("mode", "")
    if req_mode == "add":
        cat = to_int(request.args.get("category", ""))
        if cat is not None:
            c_category = cat
    elif req_mode in ("del", "edit"):
        lid = to_int(request.args.get("link", ""))
        if lid is not None:
            mode, label = req_mode, ("Delete" if req_mode == "del" else "Edit")
            row = q1("SELECT id, name, link, category FROM links WHERE id = ? AND user = ?", lid, user_id)
            if row:
                c_id, c_name, c_link, c_category = row["id"], row["name"], row["link"], row["category"]
    options = q("SELECT id, name FROM categories WHERE id != ? AND user = ? ORDER BY id",
                ROOT_CATEGORY, user_id)
    ctx.update(mode=mode, label=label, c_id=c_id, c_name=c_name, c_link=c_link,
               c_category=str(c_category), options=options)
    return "link.html"


def page_users(ctx):
    if not ctx["admin"]:
        ctx["error"] = "No admin rights."
        return "users.html"
    mode = input_var("mode")
    if not mode:
        ctx["error"] = "No mode selected."
        return "users.html"
    ctx.update(mode=mode, form=False, c_id=-1, c_username="")
    if mode == "list":
        ctx["users"] = q("SELECT id, username FROM users ORDER BY id")
    elif mode == "add":
        ctx.update(form=True, label="Add")
    elif mode in ("del", "password") and request.args.get("id", ""):
        ctx.update(form=True, label="Delete" if mode == "del" else "Change")
        uid = to_int(request.args.get("id", ""))
        row = q1("SELECT id, username FROM users WHERE id = ?", uid) if uid is not None else None
        if row:
            ctx.update(c_id=row["id"], c_username=row["username"])
    return "users.html"


def page_password(ctx):
    return "password.html"


PAGES = {
    "main": page_main,
    "login": page_login,
    "password": page_password,
    "category": page_category,
    "link": page_link,
    "users": page_users,
}


# --- The one entry point (was index.php) --------------------------------------------
@app.route("/", methods=["GET", "POST"])
@app.route("/index.php", methods=["GET", "POST"])
def index():
    default_user = get_dbvar("default_user")
    admin_user = get_dbvar("admin_user")
    pre_messages, messages = [], []

    action = input_var("action")

    if action == "submitlogin":
        if not csrf_ok():
            pre_messages.append("Failed to log in.")
        else:
            if "user_id" in session:
                session.clear()
            username = request.form.get("c_username", "")
            password = request.form.get("c_password", "")
            if username and password:
                row = q1("SELECT id, password FROM users WHERE username = ?", username)
                if row:
                    ok, legacy = verify_password(row["password"], password)
                    if ok:
                        if legacy:
                            x("UPDATE users SET password = ? WHERE id = ?", hash_password(password), row["id"])
                            get_db().commit()
                        session.clear()
                        session["user_id"] = row["id"]
                        session["admin"] = 1 if username == admin_user else 0
                        session["show_id"] = row["id"]
            if "user_id" not in session:
                pre_messages.append("Failed to log in.")
    elif action == "submitlogoff":
        session.clear()

    # A deleted user's session should not keep working.
    if "user_id" in session and not q1("SELECT 1 FROM users WHERE id = ?", session["user_id"]):
        session.clear()

    user = default_user if "show_id" not in session else ""

    logged_on = "user_id" in session
    if logged_on:
        admin = bool(session.get("admin"))
        user_id = session["user_id"]
    else:
        admin = False
        user_id = -1

    if logged_on and action in DATA_ACTIONS:
        if csrf_ok():
            messages.extend(handle_action(action, user_id, admin))
        else:
            messages.append("Failure: Invalid or expired form, please try again.")

    show = input_var("show", "main")
    user = input_var("user", user)
    if user:
        row = q1("SELECT id FROM users WHERE username = ?", user)
        session["show_id"] = row["id"] if row else -1

    show_id = session.get("show_id", -1)
    own = logged_on and show_id == user_id

    if show not in PAGES or not (show == "login" or own):
        show = "main"

    ctx = {
        "cfg": CFG,
        "pre_messages": pre_messages,
        "messages": messages,
        "logged_on": logged_on,
        "admin": admin,
        "user_id": user_id,
        "show_id": show_id,
        "own": own,
    }
    template = PAGES[show](ctx)
    return render_template(template, **ctx)


# --- CLI ------------------------------------------------------------------------------
@app.cli.command("set-password")
@click.argument("username")
def set_password_command(username):
    """Set (or reset) a user's password; creates the user if missing."""
    password = click.prompt(f"New password for {username}", hide_input=True,
                            confirmation_prompt=True)
    db = connect()
    with db:
        if db.execute("SELECT 1 FROM users WHERE username = ?", (username,)).fetchone():
            db.execute("UPDATE users SET password = ? WHERE username = ?", (hash_password(password), username))
        else:
            db.execute("INSERT INTO users (username, password) VALUES (?, ?)", (username, hash_password(password)))
    db.close()
    click.echo("Password updated.")
