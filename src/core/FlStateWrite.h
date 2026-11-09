#pragma once

// FL Studio's own saved state (plugin record 0xD5) written from a LYNX slot's parameters (round 184a S8a, for the Master
// looks): the inverse of the readers that import these plugins as LYNX's exact engines (FlEffects.cpp). A slot LYNX
// makes has no FL state of its own (FlOrigin), so without these the FL export renders it into the insert instead of
// writing the plugin. Each writer gives the bytes FL itself saves for those control values; the readers' layouts are
// documented in docs/analise/REA_rodada48 (Limiter), REA_rodada51 (Soft Clipper), REA_rodada54 (Stereo Enhancer) and
// REA_rodada56 §2.2 (Multiband Compressor).
// Header only, no JUCE.

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/FruityDynamics.h"
#include "core/FruityMultiband.h"
#include "core/FruityShapers.h"
#include "core/ParametricEqFl.h"
#include "core/StereoFx.h"
#include "core/Song.h"

namespace lynx::flstate
{
inline void putI32 (std::vector<std::uint8_t>& d, std::size_t at, int v)
{
    if (d.size() < at + 4)
        d.resize (at + 4);
    for (int b = 0; b < 4; ++b)
        d[at + (std::size_t) b] = (std::uint8_t) (((std::uint32_t) v >> (8 * b)) & 0xFF);
}

// FL's raw control from LYNX's normalised value over [min, max].
inline int rawOf (float v, int min, int max) noexcept
{
    const auto c = v < 0.0f ? 0.0 : v > 1.0f ? 1.0 : (double) v;
    return min + (int) std::lround (c * (double) (max - min));
}

// Fruity Soft Clipper: two i32 (threshold 1…127, post 0…160), no version word.
inline std::vector<std::uint8_t> softClipper (const EffectParams& p)
{
    const auto s = flshape::softClipperFromParams (p.data());
    std::vector<std::uint8_t> d;
    putI32 (d, 0, s.threshold);
    putI32 (d, 4, s.post);
    return d;
}

// Fruity Stereo Enhancer: six i32 in FL's order (pan, volume, separation, phase offset, effects order, inversion).
inline std::vector<std::uint8_t> stereoEnhancer (const EffectParams& p)
{
    const auto s = stereofx::stereoEnhancerFromParams (p.data());
    std::vector<std::uint8_t> d;
    int at = 0;
    for (int v : { s.pan, s.volume, s.separation, s.delay, s.post, s.invert })
        putI32 (d, (std::size_t) (4 * at++), v);
    return d;
}

// Fruity Limiter: a fresh FL instance's state (version 7, `base`) with the 18 controls at 0x04, side input 0 at 0xA4 and
// the "ceiling exactly" option in bit 0 of byte 0xA8. `base` = FlpExport's fruityLimiterDefaultState().
inline std::vector<std::uint8_t> limiter (const EffectParams& p, std::vector<std::uint8_t> base)
{
    if (base.size() < 0xA9)
        base.resize (0xA9, 0);
    for (int i = 0; i < fldyn::limiterControls; ++i)
        putI32 (base, 4 + 4 * (std::size_t) i, rawOf (p[(std::size_t) i], fldyn::limiterMin[(std::size_t) i], fldyn::limiterMax[(std::size_t) i]));
    putI32 (base, 0xA4, 0);
    base[0xA8] = (std::uint8_t) ((base[0xA8] & ~1u) | (p[(std::size_t) fldyn::limiterControls] >= 0.5f ? 1u : 0u));
    return base;
}

// Fruity Multiband Compressor: version 0 and 30 words (124 bytes, what every FL state in the installation is): the 28
// controls, then the two editor-only words (meter / analyser flags; they never reach the engine), left at 0.
inline std::vector<std::uint8_t> multiband (const EffectParams& p)
{
    std::vector<std::uint8_t> d;
    putI32 (d, 0, 0);
    for (int i = 0; i < flmb::controls; ++i)
        putI32 (d, 4 + 4 * (std::size_t) i, rawOf (p[(std::size_t) i], flmb::controlMin[(std::size_t) i], flmb::controlMax[(std::size_t) i]));
    putI32 (d, 4 + 4 * (std::size_t) flmb::controls, 0);
    putI32 (d, 8 + 4 * (std::size_t) flmb::controls, 0);
    return d;
}

// Fruity Parametric EQ 2 (a slot in FL's law, peqfl): a fresh FL instance's state (version 8, 354 bytes, `base` =
// FlpExport's parametricEq2DefaultState()) with the 36 controls at 0x04, HQ / design in the word at 0x124, the linear
// phase and external-HQ bits of the options byte 0x13D, FFT quality (0x135), cross-fade (0x139) and the band mask
// (0x13E). The A/B copy (0x94) and the display settings stay as a new instance has them. Layout: REA_rodada50 §1.
inline std::vector<std::uint8_t> parametricEq2 (const EffectParams& p, std::vector<std::uint8_t> base)
{
    if (base.size() < 0x142)
        base.resize (0x142, 0);
    const auto s = peqfl::fromParams (p);
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        putI32 (base, 4 + 4 * ub, s.gain[ub]);
        putI32 (base, 4 + 4 * (7 + ub), s.freq[ub]);
        putI32 (base, 4 + 4 * (14 + ub), s.width[ub]);
        putI32 (base, 4 + 4 * (21 + ub), s.type[ub]);
        putI32 (base, 4 + 4 * (28 + ub), s.order[ub]);
    }
    putI32 (base, 4 + 4 * 35, s.main);
    using P = peqfl::Processing;
    const bool hq = s.processing != P::plain;
    putI32 (base, 0x124, (hq ? 1 : 0) | (s.processing == P::hqAlternative ? 2 : 0));
    putI32 (base, 0x135, s.linearQuality);
    putI32 (base, 0x139, s.linearFade);
    base[0x13D] = (std::uint8_t) ((base[0x13D] & ~3u) | (s.linearPhase ? 1u : 0u) | (s.processing == P::hqExternal ? 2u : 0u));
    const auto mask = (std::uint32_t) base[0x13E] | ((std::uint32_t) base[0x13F] << 8) | ((std::uint32_t) base[0x140] << 16) | ((std::uint32_t) base[0x141] << 24);
    putI32 (base, 0x13E, (int) ((mask & ~0x7Fu) | (s.mask & 0x7Fu)));
    return base;
}
} // namespace lynx::flstate
