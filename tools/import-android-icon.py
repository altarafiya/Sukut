"""Import supplied artwork as Android bitmap size variants, without redrawing it."""
from pathlib import Path
import argparse, hashlib, json
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument('source',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=args.source.resolve()
image=Image.open(source).convert('RGBA')
if image.width!=image.height:raise SystemExit('Use square supplied artwork')
dest=root/'app/src/main/res/drawable-nodpi'
for name,size in [('sukut_app_icon',512),('sukut_logo_art',256)]:
    image.resize((size,size),Image.Resampling.LANCZOS).save(dest/(name+'.png'),optimize=True)
print(json.dumps(dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),source_size=image.size,variants=[512,256])))
