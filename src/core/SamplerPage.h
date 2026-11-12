#pragma once

// The laws of FL's Sampler page (Channel settings › Sampler, round 190dh) that the page's controls apply, kept apart from
// the UI so they can be tested. FL facts: FLEngine_x64.dll handlers of TChanForm (headless Ghidra, read-only; notes in
// /mnt/e/lynx-work/sampler190dh/FL-FACTS.md), form resource TPluginForm.SmpSheet (rea190bc dfm dump), REA rounds 3,
// 167h and 181b. Header-only, no JUCE.

#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::samplerpage
{
// ---- Time stretching ----

// MUL's factor: 2^(v / 10000) (FL's StretchMulWheel, -20000…20000; REA round 3 §5.1)
inline double mulFactor (int mul) noexcept
{
    return std::pow (2.0, (double) std::clamp (mul, -20000, 20000) / 10000.0);
}

// The TIME knob (beats): the length without MUL's factor (SamplerSettings::stretchBeats is TIME × factor)
inline float stretchTime (const SamplerSettings& s) noexcept
{
    return s.stretchBeats <= 0.0f ? 0.0f : (float) ((double) s.stretchBeats / mulFactor (s.stretchMul));
}

// TIME turned: the sample lasts time × MUL's factor (0 = no stretch length)
inline void setStretchTime (SamplerSettings& s, float timeBeats) noexcept
{
    s.stretchBeats = timeBeats <= 0.0f ? 0.0f : std::min (4096.0f, (float) ((double) timeBeats * mulFactor (s.stretchMul)));
}

// MUL turned: TIME stays, the length follows the factor. Without a TIME FL stretches the sample itself by the factor
// (REA round 3 §5.1); LYNX's length is in beats, so that is the sample's own length at the project tempo
// (`sampleBeats`, as the .flp import does it) times the factor. MUL back at 0 with no TIME: no length.
inline void setStretchMul (SamplerSettings& s, int mul, double sampleBeats) noexcept
{
    mul = std::clamp (mul, -20000, 20000);
    const auto time = stretchTime (s);
    s.stretchMul = mul;
    if (time > 0.0f)
        setStretchTime (s, time);
    else if (mul != 0 && sampleBeats > 0.0)
        s.stretchBeats = std::min (4096.0f, (float) (sampleBeats * mulFactor (mul)));
}

// The Mode list. FL 2026 lists Realtime (Resample, Stretch, Stretch Pro), Elastique (e3 generic, e3 mono), Special
// (Slice stretch, Slice map) and Legacy (Auto, e2 ×4) (REA round 181b, runtime shot). LYNX has none of those engines:
// as in Tools › "Switch audio clip channels to" (round 181b, the user's decision) the list holds FL's Resample and LYNX's
// own engines. An FL project's mode is kept (flStretchMode) and goes back to the .flp while it maps to the engine shown.
struct ModeItem
{
    const char* name;        // TR_NOOP-marked where it is used
    bool header;             // a section caption
    bool keepPitch;          // false: Resample
    StretchMethod method;
};

inline constexpr ModeItem modeItems[] = {
    { TR_NOOP ("Realtime"), true, false, StretchMethod::general },
    { TR_NOOP ("Resample"), false, false, StretchMethod::general },
    { TR_NOOP ("Stretch engine"), true, true, StretchMethod::general },
    { TR_NOOP ("General"), false, true, StretchMethod::general },
    { TR_NOOP ("Drums / transients"), false, true, StretchMethod::transients },
    { TR_NOOP ("Vocal / mono"), false, true, StretchMethod::mono },
    { TR_NOOP ("Warp (Ableton-style)"), true, true, StretchMethod::general },
    { TR_NOOP ("Beats"), false, true, StretchMethod::beats },
    { TR_NOOP ("Tones"), false, true, StretchMethod::tones },
    { TR_NOOP ("Texture"), false, true, StretchMethod::texture },
    { TR_NOOP ("Complex"), false, true, StretchMethod::complex },
};
inline constexpr int modeItemCount = (int) (sizeof (modeItems) / sizeof (modeItems[0]));

// The list entry a channel shows (never a header)
inline int modeIndex (const SamplerSettings& s) noexcept
{
    if (! s.keepPitch)
        return 1;
    for (int i = 0; i < modeItemCount; ++i)
        if (! modeItems[i].header && modeItems[i].keepPitch && modeItems[i].method == s.stretchMethod)
            return i;
    return 3;
}

inline void selectMode (SamplerSettings& s, int index) noexcept
{
    if (index < 0 || index >= modeItemCount || modeItems[index].header)
        return;
    s.keepPitch = modeItems[index].keepPitch;
    if (s.keepPitch)
        s.stretchMethod = modeItems[index].method;
}

// Time stretching › PITCH (FL's PitchShiftWheel, cents -1200…1200, default 0): FL's stretch pitch, kept in
// flStretchPitch. SamplerSettings::pitch holds the channel's whole pitch (the header's PITCH knob) with that part in it,
// as the .flp import adds it, so a turn moves both by the same step. FL (StretchTimeWheelRelease 0xf67130 for the PITCH
// wheel): in Resample mode a PITCH away from 0 switches the mode to Auto (FL mode 5) — LYNX's General engine, FL's Auto
// on the way back to a .flp.
inline void turnStretchPitch (SamplerSettings& s, int cents) noexcept
{
    cents = std::clamp (cents, -1200, 1200);
    const auto before = s.pitch;
    s.pitch = std::clamp (s.pitch + (float) (cents - s.flStretchPitch) / 100.0f, -24.0f, 24.0f);
    s.flStretchPitch += (int) std::lround ((s.pitch - before) * 100.0f); // (what the ±24 st clamp let through)
    if (! s.keepPitch && cents != 0)
    {
        s.keepPitch = true;
        s.stretchMethod = StretchMethod::general;
        s.flStretchMode = 5;
    }
}

// ---- Precomputed effects: SMP START and LENGTH ----

// LENGTH (FL's SmpEndWheel, 0…16384, default 16384): the played part of what follows SMP START (REA round 167h §2)
inline float lengthOf (const SamplerSettings& s) noexcept
{
    const auto start = std::clamp (std::min (s.regionStart, s.regionEnd), 0.0f, 1.0f);
    if (start >= 1.0f)
        return 1.0f;
    return std::clamp ((std::clamp (s.regionEnd, start, 1.0f) - start) / (1.0f - start), 0.0f, 1.0f);
}

inline void setLength (SamplerSettings& s, float length) noexcept
{
    const auto start = std::clamp (std::min (s.regionStart, s.regionEnd), 0.0f, 1.0f);
    s.regionStart = start;
    s.regionEnd = std::clamp (start + std::clamp (length, 0.0f, 1.0f) * (1.0f - start), start, 1.0f);
}

// SMP START (FL's SmpStartWheel, 0…16384) moves the start; FL keeps LENGTH (its stored end is relative)
inline void setSmpStart (SamplerSettings& s, float start) noexcept
{
    const auto length = lengthOf (s);
    s.regionStart = s.regionEnd = std::clamp (start, 0.0f, 1.0f);
    setLength (s, length);
}

// ---- Playback: loop START / END (FL's LoopStartWheel 0xf71a20 → FUN_01007150, LoopEndWheel 0xf71b50 → FUN_010074b0) ----

// The loop a channel shows: its own points, or the file's loop (inclusive frames; −1 = none)
inline std::pair<std::int64_t, std::int64_t> shownLoop (const SamplerSettings& s, std::int64_t fileLoopStart, std::int64_t fileLoopEnd) noexcept
{
    if (s.hasLoopOverride())
        return { s.loopStart, s.loopEnd };
    if (fileLoopStart >= 0 && fileLoopEnd > fileLoopStart)
        return { fileLoopStart, fileLoopEnd - 1 };
    return { -1, -1 };
}

// START set to `frame` (of `frames`): clamped to the sample; an END before it (or none) moves to START + 1. With
// `keepLength` (FL: Shift held) the whole loop moves, its length kept, inside the sample.
inline void setLoopStart (SamplerSettings& s, std::int64_t frame, std::int64_t frames, std::int64_t fileLoopStart,
                          std::int64_t fileLoopEnd, bool keepLength = false) noexcept
{
    const auto last = std::max<std::int64_t> (0, frames - 1);
    auto [a, b] = shownLoop (s, fileLoopStart, fileLoopEnd);
    frame = std::clamp<std::int64_t> (frame, 0, last);
    if (keepLength && a >= 0 && b > a)
    {
        auto d = frame - a;
        if (b + d > last)
            d -= b + d - last;
        a = std::clamp<std::int64_t> (a + d, 0, last);
        b = std::clamp<std::int64_t> (b + d, 0, last);
    }
    else
    {
        a = frame;
        if (b < a)
            b = std::min (a + 1, last);
    }
    s.loopStart = a, s.loopEnd = b;
}

// END set to `frame`: never before START (FL: max(START, value)). With `keepLength` the loop moves as with START.
inline void setLoopEnd (SamplerSettings& s, std::int64_t frame, std::int64_t frames, std::int64_t fileLoopStart,
                        std::int64_t fileLoopEnd, bool keepLength = false) noexcept
{
    const auto last = std::max<std::int64_t> (0, frames - 1);
    auto [a, b] = shownLoop (s, fileLoopStart, fileLoopEnd);
    frame = std::clamp<std::int64_t> (frame, 0, last);
    if (a < 0)
        a = 0;
    if (keepLength && b > a)
    {
        auto d = frame - b;
        if (a + d < 0)
            d -= a + d;
        a = std::clamp<std::int64_t> (a + d, 0, last);
        b = std::clamp<std::int64_t> (b + d, 0, last);
    }
    else
        b = std::max (a, frame);
    s.loopStart = a, s.loopEnd = b;
}

// ---- Precomputed effects: CROSSFADE (FL's FXAttackWheelChange 0xf66590) ----

// CROSSFADE above 0 switches "Use loop points" on when it is off
inline void turnCrossfade (SamplerSettings& s, int value) noexcept
{
    s.precompute.crossfade = std::clamp (value, 0, 2048);
    if (s.precompute.crossfade > 0)
        s.useLoopPoints = true;
}

// ---- Content / Declicking mode ----

// FL's DeclickModeSelect items (form TPluginForm, in FL's order; .flp 0xD7 byte 0x53)
inline constexpr const char* declickModeNames[] = { TR_NOOP ("Out only (no bleeding)"), TR_NOOP ("Transient (no bleeding)"),
                                                    TR_NOOP ("Transient (bleeding)"), TR_NOOP ("Generic (bleeding)"),
                                                    TR_NOOP ("Smooth (bleeding)"), TR_NOOP ("Crossfade (bleeding)") };

// The cut release of each mode, ms (FL's declicker table FUN_012c6ae0, REA round 167h §3; the sampler voice's startCut)
inline constexpr double declickReleaseMs[] = { 10.0, 10.0, 20.0, 20.0, 100.0, 200.0 };

// FL's Content flags as the .flp 0x8F word: bit 0 Resample, 1 Load regions, 2 Load ACID markers, 3 Use loop points,
// 8 Keep on disk
inline int contentFlags (const SamplerSettings& s) noexcept
{
    return (s.resampleOnLoad ? 1 : 0) | (s.loadRegions ? 2 : 0) | (s.loadSliceMarkers ? 4 : 0) | (s.useLoopPoints ? 8 : 0)
           | (s.keepOnDisk ? 0x100 : 0);
}

inline void setContentFlags (SamplerSettings& s, int flags) noexcept
{
    s.resampleOnLoad = (flags & 1) != 0;
    s.loadRegions = (flags & 2) != 0;
    s.loadSliceMarkers = (flags & 4) != 0;
    s.useLoopPoints = (flags & 8) != 0;
    s.keepOnDisk = (flags & 0x100) != 0;
}

// FL greys "Keep on disk" while a loop plays (FL's FUN_00f5d2c0: Use loop points on and a loop in the sample): a
// streamed sample can't loop
inline bool keepOnDiskAvailable (const SamplerSettings& s, bool sampleHasLoop) noexcept
{
    return ! (s.useLoopPoints && (sampleHasLoop || s.hasLoopOverride()));
}
} // namespace lynx::samplerpage
