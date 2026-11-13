#!/usr/bin/env python3
"""Writes every LYNX SHARE logo picture from ONE file: resources/brand/lynx-share-logo.svg (the logo by w3teal).

    python3 tools/make_logo.py site   # README, website, social preview, Store images
    python3 tools/make_logo.py app    # app/document icons, Linux icons, MSIX tiles, embedded logo and splash
    python3 tools/make_logo.py all

Needs Pillow and the system librsvg + cairo (librsvg-2.so.2 / libcairo.so.2, present on most Linux desktops; Debian
and Ubuntu: librsvg2-2). To change the logo, replace the SVG (e.g. with the author's own Inkscape file, any canvas
size) and run `all`: everything below is derived from it mechanically, nothing is drawn by hand.

Derived forms (all from the SVG's pixels):
- full: the logo cropped to its outline, centred on a square with a small margin.
- small (16-24 px icons only): the same with the thin "share" line filled in (a morphological closing) and the outer
  ring thickened inward, because at those sizes both are thinner than one pixel and only blur. It is written to
  resources/brand/lynx-share-logo-small.png so the app and the MSIX script use the same picture.
"""
import ctypes
import io
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "resources", "brand", "lynx-share-logo.svg")
SMALL_PNG = os.path.join(ROOT, "resources", "brand", "lynx-share-logo-small.png")
WORK = 2048          # master raster size (the SVG's canvas scaled to this)
SMALL_MAX = 24       # icon sizes up to this use the small form
MARGIN = 0.02        # margin around the outline in the square crops (fraction of the side)


def p(*a):
    return os.path.join(ROOT, *a)


# --- SVG -> RGBA through librsvg + cairo (ctypes, no Python packages beyond Pillow) -----------------------------------
def render_svg(path, size):
    rsvg = ctypes.CDLL("librsvg-2.so.2")
    cairo = ctypes.CDLL("libcairo.so.2")

    class Rect(ctypes.Structure):
        _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("w", ctypes.c_double), ("h", ctypes.c_double)]

    rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
    rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
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

    handle = rsvg.rsvg_handle_new_from_file(path.encode(), None)
    if not handle:
        sys.exit("make_logo: librsvg cannot read " + path)
    surface = cairo.cairo_image_surface_create(0, size, size)  # CAIRO_FORMAT_ARGB32
    cr = cairo.cairo_create(surface)
    if not rsvg.rsvg_handle_render_document(handle, cr, ctypes.byref(Rect(0, 0, size, size)), None):
        sys.exit("make_logo: librsvg could not render " + path)
    cairo.cairo_surface_flush(surface)
    stride = cairo.cairo_image_surface_get_stride(surface)
    raw = ctypes.string_at(cairo.cairo_image_surface_get_data(surface), stride * size)
    cairo.cairo_destroy(cr)
    cairo.cairo_surface_destroy(surface)
    # premultiplied BGRA (little endian) -> straight RGBA
    return Image.frombuffer("RGBa", (size, size), raw, "raw", "BGRa", stride, 1).convert("RGBA")


# --- derived forms ---------------------------------------------------------------------------------------------------
class Logo:
    def __init__(self):
        self.img = render_svg(SVG, WORK)
        self.alpha = self.img.getchannel("A")
        self.bbox = self.alpha.getbbox()
        # the colour: the most common opaque pixel
        opaque = [c for n, c in sorted(self.img.getcolors(1 << 20), reverse=True) if c[3] == 255]
        self.colour = opaque[0][:3]
        # the round badge: its left, right and bottom edges are the circle's
        x0, y0, x1, y1 = self.bbox
        self.r = (x1 - x0) / 2.0
        self.cx = (x0 + x1) / 2.0
        self.cy = y1 - self.r
        self.small = self._small()

    def _paint(self, alpha):
        out = Image.new("RGBA", alpha.size, self.colour + (255,))
        out.putalpha(alpha)
        return out

    def _ring_width(self):
        """Width of the outer ring along the horizontal through the circle's centre (master pixels)."""
        y = int(self.cy)
        x = int(self.bbox[0])
        while x < WORK and self.alpha.getpixel((x, y)) >= 128:
            x += 1
        return x - self.bbox[0]

    def _small(self):
        unit = WORK / 512.0
        a = self.alpha
        # 1. closing (radius 4/512 of the canvas): fills cut-outs thinner than ~8/512, i.e. the "share" line
        m = a.point(lambda v: 255 if v >= 128 else 0)
        for _ in range(int(4 * unit)):
            m = m.filter(ImageFilter.MaxFilter(3))
        for _ in range(int(4 * unit)):
            m = m.filter(ImageFilter.MinFilter(3))
        a = ImageChops.lighter(a, m)
        # 2. ring thickened inward by half its own width
        ring = self._ring_width()
        inner = self.r - ring - ring / 2.0
        band = Image.new("L", a.size, 0)
        d = ImageDraw.Draw(band)
        cx, cy, r = self.cx, self.cy, self.r
        d.ellipse((cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1), fill=255)
        d.ellipse((cx - inner, cy - inner, cx + inner, cy + inner), fill=0)
        return ImageChops.lighter(a, band)

    def square(self, alpha=None, margin=MARGIN):
        """The logo cropped to its outline and centred on a square (RGBA, master resolution)."""
        x0, y0, x1, y1 = self.bbox
        side = max(x1 - x0, y1 - y0)
        half = side / 2.0 * (1 + 2 * margin)
        mx, my = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        box = tuple(int(round(v)) for v in (mx - half, my - half, mx + half, my + half))
        return self._paint(alpha or self.alpha).crop(box)

    def icon(self, size, margin=MARGIN):
        src = self.square(self.small if size <= SMALL_MAX else None, margin)
        return src.resize((size, size), Image.LANCZOS)

    def outline(self, height):
        """The logo cropped to its outline (no margin), scaled to `height`."""
        im = self._paint(self.alpha).crop(self.bbox)
        return im.resize((round(im.width * height / im.height), height), Image.LANCZOS)

    def disc(self, size, margin=MARGIN):
        """Where the round badge sits inside icon(size): (centre x, centre y, radius) in pixels."""
        x0, y0, x1, y1 = self.bbox
        side = max(x1 - x0, y1 - y0) * (1 + 2 * margin)
        ox, oy = (x0 + x1) / 2.0 - side / 2.0, (y0 + y1) / 2.0 - side / 2.0
        k = size / side
        return (self.cx - ox) * k, (self.cy - oy) * k, self.r * k


def save(img, *path, **kw):
    full = p(*path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    img.save(full, optimize=True, **kw)
    print("  " + os.path.join(*path))


def on(colour, size, logo_img, at):
    out = Image.new("RGBA", size, colour + (255,))
    out.alpha_composite(logo_img, at)
    return out


# --- targets ---------------------------------------------------------------------------------------------------------
def site(L):
    save(L.icon(256), "docs", "img", "logo.png")
    save(L.icon(256), "website", "assets", "img", "logo.png")
    save(L.icon(64), "website", "assets", "img", "favicon.png")
    save(L.icon(32), "website", "assets", "img", "favicon-32.png")
    save(L.icon(16), "website", "assets", "img", "favicon-16.png")
    save(L.icon(180, 0.08), "website", "assets", "img", "apple-touch-icon.png")
    with open(SVG, "rb") as f:
        data = f.read()
    with open(p("website", "assets", "img", "lynx-share-logo.svg"), "wb") as f:
        f.write(data)
    print("  website/assets/img/lynx-share-logo.svg")
    save(L.icon(300), "store", "logo-300.png")
    # Store box art: the logo on the app's near-black, as before
    big = L.icon(860, 0)
    save(on((17, 17, 20), (1080, 1080), big, (110, 110)).convert("RGB"), "store", "box-art-1080.png")
    # GitHub social preview: the logo's place left of "LYNX" (a flat background there), badge circle 90 px as before
    sp = Image.open(p("docs", "img", "social-preview.png")).convert("RGBA")
    ImageDraw.Draw(sp).rectangle((60, 70, 172, 200), fill=(14, 15, 19, 255))
    size = 112
    cx, cy, r = L.disc(size, 0.0)
    k = 45.0 / r                     # circle radius 45 px (the old badge's)
    size = round(size * k)
    cx, cy, r = L.disc(size, 0.0)
    mark = L.icon(size, 0.0)
    sp.alpha_composite(mark, (round(117.5 - cx), round(141.5 - cy)))
    save(sp.convert("RGB"), "docs", "img", "social-preview.png")


def app(L):
    full = L.icon(1024)
    save(full, "resources", "lynx_badge.png")             # CMake ICON_BIG (macOS .icns), MSIX tiles
    save(L.icon(256), "resources", "lynx_icon_256.png")   # CMake ICON_SMALL, Linux packages
    save(L.outline(1024), "resources", "lynx_logo.png")   # splash (tools/make_splash.py)
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
    save(L.square(L.small).resize((1024, 1024), Image.LANCZOS), "resources", "brand", "lynx-share-logo-small.png")
    print("logo: colour #%02x%02x%02x" % L.colour, "outline", L.bbox, "badge centre (%.0f, %.0f) radius %.0f of %d" % (L.cx, L.cy, L.r, WORK))
    if what in ("site", "all"):
        site(L)
    if what in ("app", "all"):
        app(L)
    return 0


if __name__ == "__main__":
    sys.exit(main())
