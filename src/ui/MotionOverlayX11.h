#pragma once

// The X11 part of ui/MotionOverlay's Linux / BSD backend (round 190jx): a moving mark drawn by the X server, with the
// pixels under it saved and put back by the server ("save-under", as a software cursor). No JUCE and no X11 types here:
// MotionOverlay.cpp works in JUCE's world, MotionOverlayX11.cpp in Xlib's (whose macros clash with JUCE's names).
//
// A frame of a moving mark = four requests on JUCE's own display connection (the order of the requests is the order in
// which the server draws, so nothing JUCE blits can land between them): the pixels under the old place back, the pixels
// under the new place saved, the mark's picture composited there (XRender, Over, a sub-pixel translation sampled
// bilinearly), a flush. No paint of the view, no image sent: the picture of the mark was sent once.
//
// JUCE's blits (cmake/LynxPatchJuce.cmake fix 41, lynxX11AfterBlitHook) replace the window's pixels in their rectangles:
// where they meet a mark, the pixels under it are saved again (they are new) and the mark is drawn over them again.
// The X server clips the drawing to what of the window is visible: under a native child window (a plugin editor) or a
// sibling window the mark is hidden, as the painted one is.

#include <cstdint>

namespace lynx::ui::x11overlay
{
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const noexcept { return w <= 0 || h <= 0; }
};

// JUCE's display connection (juce::lynxX11Display, fix 41), given once by MotionOverlay.cpp before ready()
void setDisplay (void* display);
// libX11 + libXrender loaded, JUCE's display open, the Render extension there with an ARGB32 format. LYNX_OVERLAY=0 is
// MotionOverlay's business.
bool ready();
// --selftest: why not, when ready() is false
const char* whyNot();

class Sprite
{
public:
    Sprite();
    ~Sprite();
    Sprite (const Sprite&) = delete;
    Sprite& operator= (const Sprite&) = delete;

    // the picture of one phase: premultiplied ARGB (JUCE's ARGB image), `stride` bytes per row. False: the server refused.
    bool upload (int slot, const std::uint8_t* pixels, int w, int h, int stride);
    bool hasPicture (int slot) const noexcept;

    // The picture of `slot` with its top-left at window pixel (x, y), moved right by `shift` (0 <= shift < 1, sampled
    // bilinearly), only inside `clip` (window pixels). On `window` (an X window id); another window than the last call's
    // puts the old place back first. False: nothing could be drawn (the caller paints the mark).
    bool place (unsigned long window, int slot, int x, int y, double shift, Rect clip);

    // The pixels under the mark back (the mark is gone from the window).
    void restore();
    // forget the window (it is gone or going): nothing is drawn or put back on it any more
    void forget();

    // the mark is on a window now (drawn by place, not restored yet)
    bool drawn() const noexcept { return isDrawn; }
    // false: JUCE's blits over it only refresh what is saved under it (the view paints the mark itself meanwhile)
    void setShowing (bool on) noexcept { showing = on; }

    // round trips: draws so far (diagnostics)
    static int framesDrawn();

    struct State;

private:
    friend void afterBlit (unsigned long, const Rect*, int);
    friend const char* selfTestOnServer();
    bool placeOn (unsigned long drawable, bool isPixmap, int slot, int x, int y, double shift, Rect clip);
    void blitOver (const Rect* rects, int n);
    State* s = nullptr;
    bool isDrawn = false, showing = true;
};

// JUCE blitted these rectangles (window pixels) of `window` (fix 41's hook, message thread): the marks there are drawn
// again over the new pixels
void afterBlit (unsigned long window, const Rect* rects, int n);

// --selftest, on the server: a pixmap filled with a colour stands for a window; a picture drawn on it at a sub-pixel
// place and clipped, the pixels read back (XGetImage), then put back and read again. Empty = OK, else what went wrong.
const char* selfTestOnServer();

// JUCE's blits counted (presents of the X11 peers: XShmPutImage / XPutImage), and their pixels, since the start
struct BlitStats
{
    std::int64_t blits = 0, pixels = 0;
};
BlitStats blitStats();
} // namespace lynx::ui::x11overlay
