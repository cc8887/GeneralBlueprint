"""Create the 128 px Unreal Plugin Browser icon for GeneralBlueprint.

Requires Pillow. The large working canvas keeps the graph connections and
letter crisp after downsampling to the size required by Unreal Editor.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Resources" / "Icon128.png"
SCALE = 4
SIZE = 128 * SCALE

image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)

# A restrained dark surface leaves room for the four module colors.
for y in range(SIZE):
    t = y / (SIZE - 1)
    color = (
        round(15 + 12 * t),
        round(29 + 12 * t),
        round(54 + 17 * t),
        255,
    )
    draw.line((0, y, SIZE, y), fill=color)

draw.rounded_rectangle((6, 6, SIZE - 7, SIZE - 7), radius=92, outline=(89, 116, 162, 180), width=7)

colors = {
    "blueprint": (47, 190, 247, 255),
    "animation": (180, 112, 249, 255),
    "material": (244, 169, 74, 255),
    "layout": (75, 222, 190, 255),
}

connections = [
    ((152, 152), (210, 210), colors["blueprint"]),
    ((360, 152), (302, 210), colors["animation"]),
    ((152, 360), (210, 302), colors["material"]),
    ((360, 360), (302, 302), colors["layout"]),
]
for start, end, color in connections:
    draw.line((*start, *end), fill=(*color[:3], 190), width=23)

nodes = [
    (107, 107, colors["blueprint"]),
    (315, 107, colors["animation"]),
    (107, 315, colors["material"]),
    (315, 315, colors["layout"]),
]
for x, y, color in nodes:
    draw.rounded_rectangle((x, y, x + 90, y + 90), radius=23, fill=(25, 41, 69, 255), outline=color, width=12)
    draw.ellipse((x + 32, y + 32, x + 58, y + 58), fill=color)

draw.rounded_rectangle((179, 179, 333, 333), radius=37, fill=(31, 54, 90, 255), outline=(225, 237, 255, 255), width=10)
font_path = Path("C:/Windows/Fonts/segoeuib.ttf")
if not font_path.is_file():
    font_path = Path("C:/Windows/Fonts/arialbd.ttf")
font = ImageFont.truetype(str(font_path), 116)
text_box = draw.textbbox((0, 0), "G", font=font)
text_width = text_box[2] - text_box[0]
text_height = text_box[3] - text_box[1]
draw.text(
    (256 - text_width / 2 - text_box[0], 256 - text_height / 2 - text_box[1] - 2),
    "G",
    font=font,
    fill=(245, 249, 255, 255),
)

OUT.parent.mkdir(parents=True, exist_ok=True)
image.resize((128, 128), Image.Resampling.LANCZOS).save(OUT, optimize=True)
print(OUT)
