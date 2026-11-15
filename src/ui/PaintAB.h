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

// LYNX_PAINT_CACHE=0 (diagnostics, round 190gr): the paint caches are off (theme::font makes a new font on every call, the
// text cache builds its key strings, every shape is drawn straight — round 161's tiles too —, the Channel rack walks the
// notes per cell, the playing Clip-launch grid repaints whole columns as round 190ge did): the reference for --paintbench
// pixel comparisons and A/B measurements (--paintbench, --uiperf) with one build.
inline bool paintCache() noexcept
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_PAINT_CACHE");
        return v == nullptr || std::strcmp (v, "0") != 0;
    }();
    return on;
}

// LYNX_PAINT_TILES=0 (diagnostics, round 190gr): the paint caches stay on except that round's shape tiles (the launch
// triangles; on Windows also the Direct2D tiles of the knobs, which differ from the knobs drawn straight by a few levels
// of anti-aliasing): the pixel-exact part of the caches alone.
inline bool paintTiles() noexcept
{
    static const bool on = [] {
        const auto* v = std::getenv ("LYNX_PAINT_TILES");
        return paintCache() && (v == nullptr || std::strcmp (v, "0") != 0);
    }();
    return on;
}
} // namespace lynx::ui
