#pragma once

// Round 190rx (0.1.32 Q1): what a "Type in value..." box of an automation point means. A number, read up to the first
// character that can't be part of one ("25 %", "120,5" as 120.5), as a percent (0..100) or, for a tempo clip, a BPM; the
// point's 0..1 value, clamped. Nothing when no number was typed ("abc" used to set the point to 0 %, a tempo to its
// lowest): the dialog then leaves the point as it was, as the knobs' own "Type in value..." does.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "core/Song.h"

namespace lynx
{
inline std::optional<float> typedAutomationValue (const std::string& typed, bool tempo)
{
    std::string digits;
    auto i = typed.find_first_not_of (" \t\r\n");
    for (; i != std::string::npos && i < typed.size(); ++i)
    {
        const auto c = typed[i];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == ',')
            digits += c == ',' ? '.' : c;
        else
            break;
    }
    if (digits.find_first_of ("0123456789") == std::string::npos)
        return std::nullopt;
    const auto number = std::strtod (digits.c_str(), nullptr);
    if (! std::isfinite (number))
        return std::nullopt;
    const auto v = tempo ? valueFromTempo (number) : (float) number / 100.0f;
    return std::clamp (v, 0.0f, 1.0f);
}
} // namespace lynx
