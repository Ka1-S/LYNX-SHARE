#pragma once

// Round 190qe (research190qa TOP-NEXT R1, "Feel 1: dragging and the view"): the editors' drag and view helpers. No JUCE
// here: tested in tests/Feel190qeTests.inc; the Playlist, the Piano roll (notes, marquee, the event lanes, vertical), the
// event editor window and the Clip-launch grid use them.
//
//  - edgescroll (D2-PL1, UX-A11): the view runs while something is dragged past its edge.
//      * The Pattern layout follows FL Studio 2026's law, measured in round 190qc (verify_190qc/REA_rodada190qc.md, the
//        same in FL's Playlist, Piano roll notes / marquee / velocity lane / vertical and event editor window): no inner
//        zone, it starts ~1-2 px inside the edge; ≈ 10 px/s per px past it (≈ 20 px/s at the edge, 223 at 20 px, 775 at
//        80 px, flatter further out: 1319 at 150, 1927 at 250, 2167 at 400); no acceleration over time; in screen pixels,
//        so the same at every zoom. The table below is FL's measured points, joined by straight lines.
//      * The Clip-launch and Simple layouts take the Live / Logic-like law of the brief: a 24 px zone inside the edge
//        (in component pixels, so it grows with the UI scale), from 0.5 screen a second where the zone starts up to 4
//        screens a second two zones past the edge.
//      * The view moves by whole steps (`quantum`, a whole number of physical pixels, so the Playlist's cached picture
//        can shift instead of painting everything); the remainder carries to the next frame. It stops on the release, and
//        Esc holds it until the pointer is back inside the view.
//  - viewstack (D2-PL8): "Zoom to the selection" pushes the view it leaves; the same key again goes back to it (and the
//    "Previous view" action goes back whatever the view is now). At most `maxDepth` views.
//  - glide (UX-A3): key zoom, fit and zoom-to-selection glide over 150 ms with an ease-out, only while Settings ›
//    General › Animations is on (the caller asks ui::motion::enabled()). The visible range's two ends move in a straight
//    line, so the view never overshoots and the anchor of a key zoom stays put.
//  - zoomanchor (UX-A2): one rule for every editor. The wheel and pinch zoom around the pointer (as before); the keys
//    around the playhead while it plays and is on screen, else the selection's centre when it is on screen, else the
//    view's centre.
//  - selhistory (D2-PL9): the last 8 selections that a click replaced or cleared; Edit › "Restore selection" brings them
//    back, newest first.
//  - keeplabels (D2-PL2): FL's Playlist ▸ View ▸ "Keep labels on screen" (default on, FL's PLForm\KeepLabelsOnScreen
//    default 1): a clip whose start is scrolled off draws its label at the view's left edge, with the same inset as at a
//    visible start, shortened to the part of the clip still in view.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace lynx::edgescroll
{
enum class Law
{
    fl,     // the Pattern layout: FL Studio's measured law
    smooth, // the Clip-launch and Simple layouts: the Live / Logic-like law
};

// FL Studio 2026 (round 190qc [RUN], rea fits): px past the editor's edge (negative: inside) → px per second
struct Point
{
    double past, speed;
};
inline constexpr Point flTable[] = { { -2.0, 0.0 },    { 0.0, 20.0 },    { 1.0, 41.0 },     { 3.0, 62.0 },     { 8.0, 114.0 },  { 20.0, 223.0 },
                                     { 40.0, 410.0 }, { 80.0, 775.0 }, { 150.0, 1319.0 }, { 250.0, 1927.0 }, { 400.0, 2167.0 } };

// FL's speed `past` px beyond the edge: 0 from 2 px inside, the measured points joined by lines, and past the last point
// the last segment's slope (FL showed no hard cap)
inline double flSpeed (double past) noexcept
{
    constexpr auto n = sizeof (flTable) / sizeof (flTable[0]);
    if (! (past > flTable[0].past))
        return 0.0;
    for (std::size_t i = 1; i < n; ++i)
        if (past <= flTable[i].past)
        {
            const auto& a = flTable[i - 1];
            const auto& b = flTable[i];
            return a.speed + (b.speed - a.speed) * (past - a.past) / (b.past - a.past);
        }
    const auto& a = flTable[n - 2];
    const auto& b = flTable[n - 1];
    return b.speed + (b.speed - a.speed) * (past - b.past) / (b.past - a.past);
}

inline constexpr double smoothZone = 24.0;                         // px inside the edge (component px: × the UI scale)
inline constexpr double smoothSlowest = 0.5, smoothFastest = 4.0; // screens a second

// The Live / Logic-like speed: 0 before the zone; from 0.5 screen/s where it starts to 4 screens/s at two zones past the
// edge, eased (slow near the edge, where one aims), flat beyond
inline double smoothSpeed (double past, double viewPx, double zone = smoothZone) noexcept
{
    if (zone <= 0.0 || viewPx <= 0.0 || past <= -zone)
        return 0.0;
    const auto depth = std::clamp ((past + zone) / (3.0 * zone), 0.0, 1.0);
    return viewPx * (smoothSlowest + (smoothFastest - smoothSlowest) * depth * depth);
}

// The signed speed along one axis (px/s; negative: the view goes back, towards `lo`) for the pointer at `pos` in a view
// that spans [lo, hi)
inline double axisSpeed (Law law, double pos, double lo, double hi) noexcept
{
    if (hi <= lo)
        return 0.0;
    const auto view = hi - lo;
    // (FL measures "1 px inside" from the last pixel of the view: hi - 1)
    const auto pastHigh = pos - (hi - 1.0);
    const auto pastLow = lo - pos;
    auto speed = [&] (double past) { return law == Law::fl ? flSpeed (past) : smoothSpeed (past, view); };
    const auto up = speed (pastHigh), down = speed (pastLow);
    if (up > 0.0 && down > 0.0) // (a view smaller than two zones: the nearer edge)
        return pastHigh >= pastLow ? up : -down;
    return up > 0.0 ? up : (down > 0.0 ? -down : 0.0);
}

// The smallest whole number of component px that is also a whole number of physical px at `scale` (1 at 100 / 200 %,
// 2 at 150 %, 4 at 125 / 175 %); at most 4
inline int wholeQuantum (double scale) noexcept
{
    for (int q = 1; q <= 4; ++q)
        if (const auto p = q * scale; std::abs (p - std::round (p)) < 0.01)
            return q;
    return 1;
}

// One drag's edge scroll. The editor calls update() with the pointer on each frame of its FrameTicker (and on each
// mouse drag) while an item, a selection box or a lane stroke is dragged; it scrolls the view by the returned whole steps,
// then lays the dragged item out again for the pointer (whose position in the song moved with the view).
class Scroller
{
public:
    struct Step
    {
        double dx = 0.0, dy = 0.0; // component px to scroll by this frame (multiples of the quantum)
        bool running = false;      // the pointer is in a scroll spot: keep the frames coming
    };

    // `area`: the view's scrolling part, in component px; `canX` / `canY`: which ways this drag scrolls
    Step update (double nowMs, double x, double y, double left, double top, double right, double bottom, bool canX, bool canY, Law law,
                 double quantum = 1.0) noexcept
    {
        Step out;
        auto vx = canX ? axisSpeed (law, x, left, right) : 0.0;
        auto vy = canY ? axisSpeed (law, y, top, bottom) : 0.0;
        if (held)
        {
            if (vx == 0.0 && vy == 0.0) // (back out of every scroll spot: the next visit scrolls again)
                held = false;
            vx = vy = 0.0;
        }
        if (vx == 0.0 && vy == 0.0)
        {
            lastMs = -1.0;
            carryX = carryY = 0.0;
            return out;
        }
        out.running = true;
        // the first frame in the spot only starts the clock (UX-A11: it moves from the next frame, ≤ 1 frame later)
        const auto dt = lastMs < 0.0 ? 0.0 : std::clamp (nowMs - lastMs, 0.0, maxStepMs) / 1000.0;
        lastMs = nowMs;
        const auto q = std::max (1.0, quantum);
        auto whole = [q] (double& carry, double v, double seconds) {
            carry += v * seconds;
            const auto steps = carry >= 0.0 ? std::floor (carry / q) : std::ceil (carry / q);
            carry -= steps * q;
            return steps * q;
        };
        out.dx = whole (carryX, vx, dt);
        out.dy = whole (carryY, vy, dt);
        if (vx == 0.0)
            carryX = 0.0;
        if (vy == 0.0)
            carryY = 0.0;
        return out;
    }

    // the release (or a drag of another kind): no scroll until the next drag
    void stop() noexcept
    {
        lastMs = -1.0;
        carryX = carryY = 0.0;
        held = false;
    }
    // Esc: no scroll until the pointer is back inside the view
    void hold() noexcept
    {
        stop();
        held = true;
    }
    bool isHeld() const noexcept { return held; }
    bool isRunning() const noexcept { return lastMs >= 0.0; }

    static constexpr double maxStepMs = 50.0; // (a frame late by more counts as this: no jump after a stall)

private:
    double lastMs = -1.0, carryX = 0.0, carryY = 0.0;
    bool held = false;
};

// Off-screen checks (--feel190qetest, --selftest) call the editors' mouse handlers directly, with no button really held
inline bool buttonHeldForTests = false;

// The pointer kept inside [lo, hi) for laying out the dragged item: FL keeps a clip dragged past the edge at the edge
// while the view runs (round 190qc)
inline double clampInto (double pos, double lo, double hi) noexcept
{
    return hi <= lo ? lo : std::clamp (pos, lo, hi - 1.0);
}
} // namespace lynx::edgescroll

namespace lynx::viewstack
{
// One editor view: the horizontal scroll and zoom (the first visible unit, units per px: ticks in the Playlist and the
// Piano roll) and the vertical scroll (px or rows)
struct View
{
    double scroll = 0.0, pixelsPerUnit = 1.0;
    double scrollY = 0.0;

    double visibleEnd (double widthPx) const noexcept { return scroll + widthPx / pixelsPerUnit; }
};

inline bool same (const View& a, const View& b) noexcept
{
    auto approx = [] (double x, double y) { return std::abs (x - y) <= 1e-6 * std::max ({ 1.0, std::abs (x), std::abs (y) }); };
    return approx (a.scroll, b.scroll) && approx (a.pixelsPerUnit, b.pixelsPerUnit) && std::abs (a.scrollY - b.scrollY) < 0.5;
}

class Stack
{
public:
    static constexpr std::size_t maxDepth = 16;

    // "Zoom to the selection": the view to show. `current` the view now, `target` the selection's view. Pressed again
    // while the view is still the one it zoomed to: back to the view before (popped).
    View toggle (const View& current, const View& target)
    {
        if (! entries.empty() && same (current, entries.back().second))
        {
            const auto before = entries.back().first;
            entries.pop_back();
            return before;
        }
        entries.emplace_back (current, target);
        if (entries.size() > maxDepth)
            entries.pop_front();
        return target;
    }
    // Round 190ro (FL 2026 checked 11-26: a second Shift+5 leaves the view as it is): the target view, kept on the stack
    // for "Previous view", without going back when pressed again
    View push (const View& current, const View& target)
    {
        if (! same (current, target))
        {
            entries.emplace_back (current, target);
            if (entries.size() > maxDepth)
                entries.pop_front();
        }
        return target;
    }
    // "Previous view": the view before the last zoom to a selection, whatever the view is now
    std::optional<View> back()
    {
        if (entries.empty())
            return std::nullopt;
        const auto before = entries.back().first;
        entries.pop_back();
        return before;
    }
    std::size_t depth() const noexcept { return entries.size(); }
    void clear() noexcept { entries.clear(); }

private:
    std::deque<std::pair<View, View>> entries; // (the view left, the view zoomed to)
};
} // namespace lynx::viewstack

namespace lynx::glide
{
inline constexpr double zoomMs = 150.0; // DESIGN-UX A3 / §D zoomGlide
// LYNX_TEST_MODE runs (shot routes, --selftest) jump to the new view as before unless a check turns this on
inline bool inTests = false;

// ease-out cubic: fast start, gentle landing
inline double easeOut (double t) noexcept
{
    t = std::clamp (t, 0.0, 1.0);
    const auto u = 1.0 - t;
    return 1.0 - u * u * u;
}

// The view `e` (0..1) of the way from `a` to `b` in a view `widthPx` wide: the visible range's two ends move in a straight
// line (a zoom glides without overshooting; a fixed point of both views stays put), the vertical scroll too
inline viewstack::View between (const viewstack::View& a, const viewstack::View& b, double e, double widthPx) noexcept
{
    e = std::clamp (e, 0.0, 1.0);
    if (widthPx <= 0.0)
        return e < 1.0 ? a : b;
    const auto start = a.scroll + (b.scroll - a.scroll) * e;
    const auto end = a.visibleEnd (widthPx) + (b.visibleEnd (widthPx) - a.visibleEnd (widthPx)) * e;
    viewstack::View v;
    v.scroll = start;
    v.pixelsPerUnit = end > start ? widthPx / (end - start) : b.pixelsPerUnit;
    v.scrollY = a.scrollY + (b.scrollY - a.scrollY) * e;
    return v;
}

// A running glide; at() gives the view of a frame, done() once it has landed
struct Glide
{
    viewstack::View from, to;
    double startMs = 0.0, durationMs = zoomMs, widthPx = 0.0;
    bool running = false;

    void start (const viewstack::View& a, const viewstack::View& b, double nowMs, double width, double ms = zoomMs) noexcept
    {
        from = a;
        to = b;
        startMs = nowMs;
        widthPx = width;
        durationMs = ms;
        running = ! viewstack::same (a, b) && ms > 0.0;
    }
    viewstack::View at (double nowMs) const noexcept
    {
        if (! running)
            return to;
        return between (from, to, easeOut ((nowMs - startMs) / durationMs), widthPx);
    }
    bool done (double nowMs) const noexcept { return ! running || nowMs - startMs >= durationMs; }
};
} // namespace lynx::glide

namespace lynx::zoomanchor
{
enum class Source
{
    pointer, // the wheel and pinch
    keys,    // the zoom keys and the Zoom menu
};

// The anchor of a zoom step, in px from the view's left edge (a point of the song that stays where it is). `playheadPx`
// / `selectionCentrePx`: where they are now (from the view's left edge), if any.
inline double anchorPx (Source source, double pointerPx, double viewWidthPx, bool playing, std::optional<double> playheadPx,
                        std::optional<double> selectionCentrePx) noexcept
{
    auto onScreen = [viewWidthPx] (const std::optional<double>& x) { return x.has_value() && *x >= 0.0 && *x <= viewWidthPx; };
    if (source == Source::pointer)
        return pointerPx;
    if (playing && onScreen (playheadPx))
        return *playheadPx;
    if (onScreen (selectionCentrePx))
        return *selectionCentrePx;
    return viewWidthPx * 0.5;
}

// Round 190ro (EXTRAS-2.md D-2; FL 2026 checked live 11-26, /mnt/e/lynx-work/extras2_190ro): round 190qe's zoom keys are a
// LYNX extra, "editing.zoomKeysLynx" (ui-options.txt "zoomKeysLynx"), off by default, always on outside the Pattern layout.
// On: the anchor above (the playhead while playing on screen, the selection's centre, the view's centre) and the
// zoom-to-selection key pressed again goes back. Off, as FL 2026: PgUp / PgDn zoom around the mouse pointer when it is
// over the view, else keep the view's left edge, and Shift+5 pressed again changes nothing.
inline bool& lynxKeys() noexcept
{
    static bool on = false;
    return on;
}
inline bool lynxKeysNow (bool patternLayout) noexcept { return lynxKeys() || ! patternLayout; }
// FL 2026's zoom-key anchor, in px from the view's left edge: the pointer over the view, else the left edge
inline double flKeyAnchorPx (std::optional<double> pointerPx, double viewWidthPx) noexcept
{
    return pointerPx.has_value() && *pointerPx >= 0.0 && *pointerPx <= viewWidthPx ? *pointerPx : 0.0;
}

// The new scroll (first visible unit) after zooming from `oldPpu` to `newPpu` px per unit around `anchor` px (the
// unit under the anchor stays under it; never before the song's start)
inline double scrollFor (double scroll, double oldPpu, double newPpu, double anchor) noexcept
{
    const auto unit = scroll + anchor / oldPpu;
    return std::max (0.0, unit - anchor / newPpu);
}
} // namespace lynx::zoomanchor

namespace lynx::selhistory
{
// The last selections (sorted keys of what was selected, the newest last) that a click replaced or cleared
template <typename Key>
class History
{
public:
    static constexpr std::size_t keep = 8;

    // a selection changed from `before` to `after` (by a click or a box, not by Restore itself)
    void changed (const std::vector<Key>& before, const std::vector<Key>& after)
    {
        if (before.empty() || before == after || restoring)
            return;
        if (! past.empty() && past.back() == before)
            return;
        past.push_back (before);
        if (past.size() > keep)
            past.pop_front();
    }
    // "Restore selection": the newest remembered selection (not `current`), taken off the list; none when empty
    std::optional<std::vector<Key>> restore (const std::vector<Key>& current)
    {
        while (! past.empty() && past.back() == current)
            past.pop_back();
        if (past.empty())
            return std::nullopt;
        auto last = std::move (past.back());
        past.pop_back();
        return last;
    }
    std::size_t size() const noexcept { return past.size(); }
    void clear() noexcept { past.clear(); }

    // set while the editor applies a restored selection, so it is not remembered again
    bool restoring = false;

private:
    std::deque<std::vector<Key>> past;
};
} // namespace lynx::selhistory

namespace lynx::keeplabels
{
// Playlist ▸ View ▸ "Keep labels on screen" (ui-options.txt keepLabels; FL's default: on)
inline bool on = true;

inline constexpr double inset = 3.0; // px from the clip's (or the view's) left edge to the label's glyph, as FL draws it

struct Place
{
    double left = 0.0;  // where the label's box starts (its glyph `inset` px further)
    double width = 0.0; // the box's width up to the clip's end (the label is shortened to it)
    bool held = false;  // drawn at the view's edge, not at the clip's start
};

// The label of a clip spanning [clipLeft, clipRight) px in a view whose clips start at `viewLeft`
inline Place place (double clipLeft, double clipRight, double viewLeft, bool keep = on) noexcept
{
    Place p;
    p.held = keep && clipLeft < viewLeft && clipRight > viewLeft;
    p.left = p.held ? viewLeft : clipLeft;
    p.width = std::max (0.0, clipRight - p.left);
    return p;
}
} // namespace lynx::keeplabels
