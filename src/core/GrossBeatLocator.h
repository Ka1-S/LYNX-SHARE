#pragma once

// Gross Beat's playback locator in FL's colours (round 190jd; live check 190iw Q7, FL 2026, Thief's Saw Bass insert, 59 fps
// film notes/q7-grossbeat-locator.txt): a 1 px line whose colour cycles once per beat — cream (#FFFCC1 … #FFFFEB) on the
// beat, then warm orange (#E1B179 → #CD9D65 → #B07D5E) and dusty red (#8F5F5A → #764A5A) by the end of the beat — with a
// trail of up to 3 px to its left that fades with it (grey-blue #5F6667 → #817C70 next to the cream line, 1 px or none
// late in the beat). It moves in jumps at about 21 Hz. LYNX's own style (the playhead colour + a 6 px trail) stays the
// default (owner decision); this one is an option of the editor's Options menu.
//
// No JUCE: tested in tests/Livefix190jdTests.inc.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace lynx::gblocator
{
// the editor's style switch (ui-options "grossBeatFlLocator"; off = LYNX's playhead colour)
inline std::atomic<bool>& flStyleFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool flStyle() noexcept { return flStyleFlag().load (std::memory_order_relaxed); }
inline void setFlStyle (bool on) noexcept { flStyleFlag().store (on, std::memory_order_relaxed); }

// FL's locator moves about 21 times a second (jumps of 21–31 px every ~47 ms in the film)
constexpr double flRateHz = 21.0;

// how many colour steps a beat is drawn in (the GPU overlay caches one picture per step: a bounded set)
constexpr int stepsPerBeat = 16;

// where in the beat the locator is (0 on the beat … just under 1 at its end), from the envelope position 0..1 of `beats` beats
inline double beatPhase (double position, int beats = 4) noexcept
{
    const auto b = std::clamp (position, 0.0, 1.0) * (double) beats;
    return b - std::floor (b);
}

// the phase rounded down to its colour step (0 … stepsPerBeat - 1)
inline int stepOf (double phase) noexcept
{
    return std::clamp ((int) std::floor (phase * stepsPerBeat), 0, stepsPerBeat - 1);
}

// the line's colour (0xAARRGGBB) at a phase of the beat: the measured key colours, linearly between them
inline std::uint32_t lineColour (double phase) noexcept
{
    struct Key { double at; std::uint32_t rgb; };
    static constexpr std::array<Key, 7> keys { { { 0.00, 0xFFFCC1 }, { 0.15, 0xFECE93 }, { 0.35, 0xE1B179 }, { 0.50, 0xCD9D65 },
                                                 { 0.65, 0xB07D5E }, { 0.80, 0x8F5F5A }, { 1.00, 0x764A5A } } };
    const auto p = std::clamp (phase, 0.0, 1.0);
    std::size_t k = 0;
    while (k + 2 < keys.size() && p > keys[k + 1].at)
        ++k;
    const auto t = (p - keys[k].at) / (keys[k + 1].at - keys[k].at);
    const auto mix = [t] (std::uint32_t a, std::uint32_t b, int shift) {
        const auto ca = (double) ((a >> shift) & 0xFF), cb = (double) ((b >> shift) & 0xFF);
        return (std::uint32_t) std::lround (ca + (cb - ca) * std::clamp (t, 0.0, 1.0)) << shift;
    };
    return 0xFF000000u | mix (keys[k].rgb, keys[k + 1].rgb, 16) | mix (keys[k].rgb, keys[k + 1].rgb, 8) | mix (keys[k].rgb, keys[k + 1].rgb, 0);
}

// the trail's opacity (of the line colour over the editor) `px` pixels left of the line (1 … 3) at a phase: 3 px on the beat,
// fading out over the beat (FL: 3 px next to the cream line, 2 px on the orange, 1 px or none on the red)
inline float trailAlpha (int px, double phase) noexcept
{
    if (px < 1 || px > 3)
        return 0.0f;
    static constexpr std::array<float, 3> base { 0.42f, 0.24f, 0.10f };
    const auto fresh = 1.0 - std::clamp (phase, 0.0, 1.0) * 1.15; // (gone shortly before the beat's end)
    const auto reach = 3.0 * std::max (0.0, fresh);               // how many pixels the trail still has
    if ((double) px > reach + 0.5)
        return 0.0f;
    return (float) (base[(std::size_t) px - 1] * std::max (0.0, fresh));
}
} // namespace lynx::gblocator
