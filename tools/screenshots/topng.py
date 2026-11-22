# Convert 128x64 PBM frames into PNGs that look like the OLED: lit pixels with
# a small gap between them on a black panel.
import sys, pathlib
from PIL import Image, ImageDraw
src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
S, PAD = 4, 12
for p in sorted(src.glob("*.pbm")):
    rows = p.read_text().split("\n")[2:2 + 64]
    img = Image.new("RGB", (128 * S + 2 * PAD, 64 * S + 2 * PAD), (8, 10, 16))
    d = ImageDraw.Draw(img)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "1":
                x0, y0 = PAD + x * S, PAD + y * S
                d.rectangle([x0, y0, x0 + S - 2, y0 + S - 2], fill=(214, 236, 255))
    img.save(dst / (p.stem + ".png"), optimize=True)
    print("wrote", dst / (p.stem + ".png"))
