# Startpage (Flask + SQLite)

A port of the old PHP 5.6 startpage. The HTML it sends is the same markup as
before (no doctype, same tables and attributes), so it looks and behaves the same.

## Before you build

Copy the three icons from the old site into `static/startpage_images/`:
`add.gif`, `info.gif`, `trash.gif`. (`black.jpg` and `startpage.css` are already in `static/`.)

## Build and run

    docker build -t startpage .
    docker volume create startpage-data
    docker run -d --name startpage -p 8000:8000 -v startpage-data:/data \
        -e SESSION_COOKIE_SECURE=1 startpage

Set `SESSION_COOKIE_SECURE=1` when the site is served over HTTPS (e.g. behind a
reverse proxy); leave it out for plain-HTTP testing or logins won't stick.

## First login

On an empty database the app creates one account, **admin / admin**. It is the
admin (User Management) and also the page anonymous visitors see. Log in and
change the password right away via **Change password** at the bottom of the page.

## Users and passwords

- **Change password** (any logged-in user): needs your current password.
- **User Management** (admin only): add users, delete users, and set a new
  password for any user (the `password` link next to each name). The admin
  can't delete their own account.
- Locked out? Reset from the command line:
  `docker exec -it startpage flask set-password admin`

To start with a different name/password instead, set `STARTPAGE_ADMIN_USER` /
`STARTPAGE_ADMIN_PASSWORD` on the first run. They are only used when the
database has no users yet.

## Settings (environment variables)

| Variable | Default | Was in config.php |
|---|---|---|
| `STARTPAGE_TITLE` | `Startpage` | `$HTMLTitle` |
| `STARTPAGE_DESCRIPTION` | `Description` | `$HTMLDescription` |
| `STARTPAGE_KEYWORDS` | `Keywords` | `$HTMLKeywords` |
| `STARTPAGE_HEADER` | *(empty)* | `$HTMLHeader` |
| `STARTPAGE_COLUMNS` | `3` | `$DefaultColumnCount` |
| `STARTPAGE_COMBINE_CHILDS` | `1` | `$CombineCategoryChilds` |
| `STARTPAGE_ADMIN_USER` | `admin` | (first run only) |
| `STARTPAGE_ADMIN_PASSWORD` | `admin` | (first run only) |
| `STARTPAGE_DB` | `/data/startpage.db` | |
| `SECRET_KEY` | generated into `/data/.secret_key` | |
| `SESSION_COOKIE_SECURE` | `0` | |

## What changed under the hood

- All SQL uses bound parameters (the PHP version was injectable in several places,
  including the `?user=` parameter on the public page and the user-delete page).
- All output is HTML-escaped; `javascript:`/`data:` links are refused.
- Passwords: salted hashes instead of unsalted MD5 (old MD5 hashes are still
  accepted and upgraded on login, should you ever import old rows).
- Every form carries a CSRF token; changes only happen via POST.
- Adding/deleting users now really requires the admin account (before, any
  logged-in user could post those actions).
- Only known pages can be shown (`?show=` used to feed straight into `include`).
- Category moves that would create a loop are refused.
- Runs as a non-root user in a small, current Python image.

The click counter (`count.php`) is not ported: in the PHP version its `onclick`
was already commented out, so counts never changed. Existing counts are kept and
still decide the order, exactly as before.
