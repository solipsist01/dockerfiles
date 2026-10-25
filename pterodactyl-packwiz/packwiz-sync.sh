#!/bin/bash
# packwiz-sync.sh <pack.toml URL>
#
# Runs before every server start (see the egg's startup command). Reads the
# pack's [versions], installs that Minecraft version + loader when it isn't
# installed yet or has changed, then syncs the mods with packwiz-installer.
# If something fails it says so and the server starts with what it has.
#
# Afterwards the server folder holds either unix_args.txt (Forge/NeoForge
# 1.17+) or server.jar (Fabric, Quilt, older Forge), which is what the
# startup command launches.

set -u
PACK_URL="${1:-}"
BOOTSTRAP_URL="https://github.com/packwiz/packwiz-installer-bootstrap/releases/latest/download/packwiz-installer-bootstrap.jar"
MARKER=".packwiz-loader"

log() { echo "[packwiz] $*"; }

if [ -z "$PACK_URL" ]; then
    log "No pack URL set; starting without syncing."
    exit 0
fi

pack=$(curl -fsSL --retry 2 "$PACK_URL") || {
    log "Couldn't fetch $PACK_URL; starting with the current install."
    exit 0
}
versions=$(printf '%s\n' "$pack" | sed -n '/^\[versions\]/,/^\[/p')
get() { printf '%s\n' "$versions" | sed -n "s/^$1 *= *\"\(.*\)\"/\1/p" | head -1; }

mc=$(get minecraft)
loader="" ver=""
for name in neoforge forge fabric quilt; do
    ver=$(get "$name")
    if [ -n "$ver" ]; then loader=$name; break; fi
done

# --- Java version check (warn only: the Docker image decides the Java version)
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }
if [ -n "$mc" ]; then
    if version_ge "$mc" 26.1; then need=25
    elif version_ge "$mc" 1.20.5; then need=21
    elif version_ge "$mc" 1.18; then need=17
    elif version_ge "$mc" 1.17; then need=16
    else need=8; fi
    have=$(java -version 2>&1 | sed -n 's/.*version "\([0-9]*\)\(\.[0-9]*\)*.*/\1/p' | head -1)
    [ "$have" = "1" ] && have=8
    if [ -n "$have" ] && { [ "$have" -lt "$need" ] || { [ "$need" = 8 ] && [ "$have" -gt 8 ]; }; }; then
        log "WARNING: Minecraft $mc needs Java $need, this server runs Java $have."
        log "         Pick the Java $need image under Startup -> Docker Image."
    fi
fi

# --- Loader install ---------------------------------------------------------

forge_like() {  # <maven base> <group path> <artifact> <version>
    local url="$1/$2/$3/$4/$3-$4-installer.jar"
    curl -fsSL -o installer.jar "$url" || { log "Download failed: $url"; return 1; }
    log "Running the $3 $4 installer (takes a minute)..."
    if ! java -jar installer.jar --installServer >installer-output.log 2>&1; then
        tail -n 15 installer-output.log
        return 1
    fi
    rm -f installer.jar installer.jar.log installer-output.log unix_args.txt server.jar
    local args="libraries/$2/$3/$4/unix_args.txt"
    if [ -f "$args" ]; then
        ln -s "$args" unix_args.txt
    else
        # Before Minecraft 1.17 the installer leaves a runnable jar instead.
        local jar
        jar=$(ls "$3-$4"*.jar 2>/dev/null | grep -v installer | head -1)
        [ -n "$jar" ] || { log "The installer left no server jar."; return 1; }
        mv "$jar" server.jar
    fi
}

install_fabric() {
    local installer
    installer=$(curl -fsSL https://meta.fabricmc.net/v2/versions/installer |
        grep -o '"version": *"[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
    [ -n "$installer" ] || { log "Couldn't look up the Fabric installer version."; return 1; }
    rm -f unix_args.txt
    curl -fsSL -o server.jar "https://meta.fabricmc.net/v2/versions/loader/$mc/$ver/$installer/server/jar" ||
        { log "Couldn't download the Fabric server launcher."; return 1; }
}

install_quilt() {
    curl -fsSL -o quilt-installer.jar https://quiltmc.org/api/v1/download-latest-installer/java-universal ||
        { log "Couldn't download the Quilt installer."; return 1; }
    rm -f unix_args.txt server.jar quilt-server-launch.jar
    if ! java -jar quilt-installer.jar install server "$mc" "$ver" --download-server --install-dir=. >installer-output.log 2>&1; then
        tail -n 15 installer-output.log
        return 1
    fi
    rm -f quilt-installer.jar installer-output.log
    # The launcher becomes server.jar, so the vanilla jar moves aside.
    mv server.jar vanilla-server.jar
    echo "serverJar=vanilla-server.jar" > quilt-server-launcher.properties
    mv quilt-server-launch.jar server.jar
}

loader_present() {
    case "$loader" in
        forge|neoforge) [ -e unix_args.txt ] || [ -f server.jar ] ;;
        *) [ -f server.jar ] ;;
    esac
}

want="$loader $ver minecraft $mc"
if [ -z "$loader" ]; then
    log "pack.toml names no loader; leaving the server install as it is."
elif [ "$want" != "$(cat "$MARKER" 2>/dev/null)" ] || ! loader_present; then
    log "Installing $loader $ver for Minecraft $mc (was: $(cat "$MARKER" 2>/dev/null || echo nothing))"
    case "$loader" in
        forge) forge_like https://maven.minecraftforge.net net/minecraftforge forge "$mc-$ver" ;;
        neoforge)
            # NeoForge for 1.20.1 still used Forge's artifact name and versioning.
            if [ "$mc" = "1.20.1" ]; then
                forge_like https://maven.neoforged.net/releases net/neoforged forge "1.20.1-$ver"
            else
                forge_like https://maven.neoforged.net/releases net/neoforged neoforge "$ver"
            fi ;;
        fabric) install_fabric ;;
        quilt) install_quilt ;;
    esac
    if [ $? -eq 0 ]; then
        echo "$want" > "$MARKER"
        log "Installed $loader $ver for Minecraft $mc."
    else
        log "Installing $loader $ver failed; starting with the current install."
    fi
else
    log "$loader $ver for Minecraft $mc is installed."
fi

# --- Mods -------------------------------------------------------------------

if [ ! -f packwiz-installer-bootstrap.jar ]; then
    curl -fsSL -o packwiz-installer-bootstrap.jar "$BOOTSTRAP_URL" || log "Couldn't download packwiz-installer-bootstrap."
fi
java -jar packwiz-installer-bootstrap.jar -g -s server "$PACK_URL" ||
    log "Mod sync failed; starting with the current mods."
exit 0
