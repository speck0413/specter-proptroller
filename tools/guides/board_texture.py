#!/usr/bin/env python3
"""Compose the board's top-side texture for the wiring guides from KiCad layer PNGs.

Usage: board_texture.py <dir with F.Cu.png F.Mask.png F.Silkscreen.png Edge.Cuts.png> <out.webp>
Each PNG is a black-on-white plot of one layer (with the board outline) at the same size.
"""
import sys
from PIL import Image, ImageChops, ImageFilter

src, out = sys.argv[1], sys.argv[2]
def layer(name):
    im = Image.open(f"{src}/{name}.png").convert("L")
    return ImageChops.invert(im)  # white = drawn

cu, mask, silk, edge = (layer(n) for n in ("F.Cu", "F.Mask", "F.Silkscreen", "Edge.Cuts"))
# The outline is in every plot: remove it from the layers, it is drawn once below.
cu = ImageChops.subtract(cu, edge); mask = ImageChops.subtract(mask, edge); silk = ImageChops.subtract(silk, edge)
W, H = cu.size
base = Image.new("RGB", (W, H), (22, 74, 44))                       # solder mask over bare laminate
base.paste(Image.new("RGB", (W, H), (34, 104, 58)), mask=cu)          # copper under the mask, a lighter green
base.paste(Image.new("RGB", (W, H), (205, 170, 95)), mask=ImageChops.multiply(mask, cu))  # mask openings on copper: gold pads
holes = ImageChops.subtract(mask, cu)                                 # openings with no copper: drill holes
base.paste(Image.new("RGB", (W, H), (25, 25, 25)), mask=holes)
base.paste(Image.new("RGB", (W, H), (245, 245, 240)), mask=silk.filter(ImageFilter.MaxFilter(3)))  # silkscreen, a touch bolder
base = base.resize((1600, round(1600 * H / W)), Image.LANCZOS)
base.save(out, "WEBP", quality=80, method=6)
print(out, base.size)
