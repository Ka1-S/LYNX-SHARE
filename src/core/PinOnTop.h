#pragma once

#include <vector>

// Round 190ao (pain point 11, LYNX's own; FL has no pin): "Pin on top" of a plugin window. LYNX's secondary windows all
// stay above the main window (it owns them) and take each other's place in front as they are clicked; a pinned one
// stays above the ones that are not, without becoming a window above other programs.
namespace lynx::pinontop
{
// `pinned` lists LYNX's secondary windows from back to front. Returns the windows to bring forward, in the order to do
// it (back to front, so pinned windows keep their own order among themselves), so that every pinned window ends up
// above every window that is not. Empty when no pinned window is covered.
inline std::vector<int> toRaise (const std::vector<bool>& pinned)
{
    auto frontPlain = -1;
    for (int i = 0; i < (int) pinned.size(); ++i)
        if (! pinned[(std::size_t) i])
            frontPlain = i;
    std::vector<int> out;
    for (int i = 0; i < frontPlain; ++i)
        if (pinned[(std::size_t) i])
            out.push_back (i);
    return out;
}
} // namespace lynx::pinontop
