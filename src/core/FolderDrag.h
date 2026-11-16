#pragma once

// Round 190jf (slice B17, LYNX's own; FL moves Playlist tracks with Shift+wheel / Move up / down and groups them by
// dragging past the top of a row, which LYNX keeps): Alt+drag on a Playlist track header moves the track with its
// folder. Where it lands decides its folder: dropped among a folder's tracks it joins that folder; dropped after a
// folder's last track it stays outside; dragging to the right goes one level deeper per `stepPx` (as far as the place
// allows). Plain functions, no JUCE (core/NoteEdit.h › moveFolder does the move).

#include <algorithm>

#include "core/NoteEdit.h"

namespace lynx::folderdrag
{
constexpr int stepPx = 24; // a level per 24 px to the right of where the drag started

struct Drop
{
    int before = -1; // the old index the track will start at (moveFolder's `before`); −1 = nowhere (no move)
    int depth = 0;
    bool operator== (const Drop&) const = default;
};

// `rowUnder`: the row under the mouse (count = below the last row); `lowerHalf`: the mouse in that row's lower half;
// `dx`: the mouse's x minus the x where the drag started. `count`: the rows shown.
inline Drop dropAt (const Song& song, int track, int rowUnder, bool lowerHalf, int dx, int count)
{
    if (track < 0 || track >= count)
        return {};
    const auto end = noteedit::folderEnd (song, track);
    auto before = std::clamp (lowerHalf ? rowUnder + 1 : rowUnder, 0, count);
    // a collapsed folder's thin rows are one place: dropping on them lands after the folder
    if (before < count && noteedit::isFoldedAway (song, before))
        for (auto p = noteedit::folderParent (song, before); p >= 0; p = noteedit::folderParent (song, p))
            if (p < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) p].collapsed)
                before = std::max (before, noteedit::folderEnd (song, p) + 1);
    if (before > track && before <= end + 1)
        before = track; // (inside itself: it stays where it is)
    // the folder it joins: the row below the place, when that row is in a folder (dropped among its tracks)
    auto below = before;
    while (below >= track && below <= end) // (skip the moving block itself)
        ++below;
    // (left in place, the track keeps its level and only the sideways drag changes it)
    const auto base = before == track ? noteedit::folderDepth (song, track) : below < count ? noteedit::folderDepth (song, below) : 0;
    const auto steps = dx / stepPx;
    const auto depth = std::clamp (base + steps, 0, noteedit::maxFolderDepthAt (song, before, track));
    return { before, depth };
}

// Whether the drop changes anything (place or depth).
inline bool changes (const Song& song, int track, const Drop& drop)
{
    return drop.before >= 0 && (drop.before != track || drop.depth != noteedit::folderDepth (song, track));
}
} // namespace lynx::folderdrag
