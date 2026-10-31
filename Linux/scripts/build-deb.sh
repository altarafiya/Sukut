#!/bin/sh
# Stage 8: build the Sukut .deb (Linux/PLAN.md section 10).
# Assembles a source tree in ~/sukut-deb (Linux/ with debian/ at its root, the
# shared engine sources from Windows/src, the sealed files and the app assets),
# builds it offline with dpkg-buildpackage, and copies the result to Linux/dist.
#   SUKUT_DEPS_DIR (default ~/sukut-deps): the pinned dependency sources.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
VERSION=$(dpkg-parsechangelog -l "$ROOT/Linux/debian/changelog" -SVersion)
UPSTREAM=${VERSION%-*}
WORK="$HOME/sukut-deb"
TREE="$WORK/sukut-$UPSTREAM"

rm -rf "$WORK" && mkdir -p "$TREE/Windows/src/sukut-ui/Assets/Setup" "$TREE/Windows/assets"
rsync -a --exclude evidence --exclude reference --exclude dist --exclude debian "$ROOT/Linux/" "$TREE/Linux/"
cp -a "$ROOT/Linux/debian" "$TREE/debian"
# Files on the Windows drive look executable in WSL; debhelper would run them as scripts.
find "$TREE/debian" -type f -exec chmod 0644 {} + && chmod 0755 "$TREE/debian/rules"
for part in model dsp offline realtime; do cp -a "$ROOT/Windows/src/$part" "$TREE/Windows/src/"; done
mkdir -p "$TREE/Windows/src/live" && cp -a "$ROOT/Windows/src/live/AudioBuffering.h" "$TREE/Windows/src/live/"  # the buffering table
cp -a "$ROOT/Windows/src/sukut-ui/Assets/"*.ttf "$TREE/Windows/src/sukut-ui/Assets/"
cp -a "$ROOT/Windows/src/sukut-ui/Assets/Phone" "$TREE/Windows/src/sukut-ui/Assets/"
cp -a "$ROOT/Windows/src/sukut-ui/Assets/Setup/"phone-*.png "$TREE/Windows/src/sukut-ui/Assets/Setup/"
cp -a "$ROOT/Windows/assets/engine" "$TREE/Windows/assets/"

cd "$TREE"
SUKUT_DEPS_DIR="$DEPS" DEB_BUILD_OPTIONS="parallel=${SUKUT_JOBS:-6}" dpkg-buildpackage -b -us -uc > "$WORK/build.log" 2>&1 || {
    tail -n 40 "$WORK/build.log"; exit 1; }

mkdir -p "$ROOT/Linux/dist"
cp "$WORK"/sukut_"$VERSION"_amd64.deb "$WORK"/sukut_"$VERSION"_amd64.buildinfo "$WORK"/sukut_"$VERSION"_amd64.changes "$ROOT/Linux/dist/"
ls -l "$ROOT/Linux/dist/sukut_${VERSION}_amd64.deb"
