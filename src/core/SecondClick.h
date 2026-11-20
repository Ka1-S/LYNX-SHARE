#pragma once

// Issue #206 (round 190lw): a mixer insert slot's double-click. FL opens the slot's plugin menu on the first press, so the
// second press of a double-click lands on that menu's window, where the slot never sees it: the slot remembers its
// press and a press near it (screen pixels) soon after (the system's double-click time) counts as the second click.
// No JUCE here.

#include <cstdint>
#include <cstdlib>

namespace lynx::secondclick
{
struct Press
{
    int x = 0, y = 0;
    std::int64_t ms = -1; // < 0: none
};
inline constexpr int slop = 4; // px

inline bool isSecond (const Press& first, const Press& now, std::int64_t timeoutMs) noexcept
{
    return first.ms >= 0 && now.ms >= first.ms && now.ms - first.ms <= timeoutMs && std::abs (now.x - first.x) <= slop
           && std::abs (now.y - first.y) <= slop;
}
} // namespace lynx::secondclick
