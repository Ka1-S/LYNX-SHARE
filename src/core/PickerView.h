#pragma once

// FL Studio's Plugin picker view (View › Plugin picker, F8), without JUCE (tests/Gap169kPickerTests.inc).
//
// FL evidence (docs/analise/REA_rodada169k.md §2.5, one FL session, shots C:\Users\kauan\lynx-test\r169k\p1–p6.png):
// the picker covers the main window with a dark veil; a strip of tabs along the bottom holds the generator categories,
// a large "All" in the middle, then the effect categories. "All" shows two halves, "Generators" on the left and
// "Effects" on the right, every plugin as a tile; a category tab shows only its plugins, centred. The mouse wheel
// steps one tab per notch (down = the next tab to the right: All → Controller → Delay reverb → Distortion after 3
// notches), pointing at a tab shows it, the shown tab is white and bold with a small mark above it; "..." at both ends
// when the strip is wider than the window. F8 again closes it.

#include <algorithm>
#include <string>
#include <vector>

namespace lynx::pickerview
{
// The tab strip: generator categories, "All", effect categories (each list in the picker's order, empty ones left out
// by the caller).
struct Tabs
{
    std::vector<std::string> generators, effects;

    int count() const noexcept { return (int) (generators.size() + effects.size()) + 1; }
    int all() const noexcept { return (int) generators.size(); }
    bool isGenerator (int tab) const noexcept { return tab >= 0 && tab < all(); }
    bool isEffect (int tab) const noexcept { return tab > all() && tab < count(); }
    const std::string& name (int tab) const
    {
        static const std::string allName = "All";
        if (isGenerator (tab))
            return generators[(std::size_t) tab];
        if (isEffect (tab))
            return effects[(std::size_t) (tab - all() - 1)];
        return allName;
    }
};

// The wheel: one tab per notch, down (negative delta) to the right; it stops at both ends.
inline int stepTab (const Tabs& tabs, int tab, int notchesDown) noexcept
{
    return std::clamp (tab + notchesDown, 0, tabs.count() - 1);
}

// Tiles flowing in rows inside a box: the largest tile width (from `maxWidth` down to `minWidth`, `aspect` = height /
// width) at which `n` tiles fit in `width` × `height` with `gap` between them. Returns {tileWidth, columns}; when even
// the smallest size does not fit, the smallest size (the caller scrolls).
struct Flow
{
    int tileWidth = 0, tileHeight = 0, columns = 1, rows = 0;
};

inline Flow fitTiles (int n, int width, int height, int gap, int minWidth, int maxWidth, double aspect)
{
    Flow f;
    if (n <= 0 || width <= 0 || height <= 0)
        return f;
    for (int w = std::max (minWidth, maxWidth); w >= minWidth; w -= 4)
    {
        const auto h = std::max (1, (int) (w * aspect));
        const auto columns = std::max (1, (width + gap) / (w + gap));
        const auto rows = (n + columns - 1) / columns;
        f = { w, h, columns, rows };
        if (rows * h + (rows - 1) * gap <= height)
            return f;
    }
    return f;
}
} // namespace lynx::pickerview
