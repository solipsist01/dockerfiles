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

## Moving the data over (one time)

Run this on the same Docker network as the old `mariadb` container:

    docker run --rm --network <old-network> -v startpage-data:/data \
        -e MYSQL_HOST=mariadb -e MYSQL_USER=... -e MYSQL_PASSWORD=... \
        -e MYSQL_DATABASE=startpage.wenro.nl \
        startpage python migrate.py

Everyone keeps their password. The old MD5 hashes are upgraded to a proper
salted hash automatically on each user's next login.

## Fresh start instead

    docker run --rm -it -v startpage-data:/data startpage flask init-db --admin <name>
    docker run --rm -it -v startpage-data:/data startpage flask set-password <name>

## Settings (environment variables)

| Variable | Default | Was in config.php |
|---|---|---|
| `STARTPAGE_TITLE` | `Startpage` | `$HTMLTitle` |
| `STARTPAGE_DESCRIPTION` | `Description` | `$HTMLDescription` |
| `STARTPAGE_KEYWORDS` | `Keywords` | `$HTMLKeywords` |
| `STARTPAGE_HEADER` | *(empty)* | `$HTMLHeader` |
| `STARTPAGE_COLUMNS` | `3` | `$DefaultColumnCount` |
| `STARTPAGE_COMBINE_CHILDS` | `1` | `$CombineCategoryChilds` |
| `STARTPAGE_DB` | `/data/startpage.db` | |
| `SECRET_KEY` | generated into `/data/.secret_key` | |
| `SESSION_COOKIE_SECURE` | `0` | |

## What changed under the hood

- All SQL uses bound parameters (the PHP version was injectable in several places,
  including the `?user=` parameter on the public page and the user-delete page).
- All output is HTML-escaped; `javascript:`/`data:` links are refused.
- Passwords: salted hashes instead of unsalted MD5.
- Every form carries a CSRF token; changes only happen via POST.
- Adding/deleting users now really requires the admin account (before, any
  logged-in user could post those actions).
- Only known pages can be shown (`?show=` used to feed straight into `include`).
- Category moves that would create a loop are refused.
- Runs as a non-root user in a small, current Python image.

The click counter (`count.php`) is not ported: in the PHP version its `onclick`
was already commented out, so counts never changed. Existing counts are kept and
still decide the order, exactly as before.
