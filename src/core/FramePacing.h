#pragma once

// Which display frames a capped animation runs on (round 174e2, ui/FrameTicker.h). perFrame = the animation's rate
// divided by the display's (72 Hz on a 144 Hz display = 0.5). Frame n runs when the running count n * perFrame reaches
// a new whole number: the frames are spread evenly, and every animation with the same cap picks the same frames (one
// window present per frame). At 0.9 and above every frame runs (a 170 Hz cap on a 144 Hz display).

#include <cmath>

namespace lynx::framepace
{
inline bool runs (double frameIndex, double perFrame)
{
    if (perFrame >= 0.9)
        return true;
    return std::floor (frameIndex * perFrame + 1.0e-6) != std::floor ((frameIndex - 1.0) * perFrame + 1.0e-6);
}
} // namespace lynx::framepace
