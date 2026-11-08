#!/bin/sh
# Builds and installs the Flatpak locally from Linux/dist's bundle (per-user),
# then runs tests/flatpak_smoke_test.py and the Flathub linter.
# It changes the default output during the test and puts the original back.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
APP=io.github.altarafiya.Sukut
WORK="$HOME/sukut-build/flatpak-test"
EVIDENCE="$ROOT/Linux/evidence/flatpak-$(date +%Y-%m-%d)"
mkdir -p "$WORK" "$EVIDENCE"
BUNDLE=$(ls -t "$ROOT"/Linux/dist/Sukut-*-Flatpak-x86_64.tar.xz | head -n1)
cp "$BUNDLE" "$WORK/bundle.tar.xz"
SHA=$(sha256sum "$WORK/bundle.tar.xz" | cut -d' ' -f1)
sed -e "s#^        url: https://github.com/altarafiya/Sukut/releases/download/.*#        path: bundle.tar.xz#" \
    -e "s#^        sha256: [0-9a-f]\{64\}\$#&#" "$ROOT/Linux/flatpak/$APP.yaml" > "$WORK/$APP.yaml"
python3 -I - "$WORK/$APP.yaml" "$SHA" <<'PY'
import re,sys
p,sha=sys.argv[1:3]; t=open(p).read()
t=re.sub(r"(path: bundle\.tar\.xz\n\s+sha256: )[0-9a-f]{64}", r"\g<1>"+sha, t); open(p,"w").write(t)
PY
cp "$ROOT/Linux/flatpak/sukut-adb" "$WORK/"
cd "$WORK"
flatpak run --filesystem="$WORK" org.flatpak.Builder --force-clean --disable-rofiles-fuse --state-dir="$WORK/state" --repo=repo build "$APP.yaml" > "$EVIDENCE/build.log" 2>&1 || { tail -n 25 "$EVIDENCE/build.log"; exit 1; }
# The file people download from GitHub, installed the way they will.
FILE="$ROOT/Linux/dist/$(basename "$BUNDLE" -Flatpak-x86_64.tar.xz)-Linux-x86_64.flatpak"
flatpak build-bundle repo "$FILE" "$APP" master --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y --noninteractive --reinstall "$FILE" >> "$EVIDENCE/build.log" 2>&1 || { tail -n 25 "$EVIDENCE/build.log"; exit 1; }
flatpak info --user --show-metadata "$APP" > "$EVIDENCE/metadata.txt"
{ echo "== manifest"; flatpak run --filesystem="$WORK" --command=flatpak-builder-lint org.flatpak.Builder manifest "$WORK/$APP.yaml" || true
  echo "== appstream"; flatpak run --filesystem="$WORK" --command=appstreamcli org.flatpak.Builder validate --pedantic --no-net "$WORK/build/files/share/metainfo/$APP.metainfo.xml" || true
  echo "== desktop"; flatpak run --filesystem="$WORK" --command=desktop-file-validate org.flatpak.Builder "$WORK/build/files/share/applications/$APP.desktop" || true; } > "$EVIDENCE/lint.txt" 2>&1
python3 -I "$ROOT/Linux/tests/flatpak_smoke_test.py" "$EVIDENCE"
