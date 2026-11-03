#pragma once

// What a press on the Playlist's ruler does, FL Studio 2026's rules, without JUCE (tests/Sel168tTests.inc).
//
// FL evidence [DECOMP FLEngine_x64.dll, docs/analise/REA_rodada168t.md §1]: TEventEditForm.TimePanelMouseDown
// (0xe56bf0) / TimePanelMouseMove (0xe57670) / TimePanelMouseUp (0xe58d10); tool numbers from the DFM (SelectBtn
// Tag 4, ZoomBtn Tag 5).
//   - right + Ctrl, or the Zoom tool (either button): a range drag that zooms the view to it on release;
//   - right (no Ctrl), outside the marker tags: a time selection drag; when a selection exists, the press moves its
//     nearer edge (the farther one stays the anchor);
//   - right on a marker tag (Select / Zoom tools excepted): the marker menu;
//   - left with Ctrl, Shift or a double click, or with the Select tool: a time selection drag, the clips in it
//     selected; Shift with a selection: the selection moves (its length kept); on a marker tag: the time between
//     that marker and the next one;
//   - plain left: on a marker tag, the marker; elsewhere the song position (scrub).
// LYNX-own on top of FL: a right click released in place with no selection opens LYNX's ruler menu (FL: nothing);
// Ctrl+Shift+drag inside the selection moves it WITH its clips (what LYNX's Shift+drag did before 168t).

#include <algorithm>
#include <cstdint>

#include "core/EditorGestures.h"
#include "core/ModifierMap.h"

namespace lynx::ruler
{
enum class Action
{
    seek,            // scrub the song position
    marker,          // the marker under the mouse (LYNX: go to it)
    markerMenu,      // the menu of the marker under the mouse
    markerSpan,      // time selection from this marker to the next one
    select,          // a new time selection, anchored at the press
    adjust,          // the nearer edge of the selection follows the mouse
    moveSelection,   // the time selection moves, keeping its length
    moveWithClips,   // LYNX: the time selection moves with the clips in it
    zoomRange        // a range that the view zooms to on release
};

struct Press
{
    modmap::Fl mods;
    int clicks = 1;            // 2 = the second press of a double click
    EditTool tool = EditTool::pencil;
    bool onMarkerTag = false;
    bool hasSelection = false;
    bool insideSelection = false;
};

constexpr Action press (const Press& p) noexcept
{
    const auto selectTool = p.tool == EditTool::lasso, zoomTool = p.tool == EditTool::zoom;

    if (p.mods.right)
    {
        if (p.mods.ctrl || zoomTool)
            return Action::zoomRange;
        if (p.onMarkerTag && ! selectTool && ! zoomTool)
            return Action::markerMenu;
        return p.hasSelection ? Action::adjust : Action::select;
    }

    if (zoomTool)
        return Action::zoomRange;

    if (p.mods.ctrl || p.mods.shift || p.clicks >= 2 || selectTool)
    {
        if (p.mods.shift && p.hasSelection)
            return p.mods.ctrl && p.insideSelection ? Action::moveWithClips : Action::moveSelection;
        if (p.onMarkerTag)
            return Action::markerSpan;
        return Action::select;
    }

    return p.onMarkerTag ? Action::marker : Action::seek;
}

// The anchor of an `adjust` press at `tick`: the selection's edge farther from it.
constexpr std::int64_t adjustAnchor (std::int64_t from, std::int64_t to, std::int64_t tick) noexcept
{
    const auto toStart = tick > from ? tick - from : from - tick;
    const auto toEnd = tick > to ? tick - to : to - tick;
    return toEnd < toStart ? from : to;
}

// The time selection after moving [from, to) by `delta` ticks: never before the song's start, length kept.
struct Range
{
    std::int64_t from = 0, to = 0;
};

constexpr Range moved (std::int64_t from, std::int64_t to, std::int64_t delta) noexcept
{
    const auto d = std::max (delta, -from);
    return { from + d, to + d };
}

// A right press released within `slop` pixels counts as a click (no drag).
constexpr bool isClick (int dx, int dy, int slop = 3) noexcept
{
    return dx * dx + dy * dy < slop * slop;
}
} // namespace lynx::ruler
