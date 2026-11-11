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
"""
import json
import re
import sys
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parent
OUT = SRC.parent / "assets/img"


def save(im, dest, maxw):
    im = im.convert("RGB")
    if im.width > maxw:
        im = im.resize((maxw, round(im.height * maxw / im.width)), Image.LANCZOS)
    dest.parent.mkdir(parents=True, exist_ok=True)
    im.save(dest, "WEBP", quality=84, method=6)
    return [im.width, im.height]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    renders = Path(sys.argv[1])
    dims, missing = {}, []
    for p in json.loads((SRC / "catalog.json").read_text(encoding="utf-8")):
        f = renders / p.get("img", "")
        if p.get("img") and f.is_file():
            dims[p["id"]] = save(Image.open(f), OUT / "plugins" / (p["id"] + ".webp"), 960)
        else:
            missing.append(p["id"])
    (SRC / "catalog-img.json").write_text(json.dumps(dims, indent=0, sort_keys=True) + "\n", encoding="utf-8")
    used = set()
    for f in list((SRC / "wiki").glob("*.md")) + list((SRC / "pages").glob("*.html")):
        used |= set(re.findall(r"/assets/img/wiki/([\w.-]+)\.webp", f.read_text(encoding="utf-8")))
    nowiki = []
    for name in sorted(used):
        f = renders / "snap" / ("s-%s.png" % name)
        if f.is_file():
            save(Image.open(f), OUT / "wiki" / (name + ".webp"), 1600)
        else:
            nowiki.append(name)
    print("import-shots: %d plugin pictures (%d plugins without one: %s), %d wiki pictures (%d not found: %s)"
          % (len(dims), len(missing), ", ".join(missing) or "-", len(used) - len(nowiki), len(nowiki), ", ".join(nowiki) or "-"))
    return 1 if nowiki else 0


if __name__ == "__main__":
    sys.exit(main())
