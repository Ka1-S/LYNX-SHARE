#pragma once

// Round 190lx (DF-61): FL's General › Miscellaneous "Alternate meter scale" ("Alternate scale for all peak meters").
// LYNX's peak meters show -60 … +6 dB on a straight dB axis; with the alternate scale they use FL's alternate law, which
// spreads the top of the range (0 … -15 dB) over most of the meter (capture 190lx A1: the dB the hint bar reads at each
// height of an FL mixer meter). Pure logic, no JUCE.

#include <algorithm>
#include <array>
#include <cmath>

namespace lynx::meterscale
{
inline bool& alternate() noexcept
{
    static bool on = false;
    return on;
}

// FL's alternate law as points (dB, height 0..1 from the bottom), measured; linear between them.
struct Point
{
    float db, pos;
};
inline constexpr std::array<Point, 2> alternatePoints { { { -60.0f, 0.0f }, { 6.0f, 1.0f } } };

// height 0..1 of a level in dB
inline float positionOfDb (float db, bool alt) noexcept
{
    if (! alt)
        return std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f);
    const auto& p = alternatePoints;
    if (db <= p.front().db)
        return p.front().pos;
    for (std::size_t i = 1; i < p.size(); ++i)
        if (db <= p[i].db)
            return p[i - 1].pos + (p[i].pos - p[i - 1].pos) * (db - p[i - 1].db) / (p[i].db - p[i - 1].db);
    return p.back().pos;
}

// the dB at a height 0..1 (the hint bar's reading under the pointer)
inline float dbAtPosition (float pos, bool alt) noexcept
{
    pos = std::clamp (pos, 0.0f, 1.0f);
    if (! alt)
        return -60.0f + 66.0f * pos;
    const auto& p = alternatePoints;
    for (std::size_t i = 1; i < p.size(); ++i)
        if (pos <= p[i].pos)
            return p[i - 1].db + (p[i].db - p[i - 1].db) * (pos - p[i - 1].pos) / std::max (1.0e-6f, p[i].pos - p[i - 1].pos);
    return p.back().db;
}

// a linear gain's height (silence: 0)
inline float position (float gain, bool alt) noexcept
{
    const auto db = gain > 1.0e-3f ? 20.0f * std::log10 (gain) : -60.0f;
    return positionOfDb (std::max (db, -60.0f), alt);
}
} // namespace lynx::meterscale
