#!/bin/sh
# Builds the prebuilt Flatpak bundle inside the GNOME SDK, so it matches the
# runtime the app runs on. Needs the private files (sealing header, sealed
# model), which is why Flathub can't build it from the public source.
#   sh Linux/scripts/build-flatpak-bundle.sh VERSION    e.g. 0.4.0-beta.8
#   SUKUT_DEPS_DIR (default ~/sukut-deps), SUKUT_JOBS (default 6)
set -eu
VERSION=${1:?usage: build-flatpak-bundle.sh VERSION}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
RUNTIME=$(sed -n "s/^runtime-version: '\(.*\)'/\1/p" "$ROOT/Linux/flatpak/io.github.altarafiya.Sukut.yaml")
WORK="$HOME/sukut-build/flatpak"
OUT="$ROOT/Linux/dist/Sukut-$VERSION-Flatpak-x86_64.tar.xz"
mkdir -p "$WORK"
flatpak run --filesystem="$ROOT" --filesystem="$DEPS":ro --filesystem="$WORK" --env=CMAKE_POLICY_VERSION_MINIMUM=3.5 \
    --command=sh "org.gnome.Sdk//$RUNTIME" -eu -c '
ROOT=$1 DEPS=$2 WORK=$3 VERSION=$4 OUT=$5 JOBS=$6
cmake -S "$ROOT/Linux" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/app -DCMAKE_INSTALL_LIBEXECDIR=libexec \
    -DSUKUT_DEV_TOOLS=OFF -DSUKUT_DEPS_DIR="$DEPS" -DSUKUT_VERSION_LABEL="$VERSION" >/dev/null
cmake --build "$WORK/build" --parallel "$JOBS"
rm -rf "$WORK/stage"
DESTDIR="$WORK/stage" cmake --install "$WORK/build" --component sukut >/dev/null
cd "$WORK/stage/app"
# What only a system install can use: the root USB helper, its polkit action, the udev rule.
rm -rf lib share/polkit-1 share/man libexec/sukut/sukut-usb-access
strip --strip-all bin/sukut libexec/sukut/sukut-host   # no symbols: they would label the sealed key constants
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-11-08 -cJf "$OUT" bin libexec share
' sh "$ROOT" "$DEPS" "$WORK" "$VERSION" "$OUT" "${SUKUT_JOBS:-6}"
ls -l "$OUT"; sha256sum "$OUT"
