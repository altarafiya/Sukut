#!/usr/bin/env bash
# Linux/macOS helper. No download, install, pairing or arbitrary shell input.
set -euo pipefail
adb_command=(adb)
if [[ -n "${1:-}" ]]; then adb_command+=(-s "$1"); fi
if ! "${adb_command[@]}" get-state >/dev/null; then
 echo 'Connect an authorized USB phone. If multiple transports appear, pass its adb serial as argument.' >&2
 exit 1
fi
helper_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
"${adb_command[@]}" shell -T sh -s < "$helper_dir/activate-installed-worker.sh"
