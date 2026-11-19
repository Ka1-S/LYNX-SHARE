#pragma once

// Round details9_190mv (0.1.30, details pass 9): small pure helpers of the round's rows. No JUCE.

#include <algorithm>
#include <cmath>

namespace lynx::masterpitch
{
// FL's Master pitch › "Type in value..." (DF-22, capture 190mv A6: "Master pitch (Hz)", 440): the tuning of A in Hz for
// the knob's cents and back (LYNX's range ±1200 cents = 220…880 Hz).
inline double hzOfCents (double cents) noexcept
{
    return 440.0 * std::pow (2.0, std::clamp (cents, -1200.0, 1200.0) / 1200.0);
}

inline double centsOfHz (double hz) noexcept
{
    return hz > 0.0 ? std::clamp (1200.0 * std::log2 (hz / 440.0), -1200.0, 1200.0) : 0.0;
}
} // namespace lynx::masterpitch
