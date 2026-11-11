#pragma once

// FL's General › "Limit the number of open plugin editors" (round 189o; FLEngine 2026 [DECOMP]: the registry value
// "LimitEditors" is read with default 1 = on, FUN_011a5800; the box's click FUN_013559c0 warns before it is
// turned off; FUN_012babb0, run when a plugin window is shown, calls FUN_012baae0 while the option is on: the plugin
// windows with an open editor, sorted by their wrapper's +0x18 stamp, newest first, and while more than 25 (0x19) are
// open the last one, the oldest, is closed). LYNX stamps a window when it opens and whenever it becomes the active window.
// Header-only; the UI registers its editor windows here.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lynx::editorlimit
{
inline constexpr int maxOpen = 25;

// The keys to close so that at most `limit` stay open: the oldest stamps first (ties: the earlier entry).
template <typename Key>
std::vector<Key> oldestBeyond (std::vector<std::pair<Key, std::uint64_t>> open, int limit = maxOpen)
{
    std::vector<Key> out;
    if ((int) open.size() <= limit)
        return out;
    std::stable_sort (open.begin(), open.end(), [] (const auto& a, const auto& b) { return a.second < b.second; });
    for (std::size_t i = 0; i < open.size() - (std::size_t) limit; ++i)
        out.push_back (open[i].first);
    return out;
}

// --- the registry of open plugin editor windows (UI thread only) ----------------------------------------------------
inline bool& enabled() noexcept // on by default, as FL
{
    static bool on = true;
    return on;
}

struct Entry
{
    const void* key = nullptr;
    std::uint64_t stamp = 0;
    std::function<void()> close;
};

inline std::vector<Entry>& entries()
{
    static std::vector<Entry> list;
    return list;
}

inline std::uint64_t nextStamp() noexcept
{
    static std::uint64_t clock = 0;
    return ++clock;
}

// The window of `key` was shown or became active.
inline void touch (const void* key)
{
    for (auto& e : entries())
        if (e.key == key)
            e.stamp = nextStamp();
}

// The window of `key` is gone.
inline void forget (const void* key)
{
    auto& list = entries();
    list.erase (std::remove_if (list.begin(), list.end(), [key] (const Entry& e) { return e.key == key; }), list.end());
}

// Closes the oldest windows past the limit (when the option is on). `close` may call forget().
inline void enforce (int limit = maxOpen)
{
    if (! enabled())
        return;
    std::vector<std::pair<const void*, std::uint64_t>> open;
    for (const auto& e : entries())
        open.emplace_back (e.key, e.stamp);
    std::vector<std::function<void()>> closers;
    for (auto* key : oldestBeyond (open, limit))
        for (const auto& e : entries())
            if (e.key == key && e.close)
                closers.push_back (e.close);
    for (auto& close : closers)
        close();
}

// A new editor window opened: it gets the newest stamp, then the limit applies.
inline void opened (const void* key, std::function<void()> close)
{
    forget (key);
    entries().push_back ({ key, nextStamp(), std::move (close) });
    enforce();
}
} // namespace lynx::editorlimit
