#!/bin/bash
# Upload a built Sukut snap to the Snap Store and release it to a channel.
# Run inside the Ubuntu build VM (it has no keyring, so the login is exported
# to a temporary file and removed afterwards). You sign in yourself when asked.
#   bash update-snap-store.sh [SNAP_FILE] [CHANNEL]
#   CHANNEL: beta (default), candidate or stable.
# Example: bash update-snap-store.sh ~/sukut_0.5.1_amd64.snap stable
set -u
SNAP_FILE=${1:-$(ls -t "$HOME"/sukut_*_amd64.snap 2>/dev/null | head -n 1)}
CHANNEL=${2:-beta}
case "$CHANNEL" in beta|candidate|stable|edge) ;; *) echo "Unknown channel: $CHANNEL" >&2; exit 2;; esac
[ -f "$SNAP_FILE" ] || { echo "No snap file found: $SNAP_FILE" >&2; exit 2; }
echo "Uploading $SNAP_FILE to the '$CHANNEL' channel."
sha256sum "$SNAP_FILE"
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
snapcraft export-login --snaps=sukut --acls=package_access,package_push,package_update,package_release "$D/login" || exit 1
export SNAPCRAFT_STORE_CREDENTIALS="$(cat "$D/login")"
snapcraft upload --release="$CHANNEL" "$SNAP_FILE"; R=$?
snapcraft status sukut
exit $R
