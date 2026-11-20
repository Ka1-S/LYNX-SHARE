#pragma once

// Ableton Live 12.4's mixer fader law (round als190ka, issue #193; docs/analise/ALS_FORMAT.md §27). A set stores a track's
// volume and its sends as linear gains (Volume 0.000316…1.995 = −70…+6 dB, Send 0.000316…1 = −70…0 dB), but Live moves an
// envelope in a straight line along the fader's travel (its "position" 0…1), not in gain. Measured on Live's own renders
// of a 1 kHz sine under a −70 → +6 dB volume ramp, a send ramp and a return's volume ramp:
//   - above position 0.4 the fader is linear in dB: dB = 40·p − 34 (0 dB at 0.85, +6 dB at 1, −18 dB at 0.4);
//   - below it the table here (every 0.0125), down to −65.5 dB at 0.0125; position 0 is silence;
//   - a send is the same fader 6 dB lower (0 dB at the top).
// Live's curved segments (CurveControl1X/1Y/2X/2Y) bend that straight line in the same position space (within
// 0.09 dB of the renders). No JUCE here (tests/Als190kaTests.inc).

#include <algorithm>
#include <cmath>

namespace lynx::livefader
{
constexpr double step = 0.0125;
// dB at position k·step, k = 0 … 32 (position 0 is −inf in Live; −70 dB here is only the inverse's end)
constexpr double lowTable[33] = { -70.0,   -65.49,  -62.06,  -59.473, -57.14,  -54.915, -52.737, -50.613, -48.559, -46.581, -44.644,
                                  -42.774, -40.982, -39.225, -37.553, -35.943, -34.374, -32.895, -31.46,  -30.089, -28.796, -27.544,
                                  -26.366, -25.251, -24.193, -23.202, -22.271, -21.404, -20.599, -19.857, -19.176, -18.558, -18.0 };
constexpr double sendOffsetDb = 6.0;

// The volume fader's gain in dB at position p (−inf at 0)
inline double volumeDb (double p) noexcept
{
    if (p >= 0.4)
        return 40.0 * std::min (p, 1.0) - 34.0;
    if (p <= 0.0)
        return -INFINITY;
    const auto x = p / step;
    const auto k = std::min (31, (int) x);
    return lowTable[k] + (lowTable[k + 1] - lowTable[k]) * (x - k);
}

// The position of the volume fader that gives `db` (0 at −70 dB and below, 1 at +6 dB and above)
inline double volumePosition (double db) noexcept
{
    if (! (db > -70.0))
        return 0.0;
    if (db >= -18.0)
        return std::min (1.0, (db + 34.0) / 40.0);
    int k = 0;
    while (k < 31 && lowTable[k + 1] < db)
        ++k;
    return (k + (db - lowTable[k]) / (lowTable[k + 1] - lowTable[k])) * step;
}

// A stored gain ↔ its position, for the track volume (`send` false) or a send (`send` true)
inline double positionOfGain (double gain, bool send) noexcept
{
    if (! (gain > 0.000316228 * 1.0001))
        return 0.0;
    return volumePosition (20.0 * std::log10 (gain) + (send ? sendOffsetDb : 0.0));
}
inline double gainAt (double p, bool send) noexcept
{
    if (p <= 0.0)
        return 0.0;
    return std::pow (10.0, (volumeDb (p) - (send ? sendOffsetDb : 0.0)) / 20.0);
}
} // namespace lynx::livefader
