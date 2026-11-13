#!/usr/bin/env python3
"""Import the pictures the site shows from a folder of renders made by the RELEASED LYNX.exe (needs Pillow).

    python3 website/_src/import-shots.py RENDERS

RENDERS holds what these commands wrote (all off-screen, no window is opened):
    LYNX.exe --artshot RENDERS\\art after            editors with their own art      (art/<name>-after-100.png)
    LYNX.exe --plugui RENDERS\\plugui                effect editors                  (plugui/<Name>.png)
    LYNX.exe --flextrashot RENDERS\\extra            a few more editors              (extra/*.png)
    LYNX.exe --pluginthumbs RENDERS\\thumbs          the plugin picker's pictures    (thumbs/<key>.png, every plugin)
    LYNX.exe --showcase RENDERS\\snap\\Demo.lynx, then
    LYNX.exe --snapshot RENDERS\\snap\\s RENDERS\\snap\\Demo.lynx   the windows and dialogs   (snap/s-<name>.png)

Writes assets/img/plugins/<id>.webp for every entry of catalog.json (its "img" names the render) with the sizes in
catalog-img.json, and assets/img/wiki/<name>.webp for every /assets/img/wiki/<name>.webp a page or wiki article uses
(from snap/s-<name>.png). A plugin whose render is missing gets no picture: its card shows a name plate.

Sizes: a picture is never made larger than its render, and the pages never show one above its own size (build.py
writes width and height from catalog-img.json and img-sizes.json). --artshot also writes each editor at twice the size
(<name>-after-200.png): that becomes <id>@2x.webp, offered to dense screens through srcset. The other modes of the
0.1.25 program draw at 100% only, so their pictures have no @2x file. Small pictures are kept lossless, the rest at
quality 90 (@2x: 84).

The logo: where a render's program drew an older logo in the round button of a plugin's header, that button is
painted over with assets/img/logo.png (the brand's mark) at the same place and size. LOGO (below) is the old button.
(This importer belongs to the 0.1.25 pictures; scripts/site-shots.py --logo-was does the same for the current ones.)
"""
import io
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

SRC = Path(__file__).resolve().parent
OUT = SRC.parent / "assets/img"


# pictures that are a part of a snapshot: name -> (snapshot, box). The Plugin manager's sample rows name other makers'
# plugins, so the site shows only the part below them.
CROPS = {"plugin-manager-scan": ("plugin-manager", (0, 330, 980, 600))}

LOGO = None      # the logo 0.1.25 draws (RGBA, 256 px), read from <RENDERS>/logo-0.1.25.png
BADGE = Image.open(OUT / "logo.png").convert("RGBA")
_pts = {}


def logo_points(d):
    """A grid of points inside the logo's disc at diameter d: (y, x, rgb)."""
    if d not in _pts:
        t = np.asarray(LOGO.resize((d, d), Image.LANCZOS), dtype=np.float32)
        n = max(6, min(10, d // 2))
        _pts[d] = [(y, x, t[y, x, :3]) for y in (int((i + .5) * d / n) for i in range(n)) for x in (int((j + .5) * d / n) for j in range(n))
                   if (y - d / 2 + .5) ** 2 + (x - d / 2 + .5) ** 2 < (d * .36) ** 2]
    return _pts[d]


def find_logo(im, sizes, region=None, limit=14.0):
    """Where 0.1.25 drew its logo button: (x, y, diameter) or None. Mean colour difference at the points decides."""
    if LOGO is None:
        return None
    x0, y0, x1, y1 = region or (0, 0, im.width, im.height)
    a = np.asarray(im.convert("RGB"), dtype=np.float32)[y0:y1, x0:x1]
    best = None
    for d in sizes:
        h, w = a.shape[0] - d + 1, a.shape[1] - d + 1
        if h < 1 or w < 1:
            break
        pts = logo_points(d)
        e = np.zeros((h, w), dtype=np.float32)
        for y, x, c in pts:
            e += np.abs(a[y:y + h, x:x + w] - c).sum(axis=2)
        i = np.unravel_index(e.argmin(), e.shape)
        v = float(e[i]) / (3 * len(pts))
        if best is None or v < best[3]:
            best = (x0 + int(i[1]), y0 + int(i[0]), d, v)
    return best[:3] if best and best[3] < limit else None


def brand(im, scale=1, anywhere=False):
    """The picture with the brand's mark on the header's logo button (see the note at the top). -> (image, painted?)"""
    sizes = range(15 * scale, 24 * scale) if not anywhere else range(17 * scale, 20 * scale)
    hit = find_logo(im, sizes, None if anywhere else (0, 0, min(im.width, 420 * scale), min(im.height, 150 * scale)))
    if not hit:
        return im, False
    out = im.convert("RGBA")
    out.alpha_composite(BADGE.resize((hit[2], hit[2]), Image.LANCZOS), hit[:2])
    return out, True


def save(im, dest, quality=90):
    """Lossless when that costs little (flat interface pictures often compress better that way), else at `quality`.
    The large @2x files (quality below 90) are not tried lossless: they would be several times the size."""
    im = im.convert("RGB")
    dest.parent.mkdir(parents=True, exist_ok=True)
    lossy, exact = io.BytesIO(), io.BytesIO()
    im.save(lossy, "WEBP", quality=quality, method=5)
    if quality >= 90:
        im.save(exact, "WEBP", lossless=True, method=4)
    data = exact.getvalue() if 0 < exact.tell() <= max(1.35 * lossy.tell(), 40000) else lossy.getvalue()
    if not dest.exists() or dest.read_bytes() != data:
        dest.write_bytes(data)
    return [im.width, im.height]


def fit(im, maxw):
    return im if im.width <= maxw else im.resize((maxw, round(im.height * maxw / im.width)), Image.LANCZOS)


def main():
    global LOGO
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    renders = Path(sys.argv[1])
    if (renders / "logo-0.1.25.png").is_file():
        LOGO = Image.open(renders / "logo-0.1.25.png").convert("RGBA")
    dims, missing, painted = {}, [], []
    for p in json.loads((SRC / "catalog.json").read_text(encoding="utf-8")):
        f = renders / p.get("img", "")
        if not (p.get("img") and f.is_file()):
            missing.append(p["id"])
            continue
        one, hit = brand(Image.open(f))
        one = fit(one, 960)
        dims[p["id"]] = save(one, OUT / "plugins" / (p["id"] + ".webp"))
        big = f.with_name(f.name.replace("-100.png", "-200.png"))
        dense = OUT / "plugins" / (p["id"] + "@2x.webp")
        if big != f and big.is_file():
            two, hit2 = brand(Image.open(big), 2)
            hit = hit or hit2
            if two.size != (one.width * 2, one.height * 2):
                two = two.resize((one.width * 2, one.height * 2), Image.LANCZOS)
            save(two, dense, 84)
            dims[p["id"]].append(2)
        elif dense.exists():
            dense.unlink()
        if hit:
            painted.append(p["id"])
    (SRC / "catalog-img.json").write_text(json.dumps(dims, indent=0, sort_keys=True) + "\n", encoding="utf-8")
    used = set()
    for f in list((SRC / "wiki").glob("*.md")) + list((SRC / "pages").glob("*.html")):
        used |= set(re.findall(r"/assets/img/wiki/([\w.-]+)\.webp", f.read_text(encoding="utf-8")))
    nowiki = []
    for name in sorted(used):
        src, box = CROPS.get(name, (name, None))
        f = renders / "snap" / ("s-%s.png" % src)
        if f.is_file():
            im = Image.open(f)
            im, hit = brand(im.crop(box) if box else im, anywhere=True)
            save(fit(im, 1600), OUT / "wiki" / (name + ".webp"))
            if hit:
                painted.append("wiki/" + name)
        elif not (OUT / "wiki" / (name + ".webp")).is_file():     # (a few are made by hand from the running program)
            nowiki.append(name)
    sizes = {"/assets/img/" + str(f.relative_to(OUT)).replace("\\", "/"): list(Image.open(f).size)
             for f in sorted(OUT.rglob("*")) if f.suffix in (".webp", ".png") and f.parent.name != "plugins"}
    (SRC / "img-sizes.json").write_text(json.dumps(sizes, indent=0, sort_keys=True) + "\n", encoding="utf-8")
    print("import-shots: %d plugin pictures, %d with an @2x file (%d plugins without a picture: %s), %d wiki pictures (%d not found: %s)"
          % (len(dims), sum(1 for d in dims.values() if len(d) > 2), len(missing), ", ".join(missing) or "-", len(used) - len(nowiki), len(nowiki), ", ".join(nowiki) or "-"))
    print("import-shots: logo button painted in %d pictures: %s" % (len(painted), ", ".join(painted) or "-"))
    return 1 if nowiki else 0


if __name__ == "__main__":
    sys.exit(main())
