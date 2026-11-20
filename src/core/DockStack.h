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

// Issue #197 (round 190kl): a docked window that already has a size in pixels within a pixel of its exact size (w and h
// times the scale: a size some other rounding gave it, e.g. JUCE's when the window was made) keeps it when it is placed
// again. Without this, the first move of a freshly opened editor at 150 % still resized it once (Gross Beat: 1570 px made,
// 1571 px placed) and Windows showed the window's new, undrawn buffers: a white flash with only its live display drawn.
inline bool keepsSize (int nativeW, int nativeH, int w, int h, double scale) noexcept
{
    return std::abs ((double) nativeW - w * scale) < 1.0 && std::abs ((double) nativeH - h * scale) < 1.0;
}

// Round 190jy (queue item 44, Settings › General › "Draw docked LYNX editors in the main window"): a docked LYNX editor
// can be drawn by the main window itself (its own components, its own frames) instead of a native child window of its
// own: a live display then costs its paint and no second window presenting frames (Gross Beat: +8 points of process CPU
// while playing). The main window's drawing is under all its native child windows (the panels, docked plugin windows),
// so such an editor keeps its place in their stacking order through a hidden native "anchor" window, and every native
// sibling BELOW that anchor gets a hole (its window region) where the editor is: the editor shows through it, clicks
// and drops there reach the main window (the system's hit test follows the region), and a sibling raised above the
// anchor loses the hole and covers the editor as a native window would.
//
// `bottomUp`: the main window's native children from the bottom of the stacking order up; `anchorOf (w)`: the in-main-
// window editor that `w` is the anchor of (-1: an ordinary native window); `shown (editor)`: that editor is visible.
// Result, one entry per window of `bottomUp`: the editors above it (their indices, the nearest first); an anchor gets
// none (it is never shown). The caller turns them into holes where the rectangles meet.
template <typename Window, typename AnchorOf, typename Shown>
std::vector<std::vector<int>> editorsAbove (const std::vector<Window>& bottomUp, AnchorOf anchorOf, Shown shown)
{
    std::vector<std::vector<int>> out (bottomUp.size());
    std::vector<int> above; // (top-down, while walking down)
    for (std::size_t k = bottomUp.size(); k-- > 0;)
    {
        const int editor = anchorOf (bottomUp[k]);
        if (editor >= 0)
        {
            if (shown (editor))
                above.insert (above.begin(), editor); // (nearest first)
            continue;
        }
        out[k] = above;
    }
    return out;
}

// A rectangle in physical pixels (x, y, w, h); the holes of one window are the editors' rectangles that meet it, in its
// own coordinates (its top-left at 0, 0), clipped to it. An editor's rectangle is rounded edge by edge, as the main
// window's Direct2D clip of that component is (an aliased axis-aligned clip: each edge to the nearest pixel boundary).
inline PhysicalRect editorRect (double x, double y, double w, double h, double scale) noexcept
{
    const auto r = [] (double v) { return (int) std::lround (v); };
    const auto left = r (x * scale), top = r (y * scale);
    return { left, top, r ((x + w) * scale) - left, r ((y + h) * scale) - top };
}

inline std::vector<PhysicalRect> holesIn (const PhysicalRect& window, const std::vector<PhysicalRect>& editors)
{
    std::vector<PhysicalRect> out;
    for (const auto& e : editors)
    {
        const auto l = e.x > window.x ? e.x : window.x, t = e.y > window.y ? e.y : window.y;
        const auto rr = e.x + e.w < window.x + window.w ? e.x + e.w : window.x + window.w;
        const auto b = e.y + e.h < window.y + window.h ? e.y + e.h : window.y + window.h;
        if (rr > l && b > t)
            out.push_back ({ l - window.x, t - window.y, rr - l, b - t });
    }
    return out;
}
} // namespace lynx::dockstack
