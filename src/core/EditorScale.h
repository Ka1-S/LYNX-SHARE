#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Round 190he (issue #152: "plugin scaling fails, the FL-engine plugins can't be made smaller"): the size of the
// windows LYNX draws for a plugin (a channel's settings window, a built-in effect's window). FL's plugin menu has
// "Scaling" (FLEngine_x64.dll PluginMenu: PluginScalingMenu; FL 2026 shot shots/FL-harmor-wrapper-menu-scaling.png):
// System, Host (ticked by default), 100 % … 400 %; FL's 100 % is the plugin's own design size. LYNX opens these
// windows at FL's size × 1.25, its "Host" size; FL's 100 % is the smaller size the reporter asked for. Dragging the
// window's edge also sizes it, keeping its proportions, between `minPercent` and `maxPercent`.
namespace lynx::editorscale
{
inline constexpr int defaultPercent = 125; // LYNX's size (FL × 1.25), FL's "Host"; a channel's `editorScale` 0 means this
inline constexpr int minPercent = 40, maxPercent = 400;
inline constexpr std::array<int, 9> menuPercents { 100, 125, 150, 175, 200, 250, 300, 350, 400 }; // FL's items

// the stored value (0 = Host, LYNX's default) as a percentage of FL's size
constexpr int percentOf (int stored) noexcept
{
    return stored <= 0 ? defaultPercent : std::clamp (stored, minPercent, maxPercent);
}

// what to store for a chosen percentage (Host is stored as 0, so old projects and untouched windows write nothing)
constexpr int storedFor (int percent) noexcept
{
    return std::clamp (percent, minPercent, maxPercent);
}

// the factor the window's content is drawn at, against its default size
constexpr double factorOf (int stored) noexcept { return (double) percentOf (stored) / (double) defaultPercent; }

// the percentage a window dragged to `width` shows, for content whose default width is `defaultWidth`
inline int percentForWidth (int width, int defaultWidth) noexcept
{
    if (defaultWidth <= 0)
        return defaultPercent;
    const auto p = (int) std::lround ((double) width * defaultPercent / (double) defaultWidth);
    return std::clamp (p, minPercent, maxPercent);
}
} // namespace lynx::editorscale
