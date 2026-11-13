#pragma once

// FL Studio Mobile's sample slot — a drum pad of a rack's drum set, or the sample of an audio clip: the laws of its
// eleven parameters (sample PRMS; docs/analise/FLM_FORMAT.md §6.1). PROVEN-REA, round 190bs, in the FL-hosted build
// ("FL Studio Mobile_x64.dll"): the slot's setter 0x180748390, its voice start 0x18074bf40 and voice render
// 0x18074c070. Clean-room: the laws in our own words and code; nothing of Image-Line's was copied.
//
// The envelope is three one-pole segments stepped once per output sample: level += (target − level)·rate.
// - Attack: towards 5, clipped at 1 (so it reaches 1 after ln(1.25) / rate samples); EG A below 0.001 = at once.
// - Decay: towards EG S at EG D's rate, from the moment the level reaches 1.
// - Release: towards 0 at EG R's rate — at the note's end on a lane that holds its notes, and Length × the sample's
//   frames after the start when Length is not 0. A hit of a one-shot lane has no release at all.
// FLM's rates are per output sample whatever the device's rate. LYNX takes them at 44.1 kHz (FL's default, the rate
// the factory content was made at) and converts them, so a render does not change with the output rate: an
// assumption, the only part of this page that is not read from the code.

#include <algorithm>
#include <cmath>

namespace lynx::flmpad
{
constexpr double referenceRate = 44100.0;

// SamplerSettings::flmPad: the slot's envelope and start as the file stored them (0…1), then 1 = the lane holds notes
enum : int { attack, decay, sustain, release, length, start, hold, count };

// Level: FLM's fader law without the fader's silent bottom — 10^(3.3·v^0.6 − 3); 0.853 = 0 dB, 1 = +6 dB, 0 = −60 dB
inline float levelGain (float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    return std::pow (10.0f, std::pow (v, 0.6f) * 3.3f - 3.0f);
}

// Speed: whole semitones, (int) (48·v − 24) — truncated towards zero, ±24
inline int speedSemitones (float v) noexcept { return std::clamp ((int) (v * 48.0f - 24.0f), -24, 24); }

// Sample Start: ±0.5·(2v − 1)² seconds. Positive = that far into the sample; negative = silence first (the read
// position starts before the sample and moves at the voice's speed)
inline double startSeconds (float v) noexcept
{
    const auto x = (v + v) - 1.0f;
    return (x < 0.0f ? -0.5 : 0.5) * (double) (x * x);
}

// EG A / EG D / EG R → the segment's rate per sample at 44.1 kHz: 0.0002 / ((0.95·v + 0.05)^4 · K), K = 500 for the
// attack and 50 for decay and release; below 0.001 the segment is immediate (rate 1)
inline float segmentRate (float v, float k) noexcept
{
    if (! (v >= 0.001f))
        return 1.0f;
    auto t = v * 0.95f + 0.05f;
    t = t * t * t * t * k;
    return t >= 0.001f ? 0.0002f / t : 1.0f;
}
inline float attackRate (float v) noexcept { return segmentRate (v, 500.0f); }
inline float decayRate (float v) noexcept { return segmentRate (v, 50.0f); }

// a rate at 44.1 kHz as the rate that follows the same curve in time at `sampleRate`
inline float rateAt (float rate, double sampleRate) noexcept
{
    if (rate >= 1.0f || ! (sampleRate > 0.0) || sampleRate == referenceRate)
        return std::min (rate, 1.0f);
    return (float) (1.0 - std::pow (1.0 - (double) rate, referenceRate / sampleRate));
}

// seconds the attack takes to reach its top (0 = at once), and a decay / release segment's time constant
inline double attackSeconds (float v) noexcept
{
    const auto r = attackRate (v);
    return r >= 1.0f ? 0.0 : std::log (0.8) / std::log (1.0 - (double) r) / referenceRate;
}
inline double decayTimeConstant (float v) noexcept
{
    const auto r = decayRate (v);
    return r >= 1.0f ? 0.0 : -1.0 / std::log (1.0 - (double) r) / referenceRate;
}
} // namespace lynx::flmpad
