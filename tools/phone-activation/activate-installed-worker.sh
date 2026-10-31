#!/system/bin/sh
# Executes only the activation asset of the currently installed Sukut APK.
set -eu
[ "$(id -u)" = 2000 ] || { echo 'Use an authorized ADB shell.' >&2; exit 1; }
user="$(am get-current-user | tr -d '\r')"
case "$user" in ''|*[!0-9]*) echo 'Invalid Android user' >&2; exit 1;; esac
apk="$(pm path --user "$user" com.altarafiyah.sukut | sed -n 's/^package://p' | head -n 1 | tr -d '\r')"
case "$apk" in /data/app/com.altarafiyah.sukut-*/base.apk|/data/app/*/com.altarafiyah.sukut-*/base.apk) ;; *) echo 'Install Sukut and open it on the phone first.' >&2; exit 1;; esac
case "$apk" in *[!a-zA-Z0-9_./=+~\-]*|*/../*) echo 'Invalid installed APK path' >&2; exit 1;; esac
version="$(dumpsys package com.altarafiyah.sukut | sed -n 's/^[[:space:]]*versionCode=\([0-9]*\).*/\1/p' | head -n 1)"
case "$version" in ''|*[!0-9]*) echo 'Cannot read installed Sukut version' >&2; exit 1;; esac
: > "/data/local/tmp/sukut-altarafiyah-worker-u$user.log"
unzip -p "$apk" assets/activate-worker.sh | sh -s -- "$user"
attempt=0
while [ "$attempt" -lt 50 ]; do
 result="$(content call --user "$user" --uri content://com.altarafiyah.sukut.worker --method status 2>/dev/null || true)"
 if printf '%s' "$result" | grep -q 'state=READY' && printf '%s' "$result" | grep -q 'protocol=1' && printf '%s' "$result" | grep -Eq "version=$version[,}]"; then
  echo "Sukut is ready (installed version code $version). Activation does not start audio."
  exit 0
 fi
 attempt=$((attempt+1)); sleep 0.1
done
echo 'No matching READY handshake. Keep Sukut open and try again.' >&2
exit 1
