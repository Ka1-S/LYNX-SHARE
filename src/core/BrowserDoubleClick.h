#pragma once

// Issue #323 (round 190se): what a double click on an audio file in the Browser does.
//
// FL Studio 2026 (checked live 11-27, /mnt/e/lynx-work/issues323_190se/raw/fl4..fl6): a click previews the sample, a
// double click previews it again and adds nothing (the Channel rack keeps its channels), Enter loads it into the
// selected channel, the middle button and the menu's "Send to selected channel" too, "Open in new channel" makes a new
// one. Until 0.1.31 LYNX's double click made a new Sampler channel, so auditioning one-shots by clicking them several
// times filled the rack. That is now a LYNX extra ("browser.doubleClickAdds", ui-options.txt "browserDoubleClickAdds"),
// off by default, as it changes what FL's gesture does.
//
// Live 12 (manual "Working with the Browser"): a double click or Enter on a sample loads it into the selected track, so
// the Clip-launch layout always adds. The Simple layout follows the switch: BandLab's Sounds panel previews with a click
// and adds by dragging. No JUCE.

#include "core/LayoutStyle.h"

namespace lynx::browserclick
{
inline bool& doubleClickAdds() noexcept // the switch (Settings › LYNX extras), off by default
{
    static bool on = false;
    return on;
}

// Does a double click on an audio file in the Browser add it to a new channel in this layout?
constexpr bool addsOnDoubleClick (bool switchOn, layoutstyle::Style style) noexcept
{
    return switchOn || style == layoutstyle::Style::clipLaunch;
}
inline bool addsOnDoubleClickNow() noexcept { return addsOnDoubleClick (doubleClickAdds(), layoutstyle::current()); }
} // namespace lynx::browserclick
