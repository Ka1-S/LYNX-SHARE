#pragma once

// The Playlist picker's multi-selection and FL's multi-pattern render plan, without JUCE (tests/Gap169kTests.inc).
//
// FL evidence (docs/analise/REA_rodada169k.md §2.1–2.2):
// - PickerPanelMouseDown FUN_00e4d5d0 / PickerPanelMouseUp FUN_00e4e080 [DECOMP]: a right-button press on an entry
//   that is not selected (Ctrl up) clears the selection, then selects the entry and opens the menu (so a right-click on
//   one of several picked patterns keeps them all). A left-button release: neither Ctrl nor Shift → only that entry;
//   Ctrl → toggles it (an entry selected at the press is deselected); Shift with a selection → the range from the
//   first selected entry to the clicked one is added, in that order.
// - PSPatQuickRenderMenuClick FUN_011884b0 [DECOMP]: a Patterns-menu render opened from the picker renders every picked
//   pattern in selection order; empty ones are skipped, then "Pattern is empty, nothing to render." when only one was
//   picked, else "Empty patterns were ignored.".

#include <algorithm>
#include <vector>

namespace lynx::pickersel
{
// The picked entries (pattern indices) in the order they were picked; the first one is the Shift anchor.
struct Selection
{
    std::vector<int> items;

    bool contains (int item) const noexcept { return std::find (items.begin(), items.end(), item) != items.end(); }
    void clear() noexcept { items.clear(); }
    void add (int item)
    {
        if (! contains (item))
            items.push_back (item);
    }
    void remove (int item) { items.erase (std::remove (items.begin(), items.end(), item), items.end()); }
};

// Right button pressed on `item` (FL's mouse-down, button 1).
inline void rightDown (Selection& s, int item, bool ctrl)
{
    if (! ctrl && ! s.contains (item))
        s.clear();
    s.add (item);
}

// Left button pressed on `item`: whether a Ctrl+click release must deselect it (FL's +0x6f6 flag).
inline bool leftDownDeselects (const Selection& s, int item, bool ctrl) noexcept
{
    return ctrl && s.contains (item);
}

// Left button released on `item` (FL's mouse-up, button 0). `deselect` = leftDownDeselects at the press.
inline void leftUp (Selection& s, int item, bool ctrl, bool shift, bool deselect)
{
    if (! ctrl && ! shift)
        s.clear();
    if (ctrl && deselect)
    {
        s.remove (item);
        return;
    }
    if (shift && ! s.items.empty())
    {
        const auto anchor = s.items.front();
        const auto step = item < anchor ? -1 : 1;
        for (auto i = anchor; i != item; i += step)
            s.add (i);
    }
    s.add (item);
}

// Drops entries that no longer exist (patterns deleted since).
inline void clamp (Selection& s, int count)
{
    s.items.erase (std::remove_if (s.items.begin(), s.items.end(), [count] (int i) { return i < 0 || i >= count; }), s.items.end());
}

// What a render of the picked patterns does: the ones to render (selection order) and whether some were skipped.
struct RenderPlan
{
    std::vector<int> render;
    int skipped = 0;
    int picked = 0;
};

template <typename Renderable>
RenderPlan planRender (const std::vector<int>& picked, Renderable&& renderable)
{
    RenderPlan plan;
    plan.picked = (int) picked.size();
    for (const auto p : picked)
    {
        if (renderable (p))
            plan.render.push_back (p);
        else
            ++plan.skipped;
    }
    return plan;
}

// FL's warning after the loop: none, or which text (1 = "Pattern is empty, nothing to render.", 2 = "Empty patterns
// were ignored.").
inline int skippedWarning (const RenderPlan& plan) noexcept
{
    if (plan.skipped == 0)
        return 0;
    return plan.picked == 1 ? 1 : 2;
}
} // namespace lynx::pickersel
