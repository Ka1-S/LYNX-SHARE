#pragma once

// Round 190lx (DF-61): FL's General › Miscellaneous "Alternate meter scale" ("Alternate scale for all peak meters").
// LYNX's peak meters show -60 … +6 dB on a straight dB axis; with the alternate scale they use FL's alternate law, which
// spreads the top of the range (0 … -15 dB) over most of the meter (capture 190lx A1: the dB the hint bar reads at each
// height of an FL mixer meter). Pure logic, no JUCE.
//
// Round 190mf (owner decision): the default (normal) scale in the Pattern layout is FL's own normal law, measured from
// the same 43 readings (capture 190lx A1); the Clip-launch and Simple layouts keep LYNX's straight -60 … +6 dB axis
// (their own looks). The alternate scale stays one switch for all peak meters.

#include "core/LayoutStyle.h"

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

// FL's normal law (capture 190lx A1, the same 43 readings, least squares, max error 0.04 px of 346):
// height / H = 0.22656 · ln (1 + 41 · gain), so 0 dB sits at 85 % of the height, -6 dB at 70 %, -24 dB at 29 % and the
// top is about +6 dB (gain 1.99).
inline constexpr float flK = 0.22656f, flA = 41.0f;

enum class Law
{
    straight,   // LYNX's -60 … +6 dB axis, linear in dB (the Clip-launch and Simple layouts)
    flNormal,   // FL's normal law (the Pattern layout's default)
    flAlternate // FL's "Alternate meter scale" (every layout, when the option is on)
};

// the law the mixer meters use now: the option, else the shown layout's look
inline Law current() noexcept
{
    if (alternate())
        return Law::flAlternate;
    return layoutstyle::current() == layoutstyle::Style::pattern ? Law::flNormal : Law::straight;
}

inline float positionOfGain (float gain, float k, float a) noexcept
{
    return std::clamp (k * std::log1p (a * std::max (0.0f, gain)), 0.0f, 1.0f);
}

inline float gainAtPosition (float pos, float k, float a) noexcept { return std::expm1 (pos / k) / a; }

// height 0..1 of a level in dB
inline float positionOfDb (float db, Law law) noexcept
{
    if (law == Law::straight)
        return std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f);
    const auto gain = db <= -120.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
    return law == Law::flNormal ? positionOfGain (gain, flK, flA) : positionOfGain (gain, altK, altA);
}

// the dB at a height 0..1 (the hint bar's reading under the pointer)
inline float dbAtPosition (float pos, Law law) noexcept
{
    pos = std::clamp (pos, 0.0f, 1.0f);
    if (law == Law::straight)
        return -60.0f + 66.0f * pos;
    const auto gain = law == Law::flNormal ? gainAtPosition (pos, flK, flA) : gainAtPosition (pos, altK, altA);
    return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f;
}

// a linear gain's height (silence: 0)
inline float position (float gain, Law law) noexcept
{
    if (law == Law::flNormal)
        return positionOfGain (gain, flK, flA);
    if (law == Law::flAlternate)
        return positionOfGain (gain, altK, altA);
    const auto db = gain > 1.0e-3f ? 20.0f * std::log10 (gain) : -60.0f;
    return positionOfDb (std::max (db, -60.0f), Law::straight);
}

// (round 190lx's two-law forms: false = straight, true = alternate)
inline Law lawOf (bool alt) noexcept { return alt ? Law::flAlternate : Law::straight; }
inline float positionOfDb (float db, bool alt) noexcept { return positionOfDb (db, lawOf (alt)); }
inline float dbAtPosition (float pos, bool alt) noexcept { return dbAtPosition (pos, lawOf (alt)); }
inline float position (float gain, bool alt) noexcept { return position (gain, lawOf (alt)); }
} // namespace lynx::meterscale
