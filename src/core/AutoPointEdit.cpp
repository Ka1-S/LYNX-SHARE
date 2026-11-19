#include "core/AutoPointEdit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lynx::autopoints
{
namespace
{
std::vector<int> sortedValid (std::vector<int> selected, int count)
{
    selected.erase (std::remove_if (selected.begin(), selected.end(), [count] (int i) { return i < 0 || i >= count; }), selected.end());
    std::sort (selected.begin(), selected.end());
    selected.erase (std::unique (selected.begin(), selected.end()), selected.end());
    return selected;
}
} // namespace

std::vector<int> inBox (const std::vector<AutomationPoint>& points, std::int64_t t0, std::int64_t t1, float v0, float v1)
{
    if (t1 < t0)
        std::swap (t0, t1);
    if (v1 < v0)
        std::swap (v0, v1);
    std::vector<int> out;
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto& p = points[(std::size_t) i];
        if (p.tick >= t0 && p.tick <= t1 && p.value >= v0 && p.value <= v1)
            out.push_back (i);
    }
    return out;
}

int erase (std::vector<AutomationPoint>& points, std::vector<int> selected, int keep)
{
    selected = sortedValid (std::move (selected), (int) points.size());
    const auto canGo = std::max (0, (int) points.size() - std::max (0, keep));
    if ((int) selected.size() > canGo)
        selected.resize ((std::size_t) canGo);
    // from the last, so the indices before stay valid
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        eraseAutomationPoint (points, *it);
    return (int) selected.size();
}

Delta clampMove (const std::vector<AutomationPoint>& originals, const std::vector<int>& selectedIn, Delta want)
{
    const auto selected = sortedValid (selectedIn, (int) originals.size());
    if (selected.empty())
        return {};
    std::vector<bool> isSel (originals.size(), false);
    for (auto i : selected)
        isSel[(std::size_t) i] = true;

    auto lo = std::numeric_limits<std::int64_t>::min(), hi = std::numeric_limits<std::int64_t>::max();
    float vLo = -1.0f, vHi = 1.0f;
    for (auto i : selected)
    {
        const auto& p = originals[(std::size_t) i];
        lo = std::max (lo, -p.tick); // tick >= 0
        for (int j = i - 1; j >= 0; --j) // the unselected point before
            if (! isSel[(std::size_t) j])
            {
                lo = std::max (lo, originals[(std::size_t) j].tick - p.tick);
                break;
            }
        for (int j = i + 1; j < (int) originals.size(); ++j) // ... and after
            if (! isSel[(std::size_t) j])
            {
                hi = std::min (hi, originals[(std::size_t) j].tick - p.tick);
                break;
            }
        vLo = std::max (vLo, -p.value);
        vHi = std::min (vHi, 1.0f - p.value);
    }
    Delta d;
    d.ticks = hi < lo ? 0 : std::clamp (want.ticks, lo, hi);
    d.value = vHi < vLo ? 0.0f : std::clamp (want.value, vLo, vHi);
    return d;
}

std::vector<AutomationPoint> moved (const std::vector<AutomationPoint>& originals, const std::vector<int>& selected, Delta want)
{
    const auto d = clampMove (originals, selected, want);
    auto out = originals;
    for (auto i : sortedValid (selected, (int) out.size()))
    {
        auto& p = out[(std::size_t) i];
        p.tick += d.ticks;
        p.value = std::clamp (p.value + d.value, 0.0f, 1.0f);
    }
    return out;
}

Clipboard copy (const std::vector<AutomationPoint>& points, const std::vector<int>& selectedIn)
{
    Clipboard clip;
    const auto selected = sortedValid (selectedIn, (int) points.size());
    if (selected.empty())
        return clip;
    const auto first = points[(std::size_t) selected.front()].tick;
    for (auto i : selected)
    {
        auto p = points[(std::size_t) i];
        p.tick -= first;
        clip.points.push_back (p);
    }
    clip.span = clip.points.back().tick;
    // the last copied point ends no segment inside the copy: its curve to the next point is not part of it
    clip.points.back().tension = 0.0f;
    clip.points.back().mode = 0;
    return clip;
}

std::vector<int> paste (std::vector<AutomationPoint>& points, const Clipboard& clip, std::int64_t at)
{
    std::vector<int> out;
    if (clip.empty())
        return out;
    at = std::max<std::int64_t> (0, at);
    const auto end = at + clip.span;
    points.erase (std::remove_if (points.begin(), points.end(), [at, end] (const AutomationPoint& p) { return p.tick >= at && p.tick <= end; }),
                  points.end());
    auto pos = (std::size_t) (std::lower_bound (points.begin(), points.end(), at, [] (const AutomationPoint& p, std::int64_t t) { return p.tick < t; })
                              - points.begin());
    for (auto p : clip.points)
    {
        p.tick += at;
        points.insert (points.begin() + (std::ptrdiff_t) pos, p);
        out.push_back ((int) pos);
        ++pos;
    }
    return out;
}

int thin (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to, float tolerance)
{
    if (to < from)
        std::swap (from, to);
    int first = -1, last = -1;
    for (int i = 0; i < (int) points.size(); ++i)
        if (points[(std::size_t) i].tick >= from && points[(std::size_t) i].tick <= to)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    if (first < 0 || last - first < 2)
        return 0;

    const auto n = last - first + 1;
    std::vector<char> keep ((std::size_t) n, 0);
    keep.front() = keep.back() = 1;
    for (int k = 0; k < n; ++k)
    {
        const auto i = first + k;
        const auto& p = points[(std::size_t) i];
        const auto stepEdge = (i > 0 && points[(std::size_t) i - 1].tick == p.tick)
                              || (i + 1 < (int) points.size() && points[(std::size_t) i + 1].tick == p.tick);
        // a bent / moded point shapes its segment; and the segment ending at a kept point is not ours to drop either
        if (stepEdge || p.tension != 0.0f || p.mode != 0)
            keep[(std::size_t) k] = 1;
    }

    // Ramer-Douglas-Peucker between every two kept points (an explicit stack: a long stroke has many points)
    std::vector<std::pair<int, int>> spans;
    for (int a = 0, b = 1; b < n; ++b)
        if (keep[(std::size_t) b])
        {
            if (b - a > 1)
                spans.emplace_back (a, b);
            a = b;
        }
    while (! spans.empty())
    {
        const auto [a, b] = spans.back();
        spans.pop_back();
        const auto& pa = points[(std::size_t) (first + a)];
        const auto& pb = points[(std::size_t) (first + b)];
        const auto width = (double) (pb.tick - pa.tick);
        double worst = -1.0;
        int at = -1;
        for (int k = a + 1; k < b; ++k)
        {
            const auto& p = points[(std::size_t) (first + k)];
            const auto t = width > 0.0 ? (double) (p.tick - pa.tick) / width : 0.0;
            const auto line = (double) pa.value + t * (double) (pb.value - pa.value);
            const auto off = std::abs ((double) p.value - line);
            if (off > worst)
            {
                worst = off;
                at = k;
            }
        }
        if (at >= 0 && worst > (double) tolerance)
        {
            keep[(std::size_t) at] = 1;
            if (at - a > 1)
                spans.emplace_back (a, at);
            if (b - at > 1)
                spans.emplace_back (at, b);
        }
    }

    int removed = 0;
    std::vector<AutomationPoint> out;
    out.reserve (points.size());
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto k = i - first;
        if (k > 0 && k < n - 1 && ! keep[(std::size_t) k])
        {
            ++removed;
            continue;
        }
        out.push_back (points[(std::size_t) i]);
    }
    points = std::move (out);
    return removed;
}
} // namespace lynx::autopoints
