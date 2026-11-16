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

// Settings › General › Animation rate (round 190ip; proposal 3 of round 190hc, Live's frame clock): a user cap runs on
// every n-th display frame, a whole divisor of the refresh, so its frames are evenly paced (a 60 Hz cap on a 144 Hz
// display was every 2nd or 3rd frame, 57.6-60 a second unevenly; now every 3rd, 48 evenly). `balancedCap`: the refresh
// halved until it is at most 100 Hz (Live's rule: 72 on 144 Hz, 60 on 120 / 240 Hz, 82.5 on 165 Hz, a 60 Hz display at
// its refresh).
constexpr double balancedCap = -1.0;

inline int evenDivisor (double refreshHz, double capHz)
{
    if (refreshHz <= 0.0 || capHz == 0.0)
        return 1;
    if (capHz < 0.0) // balanced
    {
        int n = 1;
        while (refreshHz / n > 100.0 * 1.02 && n < 64)
            n *= 2;
        return n;
    }
    // the smallest n whose refresh / n is at most the cap (2 % slack: a 59.94 Hz display with a 60 Hz cap runs at its refresh)
    int n = 1;
    while (refreshHz / n > capHz * 1.02 && n < 64)
        ++n;
    return n;
}

// Every n-th frame, counted from the frame it last ran on (half a frame of slack for the stamps' jitter); the same stamp
// again (another ticker on the same blank) gets the same answer; after a stall it runs at once, without a burst.
struct Divider
{
    bool runs (double stampMs, double periodMs, int n)
    {
        if (n <= 1 || periodMs <= 0.0)
            return true;
        if (stampMs == lastStampMs)
            return lastRan;
        lastStampMs = stampMs;
        lastRan = stampMs >= nextMs;
        if (lastRan)
            nextMs = stampMs + ((double) n - 0.5) * periodMs;
        return lastRan;
    }

    double lastStampMs = -1.0, nextMs = 0.0;
    bool lastRan = false;
};
} // namespace lynx::framepace
