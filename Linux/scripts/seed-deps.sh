#!/bin/sh
# Stage 0: prepare the pinned engine dependencies for an offline Linux build.
#
# Reuses what the Windows build already downloaded and verified on D:, so the
# Linux engine is built from exactly the same sources:
#   WINDOWS_DEPS   (default /mnt/d/Sukut-Dev/deps)        pinned archives
#   WINDOWS_BUILD  (default /mnt/d/Sukut-Dev/build-0.4)   TFLite's fetched sources
#   SUKUT_DEPS_DIR (default ~/sukut-deps)                 output, on the Linux disk
#
# The only network access is NEON_2_SSE (header-only), which TFLite requires on
# x86 Linux but not on Windows. It is fetched from TFLite's own pinned mirror URL
# and checked against TFLite's own SHA-256 before use.
set -eu

WINDOWS_DEPS=${WINDOWS_DEPS:-/mnt/d/Sukut-Dev/deps}
WINDOWS_BUILD=${WINDOWS_BUILD:-/mnt/d/Sukut-Dev/build-0.4}
OUT=${SUKUT_DEPS_DIR:-$HOME/sukut-deps}

check() {  # check <file> <sha256>
    actual=$(sha256sum "$1" | cut -d' ' -f1)
    expected=$(printf '%s' "$2" | tr 'A-F' 'a-f')
    [ "$actual" = "$expected" ] || { echo "SHA-256 mismatch for $1" >&2; exit 1; }
    echo "verified $(basename "$1")"
}

mkdir -p "$OUT/src" "$OUT/prefetch" "$OUT/downloads"

# 1. Archives pinned in Windows/deps/DEPENDENCIES.lock.md, re-extracted on Linux.
check "$WINDOWS_DEPS/downloads/tensorflow-2.16.1.zip" 54C976F828182F85E10E03840DD3B0504109F57760A498075574E35E9AA983FE
check "$WINDOWS_DEPS/downloads/kissfft-131.1.0.zip" 9C2E19CC34ED910DCB509FD8AB561A523B923B6578703ACE8C8F37F5A286BB25
check "$WINDOWS_DEPS/downloads/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8.zip" 3D7B0E9C4C658A84376A1086126BE02F9B7F753CAA95E009D9AC38D11DA444DB
for archive in tensorflow-2.16.1 kissfft-131.1.0 FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8; do
    if [ ! -d "$OUT/src/$archive" ]; then
        echo "extracting $archive"
        unzip -q "$WINDOWS_DEPS/downloads/$archive.zip" -d "$OUT/src"
    fi
done

# 2. TFLite's own fetched dependencies, copied from the Windows build tree.
#    TFLite pinned each by commit and hash when Windows fetched it.
for name in abseil-cpp eigen farmhash fft2d flatbuffers gemmlowp ml_dtypes ruy cpuinfo pthreadpool-source; do
    [ -d "$WINDOWS_BUILD/$name" ] || { echo "missing $WINDOWS_BUILD/$name" >&2; exit 1; }
    rsync -a --delete "$WINDOWS_BUILD/$name/" "$OUT/prefetch/$name/"
    echo "copied $name"
done

# 3. NEON_2_SSE, from tensorflow/lite/tools/cmake/modules/neon2sse.cmake.
NEON_URL=https://storage.googleapis.com/mirror.tensorflow.org/github.com/intel/ARM_NEON_2_x86_SSE/archive/a15b489e1222b2087007546b4912e21293ea86ff.tar.gz
NEON_SHA=019fbc7ec25860070a1d90e12686fc160cfb33e22aa063c80f52b363f1361e9d
NEON_TAR="$OUT/downloads/neon2sse-a15b489e.tar.gz"
[ -f "$NEON_TAR" ] || curl -fL --retry 3 -o "$NEON_TAR" "$NEON_URL"
check "$NEON_TAR" "$NEON_SHA"
rm -rf "$OUT/prefetch/neon2sse" && mkdir -p "$OUT/prefetch/neon2sse"
tar -xzf "$NEON_TAR" -C "$OUT/prefetch/neon2sse" --strip-components=1

echo
echo "Dependencies ready in $OUT. Next: Linux/scripts/build-stage1.sh"
