#pragma once

// FL Studio 2026's Playlist track-header mouse rules (round 190ft, issues #163 / #164; evidence in
// docs/analise/REA_rodada190ft.md): the track selection a press makes, the range a drag across the headers adds or
// removes, the bottom-edge resize band, the height a resize drag gives one track or every selected track, and the
// drag past the top that groups the track with the one above. Plain functions on track indices, no JUCE.
//
// FL evidence (FLEngine_x64.dll, sha256 6d9c5ab0…): AUTracksPanelMouseDown 0xea9660, AUTracksPanelMouseMove 0xeab1f0,
// AUTracksPanelMouseUp 0xeabd30, the hit test FUN_00ea93a0, the selection modes FUN_012e83e0, the drag range
// FUN_012e8740, the resize of the selected tracks FUN_012eda20 and the row layout FUN_00ea0b30.

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/EditorPolish.h"

namespace lynx::trackhdr
{
// FUN_012e83e0's modes that a header press uses: 0 add, 1 only this one, 2 toggle, 3 remove.
enum class SelMode { add, only, toggle, remove };

// `selected` is kept sorted and without duplicates.
inline void applySelection (std::vector<int>& selected, int track, SelMode mode)
{
    if (track < 0)
        return;
    const auto it = std::lower_bound (selected.begin(), selected.end(), track);
    const auto has = it != selected.end() && *it == track;
    switch (mode)
    {
        case SelMode::only:   selected = { track }; break;
        case SelMode::add:    if (! has) selected.insert (it, track); break;
        case SelMode::remove: if (has) selected.erase (it); break;
        case SelMode::toggle: if (has) selected.erase (it); else selected.insert (it, track); break;
    }
}

// FUN_012e8740: every track from the anchor to the one under the mouse is added (or removed). FL calls it on every
// move of the drag, so a range only grows: tracks left behind by moving back stay as they are.
inline void applyRange (std::vector<int>& selected, int anchor, int track, bool adding)
{
    for (int t = std::min (anchor, track); t <= std::max (anchor, track); ++t)
        applySelection (selected, t, adding ? SelMode::add : SelMode::remove);
}

// The clips a press on the header selects (FL calls the clip grid's MouseDown with Ctrl added, flagged "from the
// header" so its box spans the whole song: the clips of the tracks it crosses).
enum class ClipPick { none, replace, add };
// What a drag across the headers does after the press (+0xda0 = 0xb select / 0xc deselect, or nothing).
enum class RangeDrag { none, select, deselect };

struct PressPlan
{
    SelMode mode = SelMode::only;
    ClipPick clips = ClipPick::none;
    bool rangeFollowsResult = false; // Ctrl+Shift: the drag selects when the press left the track selected, else deselects
    RangeDrag drag = RangeDrag::none;
    bool lynxShiftRange = false;     // LYNX's own Shift+click: the tracks from the last clicked one (kept, see REA doc)
    // LYNX extra (issue #163, owner 2026-11-14): with Ctrl alone nothing changes on the press; a drag that leaves the
    // track does FL's Ctrl press (this track alone with its clips) and FL's range; a click without that toggles the
    // track (its clips join or leave the clip selection) — FL has no Ctrl+click toggle
    bool ctrlClickToggles = false;
};

// A left press on the header's plain area (not the LED, a button, the group arrow or the resize band). `ctrl` is FL's
// Ctrl (Cmd on a Mac).
inline PressPlan planPress (bool ctrl, bool shift, bool alt)
{
    PressPlan p;
    if (ctrl && ! alt)
    {
        if (shift)
        {
            // FL: Ctrl+Shift+click = add / remove this one track; its clips join the clip selection
            p.mode = SelMode::toggle;
            p.clips = ClipPick::add;
            p.rangeFollowsResult = true;
        }
        else
        {
            // FL: Ctrl+press = this track alone and every clip on it; Ctrl+drag adds the tracks crossed (and their
            // clips). LYNX: deferred to the drag, a plain Ctrl+click toggles (ctrlClickToggles)
            p.mode = SelMode::only;
            p.clips = ClipPick::replace;
            p.drag = RangeDrag::select;
            p.ctrlClickToggles = true;
        }
        return p;
    }
    if (shift && ! ctrl && ! alt)
    {
        // FL: Shift+click opens "Rename, color and icon" (FUN_012e2510); LYNX keeps its Shift+click range
        p.lynxShiftRange = true;
        return p;
    }
    return p; // a click (Alt or not): this track alone
}

inline RangeDrag dragAfter (const PressPlan& plan, bool trackNowSelected)
{
    if (plan.rangeFollowsResult)
        return trackNowSelected ? RangeDrag::select : RangeDrag::deselect;
    return plan.drag;
}

// --- the resize band (FUN_00ea93a0) ----------------------------------------------------------------------------------

// The band at the bottom of a header that resizes the track: a quarter of the row, 4 to 14 px (FL's normal header
// style; ×UI scale in FL, LYNX works in logical px). Rows of 6 px or less have none.
inline int resizeBand (int rowHeight)
{
    return std::clamp (rowHeight / 4, 4, 14);
}

inline bool inResizeBand (int rowHeight, int yFromTop)
{
    return rowHeight > 6 && yFromTop < rowHeight && yFromTop >= rowHeight - resizeBand (rowHeight);
}

// What a left press on the band does (AUTracksPanelMouseDown): a locked row or a double click flips "Lock to this
// size" (the locked size is negated: FL's quick lock / unlock), Alt resets the size (100 %, unlocked), otherwise the
// drag resizes. A middle click on the band also resets.
enum class BandPress { resize, toggleLock, reset };

inline BandPress bandPress (bool locked, bool doubleClick, bool alt)
{
    if (locked || doubleClick)
        return BandPress::toggleLock;
    return alt ? BandPress::reset : BandPress::resize;
}

// FL's quick lock flip: the locked size is negated (−1, the default "never locked", becomes 1 = the smallest row).
inline int flippedLock (int lockedHeight)
{
    if (lockedHeight == 0)
        lockedHeight = -1;
    return -lockedHeight;
}

// --- the drag (AUTracksPanelMouseMove, FUN_012eda20) ------------------------------------------------------------------

// One track: its row follows the mouse; never below the smallest row (FL clamps the size factor at 0). The layout keeps
// rows even and at most 512 px above the smallest (FUN_00ea0b30).
inline int draggedHeight (int startHeight, int dy)
{
    return polish::evenTrackHeight (startHeight + dy);
}

// The dragged track is one of several selected tracks: they all take the same height, the distance from the top of
// the first selected track to the mouse shared by the selected tracks down to the dragged one. Returns 0 (no change)
// when that is not above the smallest row (FL then leaves the sizes alone).
inline int selectedHeight (const std::vector<int>& selected, int dragged, int firstTop, int mouseY)
{
    int index = 1;
    for (int i = 0; i < (int) selected.size(); ++i)
        if (selected[(std::size_t) i] == dragged)
        {
            index = i + 1;
            break;
        }
    const auto h = (int) std::lround ((double) (mouseY - firstTop) / index);
    return h > polish::minTrackHeight ? polish::evenTrackHeight (h) : 0;
}

// The drag goes on past the top of a row already at its smallest: 32 px above the row's top FL flips "Group with
// above track" for it (undo "group with above track" / "ungroup from above track"), puts its size back to 100 % and
// ends the drag.
constexpr int groupGestureDistance = 32;

inline bool groupGesture (int newHeight, int mouseY, int rowTop)
{
    return newHeight <= polish::minTrackHeight && mouseY - rowTop <= -groupGestureDistance;
}
// LYNX's Ctrl+click toggle (issue #163): the clip selection follows the track — its clips join when it becomes
// selected and leave when it is deselected. `clipTracks[i]` is the track of clip i.
inline void toggleTrackClips (std::vector<int>& clipSelection, const std::vector<int>& clipTracks, int track, bool nowSelected)
{
    for (int i = 0; i < (int) clipTracks.size(); ++i)
    {
        if (clipTracks[(std::size_t) i] != track)
            continue;
        const auto it = std::find (clipSelection.begin(), clipSelection.end(), i);
        if (nowSelected && it == clipSelection.end())
            clipSelection.push_back (i);
        else if (! nowSelected && it != clipSelection.end())
            clipSelection.erase (it);
    }
}
} // namespace lynx::trackhdr
