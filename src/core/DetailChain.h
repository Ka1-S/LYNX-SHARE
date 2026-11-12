#pragma once

// The Detail View's device chain (round 183a, Clip-launch workflow; docs/analise/REA_rodada183a.md): which channel and
// which mixer insert a Playlist track's chain shows. The reference DAW's tracks own their devices; a LYNX track holds
// clips whose channels play through mixer inserts, so the chain is the track's instrument channel (if it has one) and
// the insert it plays through. Header only, no JUCE; nothing here runs unless the Detail View is open.

#include <algorithm>
#include <vector>

#include "core/MixerOps.h"
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
// Puts `fx` into insert `track`'s chain at `position` (0 = first, the chain's size = last), the Device View's drop between
// two cards. FL's slot places are kept as far as they go: the effect takes a free row between its neighbours; with none
// free, the effects after it move one row down where there is room, else the ones before it move up, else the chain
// is packed from the top. Effects only ever move into empty rows (moveEffectToRow), so automations follow them.
// Returns the new effect's index, or -1 (no such insert, or all 10 slots taken).
inline int insertEffectAt (Song& song, int track, EffectSlot fx, int position)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return -1;
    const auto n = (int) song.mixer[(std::size_t) track].effects.size();
    if (n >= maxEffectSlots)
        return -1;
    position = std::clamp (position, 0, n);
    const auto rows = mixerops::effectRows (song.mixer[(std::size_t) track].effects);
    const auto rowAt = [&] (int i) { return i < 0 ? -1 : i >= n ? maxEffectSlots : rows[(std::size_t) i]; };

    if (rowAt (position) - rowAt (position - 1) < 2)
    {
        auto want = rows;
        bool done = false;
        // the effects from `position` on, one row further down each where needed
        for (int i = position, next = rowAt (position - 1) + 2; i < n; ++i)
        {
            want[(std::size_t) i] = std::max (rows[(std::size_t) i], next);
            next = want[(std::size_t) i] + 1;
        }
        if (position < n && want[(std::size_t) n - 1] < maxEffectSlots)
        {
            for (int i = n - 1; i >= position; --i) // (from the last: each target row is free)
                mixerops::moveEffectToRow (song, track, i, want[(std::size_t) i]);
            done = true;
        }
        if (! done)
        {
            want = rows;
            for (int i = position - 1, next = rowAt (position) - 2; i >= 0; --i)
            {
                want[(std::size_t) i] = std::min (rows[(std::size_t) i], next);
                next = want[(std::size_t) i] - 1;
            }
            if (position > 0 && want[0] >= 0)
            {
                for (int i = 0; i < position; ++i) // (from the first: each target row is free)
                    mixerops::moveEffectToRow (song, track, i, want[(std::size_t) i]);
                done = true;
            }
        }
        if (! done) // packed: rows 0.. with a hole at `position`
        {
            for (int i = 0; i < n; ++i)
                mixerops::moveEffectToRow (song, track, i, i);
            for (int i = n - 1; i >= position; --i)
                mixerops::moveEffectToRow (song, track, i, i + 1);
        }
    }
    const auto after = mixerops::effectRows (song.mixer[(std::size_t) track].effects);
    const auto row = position == 0 ? (n == 0 ? 0 : std::max (0, after[0] - 1)) : after[(std::size_t) position - 1] + 1;
    return mixerops::insertEffectAtRow (song, track, std::move (fx), row);
}

// The Device View's drag of a card: effect `from` dropped before the card at `position` (the chain's size = after the
// last). Returns the effect's new index, or -1 when nothing moves.
inline int moveEffectBefore (Song& song, int track, int from, int position)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return -1;
    const auto n = (int) song.mixer[(std::size_t) track].effects.size();
    if (from < 0 || from >= n)
        return -1;
    const auto to = std::clamp (position > from ? position - 1 : position, 0, n - 1);
    return to != from && mixerops::moveEffectSlot (song, track, from, to) ? to : -1;
}

// Round 190ce: the Device View's Copy / Paste / Duplicate and Ctrl-drag of a card (the reference DAW's Edit commands on
// a selected device; docs/analise/REA_rodada190ce.md §3). A copy is a new device: a new running instance, its sidechain
// sources kept, no automation of its own (automation clips stay with the original).
// Pastes `fx` before the card at effect position `position`; returns its index, or -1 (no such insert, 10 effects).
inline int pasteEffect (Song& song, int track, EffectSlot fx, int position)
{
    fx.instance.renew();
    fx.row = -1;
    return insertEffectAt (song, track, std::move (fx), position);
}

// Duplicate: a copy of effect `index` right after it (Ctrl-drag: `position` = where it is dropped; -1 = right after).
inline int duplicateEffect (Song& song, int track, int index, int position = -1)
{
    if (track < 0 || track >= (int) song.mixer.size() || index < 0 || index >= (int) song.mixer[(std::size_t) track].effects.size())
        return -1;
    auto copy = song.mixer[(std::size_t) track].effects[(std::size_t) index];
    return pasteEffect (song, track, std::move (copy), position < 0 ? index + 1 : position);
}

// Round 190dr: a device dragged out of the Device View onto another track (the reference's "drag a device to another
// track: it moves there; with Ctrl it is copied", docs/analise/REA_rodada190dr.md §3). It lands at the end of `to`'s
// chain. A move keeps the device itself (its running instance, its settings) and what automates or links to it follows
// it to its new place; a copy is a new device, as pasteEffect makes. A side input that would now be the device's own
// insert is dropped. Returns its index in `to`, or -1 (the same insert, no such device, or `to` has 10 effects).
inline int moveEffectToInsert (Song& song, int from, int index, int to, bool copy)
{
    if (from < 0 || from >= (int) song.mixer.size() || to < 0 || to >= (int) song.mixer.size() || from == to)
        return -1;
    if (index < 0 || index >= (int) song.mixer[(std::size_t) from].effects.size())
        return -1;
    const auto n = (int) song.mixer[(std::size_t) to].effects.size();
    if (n >= maxEffectSlots)
        return -1;
    auto fx = song.mixer[(std::size_t) from].effects[(std::size_t) index];
    if (fx.sidechain == to)
        fx.sidechain = -1;
    if (fx.sidechain2 == to)
        fx.sidechain2 = -1;
    if (copy)
        return pasteEffect (song, to, std::move (fx), n);
    fx.row = -1;
    const auto added = insertEffectAt (song, to, std::move (fx), n);
    if (added < 0)
        return -1;
    // the device's automation, MIDI links, modulator routes and controller sources go with it
    song.forEachTarget ([&] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == from && t.slot == index)
        {
            t.index = to;
            t.slot = added;
        }
    });
    for (auto& a : song.automations)
        if (a.source.track == from && a.source.slot == index)
        {
            a.source.track = to;
            a.source.slot = added;
        }
    mixerops::removeEffectKeepRows (song, from, index);
    return added;
}
} // namespace lynx::detailchain
