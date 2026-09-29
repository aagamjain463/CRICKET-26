"""Brand artwork for the CRICKET 26 "Floodlight" shell: graded stadium plates, the metallic logo lockup, team crests,
the OVR badge, the six-ball ring and the gradient tile faces. Everything is drawn from the generated stadium plate
and the Barlow Condensed font already in the project, so no new licences are involved.
Writes PNGs to Content/UI/Source; Scripts/import_ui.py brings them into /Game/UI.
Run: python3 Scripts/ui/make_brand_art.py  (needs Pillow and numpy)"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
OUT = os.path.join(ROOT, "Content", "UI", "Source")
FONT = os.path.join(ROOT, "Content", "UI", "Fonts", "BarlowCondensed-BlackItalic.ttf")
BOLD = os.path.join(ROOT, "Content", "UI", "Fonts", "BarlowCondensed-Bold.ttf")
SHEAR = math.tan(math.radians(16))  # the blade angle every sheared shape in the shell uses

# Brand palette (sRGB).
NIGHT, ROYAL, ELECTRIC = (5, 8, 32), (28, 52, 190), (80, 130, 255)
GOLD, GOLD_HI, GOLD_LO, GOLD_INK = (242, 182, 50), (255, 222, 128), (176, 112, 14), (26, 18, 4)
EMBER, EMBER_LO = (234, 58, 72), (70, 8, 36)


def save(im, name):
    im.save(os.path.join(OUT, f"CRICKET26_{name}.png"), optimize=True)


def arr(im):
    return np.asarray(im).astype(np.float32) / 255.0


def img(a, mode="RGB"):
    return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8), mode)


def ramp(lum, stops):
    """Maps luminance through colour stops [(t, (r, g, b)), ...]."""
    ts = [s[0] for s in stops]
    out = np.zeros(lum.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(lum, ts, [s[1][c] / 255.0 for s in stops])
    return out


def glow(size, centre, radius, colour, strength=1.0):
    """Additive soft light as an RGB float array."""
    h, w = size[1], size[0]
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt((x - centre[0]) ** 2 + (y - centre[1]) ** 2) / radius
    a = np.exp(-d * d * 2.2) * strength
    return a[..., None] * (np.array(colour, np.float32) / 255.0)


def blade_bands(size, bands, blur):
    """Soft light ribbons leaning at the blade angle: [(x at top, width, colour, alpha)]."""
    w, h = size
    layer = Image.new("RGB", size, (0, 0, 0))
    d = ImageDraw.Draw(layer)
    for x0, bw, col, a in bands:
        c = tuple(int(v * a) for v in col)
        d.polygon([(x0, 0), (x0 + bw, 0), (x0 + bw - h * SHEAR, h), (x0 - h * SHEAR, h)], fill=c)
    return arr(layer.filter(ImageFilter.GaussianBlur(blur)))


def grain(a, amount=0.018, seed=26):
    rng = np.random.default_rng(seed)
    return a + rng.normal(0, amount, a.shape[:2])[..., None]


def vignette(a, strength=0.55):
    h, w = a.shape[:2]
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((x - w / 2) / (w / 2)) ** 2 + ((y - h / 2) / (h / 2)) ** 2) / 1.2
    return a * (1 - strength * np.clip(d, 0, 1) ** 2.2)[..., None]


def stadium(w, h, blur):
    """The raw plate cropped above the generator's sparkle watermark (bottom right) and scaled to cover w x h."""
    src = Image.open(os.path.join(OUT, "raw_stadium.png")).convert("RGB").crop((0, 0, 2752, 1220))
    s = max(w / src.width, h / src.height)
    src = src.resize((round(src.width * s), round(src.height * s)), Image.LANCZOS)
    left, top = (src.width - w) // 2, int((src.height - h) * 0.25)
    src = src.crop((left, top, left + w, top + h))
    return (src.filter(ImageFilter.GaussianBlur(blur)) if blur else src), s, left, top


def lights(scale, left, top):
    # Floodlight heads in the 2752-wide raw plate.
    return [(x * scale - left, y * scale - top) for x, y in [(293, 237), (1038, 525), (1713, 525), (2493, 237)]]


def shell_bg():
    """Every hub page sits on this: the stadium thrown out of focus and graded into the brand's night royal,
    floodlights blooming gold, and blade ribbons of light."""
    w, h = 2048, 1024
    st, s, left, top = stadium(w, h, 7)
    a = arr(st)
    lum = a @ np.array([0.3, 0.55, 0.15], np.float32)
    graded = ramp(lum, [(0, NIGHT), (0.25, (14, 22, 92)), (0.5, ROYAL), (0.78, (120, 150, 255)), (1, (255, 236, 190))])
    a = graded * 0.8 + a * 0.2
    a *= 0.78
    for p in lights(s, left, top):
        a += glow((w, h), p, 190, GOLD_HI, 0.55)
    a += glow((w, h), (w * 0.25, h * 0.55), 900, (40, 80, 255), 0.35)
    a += blade_bands((w, h), [(820, 90, GOLD_HI, 0.22), (1060, 220, ELECTRIC, 0.18), (1500, 60, (255, 255, 255), 0.14),
                              (1700, 260, ELECTRIC, 0.12), (420, 140, (120, 160, 255), 0.10)], 28)
    a = vignette(grain(a), 0.5)
    save(img(a), "Shell_Bg")


def vs_bg():
    """Match setup and loading: the ground in focus, split into home royal and away ember, a gold shaft between."""
    w, h = 2048, 1024
    st, s, left, top = stadium(w, h, 2)
    a = arr(st)
    lum = a @ np.array([0.3, 0.55, 0.15], np.float32)
    home = ramp(lum, [(0, NIGHT), (0.4, (18, 40, 150)), (0.75, (90, 140, 255)), (1, (240, 245, 255))])
    away = ramp(lum, [(0, (22, 4, 16)), (0.4, (140, 20, 48)), (0.75, (255, 110, 110)), (1, (255, 240, 230))])
    x = np.linspace(0, 1, w, dtype=np.float32)[None, :, None]
    mix = np.clip((x - 0.4) / 0.2, 0, 1)
    a = (home * (1 - mix) + away * mix) * 0.85 + a * 0.15
    a *= 0.8
    for p in lights(s, left, top):
        a += glow((w, h), p, 160, GOLD_HI, 0.5)
    a += blade_bands((w, h), [(w * 0.5 + 200, 160, GOLD_HI, 0.35), (w * 0.5 + 330, 40, (255, 255, 255), 0.25)], 30)
    a += glow((w, h), (w / 2, h * 0.45), 420, GOLD, 0.35)
    a = vignette(grain(a), 0.6)
    save(img(a), "VS_Bg")


def tile(name, stops, wedge, mark_alpha):
    """A tile face: a diagonal gradient, a lighter blade wedge like the split on a football-game tile, pinstripes,
    a large ghosted "26" and a gloss along the top edge."""
    w, h = 1024, 576
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    t = np.clip((x / w) * 0.6 + (y / h) * 0.4, 0, 1)
    a = ramp(t, stops)
    wedge_mask = ((x - (h - y) * SHEAR) > w * 0.58).astype(np.float32)
    wedge_mask = np.asarray(Image.fromarray((wedge_mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32) / 255
    a = a * (1 - wedge_mask[..., None] * wedge) + wedge_mask[..., None] * wedge * np.array([1, 1, 1], np.float32) * 0.9 * a.max()
    stripes = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(stripes)
    for k in range(-h, w + h, 18):
        d.line([(k, 0), (k - h * SHEAR * 3, h)], fill=255, width=2)
    a += (arr(stripes)[..., None] * 0.025)
    mark = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mark).text((w * 0.52, -h * 0.28), "26", font=ImageFont.truetype(FONT, int(h * 1.35)), fill=255)
    a += arr(mark.filter(ImageFilter.GaussianBlur(1)))[..., None] * mark_alpha
    gloss = np.clip(1 - y / (h * 0.45), 0, 1) ** 2
    a += gloss[..., None] * 0.10
    a = vignette(grain(a, 0.012), 0.35)
    save(img(a), f"Tile_{name}")


def shield_points(w, h, inset=0):
    i = inset
    return [(w * 0.5, i), (w - i, h * 0.1 + i * 0.6), (w - i, h * 0.55), (w * 0.5, h - i), (i, h * 0.55), (i, h * 0.1 + i * 0.6)]


def metal(size, top, bottom, band=0.35):
    """Vertical metallic gradient with a bright specular band, as an (h, w, 3) float array."""
    w, h = size
    y = np.linspace(0, 1, h, dtype=np.float32)[:, None]
    col = np.array(top, np.float32) / 255 * (1 - y) + np.array(bottom, np.float32) / 255 * y
    col = col + (np.exp(-((y - band) / 0.06) ** 2) * 0.35)
    return np.repeat(col[:, None, :], w, axis=1)


def paste_masked(base, rgb, mask):
    layer = img(rgb).convert("RGBA")
    layer.putalpha(mask)
    return Image.alpha_composite(base, layer)


def crest(name, field_top, field_bottom, letter):
    """Team crest: a gold-rimmed shield, a sheared stripe, a cricket ball with its seam and the side's letter."""
    S = 2
    w, h = 460 * S, 520 * S
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    shadow = Image.new("L", (w, h), 0)
    ImageDraw.Draw(shadow).polygon(shield_points(w, h, 10 * S), fill=170)
    out = paste_masked(out, np.zeros((h, w, 3), np.float32), shadow.filter(ImageFilter.GaussianBlur(10 * S)))
    rim = Image.new("L", (w, h), 0)
    ImageDraw.Draw(rim).polygon(shield_points(w, h, 4 * S), fill=255)
    out = paste_masked(out, metal((w, h), GOLD_HI, GOLD_LO, 0.25), rim)
    field = Image.new("L", (w, h), 0)
    ImageDraw.Draw(field).polygon(shield_points(w, h, 26 * S), fill=255)
    out = paste_masked(out, metal((w, h), field_top, field_bottom, 0.2), field)
    stripe = Image.new("L", (w, h), 0)
    ImageDraw.Draw(stripe).polygon([(w * 0.56, 0), (w * 0.7, 0), (w * 0.7 - h * SHEAR, h), (w * 0.56 - h * SHEAR, h)], fill=60)
    out = paste_masked(out, np.ones((h, w, 3), np.float32), ImageChops.multiply(stripe, field))
    d = ImageDraw.Draw(out)
    f = ImageFont.truetype(FONT, int(h * 0.5))
    bb = d.textbbox((0, 0), letter, font=f)
    tx, ty = (w - (bb[2] - bb[0])) / 2 - bb[0], h * 0.44 - (bb[3] - bb[1]) / 2 - bb[1]
    d.text((tx + 6 * S, ty + 8 * S), letter, font=f, fill=(0, 0, 0, 120))
    tmask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(tmask).text((tx, ty), letter, font=f, fill=255)
    out = paste_masked(out, metal((w, h), (255, 255, 255), (200, 210, 230), 0.3), tmask)
    # A cricket ball under the letter: red leather, cream seam and stitches.
    cx, cy, r = w * 0.5, h * 0.74, 44 * S
    d = ImageDraw.Draw(out)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(196, 28, 40, 255), outline=GOLD_HI + (255,), width=4 * S)
    d.arc([cx - r * 0.55, cy - r * 1.3, cx + r * 0.55 + r * 1.2, cy + r * 1.3], 140, 220, fill=(255, 236, 200, 255), width=3 * S)
    d.arc([cx - r * 0.55 - r * 1.2, cy - r * 1.3, cx + r * 0.55, cy + r * 1.3], -40, 40, fill=(255, 236, 200, 255), width=3 * S)
    for k in range(3):
        sx = cx + (k - 1) * 22 * S
        d.polygon([(sx - 10 * S, h * 0.155), (sx - 3 * S, h * 0.135), (sx + 4 * S, h * 0.155), (sx - 3 * S, h * 0.175)], fill=GOLD_HI + (255,))
    out = out.resize((w // S, h // S), Image.LANCZOS)
    save(out, f"Crest_{name}")


def badge():
    """The OVR badge: a gold shield with a dark inner field; the rating is drawn over it by the shell."""
    S = 2
    w, h = 220 * S, 250 * S
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).polygon(shield_points(w, h, 2 * S), fill=255)
    out = paste_masked(out, metal((w, h), GOLD_HI, GOLD_LO, 0.22), m)
    inner = Image.new("L", (w, h), 0)
    ImageDraw.Draw(inner).polygon(shield_points(w, h, 12 * S), fill=255)
    out = paste_masked(out, metal((w, h), (30, 46, 120), (8, 12, 40), 0.15), inner)
    save(out.resize((w // S, h // S), Image.LANCZOS), "Ovr_Badge")


def ring():
    """The six-ball ring at the centre of the VS screen: six gold arcs, one per ball, round a soft glow."""
    S = 2
    n = 560 * S
    out = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    g = Image.new("L", (n, n), 0)
    ImageDraw.Draw(g).ellipse([n * 0.2, n * 0.2, n * 0.8, n * 0.8], fill=150)
    out = paste_masked(out, np.ones((n, n, 3), np.float32) * np.array(GOLD, np.float32) / 255, g.filter(ImageFilter.GaussianBlur(60 * S)))
    d = ImageDraw.Draw(out)
    box = [n * 0.12, n * 0.12, n * 0.88, n * 0.88]
    for k in range(6):
        start = -90 + k * 60 + 4
        d.arc(box, start, start + 52, fill=GOLD_HI + (255,), width=14 * S)
    d.ellipse([n * 0.19, n * 0.19, n * 0.81, n * 0.81], outline=(255, 255, 255, 70), width=3 * S)
    d.ellipse([n * 0.22, n * 0.22, n * 0.78, n * 0.78], fill=(6, 10, 34, 235))
    save(out.resize((n // S, n // S), Image.LANCZOS), "Ring")


def logo():
    """Logo lockup: a sheared gold "26" blade with a bevel, the chrome CRICKET wordmark and the edition line."""
    S = 2
    w, h = 1100 * S, 300 * S
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    bh, bw, x0, y0 = 200 * S, 250 * S, 40 * S, 20 * S
    blade = [(x0 + bh * SHEAR, y0), (x0 + bw + bh * SHEAR, y0), (x0 + bw, y0 + bh), (x0, y0 + bh)]
    sh = Image.new("L", (w, h), 0)
    ImageDraw.Draw(sh).polygon([(x + 8 * S, y + 12 * S) for x, y in blade], fill=150)
    out = paste_masked(out, np.zeros((h, w, 3), np.float32), sh.filter(ImageFilter.GaussianBlur(10 * S)))
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).polygon(blade, fill=255)
    out = paste_masked(out, metal((w, h), GOLD_HI, GOLD_LO, 0.18), m)
    d = ImageDraw.Draw(out)
    d.line([blade[0], blade[1]], fill=(255, 248, 220, 255), width=4 * S)
    f = ImageFont.truetype(FONT, int(bh * 1.02))
    bb = d.textbbox((0, 0), "26", font=f)
    tx = x0 + (bw + bh * SHEAR - (bb[2] - bb[0])) / 2 - bb[0]
    ty = y0 + (bh - (bb[3] - bb[1])) / 2 - bb[1]
    d.text((tx, ty), "26", font=f, fill=GOLD_INK + (255,))
    fw = ImageFont.truetype(FONT, int(170 * S))
    wx, wy = x0 + bw + bh * SHEAR + 26 * S, y0 - 14 * S
    d.text((wx + 5 * S, wy + 8 * S), "CRICKET", font=fw, fill=(0, 0, 0, 140))
    wm = Image.new("L", (w, h), 0)
    ImageDraw.Draw(wm).text((wx, wy), "CRICKET", font=fw, fill=255)
    out = paste_masked(out, metal((w, h), (255, 255, 255), (170, 186, 220), 0.42), wm)
    d = ImageDraw.Draw(out)
    fe = ImageFont.truetype(BOLD, int(38 * S))
    ex, ey = wx + 10 * S, y0 + bh - 30 * S
    for ch in "SUPER OVER EDITION":
        d.text((ex, ey), ch, font=fe, fill=GOLD + (255,))
        ex += d.textlength(ch, font=fe) + 9 * S
    bbox = out.getbbox()
    out = out.crop((0, 0, bbox[2] + 20 * S, h))
    save(out.resize((out.width // S, h // S), Image.LANCZOS), "Logo")


JOBS = {
    "Shell_Bg": shell_bg,
    "VS_Bg": vs_bg,
    "Tile_Gold": lambda: tile("Gold", [(0, GOLD_HI), (0.55, GOLD), (1, GOLD_LO)], 0.10, 0.10),
    "Tile_Royal": lambda: tile("Royal", [(0, (70, 120, 255)), (0.5, ROYAL), (1, (10, 16, 70))], 0.08, 0.07),
    "Tile_Ember": lambda: tile("Ember", [(0, (255, 110, 80)), (0.5, (200, 34, 70)), (1, EMBER_LO)], 0.08, 0.07),
    "Tile_Night": lambda: tile("Night", [(0, (44, 54, 100)), (0.5, (20, 26, 60)), (1, (8, 10, 28))], 0.06, 0.05),
    "Crest_Home": lambda: crest("Home", (70, 120, 255), (16, 30, 120), "H"),
    "Crest_Away": lambda: crest("Away", (255, 90, 100), (110, 10, 40), "A"),
    "Ovr_Badge": badge,
    "Ring": ring,
    "Logo": logo,
}

if __name__ == "__main__":
    import sys
    for name in sys.argv[1:] or JOBS:  # pass names to redraw only those
        JOBS[name]()
    print("ok")
