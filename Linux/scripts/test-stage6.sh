#!/bin/sh
# Stage 6: the GTK window. Screenshots and keyboard checks from the window's own
# --capture mode, then the real window with the real host. Everything runs on
# GTK's headless Broadway display, so no window appears on the desktop, and on
# a private session bus. No mode is started, so audio is never rerouted.
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage6-$(date +%Y-%m-%d)"

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut sukut-host

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "gtk: $(pkg-config --modversion gtk4)"
    echo "libadwaita: $(pkg-config --modversion libadwaita-1)"
} > "$EVIDENCE/environment.txt"

gtk4-broadwayd :9 > "$EVIDENCE/broadway.log" 2>&1 &
BROADWAY=$!
trap 'kill $BROADWAY 2>/dev/null || true' EXIT
sleep 1
export GDK_BACKEND=broadway BROADWAY_DISPLAY=:9

status=0
SHOTS="$EVIDENCE/screenshots"
dbus-run-session -- "$BUILD/bin/sukut" --capture "$SHOTS" > "$EVIDENCE/capture-log.txt" 2>&1 || status=1
ADW_DEBUG_HIGH_CONTRAST=1 dbus-run-session -- "$BUILD/bin/sukut" --capture "$SHOTS" --capture-set contrast \
    >> "$EVIDENCE/capture-log.txt" 2>&1 || status=1
cat "$SHOTS/capture-results.txt"
dbus-run-session -- python3 -I "$ROOT/Linux/tests/stage6_window_test.py" "$BUILD/bin" "$EVIDENCE" || status=1
exit $status
