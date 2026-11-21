#pragma once

// Round 190oo (O-5, design review CS-8's full form; LYNX extra, off by default): Live 12's three-part scale control in the
// Clip-launch control bar instead of the one key box — the scale switch ("♭#", drawn as LYNX vectors), the root chooser
// ("C ▾") and the mode chooser ("Major ▾"), the two choosers dimmed while the switch is off
// (design190ia/shots/ref/live-arrangement-audiolane.png: 17 + 44 + 66 px with 2 px gaps at Live's 100 %). It edits the
// one project key (core/ProjectKey.h): the switch on = a key is set; off = none. While it is off the choosers keep the
// key last shown (not saved, as Live keeps its root and mode while Scale Mode is off) and the switch brings it back.
// No JUCE: tested in tests/Lanes190ooTests.inc; ui/Toolbar.cpp (KeyPanel) paints it, ui/MainComponent.cpp wires it.

#include <algorithm>

namespace lynx::livescale
{
inline bool shown = false; // ui-options.txt liveScaleControl (Settings › LYNX extras, the Key menu)

inline constexpr int toggleWidth = 18, gap = 2;
inline constexpr int rootShare = 52, modeShare = 70; // (the choosers split the rest as Live's 44 : 66, rounded up)

struct Parts
{
    int toggleX = 0, toggleW = 0, rootX = 0, rootW = 0, modeX = 0, modeW = 0;
};

// The three parts in a box `width` px wide (the key box's ~145 px): the switch keeps its 18 px (less only in a very
// narrow box), the choosers share the rest.
inline Parts split (int width) noexcept
{
    Parts p;
    width = std::max (0, width);
    p.toggleW = std::min (toggleWidth, width / 4);
    const auto rest = std::max (0, width - p.toggleW - 2 * gap);
    p.rootW = rest * rootShare / (rootShare + modeShare);
    p.modeW = rest - p.rootW;
    p.rootX = p.toggleW + gap;
    p.modeX = p.rootX + p.rootW + gap;
    return p;
}

enum class Part : int { toggle = 0, root = 1, mode = 2 };

// The part under x (a gap belongs to the part on its right, so no click is lost).
inline Part partAt (int x, int width) noexcept
{
    const auto p = split (width);
    if (x < p.toggleX + p.toggleW)
        return Part::toggle;
    if (x < p.rootX + p.rootW)
        return Part::root;
    return Part::mode;
}
} // namespace lynx::livescale
