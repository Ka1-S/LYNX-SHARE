#pragma once

// LYNX_PAINT_SAVINGS=0 (diagnostics, round 190ge): the views paint and repaint as before that round's savings (the
// Piano roll and Channel rack draw everything a paint passes, the Clip-launch grid repaints itself whole every tick),
// for A/B measurements with one build (--paintbench, --uiperf). Unset or any other value: the savings are on.

#include <cstdlib>
#include <cstring>

namespace lynx::ui
{
inline bool paintSavings() noexcept
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_PAINT_SAVINGS");
        return v == nullptr || std::strcmp (v, "0") != 0;
    }();
    return on;
}
} // namespace lynx::ui
