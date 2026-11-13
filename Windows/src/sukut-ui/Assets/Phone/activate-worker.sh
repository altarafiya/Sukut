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
# An uninstall/reinstall can change the app UID while an old detached worker
# retains its lock. Retire only this user's shell worker from a removed APK.
# Never interrupt a worker belonging to the current or another readable APK.
worker_name="sukut_altarafiyah_audio_u$user"
for old_pid in $(pidof "$worker_name" 2>/dev/null || true); do
 case "$old_pid" in ''|*[!0-9]*) continue;; esac
 old_uid="$(sed -n 's/^Uid:[[:space:]]*\([0-9]*\).*/\1/p' "/proc/$old_pid/status" 2>/dev/null || true)"
 old_name="$(tr '\000' '\n' < "/proc/$old_pid/cmdline" 2>/dev/null | head -n 1)"
 [ "$old_uid" = 2000 ] && [ "$old_name" = "$worker_name" ] || continue
 old_apk="$(tr '\000' '\n' < "/proc/$old_pid/environ" 2>/dev/null | sed -n 's/^CLASSPATH=//p')"
 case "$old_apk" in /data/app/com.altarafiyah.sukut-*/base.apk|/data/app/*/com.altarafiyah.sukut-*/base.apk) ;; *) continue;; esac
 if [ "$old_apk" != "$apk" ] && [ ! -r "$old_apk" ]; then
  kill -TERM "$old_pid" 2>/dev/null || true
  remaining=30
  while [ -d "/proc/$old_pid" ] && [ "$remaining" -gt 0 ]; do sleep 0.1; remaining=$((remaining-1)); done
 fi
done
CLASSPATH="$apk" nohup setsid /system/bin/app_process64 -Djava.library.path="$lib" /system/bin --nice-name="sukut_altarafiyah_audio_u$user" com.osama.cleanwave.SukutWorker "$user" </dev/null >"/data/local/tmp/sukut-altarafiyah-worker-u$user.log" 2>&1 &
echo "SUKUT_WORKER_LAUNCHED user=$user"
