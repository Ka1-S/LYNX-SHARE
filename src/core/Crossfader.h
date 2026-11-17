#pragma once

// Live's crossfader (round 190kc, the Clip-launch layout). What Live 12's manual says (chapter "Mixing", 18.5 Using Live's
// Crossfader; docs/analise/REA_rodada190kc.md has the evidence):
//   - each track has two Crossfade Assign buttons, A and B; with neither on the crossfader does not touch the track;
//   - the crossfader "merely influences the signal volume at each track's gain stage" (a VCA: routing is unchanged);
//   - seven curves, chosen in the crossfader's context menu: Dipped, Intermediate, Constant Power, Slow Fade, Slow Cut,
//     Fast Cut, Transition (the menu's order = the set's CrossfadeCurve 0..6; Live's default set: 2);
//   - the crossfader can be MIDI-mapped; its left, centre and right positions are separately mappable to keys / notes.
// The set stores the position in MainTrack/DeviceChain/Mixer/CrossFade (-1..1), each track's assignment in
// Mixer/CrossFadeState (0 A, 1 none, 2 B) and the curve in LiveSet/CrossfadeCurve [DATA: every Live 12 set].
//
// The gains: a track assigned to A hears the crossfader through `fraction` t = (1 - x) / 2 of its own side (1 at the far
// left, 0 at the far right), one on B through t = (1 + x) / 2; each curve maps t to a gain. The curves were measured from
// Live 12.4's own renders (core/CrossfaderCurves.inc; at the centre: Dipped 0.500, Intermediate 0.582, Constant Power 0.707,
// Slow Fade / Slow Cut / Fast Cut / Transition 1.0). Pure, no allocation: the audio thread calls gain().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "core/Song.h"

namespace lynx::crossfader
{
constexpr int curveCount = 7;
constexpr int defaultCurve = 2; // Constant Power (Live's default set)

inline const char* curveName (int curve) noexcept
{
    static constexpr const char* names[curveCount] = { "Dipped", "Intermediate", "Constant Power", "Slow Fade", "Slow Cut", "Fast Cut", "Transition" };
    return curve >= 0 && curve < curveCount ? names[curve] : names[defaultCurve];
}

// The part of the crossfader's travel a track hears: 1 = all of it (its own side), 0 = none (the other side).
inline float fractionFor (CrossfadeAssign side, float position) noexcept
{
    position = std::clamp (position, -1.0f, 1.0f);
    return side == CrossfadeAssign::a ? (1.0f - position) * 0.5f : (1.0f + position) * 0.5f;
}

#include "core/CrossfaderCurves.inc"

// The gain of curve `curve` at fraction t (0..1); 1 = unattenuated: Live's measured table, linear between its 257 points.
inline float curveGain (int curve, float t) noexcept
{
    if (curve < 0 || curve >= curveCount)
        curve = defaultCurve;
    t = std::clamp (t, 0.0f, 1.0f) * (float) (crossfaderTableSize - 1);
    const auto i = std::min ((int) t, crossfaderTableSize - 2);
    const auto f = t - (float) i;
    const auto* row = crossfaderTable[curve];
    return row[i] + (row[i + 1] - row[i]) * f;
}

// The gain the crossfader gives an insert with assignment `side` at `position` (-1..1).
inline float gain (int curve, CrossfadeAssign side, float position) noexcept
{
    if (side == CrossfadeAssign::none)
        return 1.0f;
    return curveGain (curve, fractionFor (side, position));
}

// Live's three key-mappable positions (Crossfader Left / Center / Right). Measured in Live 12.4 (round 190kc): a press moves
// the crossfader to its position (again: it stays); holding one and pressing / releasing another snaps back to the held
// one on the release ("snapping back", Live manual 18.5).
enum class Spot : std::uint8_t { left, centre, right };
inline float spotPosition (Spot s) noexcept { return s == Spot::left ? -1.0f : s == Spot::right ? 1.0f : 0.0f; }

// The automation / MIDI value of a position (AutomationTargetKind::crossfader: 0..1) and back.
inline float valueOf (float position) noexcept { return std::clamp ((position + 1.0f) * 0.5f, 0.0f, 1.0f); }
inline float positionOf (float value) noexcept { return std::clamp (value * 2.0f - 1.0f, -1.0f, 1.0f); }
} // namespace lynx::crossfader
