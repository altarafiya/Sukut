#!/bin/sh
# Stage 3: build the live engine path and test it against this session's
# PipeWire with a virtual output: parity with the offline engine, end-to-end
# exactness, no gaps, Standby, and routing restore, for all three modes.
#   SUKUT_BUILD (default ~/sukut-build/stage1), SUKUT_DEPS_DIR (default ~/sukut-deps)
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
EVIDENCE="$ROOT/Linux/evidence/stage3-$(date +%Y-%m-%d)"

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS" > /dev/null
cmake --build "$BUILD" --parallel "${SUKUT_JOBS:-6}" --target sukut_pw_filter sukut_offline_replay

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
{
    echo "run: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "pipewire: $(pkg-config --modversion libpipewire-0.3)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
} > "$EVIDENCE/environment.txt"
python3 -I "$ROOT/Linux/tests/stage3_engine_test.py" "$BUILD/bin" "$ROOT/Windows/assets/engine" \
    "$ROOT/app/src/test/resources/clean-speech-reference.wav" "$EVIDENCE"
