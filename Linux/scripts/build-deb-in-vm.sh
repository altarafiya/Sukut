#!/bin/sh
# Build the Sukut .deb inside the Ubuntu 26.04 build VM, which sees this project
# read-only at /mnt/sukut-master. Copies only what build-deb.sh reads, then
# builds there; the result is in ~/sukut-deb-src/Linux/dist.
#   sh /mnt/sukut-master/Linux/scripts/build-deb-in-vm.sh
set -eu
ROOT=/mnt/sukut-master
SRC="$HOME/sukut-deb-src"
sudo apt-get install -y --no-install-recommends build-essential debhelper dpkg-dev libgtk4-layer-shell-dev cmake ninja-build g++ pkgconf \
    libpipewire-0.3-dev libgtk-4-dev libadwaita-1-dev libglib2.0-dev python3 rsync >/dev/null
mkdir -p "$SRC/Windows/src" "$SRC/Windows/assets"
rsync -a --delete "$ROOT/Linux/" "$SRC/Linux/" --exclude=dist --exclude=evidence --exclude=work --exclude=.flatpak-builder
rsync -a --delete "$ROOT/Windows/src/" "$SRC/Windows/src/" --exclude=obj --exclude=bin
rsync -a --delete "$ROOT/Windows/assets/engine/" "$SRC/Windows/assets/engine/"
# CMake 4 refuses the old minimum versions of some pinned dependencies.
CMAKE_POLICY_VERSION_MINIMUM=3.5 SUKUT_JOBS="${SUKUT_JOBS:-4}" sh "$SRC/Linux/scripts/build-deb.sh"
sha256sum "$SRC"/Linux/dist/sukut_*_amd64.deb
