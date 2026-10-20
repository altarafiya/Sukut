"""Convert the copied Android vector/background to a multiresolution Windows icon."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent.parent
vector = root / "reference/protected/app/src/main/res/drawable/ic_launcher_foreground.xml"
colors = root / "reference/protected/app/src/main/res/values/colors.xml"
ns = "{http://schemas.android.com/apk/res/android}"
element = ET.parse(vector).getroot()
path = element.find("path")
data = path.attrib[ns + "pathData"]
parts = re.findall(r"([ML])([0-9.]+),([0-9.]+)", data)
if " ".join(f"{op}{x},{y}" for op, x, y in parts) != data:
    raise RuntimeError("Unsupported copied vector path; do not approximate another icon")
background = next(c.text for c in ET.parse(colors).getroot() if c.attrib.get("name") == "launcher_background")
scale = 1024 / float(element.attrib[ns + "viewportWidth"])
points = [(float(x) * scale, float(y) * scale) for _, x, y in parts]
width = float(path.attrib[ns + "strokeWidth"]) * scale
image = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle((0, 0, 1023, 1023), radius=220, fill=background)
draw.line(points, fill="white", width=round(width), joint="curve")
for x, y in points:
    draw.ellipse((x-width/2,y-width/2,x+width/2,y+width/2),fill="white")
image = image.resize((256, 256), Image.Resampling.LANCZOS)
destination = root / "assets/windows"
destination.mkdir(parents=True, exist_ok=True)
image.save(destination / "cleanwave.png")
image.save(destination / "cleanwave.ico", sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
print(f"Converted original wave path and {background} background; ICO sizes=16/24/32/48/64/128/256")
