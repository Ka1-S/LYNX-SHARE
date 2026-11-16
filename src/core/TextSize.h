#pragma once

// Settings › General › Text size (issue #189, round 190ir): LYNX's text roles (ui/Theme.h theme::type) scaled per role.
// JUCE-free, so the rules are tested in the core tests.
//
// Every role takes the chosen percentage, except the two that sit in boxes of a fixed size that a larger text would
// overflow: the transport position (`display`, in FL's time box) and the small grid numbers (`caption`, in rows of FL's
// height). They grow up to 125 % and stay there. The other roles' boxes already fit a font to their height (buttons,
// combo boxes) or are laid out from the text (labels, menus), so they go the whole way. This is a per-role choice, not
// one global zoom (design190ia PLAN §2 "Type ramp"): the UI size setting stays the way to make everything larger.

#include <algorithm>
#include <array>
#include <string>

namespace lynx::textsize
{
enum class Role : int { caption = 0, small = 1, body = 2, title = 3, display = 4 };

inline constexpr int minimum = 90, maximum = 150, standard = 100;
inline constexpr std::array<int, 5> choices { 90, 100, 110, 125, 150 };
inline constexpr int fixedBoxLimit = 125; // caption and display stop here

inline int clamp (int percent) noexcept { return std::clamp (percent, minimum, maximum); }

// the percentage `role` is drawn at when the setting is `percent`
inline int forRole (int percent, Role role) noexcept
{
    const auto p = clamp (percent);
    if ((role == Role::caption || role == Role::display) && p > fixedBoxLimit)
        return fixedBoxLimit;
    return p;
}

inline float scaled (float base, int percent, Role role) noexcept { return base * (float) forRole (percent, role) / 100.0f; }

// The saved text ("125"); anything else (an empty or broken file) is the standard size.
inline int parse (const std::string& text) noexcept
{
    int value = 0;
    bool digits = false;
    for (const auto ch : text)
    {
        if (ch >= '0' && ch <= '9')
        {
            value = value * 10 + (ch - '0');
            digits = true;
            if (value > 1000)
                return standard;
        }
        else if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '%')
            continue;
        else
            return standard;
    }
    return digits ? clamp (value) : standard;
}
} // namespace lynx::textsize
