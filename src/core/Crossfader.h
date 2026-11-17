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
// Live 12.4's own renders (see the table's comment). Pure, no allocation: the audio thread calls gain().

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

// The gain of curve `curve` at fraction t (0..1); 1 = unattenuated.
inline float curveGain (int curve, float t) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    constexpr float halfPi = 1.57079632679f;
    switch (curve)
    {
        case 0: return t; // Dipped
        case 1: return std::pow (t, 0.7f); // Intermediate
        case 3: return t >= 0.5f ? 1.0f : std::sin (halfPi * t * 2.0f); // Slow Fade
        case 4: return t >= 0.25f ? 1.0f : std::sin (halfPi * t * 4.0f); // Slow Cut
        case 5: return t >= 0.03f ? 1.0f : t / 0.03f; // Fast Cut
        case 6: return t >= 0.5f ? 1.0f : t * t * 4.0f; // Transition
        default: return std::sin (halfPi * t); // Constant Power
    }
}

// The gain the crossfader gives an insert with assignment `side` at `position` (-1..1).
inline float gain (int curve, CrossfadeAssign side, float position) noexcept
{
    if (side == CrossfadeAssign::none)
        return 1.0f;
    return curveGain (curve, fractionFor (side, position));
}

// Live's three key-mappable positions (Crossfader Left / Center / Right): a key mapped to one of them flips the
// crossfader between that position's two ends [Live manual: "A key mapped to any one of the three assignable crossfader
// positions (left, center or right) will toggle the crossfader's absolute left and right positions."]
enum class Spot : std::uint8_t { left, centre, right };
inline float spotPosition (Spot s) noexcept { return s == Spot::left ? -1.0f : s == Spot::right ? 1.0f : 0.0f; }

// The automation / MIDI value of a position (AutomationTargetKind::crossfader: 0..1) and back.
inline float valueOf (float position) noexcept { return std::clamp ((position + 1.0f) * 0.5f, 0.0f, 1.0f); }
inline float positionOf (float value) noexcept { return std::clamp (value * 2.0f - 1.0f, -1.0f, 1.0f); }
} // namespace lynx::crossfader
