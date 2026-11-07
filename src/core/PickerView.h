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
#include <cmath>
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

// --- round 175b: FL's own packing (TPlugListForm, docs/analise/REA_rodada175b.md) ------------------------------------
//
// FL lays each half out as rows of tiles at their pictures' own sizes (FUN_00ec0510, row finisher FUN_00ec0180):
//  * a tile is its picture + 2 px; tiles go left to right with an 8 px gap; a tile that would pass the right edge starts
//    a new row;
//  * a row is as tall as its tallest tile, every tile centred on it;
//  * a finished row is spread over the whole width (the spare room shared between its gaps), except: a row with a single
//    tile — the only row: centred; a generator: pushed to the right edge (FL's quirk); else at the left — and when the
//    only row has room to spare, its gaps are at most 32 px and the row is centred;
//  * the rows are stacked with min(16, spare / gaps) between them (negative when they don't fit: they overlap, FL never
//    scrolls), the block centred in the box.
struct PackTile
{
    int w = 0, h = 0;       // the picture (the tile is 2 px bigger)
    bool shown = true;      // in the shown tab
    bool generator = false; // (the single-tile row quirk)
};

struct Packed
{
    int x = 0, y = 0, w = 0, h = 0; // the tile (picture + 2), top-left; empty when not shown
    int row = -1, rowHeight = 0;
    bool shown = false;
};

struct PackBox
{
    int left = 0, top = 0, right = 0, bottom = 0;
};

struct PackResult
{
    std::vector<Packed> tiles;
    int rows = 0;      // rows used
    int reach = 0;     // max (tile bottom + 2 × its row height): where FL's tab strip may go (TPlugListForm +0x804)
};

inline int roundFl (double v) { return (int) std::nearbyint (v); } // (Delphi's Round: halves to even)

inline PackResult packRows (const std::vector<PackTile>& in, PackBox box, int gap = 8, int rowGapMax = 16, int spreadMax = 32)
{
    PackResult out;
    out.tiles.resize (in.size());
    std::vector<double> cx (in.size()), cy (in.size());
    int x = box.left, rowTop = 0, row = 0, total = 0;
    std::vector<int> rowItems; // the current row's tiles, hidden ones too

    auto finish = [&] (bool last) -> int {
        int rowH = 0, n = 0;
        for (const auto i : rowItems)
            if (in[(std::size_t) i].shown)
            {
                rowH = std::max (rowH, out.tiles[(std::size_t) i].h);
                ++n;
            }
        for (const auto i : rowItems)
            if (in[(std::size_t) i].shown)
            {
                auto& t = out.tiles[(std::size_t) i];
                cy[(std::size_t) i] = roundFl (rowTop + rowH * 0.5);
                total = std::max (total, roundFl (cy[(std::size_t) i] + t.h * 0.5));
                t.rowHeight = rowH;
            }
        if (n > 0)
        {
            double dx = (double) (box.right - (x - gap)) / (double) std::max (1, n - 1);
            double start = box.left;
            if (n == 1)
            {
                if (last && row == 0)
                    start += dx * 0.5;
                else if (! rowItems.empty() && in[(std::size_t) rowItems.back()].generator)
                    start += dx;
            }
            else if (last && row == 0 && dx > spreadMax)
            {
                start += (n - 1) * (dx - spreadMax) * 0.5;
                dx = spreadMax;
            }
            for (const auto i : rowItems)
                if (in[(std::size_t) i].shown)
                {
                    const auto w = out.tiles[(std::size_t) i].w;
                    const auto l = roundFl (start);
                    cx[(std::size_t) i] = roundFl ((l + l + w) * 0.5);
                    start += w + gap + dx;
                }
        }
        return rowH;
    };

    for (int i = 0; i < (int) in.size(); ++i)
    {
        auto& t = out.tiles[(std::size_t) i];
        if (in[(std::size_t) i].shown)
        {
            t.w = in[(std::size_t) i].w + 2;
            t.h = in[(std::size_t) i].h + 2;
            if (box.right < x + t.w)
            {
                rowTop += finish (false);
                rowItems.clear();
                x = box.left;
                ++row;
            }
            t.row = row;
            t.shown = true;
            x += t.w + gap;
        }
        rowItems.push_back (i);
    }
    finish (true);

    auto spare = (box.bottom - box.top) - total;
    int rowGap = 0;
    if (row > 0)
    {
        rowGap = std::min (rowGapMax, spare / row);
        spare -= rowGap * row;
    }
    const auto y0 = box.top + spare / 2;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        auto& t = out.tiles[i];
        if (! t.shown)
            continue;
        const auto centreX = (int) cx[i], centreY = roundFl (cy[i] + y0 + t.row * rowGap);
        t.x = centreX - t.w / 2;
        t.y = centreY - t.h / 2;
        out.reach = std::max (out.reach, t.y + t.h + t.rowHeight * 2);
    }
    out.rows = row + 1;
    return out;
}

// The tab labels of one half (FL's FUN_00ec3e10): the largest font from 19 down to 12 (× the UI scale) at which the
// labels, each with 2 × the size around it, fit in `room` (the half's width; the whole width with one half).
// `textWidth (size)` = the labels' summed text width at that size.
template <typename WidthAt>
int tabFontSize (WidthAt textWidth, int count, int room, double uiScale = 1.0)
{
    auto size = roundFl (uiScale * 19.0);
    for (; size > 12; --size)
        if (textWidth (size) + count * size * 2 < room)
            break;
    return size;
}

// The tab strip's row (FL's TPlugListForm.show): just under the tiles (their reach + 80), at most the form's height;
// the tabs' centre line is half the tallest tab + 21 above it.
inline int tabCentreY (int reach, int formHeight, int tabHeight, double uiScale = 1.0)
{
    const auto base = std::min (formHeight, reach + roundFl (80.0 * uiScale));
    return base - (tabHeight / 2 + roundFl (21.0 * uiScale));
}

// FL's picture budget for a plugin (its Plugin database bitmaps, 192 dpi): half the editor, at most 258 × 128 pixels;
// shown at half that size on a 100 % display.
inline std::pair<int, int> pictureSize (int editorW, int editorH, int maxW = 258, int maxH = 128)
{
    if (editorW <= 0 || editorH <= 0)
        return { 0, 0 };
    const auto k = std::min ({ 0.5, (double) maxW / editorW, (double) maxH / editorH });
    return { std::max (1, (int) std::lround (editorW * k)), std::max (1, (int) std::lround (editorH * k)) };
}
} // namespace lynx::pickerview
