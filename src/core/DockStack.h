#pragma once

#include <cstddef>
#include <vector>

// Round 190ev (issue #152, 0.1.26: "selecting the window behind does not bring it over the one in front; the Channel
// rack almost never comes to the front"): the stacking rules of the windows docked in LYNX's main window (FL's docked
// plugin windows, issue #116), kept apart from the native window code so they can be tested anywhere.
namespace lynx::dockstack
{
// The docked window a mouse click belongs to. `ancestry` is the clicked native window and its parents, innermost first
// (a click in a plugin's own view lands in a window of the plugin, several levels under the docked frame); `isDocked`
// tells which of them is a docked window that comes in front when clicked. -1: none (a click elsewhere).
template <typename Window, typename IsDocked>
int clickedWindow (const std::vector<Window>& ancestry, IsDocked isDocked)
{
    for (std::size_t i = 0; i < ancestry.size(); ++i)
        if (isDocked (ancestry[i]))
            return (int) i;
    return -1;
}

// Whether `target` has to be raised: a sibling above it counts unless `staysAbove` says it is one that is always in front
// (a hidden window, the dancers that "Keep in front", the setup wizard). `bottomUp`: the siblings, from the bottom of
// the stacking order up. A window that is not among them is not raised.
template <typename Window, typename StaysAbove>
bool needsRaise (const std::vector<Window>& bottomUp, const Window& target, StaysAbove staysAbove)
{
    bool found = false;
    for (const auto& w : bottomUp)
    {
        if (! found)
        {
            found = w == target;
            continue;
        }
        if (! staysAbove (w))
            return true;
    }
    return false;
}

// The order JUCE's list of desktop windows must have after the main window (a drop goes to the last window under the
// mouse in that list): the main window's docked children in their stacking order, then the windows kept above it, in
// their screen order. `listIndexOf` is a window's place in JUCE's list (-1 = not there). True when the list already
// has that order, so nothing has to be touched.
template <typename Window, typename IndexOf>
bool listMatches (const Window& main, const std::vector<Window>& wanted, IndexOf listIndexOf)
{
    auto last = listIndexOf (main);
    for (const auto& w : wanted)
    {
        const auto at = listIndexOf (w);
        if (at <= last)
            return false;
        last = at;
    }
    return true;
}

// The open / close fade of a floating panel (Settings › General › Animations, round 179a). A panel that is a child
// window of the main window could only fade by its window's alpha (round 190i), and on Windows that switches the
// window between two kinds of drawing surface at the start and the end of every fade (an opaque one and a layered
// one, each made new and empty): the panel flashed when it opened and when it closed. FL's docked windows don't fade.
// So a panel fades only while it is a plain component (docking off); a docked one shows and hides at once.
constexpr bool panelFades (bool animationsOn, bool panelIsChildWindow, bool windowsAlphaFlashes) noexcept
{
    return animationsOn && ! (panelIsChildWindow && windowsAlphaFlashes);
}
} // namespace lynx::dockstack
