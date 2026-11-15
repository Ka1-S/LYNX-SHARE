#pragma once

#include <cmath>
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

// Round 190he: a click inside a docked window that the UI thread's message hook never sees (a plugin view made by
// another thread, or by another process: a bridged 32-bit plugin) reaches the docked window as the system's notice to
// the clicked window's ancestors; it raises the docked window then. A window of the UI thread itself is left to the hook
// (one raise per click). 0 = no thread (the window went away meanwhile): nothing.
constexpr bool clickNeedsParentNotice (unsigned long clickedThread, unsigned long uiThread) noexcept
{
    return clickedThread != 0 && clickedThread != uiThread;
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
// Issue #178 (round 190hx): a docked window's place in its parent's physical pixels. The position and the size are
// rounded each on its own (as JUCE does for its own windows), so a window that only moves keeps its size in pixels at
// any scale. Rounding the edges instead gave a size that depends on the position at 125 % / 150 % (a 101-unit-wide
// window was 152 px at x = 0, 151 px at x = 1): every step of a drag resized the native window, and Windows showed the
// new, still undrawn Direct2D buffers for a moment (a rapid flicker of other programs' old pictures).
struct PhysicalRect
{
    int x = 0, y = 0, w = 0, h = 0;
};
inline PhysicalRect physicalPlace (int x, int y, int w, int h, double scale) noexcept
{
    const auto r = [] (double v) { return (int) std::lround (v); };
    return { r (x * scale), r (y * scale), r (w * scale), r (h * scale) };
}
} // namespace lynx::dockstack
