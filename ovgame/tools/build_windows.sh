#!/bin/sh
# Bouwt OVGame.exe, OVLauncher.exe en (als makensis er is) OVGameSetup.exe voor Windows met mingw-w64
# (Linux, ook in de Docker-build).
#   tools/build_windows.sh [uitvoermap]     (standaard: build/)
# Op Windows zelf: build.bat (w64devkit).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
# geen tijdstempel in de exe: zelfde broncode = zelfde bestand (SmartScreen-reputatie hoort bij de hash)
FLAGS="-std=c++17 -O2 -Wall -Wno-unused-function -static -s -Wl,--no-insert-timestamp"
cd "$ROOT/src"
GAME="engine/gl.cpp engine/platform.cpp engine/renderer.cpp engine/text.cpp engine/image.cpp engine/audio.cpp
 game/world.cpp game/player.cpp game/bus.cpp game/citybus.cpp game/character.cpp game/people.cpp game/announcer.cpp
 game/busstation.cpp game/beststation.cpp game/collision.cpp game/signs.cpp game/railways.cpp game/station.cpp
 game/train.cpp game/flirt.cpp game/virm.cpp game/traffic.cpp game/mapview.cpp game/menu.cpp game/trainsound.cpp
 game/datastore.cpp game/netplay.cpp net/net.cpp net/inflate.cpp game/main.cpp"
echo "[1/3] Launcher..."
(cd launcher && $WINDRES launcher.rc -O coff -o "$OUT/launcher_res.o")
$CXX $FLAGS -municode -mwindows -o "$OUT/OVLauncher.exe" launcher/launcher.cpp net/net.cpp "$OUT/launcher_res.o" \
    -lwinhttp -lcomctl32 -lole32 -luuid -lshell32
rm -f "$OUT/launcher_res.o"
echo "[2/3] Game..."
# eerst los compileren (parallel), dan linken
OBJ="$OUT/obj-linux"
mkdir -p "$OBJ"
pids=""
objs=""
for f in $GAME; do
    o="$OBJ/$(echo "$f" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    objs="$objs $o"
    $CXX $FLAGS -c -o "$o" "$f" &
    pids="$pids $!"
done
fail=0
for p in $pids; do wait "$p" || fail=1; done
[ $fail = 0 ] || { echo "BUILD MISLUKT"; exit 1; }
(cd game && $WINDRES game.rc -O coff -o "$OBJ/game_res.o")
$CXX $FLAGS -mwindows -o "$OUT/OVGame.exe" $objs "$OBJ/game_res.o" -lopengl32 -lgdi32 -luser32 -lwinmm -lwinhttp
rm -rf "$OBJ"
if command -v makensis >/dev/null 2>&1; then
    echo "[3/3] Installer..."
    (cd installer && makensis -V2 -DOUTDIR="$OUT" -DBINDIR="$OUT" ovgame.nsi)
    echo "Klaar: $OUT/OVGame.exe, $OUT/OVLauncher.exe, $OUT/OVGameSetup.exe"
else
    echo "[3/3] Installer overgeslagen (makensis niet gevonden)"
    echo "Klaar: $OUT/OVGame.exe, $OUT/OVLauncher.exe"
fi
