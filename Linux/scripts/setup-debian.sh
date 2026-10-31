#!/bin/sh
# Debian 13 build tools for Sukut. Run once, as your normal user (it uses sudo).
# Stage 0: compiler and tools. Stage 2: PipeWire. Stage 5: GLib/GIO, python3-gi
# (tests) and RealtimeKit. Stage 6: GTK 4 and libadwaita, and libgtk-4-bin for
# the headless (Broadway) display the screenshot test uses; librsvg2-common
# draws SVG icons (a desktop install always has it, a minimal one may not).
# Stage 7: Debian's adb (with android-udev-rules) for the phone tools.
set -eu

. /etc/os-release
if [ "${ID:-}" != "debian" ] || [ "${VERSION_ID:-}" != "13" ]; then
    echo "Expected Debian 13 (trixie); found ${PRETTY_NAME:-unknown}. Continuing, but results may differ." >&2
fi

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkgconf python3 unzip rsync curl ca-certificates file \
    pipewire pipewire-bin wireplumber libpipewire-0.3-dev libspa-0.2-dev dbus-user-session \
    libglib2.0-dev libglib2.0-bin python3-gi rtkit \
    libgtk-4-dev libadwaita-1-dev libgtk-4-bin librsvg2-common \
    adb

echo
echo "cmake: $(cmake --version | head -n1)"
echo "g++:   $(g++ --version | head -n1)"
echo "Next: Linux/scripts/seed-deps.sh"
