#pragma once

// Issue #191 point 3 (round 190iu): automation as foldable child lanes under the instrument's track instead of tracks of
// their own. LYNX has had the lanes since round 177b (core/PlaylistOps.h autolanes: Playlist menu › Automation lanes, off
// in the Pattern layout, on in the Clip-launch layout as in Live's Arrangement; each track's lanes fold with its arrow;
// "Automate in lane" on a control; "Move clip into lane" / "Lane to automation clip" on one clip; the .flp export turns
// lanes into clips). What was missing for the issue: the automation clips a project already has (recorded envelopes,
// imported FL projects) stay on tracks of their own. `clipsToLanes` moves, in one undo step, every automation clip
// whose control belongs to a track — the instrument track of its channel, or the track linked to its mixer insert — into
// that track's lane; clips of controls no track owns stay where they are. Playlist menu › "Move automation clips into
// their tracks' lanes" (shown while lanes are on). No JUCE: tested in tests/Workflow190iuTests.inc.

#include <algorithm>
#include <vector>

#include "core/PlaylistOps.h"
#include "core/Song.h"

namespace lynx::laneview
{
// The track that owns `target`: one that already has a lane for it; the instrument track of its channel; a track
// linked to its mixer insert (a channel's own insert counts). -1: none (tempo, the Master, an unlinked insert...).
inline int ownerTrack (const Song& song, const AutomationTarget& target)
{
    const auto& tracks = song.playlistTracks;
    for (int t = 0; t < (int) tracks.size(); ++t)
        for (const auto& l : tracks[(std::size_t) t].lanes)
            if (l.automation >= 0 && l.automation < (int) song.automations.size() && song.automations[(std::size_t) l.automation].target == target)
                return t;
    int channel = -1, insert = -1;
    switch (target.kind)
    {
        case AutomationTargetKind::channelVolume:
        case AutomationTargetKind::channelPan:
        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::channelAdjust:
            channel = target.index;
            if (channel >= 0 && channel < (int) song.channels.size())
                insert = song.channels[(std::size_t) channel].mixerTrack;
            break;
        case AutomationTargetKind::mixerVolume:
        case AutomationTargetKind::mixerPan:
        case AutomationTargetKind::effectParam:
        case AutomationTargetKind::effectMix:
        case AutomationTargetKind::mixerSend:
            insert = target.index;
            break;
        default:
            break;
    }
    if (channel >= 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode == PlaylistTrackMode::instrument && tracks[(std::size_t) t].channel == channel)
                return t;
    if (insert > 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode != PlaylistTrackMode::unassigned && tracks[(std::size_t) t].mixerTrack == insert)
                return t;
    return -1;
}

// The automation clips of the current arrangement `clipsToLanes` would move (indices into Song::playlist).
inline std::vector<int> movableClips (const Song& song)
{
    std::vector<int> out;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind == ClipKind::automation && c.gridSlot < 0 && c.length > 0 && c.automation >= 0 && c.automation < (int) song.automations.size()
            && ! song.automations[(std::size_t) c.automation].isLive() && ownerTrack (song, song.automations[(std::size_t) c.automation].target) >= 0)
            out.push_back (i);
    }
    return out;
}

// Moves them (autolanes::clipToLane each; later clips first, so the indices stay right). An automation whose clips all
// moved goes with them (its curve now lives in the lane): the picker does not list a leftover. Returns how many moved.
inline int clipsToLanes (Song& song)
{
    auto clips = movableClips (song);
    std::vector<int> sources;
    int moved = 0;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        const auto c = song.playlist[(std::size_t) *it];
        const auto owner = ownerTrack (song, song.automations[(std::size_t) c.automation].target);
        if (owner >= 0 && autolanes::clipToLane (song, *it, owner) >= 0)
        {
            ++moved;
            sources.push_back (c.automation);
        }
    }
    std::sort (sources.rbegin(), sources.rend());
    sources.erase (std::unique (sources.begin(), sources.end()), sources.end());
    for (const auto a : sources) // (from the last: the indices before stay valid)
    {
        bool used = false;
        song.forEachClipList ([&] (const std::vector<Clip>& list) {
            for (const auto& k : list)
                used = used || (k.kind == ClipKind::automation && k.automation == a);
        });
        for (const auto& t : song.playlistTracks)
            for (const auto& l : t.lanes)
                used = used || l.automation == a;
        if (! used)
            song.removeAutomation (a);
    }
    return moved;
}
} // namespace lynx::laneview
