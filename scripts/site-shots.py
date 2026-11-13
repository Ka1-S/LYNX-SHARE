#!/usr/bin/env python3
"""Turn the renders of scripts/site-shots.sh into the pictures the website and the README use, at 1x and 2x (Pillow).

    python3 scripts/site-shots.py RENDERS [--site website] [--docs docs/img] [--logo-was LOGO.png] [--dry-run]

RENDERS is what site-shots.sh wrote: x1/ and x2/ (one folder per pixel scale, the same file names in each: snap/,
snap1600/, grid/, grid1600/, plugui/, extra/, thumbs/, dance/) and art/ (--artshot draws 100 % and 200 % itself).

Writes (names as website/_src/build.py reads them):
  website/assets/img/<name>.webp + <name>@2x.webp           the Home / Features pictures (TOP below)
  website/assets/img/plugins/<id>.webp + <id>@2x.webp       every entry of _src/catalog.json (its "img" names the render)
  website/assets/img/wiki/<name>.webp + <name>@2x.webp      every /assets/img/wiki/<name>.webp a page or article uses
  website/_src/catalog-img.json, website/_src/img-sizes.json the 1x sizes (build.py writes width/height from them;
                                                             a catalog entry [w, h, 2] has an @2x file for srcset)
  docs/img/<name>.png + <name>@2x.png                        the README's pictures (README_PICTURES below)
  docs/img/social-preview.png                                the GitHub social preview (scripts/social-preview.py: its text
                                                             lives there) around the hero's render
  store/screenshots/<name>.png                               the Store screenshots 01-04 (STORE below), 1x
  website/assets/media/poster.jpg                            the Features video's poster, 1280 x 720 from the 2x hero
The @2x file is always exactly twice the 1x file's size and is drawn by the program at 2x (never an upscale). A 2x
render missing for a picture: no @2x file is written (and an old one is removed), the table says so.
Pictures larger than the page shows are fitted (plugins 960 px wide, wiki 1600 px) with the same factor at both scales.
WebP: lossless when that costs little (flat interface pictures), else quality 92 (1x) / 88 (2x). Prints a table.

--logo-was LOGO.png: the logo the build that made the renders draws (its src/ui/LogoData.h image), for renders made
before a logo change. Wherever that build drew its logo button (plugin headers), the button is repainted with the
brand's mark (the logo by w3teal without its app-icon tile, rendered by tools/make_logo.py from resources/brand; up to
32 px its small-size form, below 24 px without the pepper stem): a dark disc over the old button and the mark on it,
its ears inside the picture, as the program draws it (src/ui/PluginSkin.cpp brandMark). Leave it out for renders of a build that draws the current logo.
"""
import argparse
import io
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# Home / Features pictures: name -> (render, crop box at 1x or None)
TOP = {
    "hero": ("snap/s-desktop-all.png", None),       # Channel Rack, Piano Roll, Playlist and Mixer (the page's alt text)
    "playlist": ("snap/s-playlist.png", None),
    "pianoroll": ("snap/s-pianoroll.png", None),
    "mixer": ("snap/s-mixer.png", None),
    "rack": ("snap/s-rack.png", (0, 0, 1074, 420)),
    "automation": ("snap/s-automation.png", None),
    "synth": ("snap/s-synth.png", None),
    "eq": ("art/facet-eq-after", None),
    "cliplaunch": ("grid/g-both.png", None),
}
# the README's pictures (docs/img/<name>.png): the 1600 x 900 window for the wide ones, as the README had them
README_PICTURES = {
    "workspace": ("snap1600/s-desktop-all.png", None),  # the same four windows as the hero (the README's caption)
    "playlist": ("snap/s-playlist.png", (150, 75, 1920, 800)),
    "channel-rack": ("snap/s-rack.png", (0, 0, 1074, 420)),
    "piano-roll": ("snap1600/s-pianoroll.png", None),
    "mixer": ("snap1600/s-mixer.png", None),
    "synth": ("snap/s-synth.png", None),
    "eq": ("art/facet-eq-after", None),
    "automation": ("snap/s-automation.png", None),
    "clip-launch": ("grid1600/g-both.png", None),
}
# the Microsoft Store listing's screenshots (store/screenshots/<name>.png, 1920 x 1080, 1x only); 05-instruments-effects is
# put together by hand (Channel Rack + a synth + Parametric EQ) and is not written here
STORE = {
    "01-workspace": "snap/s-desktop-all.png",
    "02-playlist": "snap/s-playlist.png",
    "03-piano-roll": "snap/s-pianoroll-narrow150.png",
    "04-mixer": "snap/s-mixer.png",
}
# the Features page's video poster (website/assets/media/poster.jpg, 1280 x 720) and the GitHub social preview
# (docs/img/social-preview.png, drawn by scripts/social-preview.py around it): both from the hero's render
POSTER = "snap/s-desktop-all.png"

# wiki pictures that are a part of a snapshot: name -> (snapshot, box at 1x). The Plugin manager's sample rows name
# other makers' plugins, so the site shows only the part below them (same as website/_src/import-shots.py).
CROPS = {"plugin-manager-scan": ("plugin-manager", (0, 330, 980, 600))}


def render(root, ref, scale):
    """The file of render `ref` at `scale` (1 or 2), or None. art/<name>-after: --artshot's own -100 / -200 files."""
    if ref.startswith("art/"):
        name = re.sub(r"-(100|200)\.png$", "", ref)
        f = root / ("%s-%d.png" % (name, 100 * scale))
    else:
        f = root / ("x%d" % scale) / ref
    return f if f.is_file() else None


class LogoPainter:
    """Finds the old logo button in a render and paints the brand's button over it (see --logo-was)."""
    DARK = (14, 15, 19)        # the disc under the mark (src/ui/PluginSkin.cpp brandMark)

    def __init__(self, old):
        import numpy as np
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
        import make_logo                                  # the brand's pictures, from resources/brand/*.svg
        self.np = np
        self.logo = make_logo.Logo()
        self.old = Image.open(old).convert("RGBA")
        self.disc = (0.5, 0.5, 184.0 / 512.0)             # the mark's ring in its 512-unit frame (centre, radius)
        self.pts = {}
        self.count = 0

    def points(self, d):
        """A grid of points inside the old logo at diameter d: (y, x, rgb)."""
        if d not in self.pts:
            np = self.np
            t = np.asarray(self.old.resize((d, d), Image.LANCZOS), dtype=np.float32)
            n = max(6, min(10, d // 2))
            self.pts[d] = [(y, x, t[y, x, :3]) for y in (int((i + .5) * d / n) for i in range(n))
                           for x in (int((j + .5) * d / n) for j in range(n))
                           if (y - d / 2 + .5) ** 2 + (x - d / 2 + .5) ** 2 < (d * .36) ** 2]
        return self.pts[d]

    def find(self, a, sizes, limit=14.0):
        """Best (x, y, d) of the old logo in the array a, or None."""
        np = self.np
        best = None
        for d in sizes:
            h, w = a.shape[0] - d + 1, a.shape[1] - d + 1
            if h < 1 or w < 1:
                continue
            pts = self.points(d)
            e = np.zeros((h, w), dtype=np.float32)
            for y, x, c in pts:
                e += np.abs(a[y:y + h, x:x + w] - c).sum(axis=2)
            i = np.unravel_index(e.argmin(), e.shape)
            v = float(e[i]) / (3 * len(pts))
            if best is None or v < best[3]:
                best = (int(i[1]), int(i[0]), d, v)
        return best[:3] if best and best[3] < limit else None

    def paint(self, im, scale):
        """im with every old logo button repainted (sizes: the 18 and 20 px header buttons, Strike's 40 px plate)."""
        np = self.np
        rgb = np.asarray(im.convert("RGB"), dtype=np.float32)
        # the old logo is a red pepper with a green stem: search only around pixels of that green
        g = (rgb[..., 1] > rgb[..., 0] + 40) & (rgb[..., 1] > rgb[..., 2] + 40) & (rgb[..., 1] > 90)
        ys, xs = np.nonzero(g)
        if len(xs) == 0:
            return im, 0
        sizes = sorted({v * scale + k for v in (18, 20, 40) for k in (-1, 0, 1)})
        cell, span = 48 * scale, 48 * scale
        cells = sorted({(int(y) // cell, int(x) // cell) for y, x in zip(ys, xs)})
        hits = []
        for cy, cx in cells:
            y0, x0 = max(0, cy * cell - span), max(0, cx * cell - span)
            y1, x1 = min(rgb.shape[0], (cy + 1) * cell + span), min(rgb.shape[1], (cx + 1) * cell + span)
            hit = self.find(rgb[y0:y1, x0:x1], sizes)
            if hit:
                hit = (hit[0] + x0, hit[1] + y0, hit[2])
                if all(abs(hit[0] - h[0]) > 4 or abs(hit[1] - h[1]) > 4 for h in hits):
                    hits.append(hit)
        if not hits:
            return im, 0
        out = im.convert("RGBA")
        for x, y, d in hits:
            self.draw(out, x + d / 2.0, y + d / 2.0, d, scale)
        self.count += len(hits)
        return out.convert(im.mode if im.mode in ("RGB", "RGBA") else "RGB"), len(hits)

    def draw(self, out, cx, cy, d, scale):
        """The brand's button over an old one of diameter d centred at (cx, cy) in the RGBA picture out."""
        r = d / 2.0 + 1.5 * scale                 # over the old button's dark edge and red ring
        fx, fy, fr = self.disc
        side = min(r / fr, cy / fy)               # (the ears stay inside the picture, as the program keeps them in the header)
        r0, r = r, side * fr
        k = 4                                     # the disc drawn 4x and scaled down (smooth edge)
        rd = max(r0, r + scale)                   # the dark disc: over the old button, at least 1 px beyond the mark
        box = (int(cx - rd) - 2, int(cy - rd) - 2, int(cx + rd) + 3, int(cy + rd) + 3)
        w, h = box[2] - box[0], box[3] - box[1]
        mask = Image.new("L", (w * k, h * k), 0)
        ImageDraw.Draw(mask).ellipse(((cx - rd - box[0]) * k, (cy - rd - box[1]) * k, (cx + rd - box[0]) * k, (cy + rd - box[1]) * k), fill=255)
        disc = Image.new("RGBA", (w, h), self.DARK + (255,))
        disc.putalpha(mask.resize((w, h), Image.LANCZOS))
        out.alpha_composite(disc, box[:2])
        px = max(1, round(side))
        mark = self.logo.mark(px, small=side <= 32)
        out.alpha_composite(mark, (round(cx - fx * side), round(cy - fy * side)))


def webp(im, quality):
    im = im.convert("RGB")
    lossy, exact = io.BytesIO(), io.BytesIO()
    im.save(lossy, "WEBP", quality=quality, method=6)
    im.save(exact, "WEBP", lossless=True, method=4)
    return exact.getvalue() if exact.tell() <= 1.35 * lossy.tell() else lossy.getvalue()


def png(im):
    out = io.BytesIO()
    im.convert("RGB").save(out, "PNG", optimize=True)
    return out.getvalue()


class Writer:
    def __init__(self, dry, painter=None):
        self.dry, self.rows, self.painter, self.painted = dry, [], painter, []

    def put(self, dest, data, size, source):
        if not self.dry:
            dest.parent.mkdir(parents=True, exist_ok=True)
            if not dest.exists() or dest.read_bytes() != data:
                dest.write_bytes(data)
        self.rows.append((str(dest), "%dx%d" % size, len(data), source))

    def drop(self, dest):
        if dest.exists() and not self.dry:
            dest.unlink()

    def pair(self, root, ref, box, dest1, dest2, maxw=None, fmt="webp"):
        """One picture at 1x and 2x. Returns the 1x size [w, h] (+ [2] when the @2x file exists), or None."""
        f1, f2 = render(root, ref, 1), render(root, ref, 2)
        if f1 is None:
            self.rows.append((str(dest1), "-", 0, "MISSING " + ref))
            return None
        one = Image.open(f1)
        if box:
            one = one.crop(box)
        if self.painter:
            one, n = self.painter.paint(one, 1)
            if n:
                self.painted.append(str(dest1))
        k = 1.0 if not maxw or one.width <= maxw else maxw / one.width
        if k != 1.0:
            one = one.resize((maxw, round(one.height * k)), Image.LANCZOS)
        enc = (lambda im, q: webp(im, q)) if fmt == "webp" else (lambda im, q: png(im))
        self.put(dest1, enc(one, 92), one.size, str(f1))
        dims = [one.width, one.height]
        if f2 is None:
            self.drop(dest2)
            self.rows.append((str(dest2), "-", 0, "no 2x render of " + ref))
            return dims
        two = Image.open(f2)
        if box:
            two = two.crop(tuple(2 * v for v in box))
        if self.painter:
            two, n = self.painter.paint(two, 2)
            if n:
                self.painted.append(str(dest2))
        want = (2 * one.width, 2 * one.height)
        if two.size != want:
            # (a fitted picture, or a render a pixel off from rounding: scaled to exactly twice the 1x file)
            if abs(two.width - want[0]) > max(2, want[0] // 100) and k == 1.0:
                self.rows.append((str(dest2), "%dx%d" % two.size, 0, "SIZE MISMATCH, expected %dx%d: not written" % want))
                self.drop(dest2)
                return dims
            two = two.resize(want, Image.LANCZOS)
        self.put(dest2, enc(two, 88), two.size, str(f2))
        return dims + [2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("renders")
    ap.add_argument("--site", default="website")
    ap.add_argument("--docs", default="docs/img")
    ap.add_argument("--store", help="the Store screenshots folder (default: store/screenshots beside the site folder)")
    ap.add_argument("--logo-was", help="the logo the renders' build draws (repaint its button with the brand's mark)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    root, site, docs = Path(a.renders), Path(a.site), Path(a.docs)
    img, src = site / "assets/img", site / "_src"
    w = Writer(a.dry_run, LogoPainter(a.logo_was) if a.logo_was else None)

    sizes = {}
    for name, (ref, box) in TOP.items():
        d = w.pair(root, ref, box, img / (name + ".webp"), img / (name + "@2x.webp"))
        if d:
            sizes["/assets/img/%s.webp" % name] = d[:2]

    for name, (ref, box) in README_PICTURES.items():
        w.pair(root, ref, box, docs / (name + ".png"), docs / (name + "@2x.png"), fmt="png")

    # the Store screenshots (1x), the video poster and the social preview, from the hero's render
    store = Path(a.store) if a.store else site.resolve().parent / "store" / "screenshots"
    for name, ref in STORE.items():
        f1 = render(root, ref, 1)
        if f1 is None:
            w.rows.append((str(store / (name + ".png")), "-", 0, "MISSING " + ref))
            continue
        one = Image.open(f1)
        if w.painter:
            one, _ = w.painter.paint(one, 1)
        w.put(store / (name + ".png"), png(one), one.size, str(f1))
    two = render(root, POSTER, 2)
    big = two or render(root, POSTER, 1)
    if big is None:
        w.rows.append((str(site / "assets/media/poster.jpg"), "-", 0, "MISSING " + POSTER))
    else:
        im = Image.open(big)
        if w.painter:
            im, _ = w.painter.paint(im, 2 if two else 1)
        out = io.BytesIO()
        im.convert("RGB").resize((1280, 720), Image.LANCZOS).save(out, "JPEG", quality=88, optimize=True, progressive=True)
        w.put(site / "assets/media/poster.jpg", out.getvalue(), (1280, 720), str(big))
        social = docs / "social-preview.png"
        tool = Path(__file__).resolve().parent / "social-preview.py"
        tmp = Path(str(social) + ".new.png")
        import subprocess
        done = subprocess.run([sys.executable, str(tool), "--window", str(big), "--out", str(tmp)], capture_output=True, text=True)
        if done.returncode != 0 or not tmp.is_file():
            w.rows.append((str(social), "-", 0, "MISSING: social-preview.py failed: " + (done.stderr or done.stdout).strip()))
        else:
            data = tmp.read_bytes()
            tmp.unlink()
            w.put(social, data, Image.open(io.BytesIO(data)).size, "social-preview.py + " + str(big))

    catalog = src / "catalog.json"
    if catalog.is_file():
        dims = {}
        for p in json.loads(catalog.read_text(encoding="utf-8")):
            if not p.get("img"):
                continue
            d = w.pair(root, p["img"], None, img / "plugins" / (p["id"] + ".webp"), img / "plugins" / (p["id"] + "@2x.webp"), maxw=960)
            if d:
                dims[p["id"]] = d
        if not a.dry_run:
            (src / "catalog-img.json").write_text(json.dumps(dims, indent=0, sort_keys=True) + "\n", encoding="utf-8")
        used = set()
        for f in list((src / "wiki").glob("*.md")) + list((src / "pages").glob("*.html")):
            used |= set(re.findall(r"/assets/img/wiki/([\w.-]+)\.webp", f.read_text(encoding="utf-8")))
        for name in sorted(used):
            snap, box = CROPS.get(name, (name, None))
            if render(root, "snap/s-%s.png" % snap, 1) is None and (img / "wiki" / (name + ".webp")).is_file():
                w.rows.append((str(img / "wiki" / (name + ".webp")), "-", 0, "kept: no render (a picture made by hand)"))
                continue
            d = w.pair(root, "snap/s-%s.png" % snap, box, img / "wiki" / (name + ".webp"), img / "wiki" / (name + "@2x.webp"), maxw=1600)
            if d:
                sizes["/assets/img/wiki/%s.webp" % name] = d[:2]
        old = src / "img-sizes.json"
        if old.is_file():
            merged = json.loads(old.read_text(encoding="utf-8"))
            merged.update(sizes)
            sizes = merged
        if not a.dry_run:
            old.write_text(json.dumps(sizes, indent=0, sort_keys=True) + "\n", encoding="utf-8")
    else:
        print("note: %s not found (the site generator's data): only the Home pictures and the README's were written" % catalog)

    width = max(len(r[0]) for r in w.rows) if w.rows else 10
    print("%-*s  %-11s %9s  %s" % (width, "file", "pixels", "bytes", "from"))
    for f, px, n, s in w.rows:
        print("%-*s  %-11s %9s  %s" % (width, f, px, n or "-", s))
    if w.painter:
        print("site-shots: logo button repainted %d times in %d files" % (w.painter.count, len(w.painted)))
    written = [r for r in w.rows if r[2]]
    problems = [r for r in w.rows if not r[2]]
    print("site-shots: %d files %s, %d with a note (missing render or no 2x)%s"
          % (len(written), "checked (dry run)" if a.dry_run else "written", len(problems), "" if problems else ""))
    return 1 if any(r[3].startswith(("MISSING", "SIZE")) for r in problems) else 0


if __name__ == "__main__":
    sys.exit(main())
