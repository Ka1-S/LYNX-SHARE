#!/usr/bin/env python3
"""Draws the GitHub social preview (docs/img/social-preview.png, 1280 x 640) from its text below, the app icon and a
picture of the workspace (Pillow).

    python3 scripts/social-preview.py [--window PICTURE] [--out docs/img/social-preview.png]

--window: the workspace picture shown in the frame on the right, best the 2x render (3840 x 2160) of the hero
(snap/s-desktop-all.png of scripts/site-shots.sh: Channel Rack, Piano Roll, Playlist and Mixer on the demo song).
Default: website/assets/img/hero@2x.webp, else hero.webp. scripts/site-shots.py calls this with the render itself;
tools/make_logo.py (site) calls it with the default after a logo change.

The words live in TEXT below: change them here, never in the picture (README, website and this card say the same:
the formats LYNX opens and exports, the plugin formats per OS, the repository URL). The fonts are Segoe UI (bold,
semibold, regular) as the card always had; where Windows' fonts are not found (C:\\Windows\\Fonts, also through
/mnt/c in WSL) the app's own Open Sans (resources/fonts) is used instead, and the script says so.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

# --- the words ---------------------------------------------------------------------------------------------------------
TEXT = {
    "title": "LYNX SHARE",
    "headline": ["FL Studio-style patterns,", "Ableton Live-style clips."],
    "lines": [
        "Channel Rack · Piano Roll · Playlist · Mixer",
        "Opens .flp, .als and .flm · exports .flp and .als",
        "VST3, CLAP, VST2, AU, LV2 · free and open source",
    ],
    "pills": ["Windows", "macOS", "Linux"],
    "site": "lynx-share.com",
    "url": "github.com/Ka1-S/LYNX-SHARE",
}

# --- the layout (1280 x 640) -------------------------------------------------------------------------------------------
W, H = 1280, 640
BG = (14, 15, 19)                 # the page
GLOW = (51, 18, 23)               # the red glow towards the right edge
LEFT = 76                         # the text column
TILE_XY, TILE = (75, 105), 96     # the app icon
TITLE_X, TITLE_MAX = 186, 81      # the name: left of it the icon, at most this size, centred on the icon
HEAD_Y, HEAD_SIZE, HEAD_STEP = 222, 38, 46
LINES_Y, LINES_SIZE, LINES_STEP = 340, 23, 36
PILL_Y, PILL_H, PILL_SIZE, PILL_PAD, PILL_GAP = 486, 41, 20, 18, 11
URL_Y, URL_SIZE = 566, 20
WIN_X, WIN_Y, WIN_H = 625, 120, 507   # the window frame: 2 px border, the picture inside, cut by the right edge
WIN_R = 10

INK = {"title": (245, 245, 247), "head": (235, 235, 240), "lines": (170, 172, 182), "url": (120, 122, 132),
       "pill": (32, 34, 42), "pill_edge": (64, 66, 78), "pill_ink": (235, 235, 240), "site": (217, 48, 56),
       "site_ink": (255, 255, 255), "frame": (70, 72, 82)}


def fonts():
    """(bold, semibold, regular) font files, and whether they are the card's own (Segoe UI)."""
    for d in ("/mnt/c/Windows/Fonts", "C:\\Windows\\Fonts", os.path.expanduser("~/.local/share/fonts"), "/usr/share/fonts/truetype/msttcorefonts"):
        files = [os.path.join(d, f) for f in ("segoeuib.ttf", "seguisb.ttf", "segoeui.ttf")]
        if all(os.path.isfile(f) for f in files):
            return files, True
    own = os.path.join(ROOT, "resources", "fonts")
    return [os.path.join(own, "OpenSans-Bold.ttf")] * 2 + [os.path.join(own, "OpenSans-Regular.ttf")], False


def background():
    im = Image.new("RGB", (W, H), BG)
    px = im.load()
    for x in range(W):
        t = min(1.0, max(0.0, (x - 560) / 560.0))
        t = t * t * (3 - 2 * t)
        c = tuple(round(BG[i] + (GLOW[i] - BG[i]) * t) for i in range(3))
        for y in range(H):
            px[x, y] = c
    return im


def window(src, h):
    """The workspace picture scaled to the frame's inner height."""
    im = Image.open(src).convert("RGB")
    w = round(im.width * h / im.height)
    return im.resize((w, h), Image.LANCZOS)


def draw(win_src, out):
    files, own = fonts()
    if not own:
        print("social-preview: Segoe UI not found, using Open Sans (resources/fonts)")
    bold = lambda s: ImageFont.truetype(files[0], s)
    semi = lambda s: ImageFont.truetype(files[1], s)
    reg = lambda s: ImageFont.truetype(files[2], s)

    im = background().convert("RGBA")

    # the window: a soft shadow, the 2 px frame with round corners, the picture inside (cut by the right edge)
    pic = window(win_src, WIN_H - 4)
    fw = pic.width + 4
    shadow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((WIN_X - 6, WIN_Y + 4, WIN_X + fw + 6, WIN_Y + WIN_H + 14), WIN_R + 6, fill=170)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    dark = Image.new("RGBA", (W, H), (4, 3, 4, 0))
    dark.putalpha(shadow)
    im.alpha_composite(dark)
    k = 4
    mask = Image.new("L", (fw * k, WIN_H * k), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, fw * k - 1, WIN_H * k - 1), WIN_R * k, fill=255)
    mask = mask.resize((fw, WIN_H), Image.LANCZOS)
    framed = Image.new("RGBA", (fw, WIN_H), INK["frame"] + (255,))
    inner = Image.new("L", (fw * k, WIN_H * k), 0)
    ImageDraw.Draw(inner).rounded_rectangle((2 * k, 2 * k, (fw - 2) * k - 1, (WIN_H - 2) * k - 1), (WIN_R - 2) * k, fill=255)
    inner = inner.resize((fw, WIN_H), Image.LANCZOS)
    framed.paste(pic, (2, 2), inner.crop((2, 2, 2 + pic.width, 2 + pic.height)))
    framed.putalpha(mask)
    im.alpha_composite(framed.crop((0, 0, min(fw, W - WIN_X), WIN_H)), (WIN_X, WIN_Y))

    d = ImageDraw.Draw(im)
    # the icon and the name, the name as large as fits before the window (at most TITLE_MAX), centred on the icon
    import make_logo
    im.alpha_composite(make_logo.Logo().icon(TILE), TILE_XY)
    size = TITLE_MAX
    while size > 40 and bold(size).getlength(TEXT["title"]) > WIN_X - 36 - TITLE_X:
        size -= 1
    f = bold(size)
    top, bottom = f.getbbox("LYNX")[1], f.getbbox("LYNX")[3]
    d.text((TITLE_X, TILE_XY[1] + TILE / 2 - (top + bottom) / 2), TEXT["title"], font=f, fill=INK["title"])

    for i, t in enumerate(TEXT["headline"]):
        d.text((LEFT - 2, HEAD_Y + i * HEAD_STEP), t, font=semi(HEAD_SIZE), fill=INK["head"])
    for i, t in enumerate(TEXT["lines"]):
        d.text((LEFT, LINES_Y + i * LINES_STEP), t, font=reg(LINES_SIZE), fill=INK["lines"])

    # the platforms, then the website in red
    f = semi(PILL_SIZE)
    x = LEFT
    for t, site in [(p, False) for p in TEXT["pills"]] + [(TEXT["site"], True)]:
        w = round(f.getlength(t)) + 2 * PILL_PAD
        box = (x, PILL_Y, x + w, PILL_Y + PILL_H)
        if site:
            d.rounded_rectangle(box, PILL_H // 2, fill=INK["site"])
        else:
            d.rounded_rectangle(box, PILL_H // 2, fill=INK["pill"], outline=INK["pill_edge"], width=1)
        d.text((x + w / 2, PILL_Y + PILL_H / 2), t, font=f, anchor="mm", fill=INK["site_ink" if site else "pill_ink"])
        x += w + PILL_GAP

    d.text((LEFT, URL_Y), TEXT["url"], font=reg(URL_SIZE), fill=INK["url"])

    # nothing may run into the window
    widest = max([LEFT + reg(LINES_SIZE).getlength(t) for t in TEXT["lines"]]
                 + [LEFT + semi(HEAD_SIZE).getlength(t) for t in TEXT["headline"]] + [x - PILL_GAP])
    if widest > WIN_X - 12:
        sys.exit("social-preview: a line is %d px wide and runs into the window: shorten it" % (widest - LEFT))

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    im.convert("RGB").save(out, optimize=True)
    print("  %s (%dx%d, window from %s)" % (os.path.relpath(out, ROOT), W, H, os.path.relpath(win_src, ROOT)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--window")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "img", "social-preview.png"))
    a = ap.parse_args()
    src = a.window
    if not src:
        img = os.path.join(ROOT, "website", "assets", "img")
        src = next((f for f in (os.path.join(img, "hero@2x.webp"), os.path.join(img, "hero.webp")) if os.path.isfile(f)), None)
    if not src or not os.path.isfile(src):
        sys.exit("social-preview: no workspace picture (give --window)")
    draw(src, a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
