#pragma once

// Round qafix190kq (QA-6 of 0.1.30): when an export's tail ("Leave remainder") counts as silent. A block is quiet when
// its peak stays under `silence`, or when every channel holds one value within `silence` over it: a DC offset (the
// FL demo "Never Out" ended on -73 dBFS of DC, which kept the tail running to its cap, 5.5 s more) is no sound.

#include <algorithm>

namespace lynx::tailsilence
{
inline bool quietBlock (const float* const* channels, int numChannels, int n, float silence) noexcept
{
    if (n <= 0 || numChannels <= 0)
        return true;
    auto peak = 0.0f;
    auto flat = true;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* x = channels[ch];
        auto lo = x[0], hi = x[0];
        for (int i = 1; i < n; ++i)
        {
            lo = std::min (lo, x[i]);
            hi = std::max (hi, x[i]);
        }
        peak = std::max ({ peak, -lo, hi });
        flat = flat && hi - lo < silence;
    }
    return peak < silence || flat;
}
} // namespace lynx::tailsilence
