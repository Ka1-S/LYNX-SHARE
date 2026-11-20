#pragma once

// FL's wrapper Presets menu for LYNX's native editors (round 190ih, PLAN R8 part: DF-122 / DP-92 / DP-93, owner
// decision PL4). FL's manual (Plugin wrapper › Presets): "Randomize gives random values to the published (i.e. known
// to FL Studio) plugin settings"; "Hybridize blends between the previous and next selected preset ... When enabled,
// selecting a preset will load a random 50% of the settings". Here "published" = the effect's listed parameters
// (EffectInfo::params); the rest of the slot (hidden values, the state blob) is left alone. Each is one edit, one undo
// step. Deterministic for a seed (tests).

#include <cstdint>

#include "core/Song.h"

namespace lynx::presetmix
{
// FL's Hybridize is one mode of the wrapper's Presets menu: LYNX's native editors (round 190ih) and the hosted plugins'
// wrapper (round 190in) share it, so switching it in one window shows ticked in the other (rollup 0.1.30, round 190iy).
// Message thread; not saved (a mode of the session, off at start-up).
inline bool& hybridizeMode() noexcept
{
    static bool on = false;
    return on;
}

// a small LCG (no allocation, the same on every platform)
inline float next (std::uint32_t& seed) noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return (float) ((seed >> 8) * (1.0 / 16777216.0));
}

// random values (0 … 1) for the first `published` parameters
inline void randomize (EffectParams& params, int published, std::uint32_t seed) noexcept
{
    for (int i = 0; i < published && i < maxEffectParams; ++i)
        params[(std::size_t) i] = next (seed);
}

// a preset loaded in Hybridize mode: each of the first `published` parameters comes from the new preset or stays as
// it was, half and half at random; the parameters past `published` follow the preset
inline EffectParams hybridize (const EffectParams& before, const EffectParams& preset, int published, std::uint32_t seed) noexcept
{
    auto out = preset;
    for (int i = 0; i < published && i < maxEffectParams; ++i)
        if (next (seed) < 0.5f)
            out[(std::size_t) i] = before[(std::size_t) i];
    return out;
}
} // namespace lynx::presetmix
