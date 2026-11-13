#!/bin/sh
# Build the Sukut snap inside the Ubuntu 26.04 build VM.
# The VM sees this project read-only at /mnt/sukut-master (9p tag "sukut").
# Only the build tree is copied: no keys, work records, evidence or packages.
#   sh /mnt/sukut-master/Linux/snap/build-in-vm.sh
set -eu
ROOT=/mnt/sukut-master
VERSION=$(sed -n 's/^version: *//p' "$ROOT/Linux/snap/snapcraft.yaml")
DEST="$HOME/sukut-snap/project"
mkdir -p "$DEST/Windows" "$DEST/snap"
rsync -a --delete "$ROOT/Linux/" "$DEST/Linux/" --exclude=dist --exclude=evidence --exclude=work --exclude=.flatpak-builder
rsync -a --delete "$ROOT/Windows/src/" "$DEST/Windows/src/" --exclude=obj --exclude=bin
rsync -a --delete "$ROOT/Windows/assets/" "$DEST/Windows/assets/"
cp "$ROOT/Linux/snap/snapcraft.yaml" "$DEST/snap/snapcraft.yaml"
cd "$DEST"
sudo env CRAFT_PARALLEL_BUILD_COUNT="${SUKUT_JOBS:-4}" snapcraft pack --destructive-mode
SNAP="$DEST/sukut_${VERSION}_amd64.snap"
test -f "$SNAP"
cp "$SNAP" "$HOME/"
sha256sum "$HOME/sukut_${VERSION}_amd64.snap"
