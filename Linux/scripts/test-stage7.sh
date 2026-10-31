#!/bin/sh
# Stage 7: the phone tools. Every Phone flow against a stand-in adb (a pretend
# phone, tests/fake_adb.py), then the Phone page's screenshots and keyboard
# check on GTK's headless display. No real phone or USB device is used; the
# live check on the owner's phone is done on real Debian (stage 9).
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage7-$(date +%Y-%m-%d)"

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "adb: $(adb version 2>/dev/null | sed -n 2p)"
    echo "bundled apk: $(tr '\n' ' ' < "$BUILD/share/sukut/android/Sukut.apk.version")"
} > "$EVIDENCE/environment.txt"

status=0
python3 -I "$ROOT/Linux/tests/stage7_phone_test.py" "$BUILD/bin" "$BUILD/share/sukut" \
    "$ROOT/Windows/src/sukut-ui/Assets/Phone" "$EVIDENCE" || status=1

gtk4-broadwayd :10 > "$EVIDENCE/broadway.log" 2>&1 &
BROADWAY=$!
trap 'kill $BROADWAY 2>/dev/null || true' EXIT
sleep 1
export GDK_BACKEND=broadway BROADWAY_DISPLAY=:10
SHOTS="$EVIDENCE/screenshots"
dbus-run-session -- "$BUILD/bin/sukut" --capture "$SHOTS" --capture-set phone > "$EVIDENCE/capture-log.txt" 2>&1 || status=1
dbus-run-session -- "$BUILD/bin/sukut" --capture "$SHOTS" --capture-set keyboard >> "$EVIDENCE/capture-log.txt" 2>&1 || status=1
cat "$SHOTS/capture-results.txt"
exit $status
