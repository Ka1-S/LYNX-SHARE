#pragma once

// Round qafix190kq (QA-9 of 0.1.30): why an export came out silent. `--export` of Live's lesson sets "Playing Software
// Instruments" / "Recording Audio" (sets with no clip at all) ended in "result=ERROR" with no message; the CLI's result
// line now says which of these it is.

#include <string>

#include "core/Song.h"

namespace lynx::exportsilence
{
inline std::string reason (const Song& song, bool patternOnly)
{
    auto notes = false;
    for (const auto& p : song.patterns)
        notes = notes || ! p.notes.empty();
    const auto gridClips = song.clipGrid && ! song.clipGrid->clips.empty();
    if (song.playlist.empty() && ! notes && ! gridClips)
        return "nothing to render: the project holds no clip and no note (an empty set or template)";
    if (! patternOnly && song.playlist.empty())
        return gridClips ? "nothing to render: the song (Playlist / arrangement) is empty; the project's clips are only in the clip grid (Session view)"
                         : "nothing to render: the song (Playlist / arrangement) is empty; the project's notes are only in its patterns (--pattern renders the current one)";
    return "the render is silent (its peak stays below -60 dBFS): every clip is muted or silent, or plays through a muted or silent track";
}
} // namespace lynx::exportsilence
