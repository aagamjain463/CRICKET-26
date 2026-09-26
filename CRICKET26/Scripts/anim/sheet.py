#!/usr/bin/env python3
# Tiles captured frames into one contact sheet, to inspect a stroke frame by frame.
# Usage: sheet.py out.png first last step [crop x0,y0,x1,y1] [glob]   (frames Saved/Screenshots/*/Ball1_NNN.png)
import glob, sys
from PIL import Image, ImageDraw
out, first, last, step = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
crop = tuple(int(v) for v in sys.argv[5].split(',')) if len(sys.argv) > 5 else None
pat = sys.argv[6] if len(sys.argv) > 6 else 'Saved/Screenshots/*/Ball1_%03d.png'
tiles = []
for i in range(first, last + 1, step):
    f = glob.glob(pat % i)
    if not f: continue
    im = Image.open(f[0]).convert('RGB')
    if crop: im = im.crop(crop)
    ImageDraw.Draw(im).text((4, 4), str(i), fill=(255, 255, 0))
    tiles.append(im)
if not tiles: sys.exit('no frames')
cols = min(6, len(tiles)); rows = (len(tiles) + cols - 1) // cols
w, h = tiles[0].size
sheet = Image.new('RGB', (cols * w, rows * h))
for k, t in enumerate(tiles): sheet.paste(t, ((k % cols) * w, (k // cols) * h))
sheet.save(out)
print(out, len(tiles), sheet.size)
