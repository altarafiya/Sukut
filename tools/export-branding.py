"""Encode Sukut's app icons (0.5.2) into native platform resources.

App icons: one of five marks (assets/branding/hand-icon-design-2026-11-13/0N-*.png)
on a rounded square whose background colour the user picks. Default: mark 05
(white and blue) on black. Small system icons (tray, Android Quick Settings tile,
notification) keep the original س from assets/branding/system-glyph/, copied unchanged.

The ids and colours below are mirrored in AppIcon.kt (Android), AppIcons.cs
(Windows) and AppIcon.h (Linux); keep them in the same order.
"""
from pathlib import Path
import hashlib, json, shutil
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / 'assets/branding'
MARKS_DIR = BRAND / 'hand-icon-design-2026-11-13'
GLYPH = BRAND / 'system-glyph'
LANCZOS = Image.Resampling.LANCZOS

MARKS = [('soft', '01-Sukut-Seen-Soft-Transparent.png'), ('wave', '02-Sukut-Seen-Wave-Transparent.png'),
         ('wordmark', '03-Sukut-Wordmark-Transparent.png'), ('gold', '04-Sukut-Original-Gold-Transparent.png'),
         ('blue', '05-Sukut-White-Blue-Transparent.png')]
COLORS = [('black', '#000000'), ('graphite', '#2B2E33'), ('navy', '#0E2238'),
          ('forest', '#0F3B2E'), ('burgundy', '#4A1420'), ('plum', '#2E1A47')]
DEFAULT = ('blue', 'black')
RADIUS = 0.225   # corner radius / side: the rounded square on every platform
MARK_BOX = 0.72  # the mark fits a centred box this share of the side

marks = {}
for key, name in MARKS:
    image = Image.open(MARKS_DIR / name).convert('RGBA')
    marks[key] = image.crop(image.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())

def fit(mark, box):
    scale = min(box / mark.width, box / mark.height)
    return mark.resize((max(1, round(mark.width * scale)), max(1, round(mark.height * scale))), LANCZOS)

def tile(key, color, size):
    """The rounded-square icon, drawn at 4x and reduced for smooth corners."""
    big = size * 4
    canvas = Image.new('RGBA', (big, big))
    shape = Image.new('L', (big, big))
    ImageDraw.Draw(shape).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(big * RADIUS), fill=255)
    canvas.paste(Image.new('RGBA', (big, big), color), (0, 0), shape)
    art = fit(marks[key], round(big * MARK_BOX))
    canvas.alpha_composite(art, ((big - art.width) // 2, (big - art.height) // 2))
    return canvas.resize((size, size), LANCZOS)

def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

DEFAULT_HEX = dict(COLORS)[DEFAULT[1]]
default_tile = tile(DEFAULT[0], DEFAULT_HEX, 1024)
default_tile.save(BRAND / 'sukut-app-icon.png', optimize=True)

# Android -------------------------------------------------------------------
res = ROOT / 'app/src/main/res'
nodpi, drawable = res / 'drawable-nodpi', res / 'drawable'
for old in ['sukut_app_icon.png', 'sukut_logo_art.png', 'sukut_launcher_mark.png', 'sukut_launcher_mono.png']:
    (nodpi / old).unlink(missing_ok=True)
for old in ['sukut_launcher_background.xml', 'sukut_launcher_foreground.xml', 'sukut_launcher_monochrome.xml', 'sukut_logo.xml']:
    (drawable / old).unlink(missing_ok=True)
# Adaptive foreground: 108 dp at 4x. The launcher shows the middle 72 dp; the mark
# fits a 52 dp box (72 % of the visible icon, like the desktop tiles), inside the
# 66 dp safe zone. The launcher masks the layer to its own shape.
layer = 432
for key, _ in MARKS:
    art = fit(marks[key], 52 * 4)
    fg = Image.new('RGBA', (layer, layer))
    fg.alpha_composite(art, ((layer - art.width) // 2, (layer - art.height) // 2))
    fg.save(nodpi / f'icon_mark_{key}.png', optimize=True)
    mono = Image.new('RGBA', fg.size, (255, 255, 255, 255))
    mono.putalpha(fg.getchannel('A'))
    mono.save(nodpi / f'icon_mono_{key}.png', optimize=True)
    for kind in ('mark', 'mono'):
        write_if_changed(drawable / f'icon_{kind}_{key}_layer.xml',
            f'<bitmap xmlns:android="http://schemas.android.com/apk/res/android" android:src="@drawable/icon_{kind}_{key}" android:gravity="fill" android:filter="true"/>\n')
write_if_changed(res / 'values/icon_colors.xml', '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n' +
    ''.join(f'    <color name="icon_bg_{c}">{v}</color>\n' for c, v in COLORS) + '</resources>\n')
for folder in ['mipmap-anydpi', 'mipmap-anydpi-v33']:
    target = res / folder
    for old in target.glob('ic_launcher*.xml'):
        old.unlink()
    for key, _ in MARKS:
        for color, _ in COLORS:
            mono = f'\n    <monochrome android:drawable="@drawable/icon_mono_{key}_layer" />' if folder.endswith('v33') else ''
            xml = ('<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                   f'    <background android:drawable="@color/icon_bg_{color}" />\n'
                   f'    <foreground android:drawable="@drawable/icon_mark_{key}_layer" />{mono}\n</adaptive-icon>\n')
            write_if_changed(target / f'ic_launcher_{key}_{color}.xml', xml)
            if (key, color) == DEFAULT:
                write_if_changed(target / 'ic_launcher.xml', xml)
shutil.copy2(GLYPH / 'android/ic_sukut_tile.xml', drawable / 'ic_sukut_tile.xml')

# Windows -------------------------------------------------------------------
assets = ROOT / 'Windows/src/sukut-ui/Assets'
ico_sizes = [(n, n) for n in [16, 20, 24, 32, 40, 48, 64, 128, 256]]
default_tile.resize((256, 256), LANCZOS).save(assets / 'Sukut.png')
default_tile.save(assets / 'Sukut.ico', sizes=ico_sizes)
legacy = ROOT / 'Windows/assets/windows'  # the diagnostic desktop executable embeds these
shutil.copy2(assets / 'Sukut.png', legacy / 'cleanwave.png')
shutil.copy2(assets / 'Sukut.ico', legacy / 'cleanwave.ico')
choices = assets / 'AppIcons'
choices.mkdir(exist_ok=True)
for key, _ in MARKS:
    for color, value in COLORS:
        tile(key, value, 256).save(choices / f'{key}-{color}.ico', sizes=[(n, n) for n in [16, 24, 32, 48, 64, 256]])
    tile(key, '#000000', 128).save(choices / f'preview-{key}.png', optimize=True)
for source in sorted((GLYPH / 'windows').glob('Tray-*')):
    shutil.copy2(source, assets / source.name)

# Linux ---------------------------------------------------------------------
icons = ROOT / 'Linux/data/icons/hicolor'
for size in [16, 22, 24, 32, 48, 64, 128, 256, 512]:
    dest = icons / f'{size}x{size}/apps/io.github.altarafiya.Sukut.png'
    dest.parent.mkdir(parents=True, exist_ok=True)
    tile(DEFAULT[0], DEFAULT_HEX, size).save(dest, optimize=True)
marks_out = ROOT / 'Linux/data/icons/marks'
marks_out.mkdir(parents=True, exist_ok=True)
for key, _ in MARKS:  # the window draws the chosen tile from these at runtime
    fit(marks[key], 512).save(marks_out / f'{key}.png', optimize=True)
for source in sorted((GLYPH / 'linux').glob('*.svg')):
    shutil.copy2(source, icons / 'scalable/status' / source.name)

print(json.dumps({'default': DEFAULT, 'master_sha256': hashlib.sha256((BRAND / 'sukut-app-icon.png').read_bytes()).hexdigest(),
                  'marks': [k for k, _ in MARKS], 'colors': [c for c, _ in COLORS]}))
