#!/system/bin/sh
# Start only the installed, signed Sukut APK through an authorised ADB session.
set -eu
[ "$(id -u)" = 2000 ] || { echo "Use an authorised ADB shell." >&2; exit 1; }
user="${1:-$(am get-current-user)}"
case "$user" in ''|*[!0-9]*) echo "Invalid Android user" >&2; exit 1;; esac
apk="$(pm path --user "$user" com.altarafiyah.sukut | sed -n 's/^package://p' | head -n 1 | tr -d '\r')"
case "$apk" in /data/app/com.altarafiyah.sukut-*/base.apk|/data/app/*/com.altarafiyah.sukut-*/base.apk) ;; *) echo "Install Sukut first." >&2; exit 1;; esac
case "$apk" in *[!a-zA-Z0-9_./=+~\-]*|*/../*) echo "Invalid APK path" >&2; exit 1;; esac
info="$(dumpsys package com.altarafiyah.sukut)"
lib="$(printf '%s\n' "$info" | sed -n 's/^[[:space:]]*nativeLibraryDir=//p' | head -n 1 | tr -d '\r')"
if [ -z "$lib" ]; then
 lib="$(printf '%s\n' "$info" | sed -n 's/^[[:space:]]*legacyNativeLibraryDir=//p' | head -n 1 | tr -d '\r')/arm64"
fi
case "$lib" in /data/app/com.altarafiyah.sukut-*/lib/arm64|/data/app/*/com.altarafiyah.sukut-*/lib/arm64) ;; *) echo "Sukut requires its installed ARM64 native libraries." >&2; exit 1;; esac
case "$lib" in *[!a-zA-Z0-9_./=+~\-]*|*/../*) echo "Invalid library path" >&2; exit 1;; esac
[ -r "$apk" ] && [ -d "$lib" ] || { echo "Installed APK or libraries unavailable." >&2; exit 1; }
CLASSPATH="$apk" nohup setsid /system/bin/app_process64 -Djava.library.path="$lib" /system/bin --nice-name="sukut_altarafiyah_audio_u$user" com.osama.cleanwave.SukutWorker "$user" </dev/null >"/data/local/tmp/sukut-altarafiyah-worker-u$user.log" 2>&1 &
echo "SUKUT_WORKER_LAUNCHED user=$user"
