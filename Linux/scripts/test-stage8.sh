#!/bin/sh
# Stage 8, part 1 (as your user): check the built .deb in Linux/dist.
# Contents, sealed files identical to Windows, stripped and hardened binaries,
# no -dbgsym package, lintian, desktop-file-validate and appstreamcli.
# Part 2 (install, upgrade, remove, purge) is test-stage8-install.sh, run as root.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(dpkg-parsechangelog -l "$ROOT/Linux/debian/changelog" -SVersion)
DEB="$ROOT/Linux/dist/sukut_${VERSION}_amd64.deb"
EVIDENCE="$ROOT/Linux/evidence/stage8-$(date +%Y-%m-%d)"
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md ! -name 'install-*' -exec rm -rf {} +
LOG="$EVIDENCE/stage8-package-log.txt"
: > "$LOG"
pass=0; fail=0
check() { # name ok detail
    if [ "$2" = 0 ]; then pass=$((pass+1)); echo "$1=PASS $3" | tee -a "$LOG"; else fail=$((fail+1)); echo "$1=FAIL $3" | tee -a "$LOG"; fi
}

{
    echo "run: $(date -Iseconds)"; echo "system: $(. /etc/os-release; echo "$PRETTY_NAME")"  # a subshell: os-release sets VERSION too
    echo "package: $(basename "$DEB") $(stat -c %s "$DEB") bytes"
    echo "sha256: $(sha256sum "$DEB" | cut -d' ' -f1)"
    echo "lintian: $(lintian --version)"
} > "$EVIDENCE/environment.txt"

X="$HOME/sukut-deb/extract"; rm -rf "$X"; mkdir -p "$X"
dpkg-deb -x "$DEB" "$X"; dpkg-deb -c "$DEB" > "$EVIDENCE/contents.txt"; dpkg-deb -I "$DEB" > "$EVIDENCE/control.txt"

missing=""
for f in usr/bin/sukut usr/libexec/sukut/sukut-host usr/share/sukut/engine/separator.tflite usr/share/sukut/engine/window_576_q15.bin \
         usr/share/sukut/engine/window_960_q15.bin usr/share/sukut/fonts/ReadexPro.ttf usr/share/sukut/fonts/Handjet.ttf \
         usr/share/sukut/fonts/IBMPlexSansArabic-Regular.ttf usr/share/sukut/fonts/IBMPlexSansArabic-SemiBold.ttf \
         usr/share/sukut/android/Sukut.apk usr/share/sukut/android/Sukut.apk.sha256 usr/share/sukut/android/Sukut.apk.version \
         usr/share/sukut/android/activate-worker.sh usr/share/sukut/android/activate-worker.sh.sha256 \
         usr/libexec/sukut/sukut-usb-access usr/share/polkit-1/actions/io.github.altarafiya.Sukut.policy \
         usr/lib/udev/rules.d/70-sukut-android.rules \
         usr/share/sukut/images/phone-developer-options.png usr/share/sukut/images/phone-usb-debugging.png \
         usr/share/applications/io.github.altarafiya.Sukut.desktop usr/share/metainfo/io.github.altarafiya.Sukut.metainfo.xml \
         usr/share/icons/hicolor/512x512/apps/io.github.altarafiya.Sukut.png usr/share/icons/hicolor/16x16/apps/io.github.altarafiya.Sukut.png \
         usr/share/icons/hicolor/scalable/status/io.github.altarafiya.Sukut-tray-on.svg \
         usr/share/icons/hicolor/scalable/status/io.github.altarafiya.Sukut-tray-idle-symbolic.svg \
         usr/share/icons/hicolor/scalable/actions/io.github.altarafiya.Sukut-music-filter-symbolic.svg \
         usr/share/man/man1/sukut.1.gz usr/share/doc/sukut/copyright usr/share/doc/sukut/licenses/TensorFlow-Lite/LICENSE \
         usr/share/doc/sukut/licenses/KissFFT/COPYING; do
    # dh_compress gzips larger documents, as Debian policy asks.
    [ -e "$X/$f" ] || [ -e "$X/$f.gz" ] || missing="$missing $f"
done
check C_contents "$([ -z "$missing" ]; echo $?)" "${missing:-all expected files present}"

same=0
for f in separator.tflite window_576_q15.bin window_960_q15.bin; do
    cmp -s "$X/usr/share/sukut/engine/$f" "$ROOT/Windows/assets/engine/$f" || same=1
done
# The APK is the Windows build's; the activation script is Android 0.4.2's (Linux/data/android).
for f in "$ROOT/Windows/src/sukut-ui/Assets/Phone/Sukut.apk"*; do cmp -s "$X/usr/share/sukut/android/$(basename "$f")" "$f" || same=1; done
for f in "$ROOT/Linux/data/android/"*; do cmp -s "$X/usr/share/sukut/android/$(basename "$f")" "$f" || same=1; done
check C_sealed_and_apk_identical "$same" "engine files, Windows APK and Android 0.4.2 activation script byte-identical"

stripped=0
for b in usr/bin/sukut usr/libexec/sukut/sukut-host; do
    readelf -S "$X/$b" | grep -qE '\.symtab|\.debug_' && stripped=1
done
debug=$(find "$X/usr/lib/debug" -type f 2>/dev/null | wc -l)
check C_no_debug_data "$([ "$debug" = 0 ]; echo $?)" "files under /usr/lib/debug: $debug"
check C_stripped "$stripped" "$(file -b "$X/usr/libexec/sukut/sukut-host" | cut -d, -f1-2,8-)"
ls "$ROOT/Linux/dist/" "$HOME/sukut-deb/" | grep -q dbgsym; check C_no_dbgsym "$([ $? -ne 0 ]; echo $?)" "no -dbgsym package built"

hard=0; detail=""
for b in usr/bin/sukut usr/libexec/sukut/sukut-host; do
    h=$(readelf -h "$X/$b" | grep -q 'DYN' && echo PIE)
    r=$(readelf -l "$X/$b" | grep -q GNU_RELRO && echo RELRO)
    n=$(readelf -d "$X/$b" | grep -qE 'BIND_NOW|FLAGS_1.*NOW' && echo NOW)
    s=$(readelf -W --dyn-syms "$X/$b" | grep -q __stack_chk_fail && echo SSP)
    f=$(readelf -W --dyn-syms "$X/$b" | grep -E '_chk@' | grep -qv stack_chk && echo FORTIFY)
    detail="$detail $(basename "$b"):$h,$r,$n,$s,$f"
    [ -n "$h" ] && [ -n "$r" ] && [ -n "$n" ] && [ -n "$s" ] || hard=1
done
check C_hardened "$hard" "$detail"

# The label debian/rules derives from the changelog: 0.4.0~beta.2-1 -> 0.4.0-beta.2.
LABEL=$(echo "${VERSION%-*}" | sed 's/~/-/')
grep -a -q -F "$LABEL" "$X/usr/bin/sukut"; check C_version_label $? "About shows $(grep -a -o -F "$LABEL" "$X/usr/bin/sukut" | head -n1)"

lintian --info --display-info --pedantic "$DEB" > "$EVIDENCE/lintian.txt" 2>&1
ran=$?
errors=$(grep -c '^E:' "$EVIDENCE/lintian.txt"); warnings=$(grep -c '^W:' "$EVIDENCE/lintian.txt")
info=$(grep -cE '^(I|P|X):' "$EVIDENCE/lintian.txt")
# Exit 0 or 1 means lintian ran (1: it found something); anything else is a failure to run.
check C_lintian_no_errors "$([ $ran -le 1 ] && [ "$errors" = 0 ]; echo $?)" "errors=$errors warnings=$warnings info/pedantic=$info (lintian.txt)"

desktop-file-validate "$X/usr/share/applications/io.github.altarafiya.Sukut.desktop" > "$EVIDENCE/desktop-validate.txt" 2>&1
check C_desktop_valid $? "$(cat "$EVIDENCE/desktop-validate.txt" | head -c 300)"
appstreamcli validate --no-net --explain "$X/usr/share/metainfo/io.github.altarafiya.Sukut.metainfo.xml" > "$EVIDENCE/appstream-validate.txt" 2>&1
check C_appstream_valid $? "$(tail -n 1 "$EVIDENCE/appstream-validate.txt")"

echo "RESULT=$([ $fail = 0 ] && echo PASS || echo FAIL) $pass/$((pass+fail))" | tee -a "$LOG"
[ $fail = 0 ]
