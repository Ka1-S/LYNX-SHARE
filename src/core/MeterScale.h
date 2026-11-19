#pragma once

// Round 190lx (DF-61): FL's General › Miscellaneous "Alternate meter scale" ("Alternate scale for all peak meters").
// LYNX's peak meters show -60 … +6 dB on a straight dB axis; with the alternate scale they use FL's alternate law, which
// spreads the top of the range (0 … -15 dB) over most of the meter (capture 190lx A1: the dB the hint bar reads at each
// height of an FL mixer meter). Pure logic, no JUCE.

#include <algorithm>
#include <cmath>

namespace lynx::meterscale
{
inline bool& alternate() noexcept
{
    static bool on = false;
    return on;
}

// FL's alternate law (capture 190lx A1: the hint bar's dB at every 8 px of FL's Master meter, 43 readings, fitted to
// within 0.2 px): height / H = 0.7686 · ln (1 + 2.01 · gain), so 0 dB sits at 85 % of the height, -6 dB at 54 % and the
// top is about +2.5 dB.
inline constexpr float altK = 0.7686f, altA = 2.01f;

// height 0..1 of a level in dB
inline float positionOfDb (float db, bool alt) noexcept
{
    if (! alt)
        return std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f);
    const auto gain = db <= -120.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
    return std::clamp (altK * std::log1p (altA * gain), 0.0f, 1.0f);
}

// the dB at a height 0..1 (the hint bar's reading under the pointer)
inline float dbAtPosition (float pos, bool alt) noexcept
{
    pos = std::clamp (pos, 0.0f, 1.0f);
    if (! alt)
        return -60.0f + 66.0f * pos;
    const auto gain = std::expm1 (pos / altK) / altA;
    return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f;
}

// a linear gain's height (silence: 0)
inline float position (float gain, bool alt) noexcept
{
    if (alt)
        return std::clamp (altK * std::log1p (altA * std::max (0.0f, gain)), 0.0f, 1.0f);
    const auto db = gain > 1.0e-3f ? 20.0f * std::log10 (gain) : -60.0f;
    return positionOfDb (std::max (db, -60.0f), false);
}
} // namespace lynx::meterscale
