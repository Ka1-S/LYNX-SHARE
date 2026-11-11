#pragma once

// FL Studio 2026's Playlist "Magic lasso" (Select menu, EESelMagicAction; REA round 189g, FLEngine_x64.dll):
// with the option on, the mouse moving over the Playlist with no button down leaves a trail (FUN_00e6ff80, fed by a
// WH_MOUSE hook, FUN_00e6f2b0 / FUN_00e6f840). When the trail closes on itself, the loop is a polygon and the clips
// inside it are selected (FUN_00e6f330). Pure geometry here, the Playlist applies it.
//
// FL's numbers: a pause of more than 200 ms (or a mouse capture) starts a new trail; moves under 4 px are ignored, a
// point less than 12 px from the last one is tested and then dropped; from 11 points on, the newest segment is tested
// against every segment but the last 7, and a distance under 18 px closes the loop from that segment on; the trail
// keeps its last 250 points once it passes 275. A clip is inside when its rectangle, inset by max(16, (w - 6) / 8) px
// on the sides (at most half) and max(8, (h - 6) / 4) px at the top and bottom (at most half), lies wholly in the
// polygon.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::magiclasso
{
struct Point
{
    double x = 0.0, y = 0.0;
};

struct Rect
{
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
};

inline double distance (Point a, Point b) noexcept
{
    return std::hypot (a.x - b.x, a.y - b.y);
}

inline double pointSegmentDistance (Point p, Point a, Point b) noexcept
{
    const auto dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 <= 0.0)
        return distance (p, a);
    const auto t = std::clamp (((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
    return distance (p, { a.x + t * dx, a.y + t * dy });
}

inline bool segmentsCross (Point a, Point b, Point c, Point d) noexcept
{
    const auto cross = [] (Point o, Point p, Point q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
    const auto d1 = cross (c, d, a), d2 = cross (c, d, b), d3 = cross (a, b, c), d4 = cross (a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// The shortest distance between segments ab and cd (0 when they cross).
inline double segmentDistance (Point a, Point b, Point c, Point d) noexcept
{
    if (segmentsCross (a, b, c, d))
        return 0.0;
    return std::min ({ pointSegmentDistance (a, c, d), pointSegmentDistance (b, c, d), pointSegmentDistance (c, a, b),
                       pointSegmentDistance (d, a, b) });
}

// Non-zero winding (FL's CreatePolygonRgn WINDING).
inline bool insidePolygon (const std::vector<Point>& poly, Point p) noexcept
{
    int winding = 0;
    for (std::size_t i = 0, n = poly.size(); i < n; ++i)
    {
        const auto a = poly[i], b = poly[(i + 1) % n];
        const auto side = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
        if (a.y <= p.y)
        {
            if (b.y > p.y && side > 0)
                ++winding;
        }
        else if (b.y <= p.y && side < 0)
            --winding;
    }
    return winding != 0;
}

// The rectangle lies wholly in the polygon: its corners are inside and no polygon edge crosses it.
inline bool rectInsidePolygon (const std::vector<Point>& poly, Rect r) noexcept
{
    if (poly.size() < 3)
        return false;
    const Point corners[] { { r.left, r.top }, { r.right, r.top }, { r.right, r.bottom }, { r.left, r.bottom } };
    for (const auto& c : corners)
        if (! insidePolygon (poly, c))
            return false;
    for (std::size_t i = 0, n = poly.size(); i < n; ++i)
    {
        const auto a = poly[i], b = poly[(i + 1) % n];
        if ((a.x > r.left && a.x < r.right && a.y > r.top && a.y < r.bottom) || (b.x > r.left && b.x < r.right && b.y > r.top && b.y < r.bottom))
            return false;
        for (int k = 0; k < 4; ++k)
            if (segmentsCross (a, b, corners[k], corners[(k + 1) % 4]))
                return false;
    }
    return true;
}

// FL's test rectangle of a clip (FUN_00e6f330): the clip's bounds inset so the trail has to go around its middle.
inline Rect innerRect (Rect clip) noexcept
{
    const auto w = std::max (0.0, clip.right - clip.left - 6.0), h = std::max (0.0, clip.bottom - clip.top - 6.0);
    const auto dx = std::min (std::max (16.0, std::floor (w / 8.0)), std::floor (w / 2.0));
    const auto dy = std::min (std::max (8.0, std::floor (h / 4.0)), std::floor (h / 2.0));
    Rect r { std::max (0.0, clip.left + dx), clip.top + dy, clip.right - dx, clip.bottom - dy };
    r.right = std::max (r.right, r.left + 1.0);
    r.bottom = std::max (r.bottom, r.top + 1.0);
    return r;
}

class Trail
{
public:
    void clear() noexcept { points.clear(); }
    const std::vector<Point>& getPoints() const noexcept { return points; }

    // A mouse move at `ms` (milliseconds). `captured`: a button is down (FL: GetCapture()). Returns the polygon when the
    // trail just closed (empty otherwise); call consumed() when that loop selected something.
    std::vector<Point> move (Point p, std::uint32_t ms, bool captured)
    {
        if (captured || (hasLast && ms - lastMs > 200))
            points.clear();
        hasLast = true;
        lastMs = ms;
        std::vector<Point> loop;
        if (! points.empty() && points.back().x == p.x && points.back().y == p.y)
            return loop;
        auto step = 1.0e9;
        if (! points.empty())
        {
            step = distance (points.back(), p);
            if (step < 4.0)
                return loop;
        }
        points.push_back (p);
        const auto n = points.size();
        if (n > 10)
        {
            const auto a = points[n - 2], b = points[n - 1];
            const auto minX = std::min (a.x, b.x) - 18.0, maxX = std::max (a.x, b.x) + 18.0;
            const auto minY = std::min (a.y, b.y) - 18.0, maxY = std::max (a.y, b.y) + 18.0;
            for (std::size_t i = 0; i + 8 <= n; ++i)
            {
                const auto c = points[i], d = points[i + 1];
                if (std::max (c.x, d.x) <= minX || std::min (c.x, d.x) >= maxX || std::max (c.y, d.y) <= minY || std::min (c.y, d.y) >= maxY)
                    continue;
                if (segmentDistance (a, b, c, d) < 18.0)
                {
                    loop.assign (points.begin() + (std::ptrdiff_t) i, points.end());
                    break;
                }
            }
        }
        if (step < 12.0 && ! points.empty())
            points.pop_back(); // (FL: the next point takes its place)
        if (points.size() > 275)
            points.erase (points.begin(), points.end() - 250);
        return loop;
    }

private:
    std::vector<Point> points;
    std::uint32_t lastMs = 0;
    bool hasLast = false;
};

// What a closed loop does (FUN_00e6f330): `shift` held or nothing selected = add the enclosed clips to the selection
// (2 or more clips, or 1 with Shift); otherwise, when the loop encloses 2 or more clips and exactly the selected ones,
// the view zooms to the selection's time (FUN_00e4ea50 + FUN_00e7d8e0; checked in FL 2026). `enclosed`: the clips inside; `selectedCount` / `isSelected`: the
// current selection.
enum class Result
{
    nothing,
    select,          // add `enclosed` to the selection
    zoomToSelection  // zoom the view to the selected clips' time
};

inline Result decide (const std::vector<int>& enclosed, bool shift, int selectedCount, const std::vector<bool>& enclosedSelected) noexcept
{
    const auto count = (int) enclosed.size() + (shift ? 1 : 0);
    if (shift || selectedCount == 0)
        return count > 1 ? Result::select : Result::nothing;
    if (enclosed.size() < 2 || (int) enclosed.size() != selectedCount)
        return Result::nothing;
    return std::all_of (enclosedSelected.begin(), enclosedSelected.end(), [] (bool b) { return b; }) ? Result::zoomToSelection : Result::nothing;
}
} // namespace lynx::magiclasso
