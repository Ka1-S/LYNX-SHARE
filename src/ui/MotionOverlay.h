#pragma once

// GPU overlay for moving marks (round 190ip; proposal 4 of round 190hc): a playhead, a locator or a position line is
// composited over its view by the system compositor instead of repainting the part of the view under it on every frame.
//
// Before, each playhead frame repainted a column of its view (the Playlist blitted its cached picture there, the Piano
// roll and the Event editor painted that column again), and the window then presented a new frame (a docked panel: the
// main window presented one more, cmake/LynxPatchJuce.cmake fix 34). With the overlay, a frame only moves a small
// picture of the mark: no paint, no present.
//
// Windows: a DirectComposition visual in a topmost composition target of the view's window (above the window's own
// Direct2D swap chain; the system keeps it under the sibling windows that cover that window). Its picture is painted once
// per look (by the view's own paint code, at 1/32-pixel phases, on first use) and only moved afterwards; a frame = an
// offset and a clip, and one Commit. Linux / macOS: not available yet; show() returns false and the view paints the mark
// as before.
//
// The view's own path stays the automatic fallback: show() returns false whenever the overlay cannot show the mark
// exactly where the view would draw it (another platform, LYNX_OVERLAY=0, the window drawn by the CPU (Settings ›
// General › Draw with the GPU off), no device yet, a component or a native window of the view's window covering the
// mark, a transformed view). The view then paints the mark itself, as before this round.
//
// Pixel-identical when nothing moves: the views use the overlay only while their mark moves (the transport plays). When
// it stops, they paint the mark themselves again and hide the overlay a few frames later (hide (true)), so there is no
// frame without the mark.
//
// Use (message thread):
//     ui::MotionOverlay overlay { *this, [this] (juce::Graphics& g, float x) { paintMark (g, x); } };
//     // each frame while it moves:
//     if (overlay.show ({ x, extentAroundX, clip, lookKey }))  -> the view does not paint the mark (paint() asks isShowing())
//     else                                                     -> repaint the mark's column, as before
//     // stopped: overlay.hide (true) and repaint the mark's column once
//     overlay.onHidden = [this] { repaint (column); }; // it hid itself (no show() for a while): paint the mark again
//
// The painter draws only the mark, in owner coordinates, at horizontal position x (the same code as the view's paint),
// clipped as the view clips it vertically. Horizontally the overlay clips it to Mark::clip.
//
// LYNX_OVERLAY=0: never (the view paints every frame, as before). LYNX_OVERLAY_STATS=<file>: shows / fallbacks / pictures
// painted, every 2 s.

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace lynx::ui
{
class MotionOverlay final
{
public:
    using Painter = std::function<void (juce::Graphics&, float x)>;

    // overChildren: the view draws the mark over its own child components (the Playlist's cached picture); false: under
    // them, so a child the mark crosses makes it fall back
    MotionOverlay (juce::Component& owner, Painter painter, bool overChildren = false);
    ~MotionOverlay();

    struct Mark
    {
        float x = 0.0f;                  // owner coordinates
        juce::Rectangle<int> extent;     // what the painter draws, around x = 0 (e.g. { -8, 0, 16, getHeight() })
        juce::Rectangle<int> clip;       // owner coordinates: nothing outside it is shown
        std::uint64_t look = 0;          // anything the picture depends on apart from x (colours, heights): a new value paints it again
    };

    // True: the overlay shows the mark there (the view must not paint it). False: the view paints it itself.
    bool show (const Mark& mark);

    // afterFrames: it stays for a few frames more (the view's repaint of the mark, asked for at the same time, is on screen
    // before it goes)
    void hide (bool afterFrames = false);

    bool isShowing() const noexcept;

    // the overlay hid itself (no show() for ~100 ms, the view's window changed, the device was lost): the view paints the
    // mark again from now on
    std::function<void()> onHidden;

    // this platform has the overlay and LYNX_OVERLAY is not 0 (the device may still be on its way)
    static bool available();

    // diagnostics (--selftest, LYNX_OVERLAY_STATS): frames shown by an overlay, show() calls that fell back, pictures painted
    static std::atomic<int>& framesShown();
    static std::atomic<int>& fallbacks();
    static std::atomic<int>& picturesPainted();

    // --selftest: the device (made at once on this thread, not in the background)
    static bool prepareNow();

    // --selftest: Windows: the device, a composition target on a hidden window, a picture painted and committed, the
    // fallback for a view that is not showing; elsewhere "OK (not available)"
    static juce::String selfTest();

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

// LYNX_LOCATOR_PROBE=1 (measurements, round 190ip): a floating window like a plugin editor (a static picture that is
// costly to paint) with a 1 px locator and a short trail that moves 30 times a second while the song plays, as round
// 190ih's Gross Beat locator. It is moved by the overlay, or (LYNX_OVERLAY=0) by repainting its column.
// position: where the locator is, 0..1, or < 0 when stopped (message thread).
void openLocatorProbe (std::function<double()> position);

// The common pattern of a view whose mark moves while the transport plays (the Playlist, Piano roll and Event editor
// playheads): each frame, step (x, moving) either moves the overlay or says which columns to repaint.
//   onOverlay: the overlay shows the mark this frame; flipped: it changed this frame (the view repaints the mark's column
//   once: to take its painted mark away, or to paint it again)
struct MarkStep
{
    bool onOverlay = false, flipped = false;
};

//   stays: the mark is still there when it stops moving (the view paints it: the overlay stays a few frames more); false:
//   it is gone (the overlay goes at once)
inline MarkStep stepMark (MotionOverlay& overlay, bool moving, const MotionOverlay::Mark& mark, bool stays = true)
{
    const auto was = overlay.isShowing();
    auto now = false;
    if (moving)
        now = overlay.show (mark);
    else if (was)
        overlay.hide (stays);
    return { now, now != was };
}
} // namespace lynx::ui
