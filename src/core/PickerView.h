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

// FL's search (TPlugListForm.FormKeyPress, REA_rodada171c §2): the typed text is found anywhere in a plugin's name,
// ignoring case (ASCII); the tiles that don't match are dimmed, not removed.
inline std::string upperAscii (std::string s)
{
    for (auto& c : s)
        if (c >= 'a' && c <= 'z')
            c = (char) (c - 'a' + 'A');
    return s;
}

inline bool nameMatches (const std::string& query, const std::string& name)
{
    return query.empty() || upperAscii (name).find (upperAscii (query)) != std::string::npos;
}

// Where the pointed-at tile jumps as the text changes: the first name (in the list's order) that starts with it, else
// the last one that contains it; -1 = none.
inline int searchJump (const std::string& query, const std::vector<std::string>& names)
{
    if (query.empty())
        return -1;
    const auto q = upperAscii (query);
    int last = -1;
    for (int i = 0; i < (int) names.size(); ++i)
    {
        const auto n = upperAscii (names[(std::size_t) i]);
        if (n.rfind (q, 0) == 0)
            return i;
        if (n.find (q) != std::string::npos)
            last = i;
    }
    return last;
}

// Ctrl+Backspace: the trailing spaces, then back to the previous space (the space stays)
inline std::string dropLastWord (std::string text)
{
    while (! text.empty() && text.back() == ' ')
        text.pop_back();
    while (! text.empty() && text.back() != ' ')
        text.pop_back();
    return text;
}

// Up / Down and Ctrl+wheel: the next / previous usable entry of the list, wrapping around; `at` < 0 starts before the
// first (down) or after the last (up). -1 when none is usable.
inline int stepWrapping (const std::vector<char>& usable, int at, int delta)
{
    const auto n = (int) usable.size();
    if (n == 0 || delta == 0)
        return at;
    auto k = at < 0 ? (delta > 0 ? -1 : n) : at;
    const auto dir = delta > 0 ? 1 : -1;
    for (int steps = std::abs (delta); steps > 0; --steps)
    {
        int tries = 0;
        do
        {
            k = ((k + dir) % n + n) % n;
        } while (! usable[(std::size_t) k] && ++tries < n);
        if (! usable[(std::size_t) k])
            return -1;
    }
    return k;
}

// The tiles: one cell box for every plugin (a fixed 16:10 shape; its picture is fitted inside, letterboxed), in a
// grid per half. `split` finds the largest cell width (in [minW, maxW]) at which both halves fit `height`, giving
// each half as many columns as it needs (the generators fewer than the effects), so neither leaves a big empty zone.
struct Box
{
    int x = 0, y = 0, w = 0, h = 0;
};

struct Grid
{
    int cellW = 0, cellH = 0;
    int leftCols = 0, rightCols = 0;
    bool fits = true; // false: the smallest cells, the view scrolls
};

inline int gridRows (int n, int cols) { return n <= 0 || cols <= 0 ? 0 : (n + cols - 1) / cols; }

inline int gridHeight (int n, int cols, int cellH, int gap)
{
    const auto rows = gridRows (n, cols);
    return rows == 0 ? 0 : rows * cellH + (rows - 1) * gap;
}

inline int gridWidth (int cols, int cellW, int gap) { return cols <= 0 ? 0 : cols * cellW + (cols - 1) * gap; }

// `right` = 0: one grid (a category tab); else two, `separation` apart
inline Grid split (int left, int right, int width, int height, int gap, int separation, double aspect, int minW, int maxW)
{
    Grid best;
    auto tryWidth = [&] (int w, bool needFit) -> bool {
        const auto h = std::max (1, (int) (w / aspect + 0.5));
        const auto room = right > 0 ? width - separation : width;
        const auto total = (room + gap) / (w + gap) - (right > 0 ? 1 : 0); // (two grids: one gap fewer)
        if (total < (left > 0 ? 1 : 0) + (right > 0 ? 1 : 0))
            return false;
        int bestL = -1, bestHeight = 1 << 30;
        if (right <= 0)
        {
            bestL = std::min (total, std::max (1, left));
            bestHeight = gridHeight (left, bestL, h, gap);
        }
        else
            for (int l = left > 0 ? 1 : 0; l <= total - 1; ++l)
            {
                const auto hh = std::max (gridHeight (left, l, h, gap), gridHeight (right, total - l, h, gap));
                if (hh < bestHeight)
                {
                    bestHeight = hh;
                    bestL = l;
                }
            }
        if (bestL < 0 || (needFit && bestHeight > height))
            return false;
        best = { w, h, bestL, right > 0 ? total - bestL : 0, bestHeight <= height };
        return true;
    };
    for (int w = maxW; w >= minW; --w)
        if (tryWidth (w, true))
            return best;
    if (! tryWidth (minW, false))
        best = { minW, std::max (1, (int) (minW / aspect + 0.5)), std::max (0, std::min (left, 1)), right > 0 ? 1 : 0, false };
    return best;
}

// the cells of `n` tiles in `cols` columns, rows left to right, the last row centred
inline std::vector<Box> gridCells (int n, int cols, int cellW, int cellH, int gap)
{
    std::vector<Box> out;
    if (cols <= 0)
        return out;
    const auto full = gridWidth (cols, cellW, gap);
    for (int i = 0; i < n; ++i)
    {
        const auto row = i / cols, col = i % cols;
        const auto inRow = std::min (cols, n - row * cols);
        const auto offset = (full - gridWidth (inRow, cellW, gap)) / 2;
        out.push_back ({ offset + col * (cellW + gap), row * (cellH + gap), cellW, cellH });
    }
    return out;
}

// a picture of `w` × `h` fitted inside a cell, centred, its shape kept (never stretched or bigger than the cell)
inline Box fitInside (Box cell, int w, int h)
{
    if (w <= 0 || h <= 0 || cell.w <= 0 || cell.h <= 0)
        return cell;
    const auto k = std::min ((double) cell.w / w, (double) cell.h / h);
    const auto fw = std::max (1, (int) (w * k + 0.5)), fh = std::max (1, (int) (h * k + 0.5));
    return { cell.x + (cell.w - fw) / 2, cell.y + (cell.h - fh) / 2, fw, fh };
}
} // namespace lynx::pickerview
