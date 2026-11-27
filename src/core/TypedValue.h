#pragma once

// Round 190rx (0.1.32 Q1): what a "Type in value..." box of an automation point means. A number, read up to the first
// character that can't be part of one ("25 %", "120,5" as 120.5), as a percent (0..100) or, for a tempo clip, a BPM; the
// point's 0..1 value, clamped. Nothing when no number was typed ("abc" used to set the point to 0 %, a tempo to its
// lowest): the dialog then leaves the point as it was, as the knobs' own "Type in value..." does.

#include <cmath>
#include <optional>

#include <juce_core/juce_core.h>

#include "core/Song.h"

namespace lynx
{
inline std::optional<float> typedAutomationValue (const juce::String& typed, bool tempo)
{
    const auto t = typed.trim().replaceCharacter (',', '.');
    const auto digits = t.initialSectionContainingOnly ("+-0123456789.");
    if (digits.isEmpty() || digits.containsOnly ("+-."))
        return std::nullopt;
    const auto number = digits.getDoubleValue();
    if (! std::isfinite (number))
        return std::nullopt;
    return juce::jlimit (0.0f, 1.0f, tempo ? valueFromTempo (number) : (float) number / 100.0f);
}
} // namespace lynx
