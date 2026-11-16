#pragma once

// Issue #191 point 6 (round 190iu): the recording takes (audio clips) and the recorded envelopes (automation clips) the
// picker panel and the Browser's Current project list can be deleted one by one, and the unused ones all at once.
// FL's picker lists the same three kinds; its Channel rack deletes an audio clip's channel and the Playlist menu's
// Tools › "Purge unused audio clips" removes the unused ones (both already in LYNX). Deleting an automation clip from
// the list and "Delete unused automation clips" are LYNX's own. Every delete is one undo step; files on disk stay.
// Plain edits for AudioEngine::editSong. No JUCE: tested in tests/Workflow190iuTests.inc.

#include <algorithm>
#include <vector>

#include "core/Song.h"

namespace lynx::takecleanup
{
// An audio clip (a take) with its channel and every clip of it, in every arrangement and the clip grid. False when
// `channel` is not an audio clip channel.
inline bool deleteAudioClip (Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip)
        return false;
    song.removeChannel (channel);
    return true;
}

// An automation (a recorded envelope) with its clips and its lane.
inline bool deleteAutomation (Song& song, int index)
{
    if (index < 0 || index >= (int) song.automations.size())
        return false;
    song.removeAutomation (index);
    return true;
}

// How many clips (every arrangement and the clip grid) and lanes play automation `index`.
inline int automationUses (const Song& song, int index)
{
    int uses = 0;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        uses += (int) std::count_if (clips.begin(), clips.end(), [index] (const Clip& c) { return c.kind == ClipKind::automation && c.automation == index; });
    });
    for (const auto& t : song.playlistTracks)
        uses += (int) std::count_if (t.lanes.begin(), t.lanes.end(), [index] (const AutomationLane& l) { return l.automation == index; });
    return uses;
}

// The automation clips nothing places: no clip, no lane. A live link (an internal controller) plays without clips and
// is never "unused".
inline std::vector<int> unusedAutomations (const Song& song)
{
    std::vector<int> out;
    for (int a = 0; a < (int) song.automations.size(); ++a)
        if (! song.automations[(std::size_t) a].isLive() && automationUses (song, a) == 0)
            out.push_back (a);
    return out;
}

inline int deleteUnusedAutomations (Song& song)
{
    auto list = unusedAutomations (song);
    std::sort (list.rbegin(), list.rend()); // (from the last: the indices before stay valid)
    for (const auto a : list)
        song.removeAutomation (a);
    return (int) list.size();
}
} // namespace lynx::takecleanup
