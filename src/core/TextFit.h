#pragma once

// Round 190oo (design review 2): two small fitting rules for translated texts (no JUCE; the UI measures, these decide).
// - WG-11: the Pattern toolbar's menu titles in a long language (de, ru, fr, pt-BR, tr, nl...) set the hint column's
//   width and pushed toolbar panels out at 1920 px; before a panel drops, the titles take the condensed face, then a
//   horizontal squeeze that fits them into the room the panels leave, never below 0.9 (ui/MainComponent.cpp).
// - WG-31: a knob caption keeps >= 4 px from its neighbours, squeezed to 80 % at most; longer, it takes the 9 px caption
//   face, on two lines when it has words to break and its page gave it the room (ui/ChannelSettings.cpp, FrameKnob).

#include <algorithm>

namespace lynx::textfit
{
// The squeeze (<= 1) that fits menu titles of `textWidth` px (their padding `padding` and one px a title for the rounding
// on top, + 8 px of air after the bar) into `room - panels` px; never below `minScale`, 1 when they fit as they are.
inline double menuSqueeze (int room, int panels, int textWidth, int padding, int items, double minScale = 0.9) noexcept
{
    if (textWidth <= 0)
        return 1.0;
    const auto free = room - panels - 8 - padding - items;
    return std::clamp ((double) free / (double) textWidth, minScale, 1.0);
}

enum class CaptionFit
{
    normal,    // the caption face, squeezed to `minScale` at most
    smallFace, // the 9 px face on one line (squeezed to `minScale` at most, then "...")
    twoLines   // the 9 px face on two lines
};

// `normalWidth`: the caption's width in the caption face; `box`: the width it may take (the knob pitch less 2 px a side).
inline CaptionFit captionFit (float normalWidth, float box, bool hasSpace, bool twoLinesAllowed, float minScale = 0.8f) noexcept
{
    if (normalWidth * minScale <= box)
        return CaptionFit::normal;
    return hasSpace && twoLinesAllowed ? CaptionFit::twoLines : CaptionFit::smallFace;
}
} // namespace lynx::textfit
