#!/bin/sh
# Stage 8, part 2: install, use, upgrade, remove and purge the .deb, in three phases.
#   wsl -d Debian -u root --exec sh Linux/scripts/test-stage8-install.sh install
#   wsl -d Debian --exec sh Linux/scripts/test-stage8-install.sh user      (your normal session)
#   wsl -d Debian -u root --exec sh Linux/scripts/test-stage8-install.sh finish
# The user phase runs the installed window (headless, keyboard check) and the
# installed engine (Music Filter on, then off) inside your login session, which
# a root-only WSL start doesn't have. The finish phase ends with Sukut purged.
set -u

PHASE=${1:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(dpkg-parsechangelog -l "$ROOT/Linux/debian/changelog" -SVersion)
DEB="$ROOT/Linux/dist/sukut_${VERSION}_amd64.deb"
U=$(getent passwd 1000 | cut -d: -f1); HOMEDIR=$(getent passwd "$U" | cut -d: -f6)
EVIDENCE="$ROOT/Linux/evidence/stage8-$(date +%Y-%m-%d)"
WORK="$HOMEDIR/sukut-stage8-install"  # not /tmp: WSL clears it
LOG="$EVIDENCE/install-log.txt"
pass=0; fail=0
check() { if [ "$2" = 0 ]; then pass=$((pass+1)); echo "$1=PASS $3" | tee -a "$LOG"; else fail=$((fail+1)); echo "$1=FAIL $3" | tee -a "$LOG"; fi; }
status() { dpkg-query -W -f '${db:Status-Abbrev}${Version}' sukut 2>/dev/null; }
export DEBIAN_FRONTEND=noninteractive

case "$PHASE" in
install)
    rm -rf "$WORK"; mkdir -p "$WORK" "$EVIDENCE"; chown "$U" "$WORK"; : > "$LOG"
    dpkg-query -W sukut > /dev/null 2>&1; check I_not_installed_before "$([ $? -ne 0 ]; echo $?)" "$(status)"
    # Never the Recommends: the AppIndicator extension would pull in all of GNOME Shell.
    # Its dependencies are already present from setup-debian.sh: only sukut itself is installed.
    planned=$(apt-get -s install --no-install-recommends "$DEB" 2>/dev/null | grep -c '^Inst ')
    if [ "$planned" != 1 ]; then echo "apt would install $planned packages, not only sukut: stopping" | tee -a "$LOG"; exit 1; fi
    apt-get install -y --no-install-recommends "$DEB" > "$WORK/apt-install.log" 2>&1
    check I_install $? "$(status)"
    missing=$(ldd /usr/bin/sukut /usr/libexec/sukut/sukut-host 2>&1 | grep -cE 'not found|No such file')
    check I_libraries_resolve "$([ "$missing" = 0 ]; echo $?)" "missing=$missing"
    ;;
user)
    # The installed window finds its fonts, icons and data in /usr/share/sukut.
    gtk4-broadwayd :12 > "$WORK/broadway.log" 2>&1 &
    BROADWAY=$!; sleep 1
    GDK_BACKEND=broadway BROADWAY_DISPLAY=:12 dbus-run-session -- /usr/bin/sukut --capture "$WORK/window" --capture-set keyboard \
        > "$WORK/window.log" 2>&1
    kill $BROADWAY 2>/dev/null
    results="$WORK/window/capture-results.txt"
    window_ok=1; [ -s "$results" ] && ! grep -q FAIL "$results" && window_ok=0
    check I_installed_window "$window_ok" "$(tr '\n' ' ' < "$results" 2>/dev/null)"
    # The installed host runs the installed sealed engine.
    dbus-run-session -- python3 -I "$ROOT/Linux/tests/stage8_engine_check.py" /usr/libexec/sukut/sukut-host "$WORK/engine" > "$WORK/engine.log" 2>&1
    check I_installed_engine $? "$(grep -E '=(PASS|FAIL)' "$WORK/engine.log" | tr '\n' ' ')"
    /usr/bin/sukut --restore-audio > "$WORK/restore.log" 2>&1
    check I_restore_audio $? "$(cat "$WORK/restore.log")"
    ;;
finish)
    # Upgrade to the same contents with a newer Debian revision.
    rm -rf "$WORK/repack"; dpkg-deb -R "$DEB" "$WORK/repack"
    sed -i "s/^Version: .*/Version: ${VERSION%-*}-2/" "$WORK/repack/DEBIAN/control"
    dpkg-deb --root-owner-group -b "$WORK/repack" "$WORK/sukut-upgrade.deb" > /dev/null
    apt-get install -y --no-install-recommends "$WORK/sukut-upgrade.deb" > "$WORK/apt-upgrade.log" 2>&1
    check I_upgrade "$([ "$(status)" = "ii ${VERSION%-*}-2" ]; echo $?)" "$(status)"
    apt-get remove -y sukut > "$WORK/apt-remove.log" 2>&1
    left=$(ls /usr/bin/sukut /usr/libexec/sukut /usr/share/sukut 2>/dev/null | wc -l)
    check I_remove "$([ "$left" = 0 ]; echo $?)" "status=$(status) leftover=$left"
    dpkg --purge sukut > "$WORK/purge.log" 2>&1
    left=$(ls /usr/share/doc/sukut /usr/share/applications/io.github.altarafiya.Sukut.desktop /usr/share/metainfo/io.github.altarafiya.Sukut.metainfo.xml 2>/dev/null | wc -l)
    dpkg-query -W sukut > /dev/null 2>&1; gone=$?
    check I_purge "$([ "$left" = 0 ] && [ $gone -ne 0 ]; echo $?)" "leftover=$left"
    for f in apt-install apt-upgrade apt-remove purge engine window restore; do
        [ -e "$WORK/$f.log" ] && cp "$WORK/$f.log" "$EVIDENCE/install-$f.log"
    done
    total_fail=$(grep -c '=FAIL' "$LOG"); total=$(grep -cE '=(PASS|FAIL)' "$LOG")
    echo "RESULT=$([ "$total_fail" = 0 ] && echo PASS || echo FAIL) $((total-total_fail))/$total" | tee -a "$LOG"
    [ "$total_fail" = 0 ]; exit $?
    ;;
*) echo "usage: $0 install|user|finish" >&2; exit 2 ;;
esac
[ $fail = 0 ]
