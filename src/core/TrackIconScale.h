#pragma once

// Issue #222 (round 190ng): the Playlist's track icons grow with the track's height, as FL Studio's do. FL 2026 (v26.1.7,
// captured 2026-11-19, "Ookay - Thief", Alt+wheel vertical zoom): the small icon by the track's switch stays, and from a
// tall row on FL also draws the icon big in the middle of the header, a watermark in the header's own colour, darker:
//   row 56 / 68 / 80 px: no big icon;   row 92 / 104 / 116 px: a centred icon 41 / 48 / 55 px wide (half the row: a
//   square box of h / 2), colour = the plate's RGB x 0.85 (212,169,21 -> 180,144,18).
// The threshold lies between 80 and 92 px (not decoded); LYNX takes 86 px at 100 % (logical pixels, so @150 the same).
// No JUCE except the colour helper's type (header-only, tests/Issues190ngTests.inc).

#include <algorithm>
#include <cstdint>
#include <utility>

namespace lynx::trackiconscale
{
inline constexpr int minRowForBigIcon = 86;

// The big icon's square side for a header row `rowHeight` px tall in a plate `plateWidth` px wide (0: none).
constexpr int bigIconSize (int rowHeight, int plateWidth) noexcept
{
    if (rowHeight < minRowForBigIcon)
        return 0;
    const auto side = std::min (rowHeight / 2, plateWidth - 16);
    return side >= 24 ? side : 0;
}

// #222 follow-up (round 190nq): the part of a header plate (x, width) the big icon is centred in: the whole plate, or with
// the LYNX extra's insert effects column (Playlist › Edit › Show insert effects on linked tracks; FL has no such column)
// the part left of it. Returns { x, width }.
constexpr std::pair<float, float> bigIconArea (float plateX, float plateWidth, float insertsColumn) noexcept
{
    const auto width = plateWidth - (insertsColumn > 0.0f ? insertsColumn : 0.0f);
    return { plateX, width > 0.0f ? width : 0.0f };
}

// FL's shade: each RGB channel x 0.85 (alpha kept).
constexpr std::uint32_t shadeArgb (std::uint32_t argb) noexcept
{
    const auto ch = [argb] (int shift) { return (std::uint32_t) (((argb >> shift) & 0xffu) * 85u + 50u) / 100u << shift; };
    return (argb & 0xff000000u) | ch (16) | ch (8) | ch (0);
}

template <typename Colour>
Colour shade (Colour c) { return Colour (shadeArgb (c.getARGB())); }
} // namespace lynx::trackiconscale
