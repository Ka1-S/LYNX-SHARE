#pragma once

// Issue #191 point 5 (round 190iu), a LYNX extra: Track-only mode. The Pattern (FL) layout without the Channel rack:
// 1 Playlist track = 1 instrument, for people who work linearly and never use patterns as building blocks.
//
// It is built from FL's own pieces, so nothing new has to be learnt and the files stay FL's:
//  - FL's instrument tracks (Playlist Track mode › Instrument track, FL 20.1+): a track that belongs to one channel and
//    its mixer insert. The pencil on it places that channel's own pattern (named after it, one per instrument: FL's rule,
//    trackmodes::instrumentTrackPattern), and the pattern's Piano roll is that channel's. So the patterns are separated per
//    track by construction (point 5a), with no pattern holding several instruments by mistake.
//  - every instrument added while the mode is on (Browser, plugin picker, quick add, a MIDI file, a sample opened in a
//    new channel...) gets a new instrument track of its own (FL's Channel rack menu › "Assign to new instrument track"),
//    after the edit that added it, in the same undo step (`follow`);
//  - the Channel rack panel stays hidden (its key and button say why); the instrument's own window still opens.
// Why not a fourth layout or a Simple variant: the Simple layout already is 1 track = 1 instrument, with BandLab /
// FL Mobile looks and its own dock; Track-only mode is for people who want FL's Playlist, Piano roll and Mixer as they
// are, minus the rack. Off by default; only in the Pattern layout (Clip-launch tracks already own their instruments;
// Simple has no rack). An app setting (ui-options.txt "trackOnly").
//
// Plain edits for AudioEngine::editSong. No JUCE: tested in tests/Workflow190iuTests.inc.

#include <algorithm>
#include <atomic>
#include <cstddef>

#include "core/LayoutStyle.h"
#include "core/PlaylistOps.h"
#include "core/Song.h"

namespace lynx::trackonly
{
inline std::atomic<bool>& enabledFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool enabled() noexcept { return enabledFlag().load (std::memory_order_relaxed); }
inline void setEnabled (bool on) noexcept { enabledFlag().store (on, std::memory_order_relaxed); }
// on, and the layout shown is the Pattern one
inline bool active() noexcept { return enabled() && layoutstyle::current() == layoutstyle::Style::pattern; }

// Whether channel `c` is an instrument the mode gives a track (generators; not audio clips, automation or controllers).
inline bool wantsTrack (const Song& song, int c)
{
    return trackmodes::canBeInstrumentTrack (song, c) && song.channels[(std::size_t) c].instrument != InstrumentKind::controller;
}

// After an edit: the channels from `channelsBefore` on (an edit appends the channels it adds) that are instruments with
// no instrument track get one each, on the first free track. Returns how many tracks were assigned.
inline int follow (Song& song, std::size_t channelsBefore)
{
    int made = 0;
    for (auto c = (int) channelsBefore; c < (int) song.channels.size(); ++c)
        if (wantsTrack (song, c) && trackmodes::instrumentTracksOf (song, c).empty() && trackmodes::assignToNewInstrumentTrack (song, c) >= 0)
            ++made;
    return made;
}

// Round 190qp (Discord, 0.1.31: "double-clicking an empty clip opens the rack"): the channel whose Piano roll a
// double-click on Playlist clip `clip` opens in Track-only mode and in the Simple layout. A pattern clip on an
// instrument track is that instrument's part, so it opens in the instrument's Piano roll even while it is empty — FL's
// "Auto" rule (Piano roll when the pattern has notes, else the Channel rack) would open the rack, which Track-only mode
// hides and the Simple layout doesn't have. -1 when the clip is not a pattern clip on an instrument track, or when its
// pattern holds notes of another channel (an imported pattern played on the track: the usual rule decides).
// The Pattern layout with Track-only mode off keeps FL's rule (the caller asks only in those two cases).
inline int pianoRollChannelOf (const Song& song, int clip)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return -1;
    const auto& c = song.playlist[(std::size_t) clip];
    if (c.kind != ClipKind::pattern || c.track < 0 || c.track >= (int) song.playlistTracks.size() || c.pattern < 0
        || c.pattern >= (int) song.patterns.size())
        return -1;
    const auto& track = song.playlistTracks[(std::size_t) c.track];
    const auto channel = track.channel;
    if (track.mode != PlaylistTrackMode::instrument || channel < 0 || channel >= (int) song.channels.size())
        return -1;
    const auto& notes = song.patterns[(std::size_t) c.pattern].notes;
    return std::all_of (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }) ? channel : -1;
}

// "Give every instrument its own track" (offered when the mode is switched on for a project made with the rack): each
// instrument channel without an instrument track gets one. Returns how many.
inline int assignAll (Song& song) { return follow (song, 0); }
// The instruments that have no instrument track yet (what assignAll would give one).
inline int withoutTrack (const Song& song)
{
    int n = 0;
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (wantsTrack (song, c) && trackmodes::instrumentTracksOf (song, c).empty())
            ++n;
    return n;
}
} // namespace lynx::trackonly
