#pragma once

// The Clip-launch layout's MPE tab, as Live 12.4's Clip View › MPE tab (round 190jd; the live check of round 190iw,
// /mnt/e/lynx-work/livecheck190iw/ANSWERS.md Q3, shots q3-*):
//   - the note editor stays on top and the pitch is edited ON THE NOTES (a click on a selected note adds a pitch
//     breakpoint, a vertical drag bends it in semitones); there is no pitch lane;
//   - under the notes the lanes are stacked, Slide and Pressure by default (≈40 px each), and the ▾ button's
//     "Show Lane:" menu checks Slide, Pressure, Velocity and Release Velocity (several at once);
//   - in a lane: a click ON the line adds a breakpoint, a breakpoint is dragged to move it and double-clicked to delete
//     it, a click off the line does nothing; with Draw Mode (B) a drag draws the curve; hovering the line shows a ghost
//     point with its value.
// The pure parts (lane set, layout, hit tests, the bend law) live here so they can be tested. No JUCE here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::mpetab
{
// ---- the lanes the "Show Lane:" menu checks (in its order, which is also the stacking order, top to bottom) ---------
enum class Lane : std::uint8_t { slide = 0, pressure = 1, velocity = 2, releaseVelocity = 3 };
constexpr int laneCount = 4;
using LaneSet = std::uint8_t; // bit (1 << lane)

constexpr LaneSet bit (Lane l) noexcept { return (LaneSet) (1u << (unsigned) l); }
constexpr LaneSet defaultLanes = bit (Lane::slide) | bit (Lane::pressure); // Live's default (q3-live-mpe-show-lane-menu)
constexpr LaneSet allLanes = (LaneSet) ((1u << laneCount) - 1u);

inline bool shows (LaneSet set, Lane l) noexcept { return (set & bit (l)) != 0; }
// The menu's check item: toggles the lane. The last lane shown stays (the lane area always has one lane to edit).
inline LaneSet toggled (LaneSet set, Lane l) noexcept
{
    const auto next = (LaneSet) ((set ^ bit (l)) & allLanes);
    return next == 0 ? set : next;
}
inline std::vector<Lane> shownLanes (LaneSet set)
{
    std::vector<Lane> out;
    for (int i = 0; i < laneCount; ++i)
        if (shows (set, (Lane) i))
            out.push_back ((Lane) i);
    return out;
}
inline int countOf (LaneSet set) noexcept
{
    int n = 0;
    for (int i = 0; i < laneCount; ++i)
        n += shows (set, (Lane) i) ? 1 : 0;
    return n;
}

// ---- layout ----------------------------------------------------------------------------------------------------------
constexpr int laneHeight = 40;   // each lane at the default Clip View height (q3-pair: 40 / 41 px)
constexpr int minLaneHeight = 22; // squeezed when the dock is short, so the notes stay in view
constexpr int footerHeight = 18;  // the ▾ "Show Lane" button and the focused lane's name under the lanes

// The lane area's height (lanes + footer) for `lanes` lanes in a dock `available` pixels tall below the ruler: 40 px a
// lane, at most half of the dock, at least 22 px a lane.
inline int areaHeight (int lanes, int available) noexcept
{
    lanes = std::max (1, lanes);
    const auto wanted = lanes * laneHeight + footerHeight;
    const auto least = lanes * minLaneHeight + footerHeight;
    return std::max (least, std::min (wanted, available / 2));
}

// ---- hit tests in a lane (pixels) -------------------------------------------------------------------------------------
constexpr float pointRadius = 5.0f; // a breakpoint is grabbed within this many pixels (Chebyshev distance)
constexpr float lineReach = 4.0f;   // a click this close to the line (vertically) is "on the line"

struct Px
{
    float x = 0.0f, y = 0.0f;
};
enum class Hit : std::uint8_t { none, point, line };
struct HitResult
{
    Hit hit = Hit::none;
    int index = -1;   // the breakpoint (Hit::point)
    float lineY = 0;  // the line's y at the mouse's x (Hit::line)
};

// The line's y at x for a polyline (points sorted by x; held flat before the first and after the last)
inline float lineYAt (const std::vector<Px>& line, float x) noexcept
{
    if (line.empty())
        return 0.0f;
    if (x <= line.front().x)
        return line.front().y;
    if (x >= line.back().x)
        return line.back().y;
    for (std::size_t i = 1; i < line.size(); ++i)
        if (x <= line[i].x)
        {
            const auto& a = line[i - 1];
            const auto& b = line[i];
            const auto span = b.x - a.x;
            return span <= 0.0f ? b.y : a.y + (b.y - a.y) * (x - a.x) / span;
        }
    return line.back().y;
}

// `points`: the breakpoints (pixels); `line`: the drawn line over the note's span [x0, x1] (its points, the edges
// included). A breakpoint wins over the line; outside the span nothing is hit.
inline HitResult hitTest (const std::vector<Px>& points, const std::vector<Px>& line, float x0, float x1, Px mouse) noexcept
{
    HitResult r;
    auto best = pointRadius + 0.001f;
    for (std::size_t k = 0; k < points.size(); ++k)
    {
        const auto d = std::max (std::abs (points[k].x - mouse.x), std::abs (points[k].y - mouse.y));
        if (d < best)
        {
            best = d;
            r.hit = Hit::point;
            r.index = (int) k;
        }
    }
    if (r.hit == Hit::point)
        return r;
    if (mouse.x < x0 || mouse.x > x1 || line.empty())
        return r;
    const auto y = lineYAt (line, mouse.x);
    if (std::abs (y - mouse.y) <= lineReach)
    {
        r.hit = Hit::line;
        r.lineY = y;
    }
    return r;
}

// ---- pitch on the notes -----------------------------------------------------------------------------------------------
// A pitch breakpoint dragged by `dy` pixels (down = positive) from `startCents`, one key row (`rowHeight` px) a
// semitone, in whole semitones (Live's "0 st", "1 st" …); `fine` (LYNX's Shift) keeps whole cents. Clamped to ±48 st.
inline float bendCents (float startCents, float dy, float rowHeight, bool fine) noexcept
{
    const auto cents = startCents - dy / std::max (1.0f, rowHeight) * 100.0f;
    const auto v = fine ? std::round (cents) : std::round (cents / 100.0f) * 100.0f;
    return std::clamp (v, -4800.0f, 4800.0f);
}
} // namespace lynx::mpetab
