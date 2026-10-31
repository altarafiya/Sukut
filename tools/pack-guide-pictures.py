"""Encode the reviewed, redacted guide captures as lossless WebP for the APK.

No pixel editing/redaction occurs here. Full-resolution reviewed PNGs stay in docs.
"""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root / "docs/beta-0.4/guide-beta16"
target = root / "app/src/main/assets/setup-guide"
target.mkdir(exist_ok=True)
for name in ("developer", "wireless", "pairing", "notification"):
    with Image.open(source / f"{name}.png") as image:
        image.save(target / f"{name}.webp", lossless=True, method=6)
        with Image.open(target / f"{name}.webp") as check:
            assert image.convert("RGBA").tobytes() == check.convert("RGBA").tobytes()
    print(f"{name}: {(target / f'{name}.webp').stat().st_size} bytes; pixels identical")
