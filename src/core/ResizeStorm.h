#pragma once

// Round 190rk (hosted plugin sweep, 0.1.32): a resizable plugin editor and the LYNX window around it can answer each
// other's size changes for ever. Seen with LSP's Parametric Equalizer (VST3, Linux) at a UI scale of 150 %: the window
// fits the editor, the editor rounds the size it is given to its own physical pixels and reports a slightly different
// one, the window fits that, and so on, hundreds of times a second (the message thread never idles, the app stops
// answering and the window cannot even be closed). This counts the window's refits that answer the editor's own size
// changes: after `burst` of them within `windowMs` it says no to the next ones until the editor has been quiet for
// `windowMs`, so the window keeps the last size and the loop ends. A user dragging the window's corner or the editor
// resizing itself now and then (a page switch, a zoom choice) never comes near the limit.

#include <cstdint>
#include <deque>

namespace lynx
{
class ResizeStorm
{
public:
    explicit ResizeStorm (int burstCount = 12, double windowMilliseconds = 1000.0) noexcept
        : burst (burstCount), windowMs (windowMilliseconds)
    {
    }

    // The editor changed its size at `nowMs`: may the window follow it?
    bool allow (double nowMs)
    {
        if (blocked && nowMs - last < windowMs)
        {
            last = nowMs;
            ++refused;
            return false;
        }
        blocked = false;
        last = nowMs;
        times.push_back (nowMs);
        while (! times.empty() && nowMs - times.front() > windowMs)
            times.pop_front();
        if ((int) times.size() > burst)
        {
            blocked = true;
            times.clear();
            ++refused;
            return false;
        }
        return true;
    }

    bool isBlocked() const noexcept { return blocked; }
    std::uint64_t refusedCount() const noexcept { return refused; }

private:
    int burst;
    double windowMs;
    std::deque<double> times;
    double last = 0.0;
    bool blocked = false;
    std::uint64_t refused = 0;
};
} // namespace lynx
