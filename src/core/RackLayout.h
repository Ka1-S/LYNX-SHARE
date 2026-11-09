#pragma once

// Channel rack geometry shared by its painting and its hit test (issue #116).

namespace lynx::racklayout
{
// The dashed "+ add channel" box in the row after the last channel: the only place that opens the add menu, as FL's
// + button (FL 2026: a click in the empty rest of that row opens nothing). `rowTop` is that row's top.
struct Box
{
    int x, y, width, height;
    bool contains (int px, int py) const noexcept { return px >= x && px < x + width && py >= y && py < y + height; }
};
inline Box addChannelBox (int nameLeft, int nameWidth, int rowTop, int rowHeight) noexcept
{
    return { nameLeft, rowTop + 3, nameWidth, rowHeight - 6 };
}
} // namespace lynx::racklayout
