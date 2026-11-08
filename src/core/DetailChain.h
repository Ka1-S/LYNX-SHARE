#pragma once

// The Detail View's device chain (round 183a, Clip-launch workflow; docs/analise/REA_rodada183a.md): which channel and
// which mixer insert a Playlist track's chain shows. The reference DAW's tracks own their devices; a LYNX track holds
// clips whose channels play through mixer inserts, so the chain is the track's instrument channel (if it has one) and
// the insert it plays through. Header only, no JUCE; nothing here runs unless the Detail View is open.

#include <algorithm>
#include <vector>

#include "core/Song.h"

namespace lynx::detailchain
{
struct Chain
{
    int channel = -1;          // the instrument shown first (-1: none, an audio track or several channels)
    int insert = -1;           // the mixer insert whose effects follow (-1: none; 0 = Master)
    std::vector<int> channels; // every channel the track plays, ascending (the picker's list)
    std::vector<int> inserts;  // every insert those channels play through, ascending
    bool fromTrackMode = false; // decided by the track's own mode (FL's "Track mode"), not by its clips

    bool empty() const noexcept { return channel < 0 && insert < 0; }
};

// The channels a clip plays: an audio clip its channel, a pattern clip each channel with notes in the pattern.
inline void channelsOfClip (const Song& song, const Clip& clip, std::vector<int>& out)
{
    if (clip.kind == ClipKind::audio)
    {
        if (clip.channel >= 0 && clip.channel < (int) song.channels.size())
            out.push_back (clip.channel);
        return;
    }
    if (clip.kind != ClipKind::pattern || clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
        return;
    for (const auto& n : song.patterns[(std::size_t) clip.pattern].notes)
        if (n.channel >= 0 && n.channel < (int) song.channels.size())
            out.push_back (n.channel);
}

inline int insertOfChannel (const Song& song, int channel) noexcept
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return -1;
    const auto t = song.channels[(std::size_t) channel].mixerTrack;
    return t >= 0 && t < (int) song.mixer.size() ? t : -1;
}

// The chain of Playlist track `track`. Rules, in order:
//  1. an instrument track (FL's track mode) shows its channel, then the insert the channel plays through (else the
//     track's linked insert);
//  2. an audio track shows its linked insert;
//  3. otherwise the clips decide (the current Playlist and the clip grid): one channel = that channel and its insert;
//     several channels on one insert = that insert alone; several inserts = nothing chosen (the view offers the list).
inline Chain forTrack (const Song& song, int track)
{
    Chain c;
    if (track < 0)
        return c;
    const auto validInsert = [&] (int t) { return t >= 0 && t < (int) song.mixer.size() ? t : -1; };

    if (track < (int) song.playlistTracks.size())
    {
        const auto& pt = song.playlistTracks[(std::size_t) track];
        if (pt.mode == PlaylistTrackMode::instrument && pt.channel >= 0 && pt.channel < (int) song.channels.size())
        {
            c.channel = pt.channel;
            c.insert = insertOfChannel (song, pt.channel);
            if (c.insert < 0)
                c.insert = validInsert (pt.mixerTrack);
            c.fromTrackMode = true;
        }
        else if (pt.mode == PlaylistTrackMode::audio && validInsert (pt.mixerTrack) >= 0)
        {
            c.insert = pt.mixerTrack;
            c.fromTrackMode = true;
        }
    }

    const auto scan = [&] (const std::vector<Clip>& clips) {
        for (const auto& clip : clips)
            if (clip.track == track && clip.length > 0)
                channelsOfClip (song, clip, c.channels);
    };
    scan (song.playlist);
    if (song.clipGrid)
        scan (song.clipGrid->clips);
    std::sort (c.channels.begin(), c.channels.end());
    c.channels.erase (std::unique (c.channels.begin(), c.channels.end()), c.channels.end());
    for (const auto ch : c.channels)
        if (const auto t = insertOfChannel (song, ch); t >= 0)
            c.inserts.push_back (t);
    std::sort (c.inserts.begin(), c.inserts.end());
    c.inserts.erase (std::unique (c.inserts.begin(), c.inserts.end()), c.inserts.end());

    if (c.fromTrackMode)
        return c;
    if (c.channels.size() == 1)
    {
        c.channel = c.channels.front();
        c.insert = insertOfChannel (song, c.channel);
    }
    else if (c.inserts.size() == 1)
        c.insert = c.inserts.front();
    return c;
}
} // namespace lynx::detailchain
