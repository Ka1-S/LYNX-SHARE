#include "core/FlmNoteFx.h"

#include <algorithm>
#include <cmath>

namespace lynx::flmnotefx
{
namespace
{
constexpr std::uint16_t maskOf (std::initializer_list<int> intervals) noexcept
{
    std::uint16_t m = 0;
    for (const auto i : intervals)
        m = (std::uint16_t) (m | (1u << i));
    return m;
}

// FLM's Scale list in its order (the names and interval sets are music theory; the direction is FLM's choice per scale)
constexpr ScaleInfo scales[scaleCount] = {
    { "Chromatic", 0x0FFF, 0 },
    { "Major", maskOf ({ 0, 2, 4, 5, 7, 9, 11 }), 1 },
    { "Major Bebop", maskOf ({ 0, 2, 4, 5, 7, 8, 9, 11 }), 1 },
    { "Major Bulgarian", maskOf ({ 0, 2, 5, 6, 8, 9, 11 }), 1 },
    { "Major Pentatonic", maskOf ({ 0, 2, 4, 7, 9 }), 1 },
    { "Major Persian", maskOf ({ 0, 1, 4, 5, 6, 8, 11 }), 1 },
    { "Major Polymode", maskOf ({ 0, 2, 3, 5, 6, 8, 10, 11 }), 1 },
    { "Minor Harmonic", maskOf ({ 0, 2, 3, 5, 7, 8, 11 }), -1 },
    { "Minor Hungarian", maskOf ({ 0, 1, 2, 3, 6, 7, 8, 11 }), -1 },
    { "Minor Melodic", maskOf ({ 0, 2, 3, 5, 7, 9, 11 }), -1 },
    { "Minor Natural", maskOf ({ 0, 2, 3, 5, 7, 8, 10 }), -1 },
    { "Minor Neapolitan", maskOf ({ 0, 1, 3, 5, 7, 8, 11 }), -1 },
    { "Minor Pentatonic", maskOf ({ 0, 3, 5, 7, 10 }), -1 },
    { "Minor Polymode", maskOf ({ 0, 1, 2, 4, 5, 7, 8, 10 }), -1 },
    { "Minor Romanian", maskOf ({ 0, 2, 3, 6, 7, 9, 10 }), -1 },
    { "Arabic", maskOf ({ 0, 1, 4, 5, 7, 8, 11 }), -1 },
    { "Blues", maskOf ({ 0, 3, 5, 6, 7, 10 }), -1 },
    { "Diminished", maskOf ({ 0, 2, 3, 5, 6, 8, 9, 11 }), -1 },
    { "Dominant Bebop", maskOf ({ 0, 2, 4, 5, 7, 9, 10, 11 }), -1 },
    { "Dorian", maskOf ({ 0, 2, 3, 5, 7, 9, 10 }), -1 },
    { "Egyptian", maskOf ({ 0, 2, 5, 7, 10 }), -1 },
    { "Enigmatic", maskOf ({ 0, 1, 4, 6, 8, 10, 11 }), 1 },
    { "Hirajoshi", maskOf ({ 0, 2, 3, 7, 8 }), -1 },
    { "Iwato", maskOf ({ 0, 1, 5, 6, 10 }), -1 },
    { "Japanese Insen", maskOf ({ 0, 1, 5, 7, 10 }), -1 },
    { "Locrian", maskOf ({ 0, 1, 3, 5, 6, 8, 10 }), -1 },
    { "Locrian Super", maskOf ({ 0, 1, 3, 4, 6, 8, 10 }), -1 },
    { "Lydian", maskOf ({ 0, 2, 4, 6, 7, 9, 11 }), 1 },
    { "Mixolydian", maskOf ({ 0, 2, 4, 5, 7, 9, 10 }), -1 },
    { "Neapolitan", maskOf ({ 0, 1, 3, 5, 7, 9, 11 }), -1 },
    { "Phrygian", maskOf ({ 0, 1, 3, 5, 7, 8, 10 }), -1 },
    { "Phrygian Dominant", maskOf ({ 0, 1, 4, 5, 7, 8, 10 }), -1 },
    { "Whole Tone", maskOf ({ 0, 2, 4, 6, 8, 10 }), 1 },
};

std::uint16_t rotated (std::uint16_t intervals, int root) noexcept
{
    std::uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if ((intervals >> i) & 1u)
            m = (std::uint16_t) (m | (1u << ((i + root) % 12)));
    return m;
}

float at (std::span<const float> p, std::size_t i, float fallback) noexcept { return i < p.size() ? p[i] : fallback; }
bool enabled (std::span<const float> p) noexcept { return at (p, 0, 1.0f) >= 0.5f; }
bool inMask (std::uint16_t mask, int key) noexcept { return key >= 0 && key <= 127 && ((mask >> (key % 12)) & 1u) != 0; }
} // namespace

const ScaleInfo& scaleInfo (int index) noexcept { return scales[(std::size_t) std::clamp (index, 0, scaleCount - 1)]; }

int transposeShift (std::span<const float> params) noexcept
{
    if (! enabled (params))
        return 0;
    const auto octave = (int) std::lround (std::clamp (8.0 * (double) at (params, 1, 0.5f) - 4.0, -4.0, 4.0));
    const auto semitone = (int) std::lround (std::clamp (24.0 * (double) at (params, 2, 0.5f) - 12.0, -12.0, 12.0));
    return semitone + 12 * octave;
}

ScaleSetting scaleSetting (std::span<const float> params) noexcept
{
    ScaleSetting s;
    s.enabled = enabled (params);
    s.snap = at (params, 3, 0.0f) > 0.5f;
    const auto root = std::clamp ((int) std::lround (11.0 * (double) at (params, 1, 0.0f)), 0, 11);
    const auto index = std::clamp ((int) std::lround (33.0 * (double) at (params, 2, 0.0f)), 0, scaleCount);
    if (params.size() >= 16)
    {
        s.mask = 0;
        for (int i = 0; i < 12; ++i)
            if (params[(std::size_t) (4 + i)] > 0.5f)
                s.mask = (std::uint16_t) (s.mask | (1u << i));
    }
    else // (a file without the switches: the named scale)
        s.mask = index < scaleCount ? rotated (scales[index].mask, root) : (std::uint16_t) 0x0FFF;
    // the switches matching a named scale at this root: that scale's direction; anything else is a custom set
    s.direction = 0;
    if (index < scaleCount && rotated (scales[index].mask, root) == s.mask)
        s.direction = scales[index].direction;
    else
        for (const auto& named : scales)
            if (rotated (named.mask, root) == s.mask)
            {
                s.direction = named.direction;
                break;
            }
    return s;
}

int scaleKey (const ScaleSetting& s, int key) noexcept
{
    if ((s.mask & 0x0FFF) == 0 || inMask (s.mask, key))
        return key;
    if (s.direction != 0)
    {
        for (int k = 1; k < 12; ++k)
        {
            if (inMask (s.mask, key + k * s.direction))
                return key + k * s.direction;
            if (inMask (s.mask, key - k * s.direction))
                return key - k * s.direction;
        }
        return key;
    }
    for (int d = 1; d < 12; ++d)
    {
        if (inMask (s.mask, key + d))
            return key + d;
        if (inMask (s.mask, key - d))
            return key - d;
    }
    return key;
}

bool Chain::add (std::uint32_t type, std::span<const float> params)
{
    if (type != transpose && type != scale && type != randomizer)
        return false;
    if (! enabled (params))
        return true; // (passes every note through)
    Step s;
    s.type = type;
    if (type == transpose)
    {
        s.shift = transposeShift (params);
        if (s.shift == 0)
            return true;
    }
    else if (type == scale)
    {
        s.scale = scaleSetting (params);
        if ((s.scale.mask & 0x0FFF) == 0x0FFF || (s.scale.mask & 0x0FFF) == 0)
            return true;
        hasFilter = hasFilter || ! s.scale.snap;
    }
    else
    {
        s.range = std::clamp ((int) std::lround (24.0 * (double) at (params, 1, 0.0f)), 0, 24);
        s.velocity = std::clamp (at (params, 2, 0.0f), 0.0f, 1.0f);
        s.pan = std::clamp (at (params, 3, 0.0f), 0.0f, 1.0f);
        s.gate = std::clamp (at (params, 4, 1.0f), 0.0f, 1.0f);
        if (s.range == 0 && s.velocity <= 0.0f && s.pan <= 0.0f && s.gate >= 1.0f)
            return true;
        hasRandom = true;
    }
    steps.push_back (s);
    return true;
}

double Chain::uniform() noexcept
{
    // (a small generator of LYNX's own: FLM's draws differ at every playback anyway)
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (double) (state >> 8) / 16777216.0;
}

bool Chain::apply (Note& n)
{
    for (const auto& s : steps)
    {
        if (s.type == transpose)
            n.key = std::clamp (n.key + s.shift, 0, 127);
        else if (s.type == scale)
        {
            const auto key = scaleKey (s.scale, n.key);
            if (key != n.key && ! s.scale.snap)
                return false;
            n.key = key;
        }
        else
        {
            if (s.gate <= 0.0f || (s.gate < 1.0f && (double) s.gate < uniform()))
                return false;
            if (s.range > 0)
                n.key = std::clamp (n.key + (int) std::floor (uniform() * (2 * s.range + 1)) - s.range, 0, 127);
            if (s.velocity > 0.0f)
                n.velocity = std::clamp (n.velocity + (float) ((2.0 * uniform() - 1.0) * s.velocity), 0.0f, 1.0f);
            if (s.pan > 0.0f)
                n.pan = (float) ((std::clamp (2.0 * n.pan - 1.0 + (2.0 * uniform() - 1.0) * s.pan, -1.0, 1.0) + 1.0) * 0.5);
        }
    }
    return true;
}
} // namespace lynx::flmnotefx
