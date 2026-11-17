#pragma once

// Round 190jq: three Playlist view options, all LYNX extras (FL's look and behaviour stay the default). No JUCE here:
// tested in tests/Playlist190jqTests.inc; the Playlist (ui/PlaylistComponent.cpp) reads them, ui/WorkflowExtras.cpp
// saves them in ui-options.txt (preferences of this computer, not of a project).
//
//  - headerwidth (issue #164): the track header column's width can be dragged at its right edge. FL 2026 keeps the
//    column at a fixed width (checked in FL for the issue thread), so the switch is off by default and the default width
//    stays FL's. The width is a preference (ui-options.txt headerWidthExtra), not a project setting: the thread promised
//    "an option", and the column's width is how one person likes to see every project, as the Playlist's other view
//    options (header levels, insert effects on linked tracks) are kept.
//  - pointerclick (issue #191, Nuelka's comment): with the switch on, a single click on an empty spot of the Playlist with
//    the Pencil or the Brush works like FL's Select tool (a click deselects, a drag draws a selection box) and also
//    moves the song position there, as a click in Live's Arrangement does; a double click places the clip (what the
//    single click did). The switch sits in the Playlist toolbar when "Pointer click switch in the Playlist toolbar"
//    (Settings › LYNX extras) is on.
//  - cliplanes (issue #191.3, the view option round 190iu left open): automation clips drawn as foldable child lanes under
//    the track that owns their control (core/LaneView.h › ownerTrack), as Live draws automation lanes under each track.
//    Nothing in the song changes: a track whose clips are all automation clips of one owner is drawn right under that
//    owner, indented as a lane, and folds with the owner's lane arrow. On in the Clip-launch layout (Live), off in the
//    Pattern layout (FL draws automation clips on tracks of their own), never in Simple.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/EditorGestures.h"
#include "core/LaneView.h"
#include "core/NoteEdit.h"
#include "core/Song.h"

namespace lynx::headerwidth
{
inline bool draggable = false; // ui-options.txt headerWidthDrag (Settings › LYNX extras, Playlist menu)
inline int extra = 0;          // ui-options.txt headerWidthExtra: px added to the layout's own width (0 = FL's width)
inline constexpr int maxExtra = 360;
// the grip: 1 px of the selection strip and 3 px of the arrangement past the column's right edge (the strip itself keeps
// selecting tracks, as in FL; the issue's drag landed in the arrangement and painted with the Pencil)
inline constexpr int gripInside = 1, gripOutside = 3;

inline int clampExtra (int px) noexcept { return std::clamp (px, 0, maxExtra); }
// The column's width for a layout whose own width is `base`.
inline int widthOf (int base) noexcept { return draggable ? base + clampExtra (extra) : base; }
inline bool onGrip (int x, int headerWidth) noexcept { return draggable && x >= headerWidth - gripInside && x < headerWidth + gripOutside; }
// A drag that started with `startExtra` and moved `dx` px; the column never takes more than half of the Playlist.
inline int dragged (int startExtra, int dx, int base, int playlistWidth) noexcept
{
    const auto most = std::max (0, std::min (maxExtra, playlistWidth / 2 - base));
    return std::clamp (startExtra + dx, 0, most);
}
} // namespace lynx::headerwidth

namespace lynx::pointerclick
{
inline bool buttonShown = false; // ui-options.txt pointerClickButton: the switch in the Playlist toolbar (LYNX extra)
inline bool on = false;          // ui-options.txt pointerClick: the switch itself

// Turning the extra on shows the switch, lit; turning it off hides it and ends the mode (no hidden state).
inline void setButtonShown (bool shown) noexcept
{
    buttonShown = shown;
    on = shown;
}
// The switch (toolbar button, Playlist menu, key): on also shows the button, so the state is always visible.
inline void setOn (bool value) noexcept
{
    on = value;
    if (value)
        buttonShown = true;
}

enum class EmptyPress { place, pointer };
// A left press on an empty spot of the arrangement (no clip, no Ctrl / Shift: those select in every tool).
inline EmptyPress onEmpty (bool switchOn, EditTool tool, int clicks) noexcept
{
    if (! switchOn || (tool != EditTool::pencil && tool != EditTool::brush))
        return EmptyPress::place;
    return clicks >= 2 ? EmptyPress::place : EmptyPress::pointer;
}
} // namespace lynx::pointerclick

namespace lynx::cliplanes
{
inline bool patternOn = false; // ui-options.txt clipLanesPattern (FL: automation clips on tracks of their own)
inline bool liveOn = true;     // ui-options.txt clipLanesLive (Live: automation lanes under each track)

inline bool shownIn (bool clipLaunch, bool simple) noexcept { return ! simple && (clipLaunch ? liveOn : patternOn); }
inline void setShownIn (bool clipLaunch, bool value) noexcept { (clipLaunch ? liveOn : patternOn) = value; }

// The rows as drawn: `order[row]` = the track on that row, `rowOf[track]` = its row, `owner[track]` = the track it is
// drawn under as a lane (-1: an ordinary row), `children[track]` = how many lanes are drawn under it.
struct Projection
{
    std::vector<int> order, rowOf, owner, children;

    bool any() const noexcept
    {
        return std::any_of (owner.begin(), owner.end(), [] (int o) { return o >= 0; });
    }
    bool isChild (int track) const noexcept { return track >= 0 && track < (int) owner.size() && owner[(std::size_t) track] >= 0; }
    int ownerOf (int track) const noexcept { return isChild (track) ? owner[(std::size_t) track] : -1; }
    int childCount (int track) const noexcept { return track >= 0 && track < (int) children.size() ? children[(std::size_t) track] : 0; }
    int row (int track) const noexcept { return track >= 0 && track < (int) rowOf.size() ? rowOf[(std::size_t) track] : track; }
    int trackAt (int r) const noexcept { return r >= 0 && r < (int) order.size() ? order[(std::size_t) r] : r; }
};

// The owner a track is drawn under: every clip of the track's arrangement is an automation clip (not a live recording),
// and all their controls belong to the same other track. Group headers, grouped tracks and instrument / audio tracks
// stay where they are (a group is defined by the order of its tracks). -1: none.
inline int ownerOfTrack (const Song& song, int track, std::vector<int>& ownerCache)
{
    if (track < 0)
        return -1;
    if (track < (int) song.playlistTracks.size())
    {
        const auto& t = song.playlistTracks[(std::size_t) track];
        if (t.grouped || t.mode != PlaylistTrackMode::unassigned)
            return -1;
    }
    if (noteedit::isGroupHeader (song, track))
        return -1;
    int owner = -1;
    bool any = false;
    for (const auto& c : song.playlist)
    {
        if (c.track != track || c.gridSlot >= 0)
            continue;
        if (c.kind != ClipKind::automation || c.automation < 0 || c.automation >= (int) song.automations.size())
            return -1;
        auto& cached = ownerCache[(std::size_t) c.automation];
        if (cached == -2)
        {
            const auto& a = song.automations[(std::size_t) c.automation];
            cached = a.isLive() ? -1 : laneview::ownerTrack (song, a.target);
        }
        if (cached < 0 || cached == track || (any && cached != owner))
            return -1;
        owner = cached;
        any = true;
    }
    return any ? owner : -1;
}

// The projection of `tracks` rows. Off (`on` false) or nothing to move: every track on its own row.
inline Projection project (const Song& song, int tracks, bool on)
{
    Projection p;
    tracks = std::max (0, tracks);
    p.order.resize ((std::size_t) tracks);
    p.rowOf.resize ((std::size_t) tracks);
    p.owner.assign ((std::size_t) tracks, -1);
    p.children.assign ((std::size_t) tracks, 0);
    for (int t = 0; t < tracks; ++t)
        p.order[(std::size_t) t] = p.rowOf[(std::size_t) t] = t;
    if (! on || tracks == 0)
        return p;

    std::vector<int> cache (song.automations.size(), -2);
    std::vector<int> wanted ((std::size_t) tracks, -1);
    bool anyWanted = false;
    for (int t = 0; t < tracks; ++t)
        if (const auto o = ownerOfTrack (song, t, cache); o >= 0 && o < tracks)
        {
            wanted[(std::size_t) t] = o;
            anyWanted = true;
        }
    if (! anyWanted)
        return p;
    // one level: a track that is itself drawn as a lane holds no lanes (its would-be children stay where they are)
    for (int t = 0; t < tracks; ++t)
        if (const auto o = wanted[(std::size_t) t]; o >= 0 && wanted[(std::size_t) o] < 0)
        {
            p.owner[(std::size_t) t] = o;
            ++p.children[(std::size_t) o];
        }
    int r = 0;
    for (int t = 0; t < tracks; ++t)
    {
        if (p.owner[(std::size_t) t] >= 0)
            continue;
        p.order[(std::size_t) r++] = t;
        if (p.children[(std::size_t) t] > 0)
            for (int c = 0; c < tracks; ++c) // (its lanes in their own order)
                if (p.owner[(std::size_t) c] == t)
                    p.order[(std::size_t) r++] = c;
    }
    for (int i = 0; i < tracks; ++i)
        p.rowOf[(std::size_t) p.order[(std::size_t) i]] = i;
    return p;
}

// A vertical move of clips by `rows` drawn rows: the track each original lands on, or -1 when the move is refused (a
// lane's clips stay in their lane, and no other clip lands in a lane: the projection would change under the mouse).
inline int movedTrack (const Projection& p, int fromTrack, int rows, int tracks)
{
    if (rows == 0)
        return fromTrack;
    if (p.isChild (fromTrack))
        return -1;
    const auto target = p.row (fromTrack) + rows;
    if (target < 0)
        return -1;
    const auto track = target < tracks ? p.trackAt (target) : target; // (past the rows: the free tracks below, in order)
    return p.isChild (track) ? -1 : track;
}
} // namespace lynx::cliplanes
