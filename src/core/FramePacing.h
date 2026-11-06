#pragma once

// Which display frames a capped animation runs on (round 174e2, ui/FrameTicker.h). A Pacer runs a frame once the
// next due time is reached (half a display frame early counts), then moves the due time on by one interval of the cap:
// 72 Hz on a 144 Hz display = every other frame, 60 Hz = every 2nd / 3rd frame. Every ticker with the same cap shares
// one Pacer and the same vertical blank's time stamp, so they run on the same frames (one window present per frame);
// a stamp seen again (another ticker on the same blank) gets the same answer. A cap at 0.9 of the refresh or above
// runs every frame. Time based, it is not thrown off by a second monitor's blanks or by missed frames.

#include <algorithm>

namespace lynx::framepace
{
struct Pacer
{
    bool runs (double stampMs, double periodMs, double capHz)
    {
        if (capHz <= 0.0 || periodMs <= 0.0 || capHz * periodMs / 1000.0 >= 0.9)
            return true;
        if (stampMs == lastStampMs)
            return lastRan;
        lastStampMs = stampMs;
        const auto interval = 1000.0 / capHz;
        lastRan = stampMs + 0.5 * periodMs >= dueMs;
        if (lastRan) // (on cadence; after a stall it starts again from now instead of catching up)
            dueMs = std::max (dueMs + interval, stampMs + 0.5 * interval);
        return lastRan;
    }

    double lastStampMs = -1.0, dueMs = 0.0;
    bool lastRan = false;
};
} // namespace lynx::framepace
