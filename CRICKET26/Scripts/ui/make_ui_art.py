"""Procedural UI art for the CRICKET 26 shell: player card frames, the light-streak atmosphere layer and the button
shine band. Writes PNGs to Content/UI/Source; Scripts/import_ui.py brings them into /Game/UI.
Run: python3 Scripts/ui/make_ui_art.py"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter, ImageChops

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "Content", "UI", "Source")
S = 2  # supersample


def shield(w, h, inset=0):
    """The card silhouette: chamfered top corners, straight sides, bottom tapering to a soft point."""
    i = inset
    c = w * 0.14
    return [(c + i, i), (w - c - i, i), (w - i, c * 0.55 + i), (w - i, h * 0.84 - i * 0.5),
            (w * 0.5, h - i), (i, h * 0.84 - i * 0.5), (i, c * 0.55 + i)]


def gradient(w, h, top, bottom, angle_mix=0.35):
    g = Image.new("RGBA", (w, h))
    px = g.load()
    for y in range(h):
        for x in range(0, w):
            t = min(1, max(0, (y / h) * (1 - angle_mix) + (x / w) * angle_mix))
            px[x, y] = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)) + (255,)
    return g


def card(name, top, bottom, rim, pattern_alpha, sheen_alpha):
    w, h = 600 * S, 860 * S
    base = gradient(w // 4, h // 4, top, bottom).resize((w, h), Image.BICUBIC)
    # Diagonal pinstripes and a soft sheen give the metallic foil read.
    fx = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(fx)
    for k in range(-h, w, 22 * S):
        d.line([(k, 0), (k + h, h)], fill=(255, 255, 255, pattern_alpha), width=2 * S)
    sheen = Image.new("L", (w, h), 0)
    ds = ImageDraw.Draw(sheen)
    ds.polygon([(w * 0.15, 0), (w * 0.5, 0), (w * 0.05, h), (-w * 0.3, h)], fill=sheen_alpha)
    sheen = sheen.filter(ImageFilter.GaussianBlur(40 * S))
    fx = Image.alpha_composite(fx, Image.merge("RGBA", (Image.new("L", (w, h), 255),) * 3 + (sheen,)))
    base = Image.alpha_composite(base, fx)
    # Darken the lower third so the name reads, like the stats panel of a football card.
    shade = Image.new("L", (w, h), 0)
    for y in range(h):
        shade.paste(int(max(0, (y / h - 0.55) / 0.45) * 90), (0, y, w, y + 1))
    base = Image.alpha_composite(base, Image.merge("RGBA", (Image.new("L", (w, h), 0),) * 3 + (shade,)))

    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).polygon(shield(w, h), fill=255)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.paste(base, (0, 0), mask)
    # Outer rim and an inner hairline.
    d = ImageDraw.Draw(out)
    d.line(shield(w, h, 3 * S) + [shield(w, h, 3 * S)[0]], fill=rim + (255,), width=6 * S, joint="curve")
    d.line(shield(w, h, 16 * S) + [shield(w, h, 16 * S)[0]], fill=rim + (150,), width=2 * S, joint="curve")
    out = out.resize((w // S, h // S), Image.LANCZOS)
    out.save(os.path.join(OUT, f"CRICKET26_Card_{name}.png"))


def streaks():
    """Transparent atmosphere layer: floodlight beams, bokeh and faint angular shards over the stadium plate."""
    w, h = 2048, 1024
    random.seed(26)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    beams = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(beams)
    for x0, spread, col in [(1500, 520, (120, 170, 255, 60)), (1800, 380, (255, 214, 120, 42)), (300, 420, (90, 140, 255, 40))]:
        d.polygon([(x0 - 30, -10), (x0 + 30, -10), (x0 - spread + 160, h), (x0 - spread - 160, h)], fill=col)
    beams = beams.filter(ImageFilter.GaussianBlur(60))
    layer = Image.alpha_composite(layer, beams)
    shards = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(shards)
    for x in (1100, 1340, 1650):
        d.polygon([(x, 0), (x + 90, 0), (x - 380, h), (x - 470, h)], fill=(160, 190, 255, 14))
    layer = Image.alpha_composite(layer, shards.filter(ImageFilter.GaussianBlur(2)))
    bokeh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(bokeh)
    for _ in range(70):
        x, y, r = random.uniform(0, w), random.uniform(0, h * 0.7), random.uniform(3, 16)
        c = random.choice([(255, 220, 140), (150, 190, 255), (255, 255, 255)])
        d.ellipse([x - r, y - r, x + r, y + r], fill=c + (random.randint(30, 90),))
    layer = Image.alpha_composite(layer, bokeh.filter(ImageFilter.GaussianBlur(3)))
    layer.save(os.path.join(OUT, "CRICKET26_Streaks.png"))


def shine():
    """A soft white band; buttons slide it across their face on a timer."""
    w, h = 128, 256
    im = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    a = Image.new("L", (w, h))
    for x in range(w):
        v = math.exp(-((x - w / 2) / (w * 0.18)) ** 2)
        a.paste(int(255 * v), (x, 0, x + 1, h))
    im.putalpha(a)
    im.save(os.path.join(OUT, "CRICKET26_Shine.png"))


if __name__ == "__main__":
    card("Gold", (255, 226, 140), (196, 140, 40), (255, 244, 200), 18, 120)
    card("Elite", (28, 44, 88), (6, 10, 24), (242, 182, 50), 14, 70)
    streaks()
    shine()
    print("ok")
