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
#include <cctype>
#include <cstdlib>
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

// --- round 171c (discussion #10): FL's category tabs, search and keyboard -----------------------------------------

// The tabs of one side in the picker's order: `first` (LYNX's own plugins) at the start, `last` (installed plugins in
// no folder) at the end, the rest A-Z ignoring case as FL lists its Plugin database folders. Duplicates dropped.
inline std::vector<std::string> orderTabs (std::vector<std::string> names, const std::string& first, const std::string& last)
{
    auto lower = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    };
    auto rank = [&] (const std::string& n) { return n == first ? 0 : n == last ? 2 : 1; };
    std::sort (names.begin(), names.end(), [&] (const std::string& a, const std::string& b) {
        if (rank (a) != rank (b))
            return rank (a) < rank (b);
        return lower (a) < lower (b);
    });
    names.erase (std::unique (names.begin(), names.end()), names.end());
    return names;
}

// The search box: every word of `query` (spaces between words) appears in one of `fields`, ignoring case. An empty
// query matches everything.
inline bool matches (const std::string& query, const std::vector<std::string>& fields)
{
    auto lower = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    };
    std::vector<std::string> haystack;
    for (const auto& f : fields)
        haystack.push_back (lower (f));
    std::string word;
    auto found = [&] {
        if (word.empty())
            return true;
        for (const auto& h : haystack)
            if (h.find (word) != std::string::npos)
                return true;
        return false;
    };
    for (const auto c : lower (query) + " ")
    {
        if (c == ' ')
        {
            if (! found())
                return false;
            word.clear();
        }
        else
            word += c;
    }
    return true;
}

// Keyboard navigation over the tiles shown (their boxes; an empty box = not shown): Left / Right step in reading
// order (top to bottom, left to right), Up / Down go to the row above / below, to the tile whose centre is nearest.
// `at` < 0: the first tile. Returns the new index (`at` when there is nowhere to go).
struct Box
{
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const noexcept { return w <= 0 || h <= 0; }
};

inline int moveSelection (const std::vector<Box>& boxes, int at, int dx, int dy)
{
    std::vector<int> order;
    for (int i = 0; i < (int) boxes.size(); ++i)
        if (! boxes[(std::size_t) i].empty())
            order.push_back (i);
    if (order.empty())
        return at;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) {
        const auto& p = boxes[(std::size_t) a];
        const auto& q = boxes[(std::size_t) b];
        return p.y != q.y ? p.y < q.y : p.x < q.x;
    });
    const auto pos = std::find (order.begin(), order.end(), at);
    if (at < 0 || pos == order.end())
        return order.front();
    const auto k = (int) (pos - order.begin());
    if (dx != 0)
        return order[(std::size_t) std::clamp (k + (dx > 0 ? 1 : -1), 0, (int) order.size() - 1)];
    if (dy == 0)
        return at;
    const auto& from = boxes[(std::size_t) at];
    const auto cx = from.x + from.w / 2;
    int best = at, bestRow = 0, bestDistance = 0;
    for (const auto i : order)
    {
        const auto& b = boxes[(std::size_t) i];
        const auto rowDistance = dy > 0 ? b.y - from.y : from.y - b.y;
        if (rowDistance <= from.h / 2) // (same row, or the wrong way)
            continue;
        const auto distance = std::abs (b.x + b.w / 2 - cx);
        if (best == at || rowDistance < bestRow || (rowDistance == bestRow && distance < bestDistance))
            best = i, bestRow = rowDistance, bestDistance = distance;
    }
    return best;
}
} // namespace lynx::pickerview
