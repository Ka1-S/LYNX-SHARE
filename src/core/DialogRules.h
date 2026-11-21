#pragma once

// LYNX's one dialog system, the rules without JUCE (round 190oo, design review 2: WG-6 and WG-22), used by
// ui/DialogFrame.h and tested in tests/DialogRules190ooTests.inc.

#include <algorithm>
#include <cmath>
#include <vector>

namespace lynx::dialogrules
{
// WG-6: the order of a dialog's buttons, left to right, as indices of the order they were added in. `primary` is the
// Return button (-1: none), `cancel` the Escape one (-1: none). LYNX's order: the other buttons as added, then Cancel,
// the primary at the far right. `asAdded` keeps the order they were added in (FL's message windows: Yes | No).
inline std::vector<int> buttonOrder (int count, int primary, int cancel, bool asAdded)
{
    std::vector<int> order;
    for (int i = 0; i < count; ++i)
        if (asAdded || (i != primary && i != cancel))
            order.push_back (i);
    if (! asAdded)
    {
        if (cancel >= 0 && cancel < count && cancel != primary)
            order.push_back (cancel);
        if (primary >= 0 && primary < count)
            order.push_back (primary);
    }
    return order;
}

// WG-6: which button is the dialog's one accent button (its index, -1: none): the Return one (`returnIndex`); a box
// without one (JUCE's three-button boxes, the plugin crash prompt) has its first button as the default, the one the
// keyboard focus and so Return go to, unless that first one is the Escape button (the record question's lone Cancel).
inline int primaryButton (int count, int returnIndex, int escapeIndex)
{
    if (returnIndex >= 0 && returnIndex < count)
        return returnIndex;
    return count >= 2 && escapeIndex != 0 ? 0 : -1;
}

// WG-22: the device rows a horizontal band `height` logical px tall takes at `scale` (a whole number, so it lands on
// whole physical pixels): a hairline (1 px or less) one row per whole scale step (1 @150, 2 @200), a thicker band its
// height rounded (the 3 px strip: 5 rows @150, 6 @200), never less than one row.
inline float bandRows (float height, float scale)
{
    return height <= 1.0f ? std::max (1.0f, std::floor (scale)) : std::max (1.0f, std::round (height * scale));
}
} // namespace lynx::dialogrules
