#pragma once

// The Simple top bar's tuner (round 190ke, owner decision D5 of coverage190jw). GarageBand (Apple's GarageBand User Guide
// for Mac, "Use the Tuner"): select an audio track, click the Tuner button in the control bar; the Tuner shows the
// Keynote (the nearest note, with its sharp), the Graphic Tuning display (a needle on an arc from −50 to +50 cents,
// centred = in tune, left = flat, right = sharp), the Tune Deviation in cents and the Reference Tuning (440.0 Hz by
// default, 410–470 Hz in 0.1 Hz steps, dragged); orange while flat or sharp, green in tune. BandLab's tuner (Guitar and
// Bass tracks) shows the same line that reaches the centre when the string is in tune.
//
// LYNX: the pitch comes from the FL Tuner's detector (core/FlExtraFx.h fx::tuner::Analyzer: YIN on the full-rate
// signal, F1…C10, with FL's Bass range down to F0 for a Bass track), fed with the selected track's audio input by the
// engine's tuner tap only while the tuner shows (AudioEngine::setTunerInput). The display rules are here. No JUCE.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "core/LiveInput.h"
#include "core/Song.h"

namespace lynx::simpletuner
{
inline constexpr double defaultReference = 440.0, minReference = 410.0, maxReference = 470.0, referenceStep = 0.1;
inline constexpr double inTuneCents = 3.0; // the needle's green zone (GarageBand: green at 0 cents; ±3 is the ear's limit)

inline double clampReference (double hz) noexcept
{
    if (! std::isfinite (hz))
        return defaultReference;
    return std::round (std::clamp (hz, minReference, maxReference) / referenceStep) * referenceStep;
}

// "440.0 Hz"
inline std::string referenceText (double hz)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%.1f Hz", clampReference (hz));
    return buf;
}

inline bool inTune (double cents) noexcept { return std::abs (cents) <= inTuneCents; }

// The Keynote: the letter and whether it carries a sharp ("D#" -> {"D", true})
inline std::pair<std::string, bool> keynote (int midiNote)
{
    static const char* const letters[] = { "C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B" };
    static const bool sharps[] = { false, true, false, true, false, false, true, false, true, false, true, false };
    const auto pc = ((midiNote % 12) + 12) % 12;
    return { letters[pc], sharps[pc] };
}

// The Tune Deviation: "+20 cents", "−7 cents", "0 cents" (whole cents; the minus is U+2212)
inline std::string centsText (double cents)
{
    const auto c = (int) std::lround (std::clamp (cents, -50.0, 50.0));
    if (c == 0)
        return "0 cents";
    return (c > 0 ? "+" : "\xe2\x88\x92") + std::to_string (std::abs (c)) + " cents";
}

// The needle's angle in radians from straight up: −50 cents = −maxAngle, +50 = +maxAngle (GarageBand's arc spans about
// ±60 degrees)
inline constexpr double maxAngle = 1.0472;
inline double needleAngle (double cents) noexcept { return std::clamp (cents, -50.0, 50.0) / 50.0 * maxAngle; }

// The device input channels the tuner listens to for a track's insert: the insert's own input; without one the device's
// first input (mono). {first, count}; count 0 = the device has no input.
inline std::pair<int, int> inputFor (const Song& song, int insert, int deviceChannels) noexcept
{
    if (deviceChannels <= 0)
        return { 0, 0 };
    if (insert > 0 && insert < (int) song.mixer.size())
        if (const auto [first, count] = inputChannelsOf (song.mixer[(std::size_t) insert], deviceChannels); count > 0)
            return { first, count };
    return { 0, 1 };
}
} // namespace lynx::simpletuner
