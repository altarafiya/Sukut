"""Render the approved native monogram into Android and Windows system icons.

The SVG is the source of truth. Requires Pillow; does not alter launcher artwork.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/branding/sukut-monogram.svg"
PATH = ET.parse(SOURCE).getroot().find("{http://www.w3.org/2000/svg}path").get("d")
TOKENS = re.findall(r"[MCLZ]|-?\d+(?:\.\d+)?", PATH)


def outline():
    points, index, current = [], 0, (0.0, 0.0)
    while index < len(TOKENS):
        command = TOKENS[index]
        index += 1
        if command == "Z":
            break
        count = 6 if command == "C" else 2
        values = [float(v) for v in TOKENS[index:index + count]]
        index += count
        if command in ("M", "L"):
            current = tuple(values)
            points.append(current)
        elif command == "C":
            start = current
            a, b, end = tuple(values[:2]), tuple(values[2:4]), tuple(values[4:6])
            for step in range(1, 97):
                t = step / 96
                u = 1 - t
                points.append(tuple(u**3 * start[k] + 3*u*u*t*a[k]
                                    + 3*u*t*t*b[k] + t**3*end[k] for k in (0, 1)))
            current = end
        else:
            raise ValueError(f"Unsupported source path command: {command}")
    return points


POINTS = outline()


def render(size, color):
    scale = size * 8 / 24
    mask = Image.new("L", (size * 8, size * 8))
    ImageDraw.Draw(mask).polygon([(x * scale, y * scale) for x, y in POINTS], fill=255)
    mask = mask.resize((size, size), Image.Resampling.LANCZOS)
    result = Image.new("RGBA", (size, size), color)
    result.putalpha(mask)
    return result


def main():
    drawable = ROOT / "app/src/main/res/drawable/ic_sukut_tile.xml"
    drawable.write_bytes((
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
        '    android:width="24dp" android:height="24dp"\n'
        '    android:viewportWidth="24" android:viewportHeight="24"\n'
        '    android:autoMirrored="false">\n'
        f'    <path android:fillColor="#FFFFFFFF" android:pathData="{PATH}" />\n'
        '</vector>\n'
    ).encode())
    assets = ROOT / "Windows/src/sukut-ui/Assets"
    variants = {"on-dark": "#E8C98A", "off-dark": "#FFFFFF",
                "on-light": "#70502C", "off-light": "#303030"}
    sizes = [(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)]
    for name, color in variants.items():
        image = render(256, color)
        image.save(assets / f"Tray-{name}.png")
        # All frames are genuine alpha icons, with no app-tile background.
        image.save(assets / f"Tray-{name}.ico", sizes=sizes)
        (assets / f"Tray-{name}.svg").write_bytes((
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
            f'<path fill="{color}" d="{PATH}"/></svg>\n').encode())
    # A visual review sheet is documentation, never an application runtime asset.
    evidence = ROOT / "docs/release-0.2.29-0.3.28"
    evidence.mkdir(parents=True, exist_ok=True)
    preview = Image.new("RGB", (960, 360), "#161719")
    draw = ImageDraw.Draw(preview)
    draw.text((32, 22), "Sukut: selected Arabic monogram / system icon previews", fill="#FFFFFF", font_size=23)
    for column, (name, color) in enumerate(variants.items()):
        x = 24 + column * 234
        background = "#222326" if name.endswith("dark") else "#F5F5F5"
        draw.rounded_rectangle((x, 72, x+210, 325), 16, fill=background)
        preview.paste(render(156, color), (x+27, 85), render(156, color))
        draw.text((x+18, 236), name, fill=color, font_size=18)
        for n, offset in ((16, 18), (24, 66), (32, 126)):
            image = render(n, color)
            preview.paste(image, (x+offset, 277), image)
    preview.save(evidence / "system-icons-preview.png")
    print("Android vector and four multi-size Windows tray icons generated.")
    print(evidence / "system-icons-preview.png")


if __name__ == "__main__":
    main()
