#!/bin/sh
# Stage 4: routing ownership with the engine running, against this session's
# PipeWire with two virtual outputs and two stand-in apps (pw-play).
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage4-$(date +%Y-%m-%d)"

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut_pw_filter

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "pipewire: $(pkg-config --modversion libpipewire-0.3)"
    echo "wireplumber: $(wireplumber --version 2>/dev/null | grep -m1 -o '[0-9][0-9.]*' | head -n1)"
} > "$EVIDENCE/environment.txt"
python3 -I "$ROOT/Linux/tests/stage4_routing_test.py" "$BUILD/bin" "$ROOT/Windows/assets/engine" "$EVIDENCE"
