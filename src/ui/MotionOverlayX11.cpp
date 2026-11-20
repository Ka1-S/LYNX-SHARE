// The X11 part of ui/MotionOverlay's Linux / BSD backend (round 190jx): see MotionOverlayX11.h.
//
// libX11 and libXrender are the ones JUCE loads at run time (juce_XSymbols_linux.cpp: dlopen); they are looked up the
// same way here, so LYNX links neither and runs where JUCE runs. Without libXrender (or the Render extension, or an
// ARGB32 picture format) ready() is false and the views paint their marks as before.

#include "ui/MotionOverlayX11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <map>
#include <vector>

namespace lynx::ui::x11overlay
{
namespace
{
struct Api
{
    decltype (&::XLockDisplay) lockDisplay = nullptr;
    decltype (&::XUnlockDisplay) unlockDisplay = nullptr;
    decltype (&::XFlush) flush = nullptr;
    decltype (&::XSync) sync = nullptr;
    decltype (&::XGetGeometry) getGeometry = nullptr;
    decltype (&::XGetWindowAttributes) getWindowAttributes = nullptr;
    decltype (&::XCreatePixmap) createPixmap = nullptr;
    decltype (&::XFreePixmap) freePixmap = nullptr;
    decltype (&::XCreateGC) createGC = nullptr;
    decltype (&::XFreeGC) freeGC = nullptr;
    decltype (&::XCopyArea) copyArea = nullptr;
    decltype (&::XCreateImage) createImage = nullptr;
    decltype (&::XPutImage) putImage = nullptr;
    decltype (&::XGetImage) getImage = nullptr;
    decltype (&::XFillRectangle) fillRectangle = nullptr;
    decltype (&::XSetForeground) setForeground = nullptr;
    decltype (&::XRenderQueryExtension) renderQuery = nullptr;
    decltype (&::XRenderFindVisualFormat) findVisualFormat = nullptr;
    decltype (&::XRenderFindStandardFormat) findStandardFormat = nullptr;
    decltype (&::XRenderCreatePicture) createPicture = nullptr;
    decltype (&::XRenderFreePicture) freePicture = nullptr;
    decltype (&::XRenderComposite) composite = nullptr;
    decltype (&::XRenderSetPictureTransform) setTransform = nullptr;
    decltype (&::XRenderSetPictureFilter) setFilter = nullptr;

    Display* display = nullptr;
    XRenderPictFormat* argb = nullptr;
    int state = 0; // 0 not tried, 1 ready, -1 not available
    const char* why = "not tried";
};

Api& api()
{
    static Api a;
    return a;
}

template <typename F>
bool bind (void* lib, const char* name, F& f)
{
    f = reinterpret_cast<F> (dlsym (lib, name));
    return f != nullptr;
}

bool load (Api& a)
{
    // (already loaded by JUCE: the same library, the same Display structure)
    auto* x11 = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (x11 == nullptr)
        x11 = dlopen ("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    if (x11 == nullptr)
    {
        a.why = "no libX11";
        return false;
    }
    auto* render = dlopen ("libXrender.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (render == nullptr)
        render = dlopen ("libXrender.so", RTLD_LAZY | RTLD_LOCAL);
    if (render == nullptr)
    {
        a.why = "no libXrender";
        return false;
    }
    const auto ok = bind (x11, "XLockDisplay", a.lockDisplay) && bind (x11, "XUnlockDisplay", a.unlockDisplay) && bind (x11, "XFlush", a.flush)
                    && bind (x11, "XSync", a.sync) && bind (x11, "XGetGeometry", a.getGeometry)
                    && bind (x11, "XGetWindowAttributes", a.getWindowAttributes) && bind (x11, "XCreatePixmap", a.createPixmap)
                    && bind (x11, "XFreePixmap", a.freePixmap) && bind (x11, "XCreateGC", a.createGC) && bind (x11, "XFreeGC", a.freeGC)
                    && bind (x11, "XCopyArea", a.copyArea) && bind (x11, "XCreateImage", a.createImage) && bind (x11, "XPutImage", a.putImage)
                    && bind (x11, "XGetImage", a.getImage) && bind (x11, "XFillRectangle", a.fillRectangle)
                    && bind (x11, "XSetForeground", a.setForeground) && bind (render, "XRenderQueryExtension", a.renderQuery)
                    && bind (render, "XRenderFindVisualFormat", a.findVisualFormat) && bind (render, "XRenderFindStandardFormat", a.findStandardFormat)
                    && bind (render, "XRenderCreatePicture", a.createPicture) && bind (render, "XRenderFreePicture", a.freePicture)
                    && bind (render, "XRenderComposite", a.composite) && bind (render, "XRenderSetPictureTransform", a.setTransform)
                    && bind (render, "XRenderSetPictureFilter", a.setFilter);
    if (! ok)
        a.why = "a function of libX11 / libXrender is missing";
    return ok;
}

// JUCE calls XInitThreads; its own calls hold the display's lock (ScopedXLock), so do these
struct Locked
{
    Locked()
    {
        if (api().lockDisplay != nullptr && api().display != nullptr)
            api().lockDisplay (api().display);
    }
    ~Locked()
    {
        if (api().unlockDisplay != nullptr && api().display != nullptr)
            api().unlockDisplay (api().display);
    }
    Locked (const Locked&) = delete;
    Locked& operator= (const Locked&) = delete;
};

std::map<unsigned long, std::vector<Sprite*>>& onWindows()
{
    static std::map<unsigned long, std::vector<Sprite*>> m; // (message thread)
    return m;
}

void addOn (unsigned long window, Sprite* s)
{
    auto& list = onWindows()[window];
    if (std::find (list.begin(), list.end(), s) == list.end())
        list.push_back (s);
}

void removeFrom (unsigned long window, Sprite* s)
{
    auto& all = onWindows();
    if (const auto found = all.find (window); found != all.end())
    {
        auto& list = found->second;
        list.erase (std::remove (list.begin(), list.end(), s), list.end());
        if (list.empty())
            all.erase (found);
    }
}

Rect intersect (Rect a, Rect b)
{
    const auto x0 = std::max (a.x, b.x), y0 = std::max (a.y, b.y);
    const auto x1 = std::min (a.x + a.w, b.x + b.w), y1 = std::min (a.y + a.h, b.y + b.h);
    return x1 > x0 && y1 > y0 ? Rect { x0, y0, x1 - x0, y1 - y0 } : Rect {};
}

int frames = 0;
BlitStats blits;
} // namespace

void setDisplay (void* display)
{
    if (api().display == nullptr)
        api().display = static_cast<Display*> (display);
}

bool ready()
{
    auto& a = api();
    if (a.state != 0)
        return a.state > 0;
    a.state = -1;
    if (a.display == nullptr)
    {
        a.why = "no X display";
        return false;
    }
    if (! load (a))
        return false;
    int events = 0, errors = 0;
    {
        Locked lock;
        if (! a.renderQuery (a.display, &events, &errors))
        {
            a.why = "no Render extension on the X server";
            return false;
        }
        a.argb = a.findStandardFormat (a.display, PictStandardARGB32);
    }
    if (a.argb == nullptr)
    {
        a.why = "no ARGB32 picture format";
        return false;
    }
    a.why = "";
    a.state = 1;
    return true;
}

const char* whyNot()
{
    return api().why;
}

int Sprite::framesDrawn()
{
    return frames;
}

BlitStats blitStats()
{
    return blits;
}

struct Sprite::State
{
    // the drawable the mark is on (a window; the self-test's pixmap), what draws on it
    unsigned long target = 0;
    bool attached = false;
    GC gc = nullptr;        // (target's depth: the copies to and from the pixels saved under the mark)
    Picture targetPicture = 0;
    Pixmap under = 0;       // the pixels under the mark
    int underW = 0, underH = 0;

    struct Picture32
    {
        Pixmap pixmap = 0;
        Picture picture = 0;
        int w = 0, h = 0;
        double shift = 0.0; // the translation set on it
    };
    std::vector<Picture32> pictures = std::vector<Picture32> (64);
    GC gc32 = nullptr; // (depth 32: the uploads)

    // where it is drawn: the rectangle (target pixels) and the picture's top-left
    Rect drawnRect;
    int originX = 0, originY = 0, slot = -1;
    double shift = 0.0;

    bool attach (unsigned long drawable, bool isPixmap)
    {
        auto& a = api();
        Locked lock;
        ::Window root = 0;
        int x = 0, y = 0;
        unsigned int w = 0, h = 0, border = 0, depth = 0;
        if (a.getGeometry (a.display, drawable, &root, &x, &y, &w, &h, &border, &depth) == 0)
            return false;
        XRenderPictFormat* format = nullptr;
        if (isPixmap)
            format = a.findStandardFormat (a.display, depth == 32 ? PictStandardARGB32 : PictStandardRGB24);
        else
        {
            XWindowAttributes atts {};
            if (a.getWindowAttributes (a.display, drawable, &atts) == 0)
                return false;
            format = a.findVisualFormat (a.display, atts.visual);
        }
        if (format == nullptr)
            return false;
        XGCValues values {};
        values.graphics_exposures = False;
        values.subwindow_mode = ClipByChildren; // (a plugin editor's native window over the view hides the mark, as it hides the painted one)
        gc = a.createGC (a.display, drawable, GCGraphicsExposures | GCSubwindowMode, &values);
        XRenderPictureAttributes pa {};
        pa.subwindow_mode = ClipByChildren;
        targetPicture = a.createPicture (a.display, drawable, format, CPSubwindowMode, &pa);
        target = drawable;
        attached = gc != nullptr && targetPicture != 0;
        targetDepth = (int) depth;
        return attached;
    }

    // what is made on the target goes; `freeIt` false: the window is gone (the server freed what was on it)
    void detach (bool freeIt)
    {
        auto& a = api();
        if (! attached && gc == nullptr && targetPicture == 0 && under == 0)
            return;
        Locked lock;
        if (freeIt && targetPicture != 0)
            a.freePicture (a.display, targetPicture);
        if (gc != nullptr)
            a.freeGC (a.display, gc);
        if (under != 0)
            a.freePixmap (a.display, under);
        gc = nullptr;
        targetPicture = 0;
        under = 0;
        underW = underH = 0;
        attached = false;
        target = 0;
    }

    void freePictures()
    {
        auto& a = api();
        if (gc32 == nullptr && std::none_of (pictures.begin(), pictures.end(), [] (const Picture32& p) { return p.pixmap != 0; }))
            return;
        Locked lock;
        for (auto& p : pictures)
        {
            if (p.picture != 0)
                a.freePicture (a.display, p.picture);
            if (p.pixmap != 0)
                a.freePixmap (a.display, p.pixmap);
            p = {};
        }
        if (gc32 != nullptr)
            a.freeGC (a.display, gc32);
        gc32 = nullptr;
    }

    bool ensureUnder (int w, int h)
    {
        if (under != 0 && underW >= w && underH >= h)
            return true;
        auto& a = api();
        if (under != 0)
            a.freePixmap (a.display, under);
        underW = std::max (w, underW);
        underH = std::max (h, underH);
        under = a.createPixmap (a.display, target, (unsigned int) underW, (unsigned int) underH, (unsigned int) targetDepth);
        return under != 0;
    }

    void setShift (Picture32& p, double s)
    {
        if (p.shift == s)
            return;
        p.shift = s;
        XTransform t {};
        t.matrix[0][0] = XDoubleToFixed (1.0);
        t.matrix[1][1] = XDoubleToFixed (1.0);
        t.matrix[2][2] = XDoubleToFixed (1.0);
        t.matrix[0][2] = XDoubleToFixed (-s); // (source = destination - shift: the picture moves right by `s`)
        api().setTransform (api().display, p.picture, &t);
    }

    // the mark's picture over `r` (target pixels, inside drawnRect)
    void drawOver (Rect r)
    {
        auto& p = pictures[(std::size_t) slot];
        api().composite (api().display, PictOpOver, p.picture, 0, targetPicture, r.x - originX, r.y - originY, 0, 0, r.x, r.y,
                         (unsigned int) r.w, (unsigned int) r.h);
    }

    void save (Rect r)
    {
        api().copyArea (api().display, target, under, gc, r.x, r.y, (unsigned int) r.w, (unsigned int) r.h, r.x - originX, r.y - originY);
    }

    void putBack()
    {
        const auto r = drawnRect;
        api().copyArea (api().display, under, target, gc, r.x - originX, r.y - originY, (unsigned int) r.w, (unsigned int) r.h, r.x, r.y);
    }

    int targetDepth = 24;
};

Sprite::Sprite() : s (new State()) {}

Sprite::~Sprite()
{
    restore();
    s->detach (true);
    s->freePictures();
    delete s;
}

bool Sprite::upload (int slot, const std::uint8_t* pixels, int w, int h, int stride)
{
    if (! ready() || slot < 0 || slot >= (int) s->pictures.size() || w <= 0 || h <= 0)
        return false;
    auto& a = api();
    auto& p = s->pictures[(std::size_t) slot];
    Locked lock;
    const auto root = DefaultRootWindow (a.display);
    if (p.pixmap == 0 || p.w != w || p.h != h)
    {
        if (p.picture != 0)
            a.freePicture (a.display, p.picture);
        if (p.pixmap != 0)
            a.freePixmap (a.display, p.pixmap);
        p = {};
        p.pixmap = a.createPixmap (a.display, root, (unsigned int) w, (unsigned int) h, 32);
        if (p.pixmap == 0)
            return false;
        p.picture = a.createPicture (a.display, p.pixmap, a.argb, 0, nullptr);
        if (p.picture == 0)
            return false;
        // the rest of a sub-pixel step (setShift) is sampled between pixels; outside the picture is transparent
        a.setFilter (a.display, p.picture, FilterBilinear, nullptr, 0);
        p.w = w;
        p.h = h;
        p.shift = 0.0;
        if (s->gc32 == nullptr)
            s->gc32 = a.createGC (a.display, p.pixmap, 0, nullptr);
    }
    // JUCE's ARGB image: premultiplied, 0xAARRGGBB in the machine's byte order, as Render's a8r8g8b8 (Xlib swaps for a
    // server of the other byte order)
    auto* image = a.createImage (a.display, nullptr, 32, ZPixmap, 0, reinterpret_cast<char*> (const_cast<std::uint8_t*> (pixels)), (unsigned int) w,
                                 (unsigned int) h, 32, stride);
    if (image == nullptr)
        return false;
    a.putImage (a.display, p.pixmap, s->gc32, image, 0, 0, 0, 0, (unsigned int) w, (unsigned int) h);
    image->data = nullptr; // (the pixels are the caller's)
    XDestroyImage (image);
    return true;
}

bool Sprite::hasPicture (int slot) const noexcept
{
    return slot >= 0 && slot < (int) s->pictures.size() && s->pictures[(std::size_t) slot].picture != 0;
}

bool Sprite::placeOn (unsigned long drawable, bool isPixmap, int slot, int x, int y, double shift, Rect clip)
{
    auto& st = *s;
    if (! ready() || slot < 0 || slot >= (int) st.pictures.size() || st.pictures[(std::size_t) slot].picture == 0)
        return false;
    auto& a = api();
    if (st.attached && st.target != drawable)
    {
        if (isDrawn)
        {
            Locked lock;
            st.putBack();
            a.flush (a.display);
            removeFrom (st.target, this);
            isDrawn = false;
        }
        st.detach (true);
    }
    if (! st.attached && ! st.attach (drawable, isPixmap))
    {
        st.detach (true);
        return false;
    }
    const auto& p = st.pictures[(std::size_t) slot];
    const auto r = intersect ({ x, y, p.w, p.h }, clip);
    // the same place, picture and sub-pixel step: it is there already
    if (isDrawn && st.slot == slot && st.originX == x && st.originY == y && st.shift == shift && st.drawnRect.x == r.x && st.drawnRect.y == r.y
        && st.drawnRect.w == r.w && st.drawnRect.h == r.h)
        return true;
    Locked lock;
    if (isDrawn)
        st.putBack(); // (before the new place is saved: where they overlap the saved pixels are the window's own)
    isDrawn = false;
    if (r.empty())
    {
        removeFrom (st.target, this);
        a.flush (a.display);
        return true; // (none of it in view)
    }
    if (! st.ensureUnder (p.w, p.h))
    {
        removeFrom (st.target, this);
        a.flush (a.display);
        return false;
    }
    st.originX = x;
    st.originY = y;
    st.slot = slot;
    st.shift = shift;
    st.drawnRect = r;
    st.save (r);
    st.setShift (st.pictures[(std::size_t) slot], shift);
    if (showing)
        st.drawOver (r);
    a.flush (a.display);
    isDrawn = true;
    addOn (st.target, this);
    ++frames;
    return true;
}

bool Sprite::place (unsigned long window, int slot, int x, int y, double shift, Rect clip)
{
    return placeOn (window, false, slot, x, y, shift, clip);
}

void Sprite::restore()
{
    if (! isDrawn)
        return;
    isDrawn = false;
    removeFrom (s->target, this);
    if (! s->attached || ! ready())
        return;
    Locked lock;
    s->putBack();
    api().flush (api().display);
}

void Sprite::forget()
{
    if (isDrawn)
        removeFrom (s->target, this);
    isDrawn = false;
    s->detach (false);
}

void Sprite::blitOver (const Rect* rects, int n)
{
    if (! isDrawn || ! s->attached)
        return;
    auto any = false;
    for (int i = 0; i < n; ++i)
    {
        const auto r = intersect (rects[i], s->drawnRect);
        if (r.empty())
            continue;
        // JUCE's pixels there are new (the view's, without the mark while it is showing here): saved again, and the mark
        // over them again
        s->save (r);
        if (showing)
            s->drawOver (r);
        any = true;
    }
    if (any)
        api().flush (api().display);
}

void afterBlit (unsigned long window, const Rect* rects, int n)
{
    ++blits.blits;
    for (int i = 0; i < n; ++i)
        blits.pixels += (std::int64_t) rects[i].w * rects[i].h;
    auto& all = onWindows();
    const auto found = all.find (window);
    if (found == all.end() || api().state <= 0)
        return;
    const auto sprites = found->second; // (a copy: nothing here changes the list, but be safe)
    Locked lock;
    for (auto* sprite : sprites)
        sprite->blitOver (rects, n);
}

const char* selfTestOnServer()
{
    if (! ready())
        return whyNot();
    auto& a = api();
    const int W = 64, H = 48;
    Pixmap target = 0;
    GC fill = nullptr;
    const auto depth = (unsigned int) DefaultDepth (a.display, DefaultScreen (a.display));
    {
        Locked lock;
        target = a.createPixmap (a.display, DefaultRootWindow (a.display), W, H, depth);
        if (target == 0)
            return "no pixmap";
        fill = a.createGC (a.display, target, 0, nullptr);
    }
    auto paint = [&] (unsigned long colour) {
        Locked lock;
        a.setForeground (a.display, fill, colour);
        a.fillRectangle (a.display, target, fill, 0, 0, W, H);
    };
    // the pixel at (x, y), 0xRRGGBB
    auto pixelAt = [&] (int x, int y) -> unsigned long {
        Locked lock;
        auto* image = a.getImage (a.display, target, x, y, 1, 1, AllPlanes, ZPixmap);
        if (image == nullptr)
            return 0xffffffffUL;
        const auto v = XGetPixel (image, 0, 0) & 0xffffffUL;
        XDestroyImage (image);
        return v;
    };
    const char* result = "";
    {
        paint (0x203040);
        // a 1 px opaque red line at x = 2 of a 6 x 48 picture (transparent around it)
        std::vector<std::uint32_t> pixels (6 * 48, 0u);
        for (int y = 0; y < 48; ++y)
            pixels[(std::size_t) (y * 6 + 2)] = 0xffff0000u;
        Sprite sprite;
        if (! sprite.upload (0, reinterpret_cast<const std::uint8_t*> (pixels.data()), 6, 48, 6 * 4))
            result = "picture not sent";
        else if (! sprite.placeOn (target, true, 0, 20, 0, 0.5, { 0, 0, W, 40 }))
            result = "not drawn";
        else
        {
            // half a pixel to the right: x = 22 and 23 share the line; under the clip (y >= 40) nothing
            const auto mid = pixelAt (22, 10), next = pixelAt (23, 10), clipped = pixelAt (22, 44), beside = pixelAt (10, 10);
            const auto red = [] (unsigned long v) { return (int) ((v >> 16) & 0xff); };
            if (beside != 0x203040 || clipped != 0x203040)
                result = "drawn outside the mark or its clip";
            else if (std::abs (red (mid) - red (next)) > 24 || red (mid) < 0x70 || red (mid) > 0xb0)
                result = "the sub-pixel step is not half a pixel";
            else
            {
                // a blit of JUCE over it (here: a new colour, then the hook): drawn again over the new pixels
                paint (0x405060);
                Rect all { 0, 0, W, H };
                sprite.blitOver (&all, 1);
                if (red (pixelAt (22, 10)) < 0x70)
                    result = "not drawn again after a blit";
                else
                {
                    sprite.restore();
                    if (pixelAt (22, 10) != 0x405060 || pixelAt (23, 10) != 0x405060)
                        result = "the pixels under the mark were not put back";
                }
            }
        }
    } // (the sprite puts back what it drew and frees its picture, GC and pixmaps: the target is still there)
    {
        Locked lock;
        a.freeGC (a.display, fill);
        a.freePixmap (a.display, target);
        a.sync (a.display, False);
    }
    return result;
}
} // namespace lynx::ui::x11overlay
