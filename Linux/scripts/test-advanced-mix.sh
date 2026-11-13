#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${SUKUT_ADVANCED_TEST_DIR:-"$ROOT/work/advanced-mix/native-tests"}
mkdir -p "$WORK"
c++ -std=c++20 -I"$ROOT/Windows/src" -I"$ROOT/Linux/src" "$ROOT/Linux/tests/advanced_mix_policy_test.cpp" -o "$WORK/advanced-mix-policy-test"
"$WORK/advanced-mix-policy-test"
