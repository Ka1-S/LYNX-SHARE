#pragma once

// MPE stage 2 (issue #108, round 190hy; docs/analise/REA_rodada190hy-mpe.md): a note's own expression curves, as Ableton
// Live 11+ keeps them for each note (.als `Notes/PerNoteEventStore/EventLists/PerNoteEventList{@NoteId, @CC}`: pitch
// bend CC −2, slide CC 74, pressure CC −1; times from the note's start). FL Studio 2026 has none (its per-note values
// are one number each), so this is a LYNX addition on top of FL's note properties:
//
//   pitch      cents added to the note's fine pitch while it sounds (Live's per-note pitch bend, ±48 semitones)
//   timbre     0..1 in the Mod X law of core/Mpe.h (0.5 = CC 74 at 64): the note's Mod X / MPE slide while it sounds
//   pressure   0..1: the note's MPE pressure while it sounds (Mod Y = 0.5 + pressure / 2)
//
// Each lane is a list of breakpoints (tick from the note's start, value), linear between them, held before the first
// and after the last. An empty lane = the note's own value (fine pitch / Mod X / Mod Y) the whole time. No JUCE here.
//
// Note::curves holds them by value through NoteCurvesRef: one pointer (null = none), copied deeply and compared by
// content, so a pattern of plain notes stays as small and as quick to copy as before.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace lynx
{
struct NoteCurvePoint
{
    float tick = 0.0f;  // from the note's start (ticks, 960 per quarter), ≥ 0
    float value = 0.0f; // pitch: cents; timbre / pressure: 0..1
    bool operator== (const NoteCurvePoint&) const = default;
};

enum class NoteCurveLane : std::uint8_t { pitch = 0, timbre = 1, pressure = 2 };
constexpr int noteCurveLaneCount = 3;

namespace notecurves
{
constexpr float maxPitchCents = 4800.0f; // ±48 semitones, the MPE per-note bend range (and Live's)
constexpr std::size_t maxPoints = 4096;  // per lane (a recorded take is thinned well below this)

inline float clampValue (NoteCurveLane lane, float v) noexcept
{
    if (! std::isfinite (v))
        return lane == NoteCurveLane::pitch ? 0.0f : lane == NoteCurveLane::timbre ? 0.5f : 0.0f;
    return lane == NoteCurveLane::pitch ? std::clamp (v, -maxPitchCents, maxPitchCents) : std::clamp (v, 0.0f, 1.0f);
}
// the value a lane has without any point (what the note's own fields already give)
constexpr float neutral (NoteCurveLane lane) noexcept { return lane == NoteCurveLane::timbre ? 0.5f : 0.0f; }
} // namespace notecurves

struct NoteCurves
{
    std::array<std::vector<NoteCurvePoint>, noteCurveLaneCount> lanes;

    std::vector<NoteCurvePoint>& lane (NoteCurveLane l) noexcept { return lanes[(std::size_t) l]; }
    const std::vector<NoteCurvePoint>& lane (NoteCurveLane l) const noexcept { return lanes[(std::size_t) l]; }
    bool empty() const noexcept
    {
        return std::all_of (lanes.begin(), lanes.end(), [] (const std::vector<NoteCurvePoint>& l) { return l.empty(); });
    }
    // A lane that moves while the note sounds (two points or more): the scheduler sends its changes. One point = a
    // constant, carried by the note's start alone.
    bool moves (NoteCurveLane l) const noexcept { return lane (l).size() >= 2; }

    // The lane's value `tick` ticks into the note (linear between points; the first before it, the last after it);
    // `fallback` when the lane is empty. Audio thread: no allocation.
    float valueAt (NoteCurveLane l, double tick, float fallback) const noexcept
    {
        const auto& p = lane (l);
        if (p.empty())
            return fallback;
        if (tick <= (double) p.front().tick)
            return p.front().value;
        if (tick >= (double) p.back().tick)
            return p.back().value;
        const auto it = std::upper_bound (p.begin(), p.end(), tick, [] (double t, const NoteCurvePoint& q) { return t < (double) q.tick; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const auto span = (double) b.tick - (double) a.tick;
        if (span <= 0.0)
            return b.value;
        return (float) ((double) a.value + ((double) b.value - (double) a.value) * ((tick - (double) a.tick) / span));
    }

    // Points sorted by tick, one per tick (the later wins), ticks ≥ 0, values in range, at most maxPoints per lane.
    void normalise() noexcept
    {
        for (int i = 0; i < noteCurveLaneCount; ++i)
        {
            auto& p = lanes[(std::size_t) i];
            for (auto& q : p)
            {
                q.tick = std::isfinite (q.tick) ? std::max (0.0f, q.tick) : 0.0f;
                q.value = notecurves::clampValue ((NoteCurveLane) i, q.value);
            }
            std::stable_sort (p.begin(), p.end(), [] (const NoteCurvePoint& a, const NoteCurvePoint& b) { return a.tick < b.tick; });
            std::vector<NoteCurvePoint> kept;
            kept.reserve (p.size());
            for (const auto& q : p)
                if (! kept.empty() && kept.back().tick == q.tick)
                    kept.back() = q;
                else
                    kept.push_back (q);
            if (kept.size() > notecurves::maxPoints)
                kept.resize (notecurves::maxPoints);
            p = std::move (kept);
        }
    }
    bool operator== (const NoteCurves&) const = default;
};

// Note::curves: a value-semantic handle (copy = deep copy, == compares the curves), null when the note has none.
class NoteCurvesRef
{
public:
    NoteCurvesRef() noexcept = default;
    NoteCurvesRef (const NoteCurvesRef& o) : p (o.p != nullptr ? std::make_unique<NoteCurves> (*o.p) : nullptr) {}
    NoteCurvesRef (NoteCurvesRef&&) noexcept = default;
    NoteCurvesRef& operator= (const NoteCurvesRef& o)
    {
        if (this != &o)
            p = o.p != nullptr ? std::make_unique<NoteCurves> (*o.p) : nullptr;
        return *this;
    }
    NoteCurvesRef& operator= (NoteCurvesRef&&) noexcept = default;
    NoteCurvesRef& operator= (NoteCurves c)
    {
        c.normalise();
        p = c.empty() ? nullptr : std::make_unique<NoteCurves> (std::move (c));
        return *this;
    }

    bool has() const noexcept { return p != nullptr && ! p->empty(); }
    explicit operator bool() const noexcept { return has(); }
    const NoteCurves* get() const noexcept { return has() ? p.get() : nullptr; }
    // (empty curves when there are none: read without checking)
    const NoteCurves& operator*() const noexcept { return has() ? *p : none(); }
    const NoteCurves* operator->() const noexcept { return &**this; }
    // For editing: the note's curves, made when it has none. Call tidy() afterwards to drop empty ones.
    NoteCurves& edit()
    {
        if (p == nullptr)
            p = std::make_unique<NoteCurves>();
        return *p;
    }
    void tidy() noexcept
    {
        if (p != nullptr)
        {
            p->normalise();
            if (p->empty())
                p.reset();
        }
    }
    void clear() noexcept { p.reset(); }

    bool operator== (const NoteCurvesRef& o) const noexcept { return **this == *o; }

private:
    static const NoteCurves& none() noexcept
    {
        static const NoteCurves empty;
        return empty;
    }
    std::unique_ptr<NoteCurves> p;
};

namespace notecurves
{
// The note's curves for its part [from, to) (ticks from its start) when a note is cut there (split, chop, slice):
// the points inside, moved to start at `from`, with the values at both edges added so the piece sounds as before.
inline NoteCurvesRef slice (const NoteCurvesRef& curves, double from, double to)
{
    NoteCurvesRef out;
    if (! curves.has() || to <= from)
        return out;
    NoteCurves c;
    for (int i = 0; i < noteCurveLaneCount; ++i)
    {
        const auto l = (NoteCurveLane) i;
        const auto& p = curves->lane (l);
        if (p.empty())
            continue;
        auto& q = c.lane (l);
        q.push_back ({ 0.0f, curves->valueAt (l, from, 0.0f) });
        for (const auto& point : p)
            if ((double) point.tick > from && (double) point.tick < to)
                q.push_back ({ (float) ((double) point.tick - from), point.value });
        if (p.size() >= 2)
            q.push_back ({ (float) (to - from), curves->valueAt (l, to, 0.0f) });
    }
    out = std::move (c);
    return out;
}
// Times scaled by `factor` (a note stretched with its curves, as Live does)
inline void scale (NoteCurvesRef& curves, double factor)
{
    if (! curves.has() || ! (factor > 0.0) || ! std::isfinite (factor) || factor == 1.0)
        return;
    auto& c = curves.edit();
    for (auto& lane : c.lanes)
        for (auto& point : lane)
            point.tick = (float) ((double) point.tick * factor);
    curves.tidy();
}

// Ramer–Douglas–Peucker thinning of a recorded lane: the points needed to stay within `tolerance` of the take
// (pitch: cents, timbre / pressure: 0..1). The first and the last point stay.
inline std::vector<NoteCurvePoint> thin (const std::vector<NoteCurvePoint>& in, float tolerance)
{
    if (in.size() <= 2)
        return in;
    std::vector<char> keep (in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack { { 0, in.size() - 1 } };
    while (! stack.empty())
    {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1)
            continue;
        const auto ta = (double) in[a].tick, tb = (double) in[b].tick;
        auto worst = 0.0;
        auto at = a;
        for (auto i = a + 1; i < b; ++i)
        {
            const auto t = tb > ta ? ((double) in[i].tick - ta) / (tb - ta) : 0.0;
            const auto line = (double) in[a].value + ((double) in[b].value - (double) in[a].value) * t;
            const auto d = std::abs ((double) in[i].value - line);
            if (d > worst)
                worst = d, at = i;
        }
        if (worst > (double) tolerance)
        {
            keep[at] = 1;
            stack.push_back ({ a, at });
            stack.push_back ({ at, b });
        }
    }
    std::vector<NoteCurvePoint> out;
    for (std::size_t i = 0; i < in.size(); ++i)
        if (keep[i])
            out.push_back (in[i]);
    return out;
}
constexpr float thinTolerance (NoteCurveLane lane) noexcept { return lane == NoteCurveLane::pitch ? 1.0f : 0.004f; }

// ---- editing (the Piano roll's MPE lanes; ticks from the note's start) -------------------------------------------
// A lane drawn into for the first time starts from the note's own value at tick 0 (`base`: 0 cents, its Mod X, its
// pressure), so a bend drawn in the note's second half leaves the first half as it was.
inline void startLane (std::vector<NoteCurvePoint>& p, float base, double tick)
{
    if (p.empty() && tick > 0.0)
        p.push_back ({ 0.0f, base });
}
// Adds (or replaces, on the same tick) a point; returns its index
inline int addPoint (NoteCurvesRef& curves, NoteCurveLane lane, double tick, float value, float base)
{
    auto& p = curves.edit().lane (lane);
    tick = std::max (0.0, tick);
    startLane (p, base, tick);
    const auto it = std::lower_bound (p.begin(), p.end(), tick, [] (const NoteCurvePoint& q, double t) { return (double) q.tick < t; });
    const NoteCurvePoint point { (float) tick, clampValue (lane, value) };
    std::ptrdiff_t at = it - p.begin();
    if (it != p.end() && (double) it->tick == (double) point.tick)
        *it = point;
    else
        p.insert (it, point);
    return (int) at;
}
// Moves point `index` to (tick, value), kept between its neighbours (their ticks + 1, so the order stays); returns
// where it went (the same index)
inline void movePoint (NoteCurvesRef& curves, NoteCurveLane lane, int index, double tick, float value)
{
    if (! curves.has())
        return;
    auto& p = curves.edit().lane (lane);
    if (index < 0 || index >= (int) p.size())
        return;
    const auto lo = index > 0 ? (double) p[(std::size_t) index - 1].tick + 1.0 : 0.0;
    const auto hi = index + 1 < (int) p.size() ? (double) p[(std::size_t) index + 1].tick - 1.0 : 1.0e9;
    p[(std::size_t) index] = { (float) std::clamp (tick, lo, std::max (lo, hi)), clampValue (lane, value) };
}
inline void removePoint (NoteCurvesRef& curves, NoteCurveLane lane, int index)
{
    if (! curves.has())
        return;
    auto& p = curves.edit().lane (lane);
    if (index >= 0 && index < (int) p.size())
        p.erase (p.begin() + index);
    curves.tidy();
}
// Freehand drawing from (t0, v0) to (t1, v1): the points between the two ticks are replaced by a line of points every
// `stepTicks` (both ends included)
inline void drawLine (NoteCurvesRef& curves, NoteCurveLane lane, double t0, float v0, double t1, float v1, double stepTicks, float base)
{
    if (t1 < t0)
        std::swap (t0, t1), std::swap (v0, v1);
    t0 = std::max (0.0, t0);
    t1 = std::max (t0, t1);
    auto& p = curves.edit().lane (lane);
    startLane (p, base, t0);
    p.erase (std::remove_if (p.begin(), p.end(), [&] (const NoteCurvePoint& q) { return (double) q.tick >= t0 && (double) q.tick <= t1; }), p.end());
    const auto steps = t1 > t0 ? std::max (1, (int) std::ceil ((t1 - t0) / std::max (1.0, stepTicks))) : 0;
    for (int i = 0; i <= steps; ++i)
    {
        const auto f = steps > 0 ? (double) i / steps : 0.0;
        p.push_back ({ (float) (t0 + (t1 - t0) * f), clampValue (lane, (float) (v0 + (v1 - v0) * f)) });
    }
    curves.tidy();
}
// Erases the points between two ticks (a lane left with none is gone: the note's own value again)
inline void erase (NoteCurvesRef& curves, NoteCurveLane lane, double t0, double t1)
{
    if (! curves.has())
        return;
    if (t1 < t0)
        std::swap (t0, t1);
    auto& p = curves.edit().lane (lane);
    p.erase (std::remove_if (p.begin(), p.end(), [&] (const NoteCurvePoint& q) { return (double) q.tick >= t0 && (double) q.tick <= t1; }), p.end());
    curves.tidy();
}
inline void clearLane (NoteCurvesRef& curves, NoteCurveLane lane)
{
    if (! curves.has())
        return;
    curves.edit().lane (lane).clear();
    curves.tidy();
}
} // namespace notecurves
} // namespace lynx
