#!/usr/bin/env python3
"""Fails when the logo embedded in the app (or the shipped icons) drifted from the brand source.
The source is resources/brand/lynx-share-logo.svg (the logo by w3teal); tools/make_logo.py writes the PNGs from it.
Checks: LogoData.h / SplashData.h carry the sha256 of their current source PNGs and decode to PNGs of the right size;
resources/lynx_icon_256.png, LYNX.ico, the Linux icons and the website/README/Store logos still look like
resources/lynx_badge.png.
Usage: python3 scripts/check-logo.py   (from anywhere; exit code 1 on drift)"""
import hashlib, io, os, re, sys
from PIL import Image, ImageChops, ImageStat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def p(*a): return os.path.join(ROOT, *a)
bad = []

def header(path, array):
    s = open(p(path)).read()
    m = re.search(r"source-sha256: ([0-9a-f]{64})", s)
    body = s.split(array + "[] = {", 1)[1].split("};", 1)[0]
    data = bytes(int(x) for x in re.findall(r"\d+", body))
    return (m.group(1) if m else None), Image.open(io.BytesIO(data)).convert("RGBA")

def sha(path): return hashlib.sha256(open(p(path), "rb").read()).hexdigest()

h, img = header("src/ui/LogoData.h", "png512")
if h != sha("resources/lynx_badge.png"): bad.append("LogoData.h is stale: rerun tools/make_logo_data.py")
if img.size != (512, 512): bad.append("LogoData.h image is %s, expected 512x512" % (img.size,))
m = re.search(r"small-sha256: ([0-9a-f]{64})", open(p("src/ui/LogoData.h")).read())
if not m or m.group(1) != sha("resources/brand/lynx-share-logo-small.png"): bad.append("LogoData.h small logo is stale: rerun tools/make_logo_data.py")
h, _ = header("src/ui/SplashData.h", "markPng")
if h != sha("resources/lynx_logo.png"): bad.append("SplashData.h is stale: rerun tools/make_splash.py")

ref = Image.open(p("resources/lynx_badge.png")).convert("RGBA")
def close(name, im, tol=6.0):
    a = ref.resize((128, 128), Image.LANCZOS)
    b = im.convert("RGBA").resize((128, 128), Image.LANCZOS)
    flat = lambda i: Image.alpha_composite(Image.new("RGBA", i.size, (0, 0, 0, 255)), i).convert("RGB")
    d = sum(ImageStat.Stat(ImageChops.difference(flat(a), flat(b))).mean) / 3
    if d > tol: bad.append("%s differs from lynx_badge.png (mean diff %.1f)" % (name, d))

close("LogoData.h", img)
close("resources/lynx_icon_256.png", Image.open(p("resources/lynx_icon_256.png")))
ico = Image.open(p("resources/LYNX.ico")); close("resources/LYNX.ico", ico.ico.getimage(max(ico.ico.sizes())))
for f in ("website/assets/img/logo.png", "docs/img/logo.png", "store/logo-300.png",
          "resources/linux/icons/hicolor/256x256/apps/lynx.png", "resources/linux/icons/hicolor/512x512/apps/lynx.png"):
    close(f, Image.open(p(f)))
print("\n".join(bad) if bad else "logo OK")
sys.exit(1 if bad else 0)
