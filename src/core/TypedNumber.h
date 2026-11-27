#pragma once

// Round 190rz (0.1.32 Q1 part C): what a typed value box (a Settings slider's number: Project › Tempo, Beats per bar,
// Swing, Pitch) reads. A number up to the first character that can't be part of one ("98.5 BPM", "120,5" as 120.5);
// nothing when no digit was typed — the box then keeps its value (FL leaves a value alone on text it can't read; JUCE's
// own reading made "abc" 0, so the tempo went to its lowest, 10 BPM). Without JUCE (tests/Q1c190rzTests.inc).

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace lynx
{
inline std::optional<double> typedNumber (const std::string& typed)
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
    return number;
}
} // namespace lynx
