#!/bin/sh
# Stage 5: the host (tray, menu, socket, notification, autostart, exit), on a
# private session bus with stand-in desktop services and this session's PipeWire.
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage5-$(date +%Y-%m-%d)"

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut-host

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "pipewire: $(pkg-config --modversion libpipewire-0.3)"
    echo "glib: $(pkg-config --modversion gio-2.0)"
    echo "rtkit: $(dpkg-query -W -f '${Version}' rtkit 2>/dev/null || echo none)"
} > "$EVIDENCE/environment.txt"
dbus-run-session -- python3 -I "$ROOT/Linux/tests/stage5_host_test.py" "$BUILD/bin" "$ROOT/Windows/assets/engine" \
    "$ROOT/Linux/data/icons" "$EVIDENCE"
