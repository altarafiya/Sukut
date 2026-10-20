"""Encode the refined user artwork into app/tray source resources.

Raster editing/transparent extraction was performed with built-in image_gen.
This script only resizes and encodes those preserved RGBA masters.
"""
from pathlib import Path
import io
import struct
from PIL import Image

root = Path(__file__).resolve().parents[1]
masters = root / 'docs/redesign/icon-assets/provided-calligraphy'
assets = root / 'src/sukut-ui/Assets'

def encode(name, source, sizes):
    frames = []
    for size in sizes:
        resized = source.resize((size, size), Image.Resampling.LANCZOS)
        stream = io.BytesIO()
        resized.save(stream, format='PNG')
        frames.append((size, stream.getvalue()))
    offset = 6 + 16 * len(frames)
    header = struct.pack('<HHH', 0, 1, len(frames))
    body = b''
    for size, data in frames:
        header += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        body += data
        offset += len(data)
    (assets / f'{name}.ico').write_bytes(header + body)
    source.resize((256, 256), Image.Resampling.LANCZOS).save(assets / f'{name}.png')
    # Raster-backed SVG replaces the superseded vector so a named asset never
    # silently depicts the old logo. The runtime consumes the PNG and ICO.
    (assets / f'{name}.svg').write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><image href="{name}.png" width="256" height="256"/></svg>\n', encoding='utf-8')

with Image.open(masters / 'app-master.png') as source:
    encode('Sukut', source.convert('RGBA'), [16,20,24,32,40,48,64,128,256])
with Image.open(masters / 'app-master.png') as source:
    tray = source.convert('RGBA')
    for state in ['off', 'on']:
        for theme in ['light', 'dark']:
            encode(f'Tray-{state}-{theme}', tray, [16,20,24,32,40,48,64])
print('Encoded provided Arabic calligraphy app and transparent tray resources.')
