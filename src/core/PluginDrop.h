#pragma once

// Issue #152 point 6 (round 190hf): a plugin dragged from the Browser's Plugin database (or the Plugin picker) onto the
// tracks. Header only, no JUCE. Facts: /mnt/e/lynx-work/dnd190hf/DROP-FACTS.md (static REA of FLEngine_x64.dll, FL
// Studio 2026; Ableton Live 12's manual and release notes).
//
// Pattern workflow (FL's Playlist, FL-exact): only the track-header column takes a plugin (the lanes don't).
//  - A generator goes to every selected track when the header under the mouse is one of them, else to that track alone
//    (FL selects it first) [0xe79d60 -> 0x12ebbd0]: an instrument track gets the plugin in place of its channel's
//    generator (channel, routing and track stay); an unassigned or audio track becomes, in place, the instrument track
//    of a new channel on a free mixer track (the "Track mode › Instrument track" routine, 0xe71980).
//  - An effect goes to the mixer track the header plays through (an audio track's own, an instrument track's channel's),
//    in its first free slot [0x12e69a0, 0x12a7350]; on an unassigned track FL refuses it.
// Clip-launch workflow (Live's Session view, Live-exact where the facts are certain): a device dropped on a track's
// column. An instrument on a track with one replaces it, on an empty track makes it a MIDI track (Live's drop area: a
// new track of the matching type), and is refused on an audio track (Live's status-bar message). An audio effect goes
// to the end of the track's chain; on an empty track it makes an audio track holding it.

#include <algorithm>
#include <vector>

#include "core/ArrangeOps.h"
#include "core/DetailChain.h"
#include "core/MixerOps.h"
#include "core/PlaylistOps.h"
#include "core/RackOps.h"
#include "core/Song.h"

namespace lynx::plugindrop
{
enum class Error
{
    none,
    tooMany,               // FL: "Can't load more than 20 instruments at once."
    noMixerTrack,          // FL: "Can't create instrument/audio track" / "No available mixer track was found."
    notInstrumentTrack,    // FL: "This generator can't be used as an instrument track."
    noEffectSlot,          // FL: "There are no available effect slots on the mixer track."
    onlyGenerators,        // FL: an effect on an unassigned header, "Only generators can be used as instrument tracks."
    noTrack,               // nothing to drop on (no track there)
    onlyAudioEffects,      // Live: an instrument on an audio track (status bar)
    noInsert               // Live: the track plays through several inserts (or none it owns)
};

constexpr int maxInstrumentsAtOnce = 20; // FL 0x12ebbd0
constexpr int askAboveInstruments = 4;   // FL asks "Are you sure you want to load N plugins at once?" above this

struct Result
{
    Error error = Error::none;
    std::vector<int> tracks;   // the tracks that took the plugin
    std::vector<int> channels; // their channels (new or replaced), in the same order
    int insert = -1;           // an effect: the mixer track it went to
    int effect = -1;           // an effect: its index in that insert's chain
    bool replaced = false;     // a generator replaced an instrument track's (all of them, FL's "replace instrument track")

    bool ok() const noexcept { return error == Error::none; }
};

// FL: the tracks a generator dropped on header `target` goes to: the selection when `target` is in it, else `target`
// alone (FL makes it the only selected track first).
inline std::vector<int> generatorTargets (int target, const std::vector<int>& selected)
{
    if (target < 0)
        return {};
    if (std::find (selected.begin(), selected.end(), target) == selected.end())
        return { target };
    auto tracks = selected;
    std::sort (tracks.begin(), tracks.end());
    tracks.erase (std::unique (tracks.begin(), tracks.end()), tracks.end());
    return tracks;
}

inline bool isInstrumentTrack (const Song& song, int track)
{
    return track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::instrument
           && song.playlistTracks[(std::size_t) track].channel >= 0 && song.playlistTracks[(std::size_t) track].channel < (int) song.channels.size();
}

namespace detail
{
// FL 0xe71980 on one unassigned / audio track: a new channel of `generator` and that track its instrument track
inline Error makeInstrumentTrackOf (Song& song, const ChannelDef& generator, int track, int& channel)
{
    if (generator.instrument == InstrumentKind::audioClip || generator.instrument == InstrumentKind::controller)
        return Error::notInstrumentTrack;
    channel = trackmodes::addInstrumentTrack (song, generator, track);
    return channel >= 0 ? Error::none : Error::noMixerTrack;
}

// an instrument track's channel gets `generator` (FL's "replace instrument track"); a channel left without a mixer
// track of its own gets a free one (FL's "auto assign mixer track")
inline bool replaceInstrument (Song& song, int channel, ChannelDef generator)
{
    if (! replaceGenerator (song, channel, std::move (generator)))
        return false;
    auto& c = song.channels[(std::size_t) channel];
    if (c.mixerTrack <= 0 || c.mixerTrack >= (int) song.mixer.size())
        if (const auto free = trackmodes::freeMixerTrack (song); free > 0)
            c.mixerTrack = free;
    return true;
}

// FL 0x12a7350: the first empty slot (row 0..9) of insert `insert`, -1 when all ten are taken
inline int firstFreeRow (const Song& song, int insert)
{
    const auto rows = mixerops::effectRows (song.mixer[(std::size_t) insert].effects);
    for (int row = 0; row < maxEffectSlots; ++row)
        if (std::find (rows.begin(), rows.end(), row) == rows.end())
            return row;
    return -1;
}
} // namespace detail

// FL (0x12ebbd0): one generator onto Playlist tracks `tracks` (generatorTargets). More than 20: tooMany and nothing
// changes. Each instrument track gets the plugin in place of its channel's generator; each other track becomes the
// instrument track of a new channel. Stops at the first track that fails (the ones before keep theirs, as in FL).
inline Result generatorOnTracks (Song& song, const ChannelDef& generator, const std::vector<int>& tracks)
{
    Result r;
    if (tracks.empty())
    {
        r.error = Error::noTrack;
        return r;
    }
    if ((int) tracks.size() > maxInstrumentsAtOnce)
    {
        r.error = Error::tooMany;
        return r;
    }
    r.replaced = true;
    for (const auto track : tracks)
    {
        int channel = -1;
        if (isInstrumentTrack (song, track))
        {
            channel = song.playlistTracks[(std::size_t) track].channel;
            if (! detail::replaceInstrument (song, channel, generator))
            {
                r.error = Error::noTrack;
                break;
            }
        }
        else
        {
            r.replaced = false;
            if (const auto e = detail::makeInstrumentTrackOf (song, generator, track, channel); e != Error::none)
            {
                r.error = e;
                break;
            }
        }
        r.tracks.push_back (track);
        r.channels.push_back (channel);
    }
    if (r.tracks.empty())
        r.replaced = false;
    return r;
}

// FL (0xe79d60 / 0x12e69a0): one effect onto Playlist track `track`'s header: into the first free slot of the mixer
// track it plays through (an audio track's own, an instrument track's channel's; never the Master).
inline Result effectOnTrack (Song& song, EffectSlot fx, int track)
{
    Result r;
    if (track < 0 || track >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::unassigned)
    {
        r.error = Error::onlyGenerators; // (FL sends it down the generator path, which refuses it)
        return r;
    }
    const auto insert = arrange::insertOf (song, track);
    if (insert <= 0)
    {
        r.error = Error::noTrack;
        return r;
    }
    const auto row = detail::firstFreeRow (song, insert);
    const auto index = row >= 0 ? mixerops::insertEffectAtRow (song, insert, std::move (fx), row) : -1;
    if (index < 0)
    {
        r.error = Error::noEffectSlot;
        return r;
    }
    r.insert = insert;
    r.effect = index;
    r.tracks.push_back (track);
    return r;
}

// Live: does the track hold audio (an audio track, or audio clips and no instrument)?
inline bool isAudioTrack (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::audio)
        return true;
    const auto chain = detailchain::forTrack (song, track);
    return chain.channel < 0 && ! chain.channels.empty();
}

// Live: an instrument dropped on a Session track (a grid column). Its instrument replaced (the track's chain keeps its
// effects), an empty track made a MIDI track of a new channel (Live's drop area: a new MIDI track), an audio track
// refused (onlyAudioEffects).
inline Result instrumentOnSessionTrack (Song& song, const ChannelDef& generator, int track)
{
    Result r;
    if (track < 0 || track >= 500)
    {
        r.error = Error::noTrack;
        return r;
    }
    const auto chain = detailchain::forTrack (song, track);
    if (chain.channel >= 0 && song.channels[(std::size_t) chain.channel].instrument != InstrumentKind::audioClip)
    {
        if (generator.instrument == InstrumentKind::audioClip || generator.instrument == InstrumentKind::controller)
        {
            r.error = Error::notInstrumentTrack;
            return r;
        }
        if (! detail::replaceInstrument (song, chain.channel, generator))
        {
            r.error = Error::noTrack;
            return r;
        }
        r.replaced = true;
        r.channels.push_back (chain.channel);
        r.tracks.push_back (track);
        return r;
    }
    if (isAudioTrack (song, track))
    {
        r.error = Error::onlyAudioEffects;
        return r;
    }
    int channel = -1;
    if (const auto e = detail::makeInstrumentTrackOf (song, generator, track, channel); e != Error::none)
    {
        r.error = e;
        return r;
    }
    r.channels.push_back (channel);
    r.tracks.push_back (track);
    return r;
}

// Live: an audio effect dropped on a Session track: at the end of its chain (the insert it plays through). An empty
// track becomes an audio track on a free mixer track holding it (Live's drop area: a new audio track).
inline Result effectOnSessionTrack (Song& song, EffectSlot fx, int track)
{
    Result r;
    if (track < 0 || track >= 500)
    {
        r.error = Error::noTrack;
        return r;
    }
    auto chain = detailchain::forTrack (song, track);
    if (chain.empty() && chain.channels.empty())
    {
        const auto mixer = trackmodes::freeMixerTrack (song);
        if (mixer <= 0 || trackmodes::assignAudioTracks (song, { track }, mixer) != 1)
        {
            r.error = Error::noMixerTrack;
            return r;
        }
        chain = detailchain::forTrack (song, track);
    }
    const auto insert = chain.insert;
    if (insert <= 0 || insert >= (int) song.mixer.size())
    {
        r.error = Error::noInsert;
        return r;
    }
    const auto index = detailchain::insertEffectAt (song, insert, std::move (fx), (int) song.mixer[(std::size_t) insert].effects.size());
    if (index < 0)
    {
        r.error = Error::noEffectSlot;
        return r;
    }
    r.insert = insert;
    r.effect = index;
    r.tracks.push_back (track);
    return r;
}
} // namespace lynx::plugindrop
