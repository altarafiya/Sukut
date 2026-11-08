#!/bin/sh
# Stage 9 routing: Windows 0.4.2's output handling (new devices, missing outputs,
# outputs chosen while Off) and the EasyEffects guards (feedback loop, pinned
# apps), against this session's PipeWire with virtual outputs. The host runs on a
# private runtime directory and session bus, so a running Sukut is left alone.
# It changes the default output during the test and puts the original back.
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage9-routing-$(date +%Y-%m-%d)"

# CMake 4 refuses the old minimum versions of some pinned dependencies.
cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut_pw_filter sukut-host

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "pipewire: $(pkg-config --modversion libpipewire-0.3)"
    echo "wireplumber: $(wireplumber --version 2>/dev/null | grep -m1 -o '[0-9][0-9.]*' | head -n1)"
    echo "easyeffects: $(easyeffects --version 2>/dev/null | head -n1 || echo none)"
} > "$EVIDENCE/environment.txt"
dbus-run-session -- python3 -I "$ROOT/Linux/tests/stage9_routing_test.py" "$BUILD/bin" "$ROOT/Windows/assets/engine" \
    "$ROOT/Linux/data/icons" "$EVIDENCE"
