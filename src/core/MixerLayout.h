#pragma once

// FL Studio's "Mixer layout" choices (issue #52, REA round 168h): the drop-down in the Mixer's title bar
// (TFXForm.FXViewModeSelect, hint "Mixer layout", handler FXViewModeSelectChange 0x128e190). Its items are
// Compact, Compact 2, Compact alt | Wide, Wide 2, Wide 3, Wide alt | Extra large; the chosen index (0..7, separators
// not counted) is a global FL keeps per user (registry General\FruityLoopsMainForm "FXViewMode", FUN_0119fad0), not in
// the project. Its default is 3 = Wide (the global's initial value). LYNX keeps it in ui-options.txt ("mixerLayout").
//
// The strip width FL lays out (FUN_01280570, `s` = the UI scale, Round = Delphi's round half to even):
//   base  = even (Round (s * (20 + 2 * [Wide 3 or Extra large]) - 0.5) + 1 rounded down to even)
//   Compact group (0..2): width = base + 2 * [Compact 2]
//   Wide group and Extra large: width = 2 * base + 2 * Round (s * (1 + [Wide or Wide alt] + [Wide 3 or Extra large]
//                                                              + 8 * [Extra large]) - 0.5)
// At 100 %: Compact 20, Compact 2 22, Compact alt 20, Wide 44, Wide 2 42, Wide 3 48, Wide alt 44, Extra large 64 px
// (FL's Wide strips measured 44 px apart on a 100 % screen). Choosing an "alt" layout switches FL's "Names at top" off;
// the alt layouts place the track name in the lower part of the strip (FUN_01280570 modes 2 and 6). No JUCE.

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::mixerlayout
{
enum class Layout
{
    compact,    // 0
    compact2,   // 1
    compactAlt, // 2
    wide,       // 3 (FL's default)
    wide2,      // 4
    wide3,      // 5
    wideAlt,    // 6
    extraLarge  // 7
};

constexpr int layoutCount = 8;
constexpr Layout defaultLayout = Layout::wide;

constexpr std::string_view nameOf (Layout l) noexcept
{
    switch (l)
    {
        case Layout::compact: return TR_NOOP ("Compact");
        case Layout::compact2: return TR_NOOP ("Compact 2");
        case Layout::compactAlt: return TR_NOOP ("Compact alt");
        case Layout::wide: return TR_NOOP ("Wide");
        case Layout::wide2: return TR_NOOP ("Wide 2");
        case Layout::wide3: return TR_NOOP ("Wide 3");
        case Layout::wideAlt: return TR_NOOP ("Wide alt");
        case Layout::extraLarge: return TR_NOOP ("Extra large");
    }
    return TR_NOOP ("Wide");
}

constexpr bool isCompact (Layout l) noexcept { return (int) l <= 2; }
constexpr bool isAlt (Layout l) noexcept { return l == Layout::compactAlt || l == Layout::wideAlt; }
// a separator follows this item in the drop-down (FL: after Compact alt and Wide alt)
constexpr bool separatorAfter (Layout l) noexcept { return isAlt (l); }

// FL's index (ui-options.txt "mixerLayout", FL's FXViewMode): out of range -> Wide.
constexpr Layout fromIndex (int index) noexcept
{
    return index >= 0 && index < layoutCount ? (Layout) index : defaultLayout;
}

inline Layout parse (const std::string& text)
{
    if (text.empty() || text.find_first_not_of (" 0123456789") != std::string::npos)
        return defaultLayout;
    return fromIndex (std::atoi (text.c_str()));
}

// FL's strip width in pixels at UI scale `scale` (1 = 100 %), FUN_01280570.
inline int flStripWidth (Layout l, float scale = 1.0f)
{
    const auto m = (int) l;
    const auto round = [] (double v) { return (int) std::nearbyint (v); }; // half to even, as Delphi's Round
    const bool wide3OrXl = m == 5 || m == 7;
    const auto base = (round ((20 + (wide3OrXl ? 2 : 0)) * (double) scale - 0.4999999701976776) + 1) & ~1;
    if (isCompact (l))
        return base + (m == 1 ? 2 : 0);
    const auto extra = 1 + ((m == 3 || m == 6) ? 1 : 0) + (wide3OrXl ? 1 : 0) + (m == 7 ? 8 : 0);
    return base * 2 + round (extra * (double) scale - 0.4999999701976776) * 2;
}

// How LYNX draws a layout (LYNX skin on FL's widths; the elements' arrangement is LYNX's own, see
// docs/analise/REA_rodada168h.md).
struct Spec
{
    int stripWidth = 44;   // strip pitch (FL's width)
    int gap = 4;           // space between two strip plates
    int masterWidth = 72;  // the Master (it also carries LYNX's dB scale)
    bool verticalName = true;
    bool nameLow = false;  // alt layouts: the name in the lower part of the strip
    bool stereoKnob = true;
    bool stackedKnobs = true;   // pan and stereo one above the other (else side by side)
    int knobSize = 24;
    bool soloButton = true;
    bool dbReadout = true;
    bool fxBadge = true;
    int meterWidth = 6;
};

inline Spec specOf (Layout l)
{
    Spec s;
    s.stripWidth = flStripWidth (l);
    switch (l)
    {
        case Layout::compact:
        case Layout::compact2:
        case Layout::compactAlt:
            s.gap = 2;
            s.masterWidth = 48;
            s.stereoKnob = false;
            s.knobSize = l == Layout::compact2 ? 18 : 16;
            s.dbReadout = false;
            s.fxBadge = false;
            s.meterWidth = 3;
            break;
        case Layout::wide:
        case Layout::wideAlt:
        case Layout::wide2:
        case Layout::wide3:
            s.masterWidth = s.stripWidth + 28;
            s.knobSize = l == Layout::wide3 ? 26 : 24;
            s.meterWidth = l == Layout::wide2 ? 4 : 5;
            break;
        case Layout::extraLarge:
            s.masterWidth = s.stripWidth + 28;
            s.verticalName = false;
            s.stackedKnobs = false;
            s.knobSize = 26;
            break;
    }
    s.nameLow = isAlt (l);
    return s;
}
} // namespace lynx::mixerlayout
