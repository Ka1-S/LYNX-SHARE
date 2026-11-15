#pragma once

// Round 190fz (Discord, 2026-11-10: "make it able to pull the velocity window, it too small"): the divider between the
// Piano roll's note grid and its event editor (the velocity / event lane), as FL Studio 2026 has it. Facts from REA of
// FLEngine_x64.dll (2026-10-08 build), written up in /mnt/e/lynx-work/evpane190fz/HANDOFF.md:
//  - TEventEditForm.Splitter is a TQuickSplitter 10 px tall, cursor crVSplit, hint "Right-click / shift+enter for
//    alternate size" (form resource).
//  - The splitter keeps its place as a fraction of its travel (0 = the grid folded away, 1 = the event editor folded
//    away), so a taller or shorter window keeps the proportion (TQuickSplitter's SetMinMax re-applies the fraction).
//  - SplitterChanging (0xe42030): within 23 px (half of 46, times the UI scale) of either end the bar snaps to that end
//    and folds the pane away; otherwise each pane keeps at least 46 px.
//  - TQuickSplitter mouse (0x7f2c10 / 0x7f2cc0): left-drag moves it, a left double-click or a right-click (on release)
//    swaps the place with the "spare" one (alternate size), a middle-click goes back to the default place.
//    Shift+Enter (the Piano roll's key) and a double-click on the event target panel at the left also swap.
//  - Remembered for the Piano roll window kind, not per project: FL writes SplitterAspect (default 0.8: the event
//    editor gets the bottom fifth) and SplitterSpareAspect (default 1.0: folded away) under
//    HKCU\Software\Image-Line\FL Studio 26\Windows\<desktop>\PRForm (read on this machine: 0.8 / 1.0).
//  - While it moves, FL's hint bar shows the place as a percentage ("80%").
// Pure logic, no JUCE: the Piano roll (ui/PianoRollComponent) lays out and draws it, the tests check the rules here.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace lynx::evpane
{
constexpr int barThickness = 10;      // the divider bar's height (FL: Splitter.Height = 10)
constexpr int minPane = 46;           // the least a pane keeps while it shows (FL: round (46 * scale))
constexpr int snapZone = minPane / 2; // closer than this to an end: the pane folds away (FL: 23)
constexpr float defaultAspect = 0.8f; // FL's SplitterAspect default for the Piano roll
constexpr float defaultSpare = 1.0f;  // FL's SplitterSpareAspect default: the alternate size hides the event editor

struct State
{
    float aspect = defaultAspect; // the bar's place in its travel, 0..1
    float spare = defaultSpare;   // the alternate place (a negative value: none, the default place is used)
    bool operator== (const State& o) const noexcept { return aspect == o.aspect && spare == o.spare; }
};

inline float clampAspect (float a) noexcept { return std::isfinite (a) ? std::clamp (a, 0.0f, 1.0f) : defaultAspect; }

// The bar's top for a wanted one, with FL's SplitterChanging rule; `top` is the end where the grid is folded away and
// `bottom` the end where the event editor is (the bar's top at `bottom` = the bar lies on the window's bottom edge).
inline int clampBar (int wanted, int top, int bottom) noexcept
{
    if (bottom <= top)
        return top;
    if (wanted < top + snapZone)
        return top;
    wanted = std::max (wanted, top + minPane);
    if (wanted > bottom - snapZone)
        return bottom;
    return std::max (top, std::min (wanted, bottom - minPane));
}

// FL's TQuickSplitter: the fraction a bar top stands for (0.5 when there is no travel)
inline float aspectOf (int barTop, int top, int bottom) noexcept
{
    if (bottom <= top)
        return 0.5f;
    return clampAspect ((float) (barTop - top) / (float) (bottom - top));
}

// ...and the bar top of a fraction (rounded, then the SplitterChanging rule)
inline int barTopOf (float aspect, int top, int bottom) noexcept
{
    if (bottom <= top)
        return top;
    return clampBar (top + (int) std::lround (clampAspect (aspect) * (float) (bottom - top)), top, bottom);
}

// Right-click / double-click / Shift+Enter: swaps the place with the spare one (FL 0x7f2b90). `current` is the fraction
// of where the bar is now. Without a spare place it goes to the default place (and remembers none, as FL).
inline void toggleAlternate (State& s, float current) noexcept
{
    if (s.spare < 0.0f)
    {
        s.aspect = defaultAspect;
        return;
    }
    const auto spare = s.spare;
    s.spare = clampAspect (current);
    s.aspect = clampAspect (spare);
}

// Middle-click on the bar: the default place (FL: the splitter's default fraction); the spare place stays.
inline void resetToDefault (State& s) noexcept { s.aspect = defaultAspect; }

// The percentage FL's hint bar shows while the bar moves
inline int percentOf (float aspect) noexcept { return (int) std::lround (clampAspect (aspect) * 100.0f); }

// The settings file's text: "aspect=0.8\nspare=1\n"; unknown lines are ignored, missing values keep FL's defaults.
inline std::string toText (const State& s)
{
    // the shortest text that reads back as the same float, with a "." whatever the user's locale
    auto number = [] (float v) {
        char text[32];
        const auto end = std::to_chars (text, text + sizeof (text), v).ptr;
        return std::string (text, end);
    };
    return "aspect=" + number (clampAspect (s.aspect)) + "\nspare=" + number (s.spare < 0.0f ? -1.0f : clampAspect (s.spare)) + "\n";
}

inline State fromText (const std::string& text)
{
    State s;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto key = line.substr (0, eq);
        std::istringstream number (line.substr (eq + 1));
        number.imbue (std::locale::classic());
        float value = 0.0f;
        if (! (number >> value) || ! std::isfinite (value))
            continue;
        if (key == "aspect")
            s.aspect = clampAspect (value);
        else if (key == "spare")
            s.spare = value < 0.0f ? -1.0f : clampAspect (value);
    }
    return s;
}
} // namespace lynx::evpane
