"""Encode Sukut 0.5.1 branding into native platform resources.

App icons (launcher, window, EXE, Store, Linux menu) use the hand:
  assets/branding/sukut-app-icon.png   full tile, transparent corners
  assets/branding/hand-icon-design-2026-11-13/Sukut-Hand-Android-Transparent.png
                                       hand alone, for Android's adaptive foreground
Small system icons (tray, Quick Settings tile, notification) keep the original
س from assets/branding/system-glyph/, copied unchanged.
No creative redraw: artwork is only cropped, resized and masked.
"""
from pathlib import Path
import hashlib, json, shutil
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / 'assets/branding'
MASTER = BRAND / 'sukut-app-icon.png'
HAND = BRAND / 'hand-icon-design-2026-11-13/Sukut-Hand-Android-Transparent.png'
GLYPH = BRAND / 'system-glyph'
LANCZOS = Image.Resampling.LANCZOS

tile = Image.open(MASTER).convert('RGBA')
hand = Image.open(HAND).convert('RGBA')
hand = hand.crop(hand.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())

def sized(image, size):
    return image.resize((size, size), LANCZOS)

# Android -------------------------------------------------------------------
res = ROOT / 'app/src/main/res'
nodpi = res / 'drawable-nodpi'
sized(tile, 512).save(nodpi / 'sukut_app_icon.png', optimize=True)
sized(tile, 256).save(nodpi / 'sukut_logo_art.png', optimize=True)

# Adaptive foreground: 108 dp layer at 4x. The launcher shows the middle 72 dp
# and masks to its own shape; the hand is 46 dp tall, inside the 66 dp safe zone,
# about the share of the tile it fills in the master.
layer = 432
scale = 46 * 4 / hand.height
art = hand.resize((round(hand.width * scale), round(hand.height * scale)), LANCZOS)
foreground = Image.new('RGBA', (layer, layer))
foreground.alpha_composite(art, ((layer - art.width) // 2, (layer - art.height) // 2))
foreground.save(nodpi / 'sukut_launcher_mark.png', optimize=True)
monochrome = Image.new('RGBA', foreground.size, (255, 255, 255, 255))
monochrome.putalpha(foreground.getchannel('A'))
monochrome.save(nodpi / 'sukut_launcher_mono.png', optimize=True)

drawable = res / 'drawable'
# The master tile's own graphite, top to bottom.
(drawable / 'sukut_launcher_background.xml').write_text(
    '<shape xmlns:android="http://schemas.android.com/apk/res/android">'
    '<gradient android:angle="270" android:startColor="#FF33363B" android:endColor="#FF1E2125"/></shape>\n')
(drawable / 'sukut_launcher_foreground.xml').write_text(
    '<bitmap xmlns:android="http://schemas.android.com/apk/res/android" '
    'android:src="@drawable/sukut_launcher_mark" android:gravity="fill" android:filter="true"/>\n')
(drawable / 'sukut_launcher_monochrome.xml').write_text(
    '<bitmap xmlns:android="http://schemas.android.com/apk/res/android" '
    'android:src="@drawable/sukut_launcher_mono" android:gravity="fill" android:filter="true"/>\n')
shutil.copy2(GLYPH / 'android/ic_sukut_tile.xml', drawable / 'ic_sukut_tile.xml')

# Windows -------------------------------------------------------------------
assets = ROOT / 'Windows/src/sukut-ui/Assets'
ico_sizes = [(n, n) for n in [16, 20, 24, 32, 40, 48, 64, 128, 256]]
sized(tile, 256).save(assets / 'Sukut.png')
tile.save(assets / 'Sukut.ico', sizes=ico_sizes)
(assets / 'Sukut.svg').write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><image href="Sukut.png" width="256" height="256"/></svg>\n')
legacy = ROOT / 'Windows/assets/windows'  # the diagnostic desktop executable embeds these
shutil.copy2(assets / 'Sukut.png', legacy / 'cleanwave.png')
shutil.copy2(assets / 'Sukut.ico', legacy / 'cleanwave.ico')
for source in sorted((GLYPH / 'windows').glob('Tray-*')):
    shutil.copy2(source, assets / source.name)

# Linux ---------------------------------------------------------------------
icons = ROOT / 'Linux/data/icons/hicolor'
for size in [16, 22, 24, 32, 48, 64, 128, 256, 512]:
    dest = icons / f'{size}x{size}/apps/io.github.altarafiya.Sukut.png'
    dest.parent.mkdir(parents=True, exist_ok=True)
    sized(tile, size).save(dest, optimize=True)
for source in sorted((GLYPH / 'linux').glob('*.svg')):
    shutil.copy2(source, icons / 'scalable/status' / source.name)

print(json.dumps({
    'master_sha256': hashlib.sha256(MASTER.read_bytes()).hexdigest(),
    'hand_sha256': hashlib.sha256(HAND.read_bytes()).hexdigest(),
    'master_size': tile.size,
    'launcher_hand_px': art.size,
}))
