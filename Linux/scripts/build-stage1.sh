#!/bin/sh
# Stage 1: build the engine on Linux, run its verifiers on the sealed files and
# compare the outputs with the Windows reference (Linux/PLAN.md section 4.4).
#   SUKUT_BUILD (default ~/sukut-build/stage1)   build directory, on the Linux disk
#   SUKUT_DEPS_DIR (default ~/sukut-deps)        from seed-deps.sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${SUKUT_BUILD:-$HOME/sukut-build/stage1}
DEPS=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}
ENGINE="$ROOT/Windows/assets/engine"
EVIDENCE="$ROOT/Linux/evidence/stage1-$(date +%Y-%m-%d)"

JOBS=${SUKUT_JOBS:-6}  # TFLite compiles need ~1 GB each; 6 jobs fit 7 GB (WSL's default memory)

cmake -S "$ROOT/Linux" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUKUT_DEPS_DIR="$DEPS"
cmake --build "$BUILD" --parallel "$JOBS" --target cleanwave_bootstrap cleanwave_model_verify cleanwave_dsp_verify \
    cleanwave_realtime_verify sukut_efficiency_verify

# The three sealed files must be identical on every platform (PLAN section 4.2).
for name in separator.tflite window_576_q15.bin window_960_q15.bin; do
    a=$(sha256sum "$ENGINE/$name" | cut -d' ' -f1)
    b=$(sha256sum "$ROOT/app/src/engine/assets/$name" | cut -d' ' -f1)
    [ "$a" = "$b" ] || { echo "sealed $name differs between Windows and Android" >&2; exit 1; }
done
echo "sealed engine files identical on Windows and Android"

# Keep the hand-written SUMMARY.md; replace everything the run generates.
mkdir -p "$EVIDENCE" && find "$EVIDENCE" -mindepth 1 -maxdepth 1 ! -name SUMMARY.md -exec rm -rf {} +
mkdir -p "$EVIDENCE/model" "$EVIDENCE/dsp" "$EVIDENCE/realtime"
BIN="$BUILD/bin"
status=0
run() {  # run <label> <log> <command...>; keeps the verifier's own exit status
    label=$1; log=$2; shift 2
    if "$@" > "$log" 2>&1; then echo "$label: exit 0"; else echo "$label: FAILED (see $log)"; status=1; fi
    tail -n 2 "$log"
}
W576="$ENGINE/window_576_q15.bin"; W960="$ENGINE/window_960_q15.bin"; MODEL="$ENGINE/separator.tflite"
run model_verify "$EVIDENCE/model-run.log" "$BIN/cleanwave_model_verify" "$MODEL" "$EVIDENCE/model"
run dsp_verify "$EVIDENCE/dsp-run.log" "$BIN/cleanwave_dsp_verify" "$MODEL" "$W576" "$W960" "$EVIDENCE/dsp"
run efficiency_verify "$EVIDENCE/efficiency.txt" "$BIN/sukut_efficiency_verify" "$MODEL" "$W576" "$W960"
run realtime_verify "$EVIDENCE/realtime-run.log" "$BIN/cleanwave_realtime_verify" "$MODEL" "$W576" "$W960" "$EVIDENCE/realtime"

# Exit 0 identical, 2 numerical differences (owner decision), 1 failure.
compare=0
python3 -I "$ROOT/Linux/scripts/compare-stage1.py" "$ROOT/Linux/reference/windows-2026-10-28" "$EVIDENCE" > "$EVIDENCE/comparison.txt" || compare=$?
cat "$EVIDENCE/comparison.txt"
[ "$compare" -eq 0 ] || status=$compare
{
    echo "built: $(date -Iseconds)"
    . /etc/os-release && echo "system: $PRETTY_NAME"
    echo "compiler: $(g++ --version | head -n1)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
} > "$EVIDENCE/environment.txt"
echo "evidence: $EVIDENCE"
exit $status
