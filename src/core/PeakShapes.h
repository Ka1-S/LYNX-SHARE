#pragma once
// Round 190kl (issue #197, a user's request): LYNX's extra LFO shapes for the Peak Controller, a LYNX extra next to
// FL's own five (Sine, Triangle, Square, Saw, Random), which stay the default so FL projects play exactly as in FL.
//
// Stored in the slot's hidden parameter `extraShapeParam` (after FL's twelve; not in the parameter list, so automation,
// links and the FL import see FL's controls only) as id / 64:
// - 0 = none: FL's Shape plays, with exactly the code it always had (every project saved before 0.1.30 has no value
//   there, reads 0, and renders bit for bit as before);
// - 1…11 = the shapes below (table revision 1). The ids never change: a later revision only adds ids after the last,
//   so a project keeps its shape whatever LYNX opens it.
// A hidden parameter that is 0 is not written to .lynx (core/Effects.h savedEffectParamCount), so a project that does
// not use the extras saves byte for byte as before. FL's Phase, Tension, Speed and the song lock apply to every shape.
// JUCE-free (tests/U190klTests.inc).

#include <cmath>
#include <cstdint>

namespace lynx::peakshapes
{
constexpr int extraShapeParam = 12; // EffectSlot::params index (FL's 12: Base … Ramp)
constexpr int revision = 1;
constexpr int count = 11;           // ids 1…count
constexpr float idScale = 64.0f;    // the stored value = id / 64 (room for later revisions)

enum Id : int
{
    none = 0,
    rampDown = 1,     // falling saw (FL's Saw rises)
    expRise = 2,      // slow start, fast end: e^(k·x) curve
    expFall = 3,      // a hit that decays: the sidechain / pumping curve
    triSkew = 4,      // a triangle with its top at ¼ (fast rise, slow fall)
    halfSine = 5,     // the positive half of a sine, every period (a bounce)
    pulse25 = 6,      // high for the first quarter only
    steps4 = 7,       // a rising staircase of 4 steps
    steps8 = 8,
    steps16 = 9,
    smoothRandom = 10, // a new random value every period, glided to with a cosine curve
    randomX4 = 11      // sample & hold four times a period (FL's Random holds one value a period)
};

// names as the editor shows them (English; translated by the UI), index = id − 1
inline const char* name (int id) noexcept
{
    static const char* const names[count] { "Ramp down", "Exp rise", "Exp fall", "Triangle skew", "Half sine", "Pulse 25%",
                                            "Steps 4", "Steps 8", "Steps 16", "Smooth random", "Random x4" };
    return id >= 1 && id <= count ? names[id - 1] : "";
}

// the stored value → the id (0 for anything that is not a known id of this revision: FL's shape plays)
inline int idOf (float stored) noexcept
{
    const auto id = (int) std::lround ((double) stored * (double) idScale);
    return id >= 1 && id <= count ? id : 0;
}

inline float valueOf (int id) noexcept { return id >= 1 && id <= count ? (float) id / idScale : 0.0f; }

// a reproducible value in −1…1 for a whole number (FL's Random table is a fixed list too: the same project, the same steps)
inline double noise (std::uint32_t n, std::uint32_t seed) noexcept
{
    auto h = n * 2654435761u ^ seed;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return (double) (h & 0xffffff) / (double) 0x800000 - 1.0;
}

// shape `id` at phase φ (0…1) of period `period` (its whole-cycle count), as 0…1
inline double wave (int id, double phase, std::uint32_t period) noexcept
{
    constexpr auto pi = 3.141592653589793;
    const auto x = phase < 0.0 ? 0.0 : (phase >= 1.0 ? 0.999999999 : phase);
    switch (id)
    {
        case rampDown: return 1.0 - x;
        case expRise: return (std::exp (4.0 * x) - 1.0) / (std::exp (4.0) - 1.0);
        case expFall: return (std::exp (4.0 * (1.0 - x)) - 1.0) / (std::exp (4.0) - 1.0);
        case triSkew: return x < 0.25 ? x / 0.25 : (1.0 - x) / 0.75;
        case halfSine: return std::sin (pi * x);
        case pulse25: return x < 0.25 ? 1.0 : 0.0;
        case steps4: return std::floor (x * 4.0) / 3.0;
        case steps8: return std::floor (x * 8.0) / 7.0;
        case steps16: return std::floor (x * 16.0) / 15.0;
        case smoothRandom:
        {
            const auto a = noise (period, 0x5eedu), b = noise (period + 1u, 0x5eedu);
            const auto t = 0.5 - 0.5 * std::cos (pi * x);
            return (a + (b - a) * t + 1.0) * 0.5;
        }
        case randomX4: return (noise (period * 4u + (std::uint32_t) (x * 4.0), 0x4a11u) + 1.0) * 0.5;
        default: return 0.0;
    }
}
} // namespace lynx::peakshapes
