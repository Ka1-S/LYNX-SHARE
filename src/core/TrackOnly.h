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
#include <utility>
#include <vector>

#include "core/LayoutStyle.h"
#include "core/PlaylistOps.h"
#include "core/RackOps.h"
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

// Round 190qu (Discord 11-24, sidthenugget): "Convert project to Track-only" — a pattern project into 1 track = 1 instrument,
// in one edit. Every pattern clip of the current arrangement whose pattern holds notes is split by channel with FL's own
// Split by channel (splitPatternByChannel, the parts named "<pattern> - <channel>"; a pattern of one channel is not split)
// and each part goes, at the clip's place (start, length, offset, mute), onto its channel's instrument track — made when
// the channel has none (assignToNewInstrumentTrack, as switching the mode on offers). Clips of one pattern keep sharing
// their parts (a part is one pattern, as FL's split). A clip on a muted track stays silent (muted clip). The patterns'
// events, markers and automation clip steps stay with the first part. Clips already on the instrument track of their
// one channel, empty patterns (events only), audio and automation clips, the clip grid and the other arrangements are
// left as they are. The tracks the clips leave stay (empty), as FL leaves them.
struct ConvertReport
{
    int clipsMoved = 0;    // pattern clips that went onto instrument tracks (each part counted)
    int patternsSplit = 0; // patterns split by channel
    int tracksMade = 0;    // instrument tracks made
};
inline ConvertReport convertProject (Song& song)
{
    ConvertReport report;
    const auto channelsOf = [&song] (int pattern) {
        std::vector<int> out;
        if (pattern >= 0 && pattern < (int) song.patterns.size())
            for (const auto& n : song.patterns[(std::size_t) pattern].notes)
                if (std::find (out.begin(), out.end(), n.channel) == out.end())
                    out.push_back (n.channel);
        std::sort (out.begin(), out.end());
        return out;
    };
    const auto homeOf = [&song, &report] (int channel) {
        if (channel < 0 || channel >= (int) song.channels.size() || ! wantsTrack (song, channel))
            return -1;
        if (const auto tracks = trackmodes::instrumentTracksOf (song, channel); ! tracks.empty())
            return tracks.front();
        const auto made = trackmodes::assignToNewInstrumentTrack (song, channel);
        if (made >= 0)
            ++report.tracksMade;
        return made;
    };

    // the patterns the arrangement's clips play, split once each (from the last: the indices before stay valid)
    std::vector<int> used;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.gridSlot < 0 && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            used.push_back (c.pattern);
    std::sort (used.begin(), used.end());
    used.erase (std::unique (used.begin(), used.end()), used.end());
    // a pattern another clip list plays too (another arrangement, the clip grid, a track version) is left whole: splitting
    // it would take its other channels out of those clips
    {
        std::vector<char> elsewhere (song.patterns.size(), 0);
        song.forEachClipList ([&song, &elsewhere] (const std::vector<Clip>& list) {
            if (&list != &song.playlist)
                for (const auto& c : list)
                    if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) elsewhere.size())
                        elsewhere[(std::size_t) c.pattern] = 1;
        });
        used.erase (std::remove_if (used.begin(), used.end(), [&elsewhere] (int p) { return elsewhere[(std::size_t) p] != 0; }), used.end());
    }
    std::vector<std::vector<std::pair<int, int>>> parts (song.patterns.size()); // per original pattern: (channel, pattern)
    for (auto it = used.rbegin(); it != used.rend(); ++it)
    {
        const auto channels = channelsOf (*it);
        if (channels.empty())
            continue;
        if (channels.size() > 1 && splitPatternByChannel (song, *it, true) > 0)
            ++report.patternsSplit;
        auto& list = parts[(std::size_t) *it];
        for (std::size_t k = 0; k < channels.size(); ++k)
            list.push_back ({ channels[k], *it + (int) k });
    }
    // (the patterns after a split one moved down: map the old indices to the new ones)
    std::vector<int> shift (parts.size(), 0);
    for (std::size_t p = 0, added = 0; p < parts.size(); ++p)
    {
        shift[p] = (int) added;
        if (! parts[p].empty())
            added += parts[p].size() - 1;
    }
    for (std::size_t p = 0; p < parts.size(); ++p)
        for (auto& part : parts[p])
            part.second += shift[p];

    // the clips onto their channels' tracks
    std::vector<Clip> kept, placed;
    const auto clips = song.playlist;
    for (const auto& c : clips)
    {
        // (a pattern clip's pattern index is already the moved one; find its original by the split's parts)
        const std::vector<std::pair<int, int>>* list = nullptr;
        if (c.kind == ClipKind::pattern && c.gridSlot < 0)
            for (std::size_t p = 0; p < parts.size() && list == nullptr; ++p)
                if (! parts[p].empty() && parts[p].front().second == c.pattern)
                    list = &parts[p];
        if (list == nullptr)
        {
            kept.push_back (c);
            continue;
        }
        const auto silent = c.muted || song.isTrackMuted (c.track);
        // already home: one channel, on that channel's instrument track
        if (list->size() == 1 && c.track >= 0 && c.track < (int) song.playlistTracks.size()
            && song.playlistTracks[(std::size_t) c.track].mode == PlaylistTrackMode::instrument && song.playlistTracks[(std::size_t) c.track].channel == list->front().first)
        {
            kept.push_back (c);
            continue;
        }
        std::vector<Clip> mine;
        bool all = true;
        for (const auto& [channel, pattern] : *list)
        {
            const auto home = homeOf (channel);
            if (home < 0)
            {
                all = false;
                break;
            }
            auto moved = c;
            moved.pattern = pattern;
            moved.track = home;
            moved.muted = silent;
            mine.push_back (moved);
        }
        if (! all) // (a channel that cannot have a track: the clip stays where it was, every part in it)
        {
            for (const auto& [channel, pattern] : *list)
            {
                auto stay = c;
                stay.pattern = pattern;
                kept.push_back (stay);
            }
            continue;
        }
        report.clipsMoved += (int) mine.size();
        placed.insert (placed.end(), mine.begin(), mine.end());
    }
    kept.insert (kept.end(), placed.begin(), placed.end());
    std::stable_sort (kept.begin(), kept.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    song.playlist = std::move (kept);
    // every instrument a track of its own (the mode's rule), and its tracks draw new clips
    report.tracksMade += assignAll (song);
    for (auto& t : song.playlistTracks)
        if (t.mode == PlaylistTrackMode::instrument)
            t.ownClips = true;
    return report;
}
} // namespace lynx::trackonly
