#!/bin/sh
# The snap can't see the desktop's icon theme (KDE's Breeze, for example), and
# GTK then falls back to hicolor only. Copy the standard symbolic icons the
# window names from Adwaita into the snap's own hicolor theme.
#   bundle-icons.sh SOURCE_DIR INSTALL_DIR
set -eu
SRC=$1 DEST=$2/usr/share/icons/hicolor/scalable
missing=0
for name in $(grep -ohE '"[a-z0-9-]+-symbolic"' "$SRC"/Linux/src/window/*.cpp "$SRC"/Linux/src/window/*.h | tr -d '"' | grep -v '^io\.github' | sort -u); do
    file=$(ls /usr/share/icons/Adwaita/symbolic/*/"$name.svg" 2>/dev/null | head -n1 || true)
    # Names Adwaita no longer has: use its closest icon.
    [ -n "$file" ] || [ "$name" != emblem-ok-symbolic ] || file=$(ls /usr/share/icons/Adwaita/symbolic/*/object-select-symbolic.svg 2>/dev/null | head -n1 || true)
    if [ -z "$file" ]; then echo "bundle-icons: not in Adwaita: $name" >&2; missing=$((missing + 1)); continue; fi
    category=$(basename "$(dirname "$file")")
    install -Dm644 "$file" "$DEST/$category/$name.svg"
done
echo "bundle-icons: done, $missing not found"
