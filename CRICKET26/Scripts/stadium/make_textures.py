# Draws the stadium's printed textures: the LED advertising boards (an atlas of eight invented brands, one per
# row) and the logo painted on the outfield. Every brand is fictional and the lettering is Roboto (Apache 2.0,
# shipped with the engine), so nothing here needs a licence.
# Usage: python3 make_textures.py <output dir>
import os
import sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
FONTS = "/Users/Shared/Epic Games/UE_5.8/Engine/Content/Slate/Fonts"


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def centred(draw, box, text, fnt, fill):
    l, t, r, b = draw.textbbox((0, 0), text, font=fnt)
    x0, y0, x1, y1 = box
    draw.text(((x0 + x1 - (r - l)) / 2 - l, (y0 + y1 - (b - t)) / 2 - t), text, font=fnt, fill=fill)


# Each board: background, accent, text colour, the wordmark and a strapline. The board is 8 m by 0.9 m, so each
# row of the atlas is 1024 by 128 (the same 8.9:1 shape, near enough).
ADS = [
    ((12, 40, 110), (255, 196, 0), (255, 255, 255), "KESTREL", "BATS MADE FOR SIXES"),
    ((200, 20, 40), (255, 255, 255), (255, 255, 255), "PEAKFIZZ", "COLA"),
    ((0, 120, 90), (180, 255, 120), (255, 255, 255), "TERRAVOLT", "ENERGY"),
    ((20, 20, 24), (255, 90, 0), (255, 255, 255), "RUBYNET 5G", "FAST AS A YORKER"),
    ((255, 200, 0), (20, 20, 24), (20, 20, 24), "SAFFRON BANK", ""),
    ((90, 30, 150), (0, 220, 255), (255, 255, 255), "ZEPHYRA", "AIRWAYS"),
    ((240, 240, 240), (0, 90, 200), (0, 60, 160), "MANGOLINK", "PAY IN A TAP"),
    ((0, 0, 0), (0, 200, 255), (255, 255, 255), "CRICKET 26", "SUPER OVER"),
]


def ads():
    W, H = 1024, 128
    img = Image.new("RGB", (W, H * len(ADS)))
    d = ImageDraw.Draw(img)
    big, small = font("Roboto-BlackItalic.ttf", 78), font("Roboto-BoldCondensed.ttf", 34)
    for i, (bg, accent, fg, word, strap) in enumerate(ADS):
        y = i * H
        d.rectangle((0, y, W, y + H), fill=bg)
        # A slanted accent stripe at each end, so the boards read as designed, not as a caption.
        for x0 in (0, W - 120):
            d.polygon([(x0 + 30, y), (x0 + 90, y), (x0 + 60, y + H), (x0, y + H)], fill=accent)
        if strap:
            l, t, r, b = d.textbbox((0, 0), word, font=big)
            wl, sl = r - l, d.textlength(strap, font=small)
            x = (W - wl - 24 - sl) / 2
            d.text((x - l, y + (H - (b - t)) / 2 - t), word, font=big, fill=fg)
            st = d.textbbox((0, 0), strap, font=small)
            d.text((x + wl + 24, y + (H - (st[3] - st[1])) / 2 - st[1]), strap, font=small, fill=accent)
        else:
            centred(d, (0, y, W, y + H), word, big, fg)
    img.save(os.path.join(OUT, "T_Ads.png"))


def grass_logo():
    # White paint on grass: the wordmark in outline-free capitals with a ring emblem, alpha as the paint.
    W, H = 1024, 256
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((24, 24, 232, 232), outline=(255, 255, 255, 255), width=18)
    centred(d, (24, 24, 232, 232), "26", font("Roboto-Black.ttf", 110), (255, 255, 255, 255))
    centred(d, (250, 0, W, H), "CRICKET", font("Roboto-BlackItalic.ttf", 170), (255, 255, 255, 255))
    img.save(os.path.join(OUT, "T_GrassLogo.png"))


def mark():
    # The stamp the game draws a ball mark or footmark with: a soft-edged, slightly ragged disc in white.
    import math, random
    random.seed(26)
    S = 64
    img = Image.new("RGBA", (S, S), (255, 255, 255, 0))
    px = img.load()
    for y in range(S):
        for x in range(S):
            dx, dy = (x - S / 2 + 0.5) / (S / 2), (y - S / 2 + 0.5) / (S / 2)
            r = math.hypot(dx, dy) * (1.0 + 0.12 * math.sin(5 * math.atan2(dy, dx)))
            a = max(0.0, min(1.0, (1.0 - r) / 0.35)) * (0.75 + 0.25 * random.random())
            px[x, y] = (255, 255, 255, int(255 * a))
    img.save(os.path.join(OUT, "T_Mark.png"))


os.makedirs(OUT, exist_ok=True)
mark()
ads()
grass_logo()
print("STADIUM textures written to", OUT)
