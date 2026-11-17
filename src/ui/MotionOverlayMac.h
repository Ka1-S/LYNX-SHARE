#pragma once

// The macOS part of ui/MotionOverlay (round 190jx): the mark's picture as a CALayer over the view's own layer (JUCE's
// NSView is layer-backed: its CAMetalLayer, or the system's layer), moved with implicit animations off. Core Animation
// composites it with the window: a frame of a moving mark is a new position (and a picture of another sub-pixel phase
// now and then), no paint of the view and no present of the window. No JUCE and no Objective-C types here
// (MotionOverlayMac.mm is Objective-C++).
//
// OFF by default: it was built and checked headless on the CI's macOS runner (a layer in a window that is never shown,
// rendered with renderInContext), not on a screen. LYNX_OVERLAY_MAC=1 turns it on.

#include <cstdint>

namespace lynx::ui::maclayer
{
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0; // device pixels of the view (top-left origin)
    bool empty() const noexcept { return w <= 0 || h <= 0; }
};

// LYNX_OVERLAY_MAC=1 (and not LYNX_OVERLAY=0: MotionOverlay's business)
bool enabled();
// the view's device pixels per point (its window's backingScaleFactor; 1 without a window)
double backingScale (void* nsView);
// a visible subview of `nsView` (a plugin's editor, a native control) over `r` (device pixels): the layer would be drawn
// over it (the caller lets the view paint the mark, which goes under it)
bool subviewOver (void* nsView, Rect r);

class Sprite
{
public:
    Sprite();
    ~Sprite();
    Sprite (const Sprite&) = delete;
    Sprite& operator= (const Sprite&) = delete;

    // JUCE's ARGB image (premultiplied, BGRA in memory), `stride` bytes per row
    bool upload (int slot, const std::uint8_t* pixels, int w, int h, int stride);
    // the picture's top-left at device pixel (x + shift, y) of `nsView`, clipped to `clip`; another view: moved there
    bool place (void* nsView, int slot, int x, int y, double shift, Rect clip);
    void restore(); // the layer goes
    void forget();  // the view is gone: the layer is only let go
    bool drawn() const noexcept { return isDrawn; }
    void setShowing (bool) noexcept {}

    struct State;

private:
    friend const char* selfTest (void*);
    State* s = nullptr;
    bool isDrawn = false;
};

// --selftest: a layer on `nsView` (a window that is never shown), rendered with renderInContext: the mark where it was
// put, nothing outside its clip, gone after restore(). Empty = OK, else what went wrong.
const char* selfTest (void* nsView);
} // namespace lynx::ui::maclayer
