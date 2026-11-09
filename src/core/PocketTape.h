#pragma once

// FL Studio Mobile's "tape" (round 188h, REA_rodada182c "F5 effects, round 188h"): a loop whose phase advances by a
// speed (one loop = the delay); each sample is recorded with its time stamp and the previous loop is read back by linear
// interpolation between stamps; the speed glides to its target by 0.1 % per sample (a Time change bends the pitch).
// Used by Pocket Tape Delay and Pocket MultiFX's flanger. Internal to the Pocket engines.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lynx::pocket::tape
{
struct Point
{
    float s = 0.0f;
    double t = 0.0;
};
struct Loop
{
    std::vector<Point> points; // count recorded + one extra point appended at the wrap
    Point before { 0.0f, -1.0 }; // the point before index 0 (the previous loop's last, one loop earlier)
    int count = 0, cursor = 0;
};
struct Tape
{
    Loop w, r;
    int n = 4;
    double pos = 0.0, v = -1.0, target = 0.0;
    float fb = 0.0f;
    void allocate (int samples)
    {
        n = samples;
        w.points.assign ((std::size_t) (n + 258), Point {});
        r.points.assign ((std::size_t) (n + 258), Point {});
    }
    void clear() noexcept
    {
        for (auto* l : { &w, &r })
        {
            std::fill (l->points.begin(), l->points.end(), Point {});
            l->before = { 0.0f, -1.0 };
            l->count = l->cursor = 0;
        }
        pos = 0.0;
        v = -1.0;
    }
    void setDelay (double t) noexcept
    {
        if (! std::isfinite (t) || t < 0.0)
            t = 0.0;
        const auto m = 2.0 / n;
        target = 1.0 / (((1.0 - m) * t + m) * n);
    }
    float tick (float x) noexcept
    {
        float echo = 0.0f;
        while (r.cursor <= r.count)
        {
            const auto& p0 = r.cursor == 0 ? r.before : r.points[(std::size_t) (r.cursor - 1)];
            const auto& p1 = r.points[(std::size_t) r.cursor];
            if (pos <= p1.t)
            {
                const auto t0 = (float) p0.t, t1 = (float) p1.t;
                const auto frac = t0 < t1 ? (pos - (double) t0) / (double) (t1 - t0) : 0.0;
                echo = (float) ((double) p0.s + (double) (p1.s - p0.s) * frac);
                if (echo > -1e-9f && echo < 1e-9f)
                    echo = 0.0f;
                break;
            }
            ++r.cursor;
        }
        advance (x + echo * 1.0f * fb);
        return echo;
    }
    void advance (float y) noexcept
    {
        if (v < 0.0)
            v = target;
        pos += std::clamp (v, 1.0 / n, 0.9);
        if (pos >= 1.0)
        {
            w.before = r.count > 0 ? r.points[(std::size_t) (r.count - 1)] : Point {};
            w.before.t -= 1.0;
            if (w.count < (int) w.points.size())
                w.points[(std::size_t) w.count] = { y, pos };
            w.cursor = 0;
            std::swap (w, r);
            pos -= 1.0;
            w.count = 0;
        }
        if (w.count < (int) w.points.size() - 1)
            w.points[(std::size_t) w.count++] = { y, pos };
        v += (target - v) * 0.001;
    }
};
} // namespace lynx::pocket::tape
