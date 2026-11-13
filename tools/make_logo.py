#!/usr/bin/env python3
"""Writes every LYNX SHARE logo picture from the brand's SVGs (the logo by w3teal, in the app-icon form chosen in 2026-11:
w3teal's lynx in the red ring, a small green pepper stem on the ring, on a dark rounded-square tile).

    python3 tools/make_logo.py site   # README, website, social preview, Store images
    python3 tools/make_logo.py app    # app/document icons, macOS/Linux icons, embedded logo, splash source
    python3 tools/make_logo.py all

Sources (resources/brand/):
- lynx-share-logo.svg: the master. Groups: "tile" (the dark tile), "mark" (w3teal's lynx in the ring) and inside the
  mark "stem" (the pepper stem).
- lynx-share-logo-small.svg: the same icon for 32 px and less: share lines and ring thicker, the mark larger in the tile
  (at those sizes the master's lines are thinner than a pixel). The stem is left out at 20 px and less, where it is
  only a stray pixel.
Derived and written here: lynx-share-mark.svg (the master's mark without the tile, in the mark's own 512-unit frame) and
the PNGs the app, the MSIX script and the site scripts read.

Needs Pillow and the system librsvg + cairo (librsvg-2.so.2 / libcairo.so.2, present on most Linux desktops; Debian
and Ubuntu: librsvg2-2). Nothing below is drawn by hand: replace the SVGs and run `all`.
"""
import ctypes
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRAND = os.path.join(ROOT, "resources", "brand")
MASTER = os.path.join(BRAND, "lynx-share-logo.svg")
SMALL = os.path.join(BRAND, "lynx-share-logo-small.svg")
SMALL_MAX = 32       # icons up to this size use the small-size form
STEM_MIN = 24        # ... and below this size leave the stem out
TILE = (27, 29, 34)  # the tile's colour (#1b1d22)


def p(*a):
    return os.path.join(ROOT, *a)


# --- SVG -> RGBA through librsvg + cairo (ctypes, no Python packages beyond Pillow) -----------------------------------
def render_svg(text, size):
    rsvg = ctypes.CDLL("librsvg-2.so.2")
    cairo = ctypes.CDLL("libcairo.so.2")

    class Rect(ctypes.Structure):
        _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("w", ctypes.c_double), ("h", ctypes.c_double)]

    rsvg.rsvg_handle_new_from_data.restype = ctypes.c_void_p
    rsvg.rsvg_handle_new_from_data.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
    rsvg.rsvg_handle_render_document.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(Rect), ctypes.c_void_p]
    cairo.cairo_image_surface_create.restype = ctypes.c_void_p
    cairo.cairo_create.restype = ctypes.c_void_p
    cairo.cairo_create.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]
    cairo.cairo_image_surface_get_data.restype = ctypes.POINTER(ctypes.c_ubyte)
    cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
    cairo.cairo_image_surface_get_stride.argtypes = [ctypes.c_void_p]
    cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]

    data = text.encode()
    handle = rsvg.rsvg_handle_new_from_data(data, len(data), None)
    if not handle:
        sys.exit("make_logo: librsvg cannot read the SVG")
    surface = cairo.cairo_image_surface_create(0, size, size)  # CAIRO_FORMAT_ARGB32
    cr = cairo.cairo_create(surface)
    if not rsvg.rsvg_handle_render_document(handle, cr, ctypes.byref(Rect(0, 0, size, size)), None):
        sys.exit("make_logo: librsvg could not render the SVG")
    cairo.cairo_surface_flush(surface)
    stride = cairo.cairo_image_surface_get_stride(surface)
    raw = ctypes.string_at(cairo.cairo_image_surface_get_data(surface), stride * size)
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surface)
    # premultiplied BGRA (little endian) -> straight RGBA
    return Image.frombuffer("RGBa", (size, size), raw, "raw", "BGRa", stride, 1).convert("RGBA")


def without_group(svg, gid):
    """svg with the group <g id="gid" ...>...</g> removed (groups may nest)."""
    m = re.search(r'<g id="%s"[^>]*>' % gid, svg)
    if not m:
        sys.exit("make_logo: no group %s in the SVG" % gid)
    depth, i = 1, m.end()
    for t in re.finditer(r"<g[\s>]|</g>", svg[i:]):
        depth += -1 if t.group(0) == "</g>" else 1
        if depth == 0:
            return svg[:m.start()] + svg[i + t.end():]
    sys.exit("make_logo: group %s is not closed" % gid)


def mark_only(svg):
    """The mark without its tile, in the mark's own 512-unit frame (its placement in the tile removed)."""
    svg = without_group(svg, "tile")
    return re.sub(r'<g id="mark" transform="[^"]*">', '<g id="mark">', svg, count=1)


# --- the logo and its forms ------------------------------------------------------------------------------------------
class Logo:
    def __init__(self):
        self.master = open(MASTER).read()
        self.small_svg = open(SMALL).read()
        self.mark_svg = mark_only(self.master).replace(
            "<title>", "<!-- generated by tools/make_logo.py from lynx-share-logo.svg: the mark without its tile -->\n<title>", 1)
        self.mark_small_svg = mark_only(self.small_svg)
        self._cache = {}

    def _render(self, svg, size):
        """svg at size x size: rendered at 1024 px (or 2x for big sizes) and scaled down, as every size before."""
        key = (svg, size)
        if key not in self._cache:
            work = max(1024, 2 * size)
            img = render_svg(svg, work)
            self._cache[key] = img if work == size else img.resize((size, size), Image.LANCZOS)
        return self._cache[key]

    def form(self, size):
        """The SVG used at an icon size (master, or the small-size form with or without the stem)."""
        if size > SMALL_MAX:
            return self.master
        return self.small_svg if size >= STEM_MIN else without_group(self.small_svg, "stem")

    def icon(self, size):
        """The app icon (tile + mark) at size x size, with transparent corners."""
        return self._render(self.form(size), size).copy()

    def icon_on_plate(self, size):
        """The app icon on a full square of the tile's colour (for places that cut their own corners: iOS)."""
        out = Image.new("RGBA", (size, size), TILE + (255,))
        out.alpha_composite(self.icon(size))
        return out

    def mark(self, size, small=False):
        """The mark (with its stem) without the tile, in its 512-unit frame (transparent)."""
        svg = self.mark_small_svg if small else self.mark_svg
        if small and size < STEM_MIN:
            svg = without_group(svg, "stem")
        return self._render(svg, size).copy()

    def placed(self, canvas, fraction):
        """The icon at `fraction` of a transparent square canvas, centred (macOS icon grid)."""
        out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        side = round(canvas * fraction)
        out.alpha_composite(self.icon(side), ((canvas - side) // 2,) * 2)
        return out


def save(img, *path, **kw):
    full = p(*path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    img.save(full, optimize=True, **kw)
    print("  " + os.path.join(*path))


def write_text(text, *path):
    full = p(*path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text)
    print("  " + os.path.join(*path))


def brand(L):
    write_text(L.mark_svg, "resources", "brand", "lynx-share-mark.svg")
    # the small-size forms as pictures (the MSIX script and the site's header repaint read them)
    save(L._render(L.small_svg, 1024), "resources", "brand", "lynx-share-logo-small.png")
    save(L._render(without_group(L.small_svg, "stem"), 1024), "resources", "brand", "lynx-share-logo-tiny.png")
    save(L.mark(1024, small=True), "resources", "brand", "lynx-share-mark-small.png")


# --- targets ---------------------------------------------------------------------------------------------------------
def site(L):
    save(L.icon(256), "docs", "img", "logo.png")
    save(L.icon(256), "website", "assets", "img", "logo.png")
    save(L.icon(64), "website", "assets", "img", "favicon.png")
    save(L.icon(32), "website", "assets", "img", "favicon-32.png")
    save(L.icon(16), "website", "assets", "img", "favicon-16.png")
    save(L.icon_on_plate(180), "website", "assets", "img", "apple-touch-icon.png")
    write_text(L.master, "website", "assets", "img", "lynx-share-logo.svg")
    save(L.icon(300), "store", "logo-300.png")
    # Store box art: the icon on the app's near-black
    art = Image.new("RGBA", (1080, 1080), (17, 17, 20, 255))
    art.alpha_composite(L.icon(800), (140, 140))
    save(art.convert("RGB"), "store", "box-art-1080.png")
    # GitHub social preview: drawn whole (text, this icon, the website's hero picture) by scripts/social-preview.py
    import subprocess
    subprocess.run([sys.executable, p("scripts", "social-preview.py")], check=True)


def app(L):
    save(L.icon(1024), "resources", "lynx_badge.png")         # the icon at 1024 px: LogoData.h, MSIX tiles
    save(L.icon(256), "resources", "lynx_icon_256.png")       # Linux packages
    save(L.mark(1024), "resources", "lynx_logo.png")          # the mark alone (tools/make_logo_data.py: plugin headers)
    # macOS (CMake ICON_BIG / ICON_SMALL -> .icns): Apple's icon grid, the tile 824 of 1024 px with a transparent margin
    save(L.placed(1024, 824 / 1024), "resources", "macos", "lynx-icon-1024.png")
    save(L.placed(256, 824 / 1024), "resources", "macos", "lynx-icon-256.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    ico = [L.icon(s) for s in sizes]
    ico[-1].save(p("resources", "LYNX.ico"), format="ICO", sizes=[(s, s) for s in sizes], append_images=ico[:-1])
    print("  resources/LYNX.ico")
    for s in (16, 24, 32, 48, 64, 128, 256, 512):
        save(L.icon(s), "resources", "linux", "icons", "hicolor", "%dx%d" % (s, s), "apps", "lynx.png")


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what not in ("site", "app", "all"):
        sys.exit(__doc__)
    L = Logo()
    brand(L)
    if what in ("site", "all"):
        site(L)
    if what in ("app", "all"):
        app(L)
    return 0


if __name__ == "__main__":
    sys.exit(main())
