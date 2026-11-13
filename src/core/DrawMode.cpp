#include "core/DrawMode.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace lynx::drawmode
{
namespace
{
bool on = false;
int nextId = 1;
std::map<int, std::function<void (bool)>>& listeners()
{
    static std::map<int, std::function<void (bool)>> all;
    return all;
}

float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }
// (the old curve just left of an edge is read 1e-6 tick early: equal within this is the same value)
bool same (float a, float b) noexcept { return std::abs (a - b) <= 1.0e-5f; }
} // namespace

bool isOn() noexcept { return on; }

void set (bool value)
{
    if (value == on)
        return;
    on = value;
    const auto copy = listeners(); // (a listener may remove itself)
    for (const auto& [id, fn] : copy)
        if (fn != nullptr)
            fn (on);
}

int addListener (std::function<void (bool)> listener)
{
    const auto id = nextId++;
    listeners()[id] = std::move (listener);
    return id;
}

void removeListener (int id) { listeners().erase (id); }

std::vector<Cell> cellsBetween (std::int64_t t0, std::int64_t t1, std::int64_t grid)
{
    std::vector<Cell> out;
    if (grid <= 0)
        return out;
    const auto lo = std::max<std::int64_t> (0, std::min (t0, t1)), hi = std::max<std::int64_t> (0, std::max (t0, t1));
    constexpr std::int64_t maxCells = 100000; // (a sweep over hours of song at a one-tick grid)
    const auto k0 = lo / grid, k1 = std::min (hi / grid, k0 + maxCells - 1);
    out.reserve ((std::size_t) (k1 - k0 + 1));
    for (auto k = k0; k <= k1; ++k)
        out.push_back ({ k * grid, (k + 1) * grid });
    return out;
}

std::vector<Step> stepsAlong (std::int64_t t0, float v0, std::int64_t t1, float v1, std::int64_t grid)
{
    std::vector<Step> out;
    const auto cells = cellsBetween (t0, t1, grid);
    out.reserve (cells.size());
    for (const auto& c : cells)
    {
        auto v = v1;
        if (cells.size() > 1 && t1 != t0)
        {
            const auto mid = 0.5 * ((double) c.from + (double) c.to);
            const auto f = std::clamp ((mid - (double) t0) / ((double) t1 - (double) t0), 0.0, 1.0);
            v = (float) ((double) v0 + ((double) v1 - (double) v0) * f);
        }
        out.push_back ({ c, clamp01 (v) });
    }
    return out;
}

void drawPiece (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to, float vFrom, float vTo)
{
    from = std::max<std::int64_t> (0, from);
    if (to <= from)
        return;
    vFrom = clamp01 (vFrom);
    vTo = clamp01 (vTo);

    // the old curve at the edges: the value just left of `from` (a jump there keeps its left side) and the value at `to`
    const auto left = automationCurveAt (points, (double) from - 1.0e-6);
    const auto right = automationCurveAt (points, (double) to);
    const auto anyBefore = std::any_of (points.begin(), points.end(), [from] (const AutomationPoint& p) { return p.tick < from; });

    // the point that starts the curve after `to`: the last one on `to` itself if there is one (it keeps its segment's
    // mode and bend); otherwise a new one, cut from the segment that crosses `to` (its mode and bend)
    int lastAtTo = -1, crossing = -1;
    for (int i = 0; i < (int) points.size(); ++i)
    {
        if (points[(std::size_t) i].tick == to)
            lastAtTo = i;
        if (points[(std::size_t) i].tick < to)
            crossing = i;
    }
    AutomationPoint after { to, right, 0.0f, 0 };
    const auto keepAfter = lastAtTo >= 0;
    const auto anyAfter = std::any_of (points.begin(), points.end(), [to] (const AutomationPoint& p) { return p.tick > to; });
    if (! keepAfter && crossing >= 0 && anyAfter)
    {
        after.mode = points[(std::size_t) crossing].mode;
        after.tension = points[(std::size_t) crossing].tension;
    }

    // out with what was in [from, to) and the earlier points on `to`
    std::vector<AutomationPoint> kept;
    kept.reserve (points.size() + 4);
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const auto t = points[(std::size_t) i].tick;
        if ((t >= from && t < to) || (t == to && i != lastAtTo))
            continue;
        kept.push_back (points[(std::size_t) i]);
    }

    std::vector<AutomationPoint> piece;
    if (anyBefore || from > 0)
        piece.push_back ({ from, left, 0.0f, 0 });
    piece.push_back ({ from, vFrom, 0.0f, 0 });
    piece.push_back ({ to, vTo, 0.0f, 0 });
    if (! keepAfter)
        piece.push_back (after);

    const auto at = std::find_if (kept.begin(), kept.end(), [from] (const AutomationPoint& p) { return p.tick >= from; });
    kept.insert (at, piece.begin(), piece.end());

    // tidy the drawn range: a point that repeats the next one (same tick and value) goes; so does the middle of three
    // points with one value (a flat run stays flat whatever the segments' modes)
    auto inside = [from, to] (const AutomationPoint& p) { return p.tick >= from && p.tick <= to; };
    for (std::size_t i = 0; i + 1 < kept.size();)
        if (inside (kept[i]) && kept[i].tick == kept[i + 1].tick && same (kept[i].value, kept[i + 1].value))
            kept.erase (kept.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    for (std::size_t i = 1; i + 1 < kept.size();)
        if (inside (kept[i]) && same (kept[i - 1].value, kept[i].value) && same (kept[i].value, kept[i + 1].value))
            kept.erase (kept.begin() + (std::ptrdiff_t) i);
        else
            ++i;

    points = std::move (kept);
}

void drawLine (std::vector<AutomationPoint>& points, std::int64_t t0, float v0, std::int64_t t1, float v1)
{
    if (t1 < t0)
    {
        std::swap (t0, t1);
        std::swap (v0, v1);
    }
    if (t1 == t0)
        t1 = t0 + 1;
    drawPiece (points, t0, t1, v0, v1);
}
} // namespace lynx::drawmode
