#pragma once

// FL's middle ellipsis (round 190io, design190ia FM-14): a name too long for its box keeps its start and its end with
// ".." between them ("Titey..m #2", "WA_..6BPM", "CUTLAY..idated)"), so the numbers, BPM and "#2" at the end stay
// readable. Measured in FL Studio 2026 (fl-channelrack.png, fl-mixer.png): the two parts take about half of the box
// each, by width, not by letters. No JUCE: the caller measures (ui/Theme.cpp fitMiddle).
// Rollup 0.1.30: the one rule for every name (Channel rack, Mixer, the Playlist's clip titles and track headers). Round
// 190ig's Playlist kept the end first at 40 % of the room, so the start took up to 60 %; FL's Playlist shows the same
// split as its rack (design190ia shots/ref fl-channelrack.png, the Playlist beside it: "VOC..CHED" keeps more of the end
// than of the start, as "Titey..m #2", "Grv C..sh 01", "GROSS..w Bass" in the rack), which 40 % / 60 % cannot give.

#include <algorithm>
#include <utility>

namespace lynx::middleellipsis
{
// How many characters of an n-character text to keep at its start and at its end so that start + ".." + end fits in
// `width`, where `dots` is the width of "..". widthOf (first, count) measures `count` characters from `first`.
// The start gets up to half of the room left after the dots, the end the rest. {n, 0} when the whole text fits;
// {0, 0} when not even one character on each side fits.
template <typename WidthOf>
std::pair<int, int> split (int n, float width, float dots, WidthOf&& widthOf)
{
    if (n <= 0 || widthOf (0, n) <= width)
        return { std::max (0, n), 0 };
    const auto room = width - dots;
    if (room <= 0.0f)
        return { 0, 0 };
    // the start: the longest prefix within half the room (always keeps one character for the end)
    auto head = 0;
    while (head + 1 < n - 1 && widthOf (0, head + 1) <= room * 0.5f)
        ++head;
    if (head == 0)
        return { 0, 0 };
    const auto left = room - widthOf (0, head);
    auto tail = 0;
    while (tail + 1 <= n - head - 1 && widthOf (n - tail - 1, tail + 1) <= left)
        ++tail;
    if (tail == 0)
        return { 0, 0 };
    return { head, tail };
}
} // namespace lynx::middleellipsis
