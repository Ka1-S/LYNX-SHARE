#pragma once

// FL's zipped channels (round 190mk, details DE-38; capture 190mf A4 fl-rack-zipped.png, FL manual "Channel Rack ›
// Options › Zip selected (Alt+Z) - Sets selected Channels to compact view mode. Right-Click the button of a zipped
// Channel to unzip it", "Unzip all (Alt+U)"): the zipped channels next to each other share ONE strip of half a row
// (FL: 15 px where a row is 30) at the place of the first; in it each channel is a 5 px bar in its colour, as wide as
// the channel button, side by side 14 px apart (FL: 80 px bars at x 318 and 412), the first under the button. The flag
// is the channel's (ChannelDef::zipped, saved in .lynx). No JUCE: pure layout.

#include <cstdint>
#include <vector>

namespace lynx::rackzip
{
constexpr int barHeight = 5;
constexpr int barGap = 14;

struct Layout
{
    std::vector<int> top;    // per row (the "add channel" row last): its top, before the graph lane's offset
    std::vector<int> height; // its height (0: hidden by the filter)
    std::vector<int> slot;   // a zipped row: its place in its strip (0 = the first, which holds the strip); else -1
    std::vector<int> first;  // a zipped row: the strip's first row; else the row itself
    int total = 0;           // the rows' height, the "add channel" row included
};

// `zipped[r]` / `shown[r]` for every channel row; the "add channel" row (index zipped.size()) is a normal row.
inline Layout layout (const std::vector<bool>& zipped, const std::vector<bool>& shown, int rowHeight, int startY = 4)
{
    const auto count = (int) zipped.size();
    Layout l;
    l.top.assign ((std::size_t) count + 1, startY);
    l.height.assign ((std::size_t) count + 1, 0);
    l.slot.assign ((std::size_t) count + 1, -1);
    l.first.resize ((std::size_t) count + 1);
    auto y = startY;
    int stripFirst = -1, stripSlots = 0;
    for (int r = 0; r < count; ++r)
    {
        l.first[(std::size_t) r] = r;
        const auto visible = r < (int) shown.size() ? (bool) shown[(std::size_t) r] : true;
        if (! visible)
        {
            l.top[(std::size_t) r] = y; // (a hidden row neither breaks nor starts a strip)
            continue;
        }
        if (zipped[(std::size_t) r])
        {
            if (stripFirst < 0)
            {
                stripFirst = r;
                stripSlots = 0;
                l.height[(std::size_t) r] = rowHeight / 2;
                l.top[(std::size_t) r] = y;
                y += rowHeight / 2;
            }
            else
            {
                l.top[(std::size_t) r] = l.top[(std::size_t) stripFirst];
                l.height[(std::size_t) r] = rowHeight / 2;
            }
            l.slot[(std::size_t) r] = stripSlots++;
            l.first[(std::size_t) r] = stripFirst;
            continue;
        }
        stripFirst = -1;
        l.top[(std::size_t) r] = y;
        l.height[(std::size_t) r] = rowHeight;
        y += rowHeight;
    }
    l.first[(std::size_t) count] = count;
    l.top[(std::size_t) count] = y;
    l.height[(std::size_t) count] = rowHeight;
    l.total = y + rowHeight - startY;
    return l;
}

// The row under y (the "add channel" row = count; a strip: its first row), -1 above the first row; past the last: count + n
inline int rowAt (const Layout& l, int y, int rowHeight)
{
    const auto count = (int) l.top.size() - 1;
    if (count < 0 || y < l.top.front())
        return -1;
    for (int r = 0; r <= count; ++r)
        if (l.height[(std::size_t) r] > 0 && y >= l.top[(std::size_t) r] && y < l.top[(std::size_t) r] + l.height[(std::size_t) r])
            return l.first[(std::size_t) r];
    const auto below = y - l.top[(std::size_t) count];
    return count + (rowHeight > 0 ? below / rowHeight : 0);
}

// In a strip that starts at `first`: the zipped row whose bar spans x (bars from `barsLeft`, each `barWidth` wide),
// -1 between / past the bars
inline int barAt (const Layout& l, int first, int x, int barsLeft, int barWidth)
{
    if (first < 0 || first >= (int) l.slot.size() || l.slot[(std::size_t) first] != 0 || barWidth <= 0 || x < barsLeft)
        return -1;
    const auto pitch = barWidth + barGap;
    const auto index = (x - barsLeft) / pitch;
    if ((x - barsLeft) % pitch >= barWidth)
        return -1;
    for (int r = first; r < (int) l.slot.size(); ++r)
        if (l.first[(std::size_t) r] == first && l.slot[(std::size_t) r] == index)
            return r;
    return -1;
}

// FL's bar shading (fl-rack-zipped.png: red (156,51,44) -> (124,19,12), orange (213,112,69) -> (159,58,15)): the
// channel colour at the top, about 32 darker per component at the bottom (0xAARRGGBB)
inline std::uint32_t barBottom (std::uint32_t top) noexcept
{
    const auto down = [] (std::uint32_t c, int shift) { const auto v = (int) ((c >> shift) & 0xffu) - 32; return (std::uint32_t) (v < 0 ? 0 : v) << shift; };
    return (top & 0xff000000u) | down (top, 16) | down (top, 8) | down (top, 0);
}
} // namespace lynx::rackzip
