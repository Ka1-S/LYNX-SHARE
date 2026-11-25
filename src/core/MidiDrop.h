#pragma once

// Issue #191 point 2 (round 190iu): a MIDI file dropped straight on the Playlist (Pattern layout), on the Clip-launch
// layout's arrangement or on a clip-grid slot becomes a clip, with no trip through the Piano roll. The same path takes
// the temporary .mid files that drum and instrument plugins hand over when their MIDI is dragged out of their window
// (an ordinary file drop for the host).
//
// - Playlist (FL's model: a pattern holds several channels; FL itself takes MIDI only on the Channel rack and the Piano
//   roll, docs/analise/REA_rodada190iu.md): one new pattern named after the file at the drop. On an instrument track
//   every note goes to the track's channel; elsewhere each MIDI track with notes gets a new channel (LYNX Synth, as
//   File › Import › MIDI file makes them), all in that one pattern. The track's mode is not changed.
// - Clip-launch arrangement and clip-grid slots (Live's model: one instrument per track): the notes go to the track's
//   instrument; a track without one becomes an instrument track of a new LYNX Synth channel, named after the file. In
//   the arrangement each further MIDI track of the file goes on its own track below (Live puts a multi-track file on
//   several tracks); a slot takes all of them merged. An audio track takes no MIDI (Live refuses it too).
//
// FL 2026 takes MIDI files on the Channel rack, the Piano roll and the desktop only (its manual; a drop on the Playlist
// grid is not documented), so the Playlist drop in the Pattern layout is a LYNX extra, off by default (Settings › LYNX
// extras, the Playlist menu; ui-options.txt "midiDrop"). Live takes them in both its views, so the Clip-launch layout
// always does.
//
// The file's timing is kept (no realign); its tempo and time signature are not applied (a drop never retimes the song).
// Plain edits for AudioEngine::editSong (one undo step). No JUCE: tested in tests/Workflow190iuTests.inc.

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "core/ClipLauncher.h"
#include "core/DetailChain.h"
#include "core/MidiImport.h"
#include "core/PlaylistOps.h"
#include "core/Song.h"
#include "core/TrackOnly.h"

namespace lynx::mididrop
{
// (on by default since 0.1.31.2, the owner's choice on #287: FL ignores a .mid dropped on its Playlist, so no FL gesture
// changes; Settings › LYNX extras turns it off)
inline std::atomic<bool>& playlistDropFlag() noexcept
{
    static std::atomic<bool> on { true };
    return on;
}
inline bool playlistDrop() noexcept { return playlistDropFlag().load (std::memory_order_relaxed); }
inline void setPlaylistDrop (bool on) noexcept { playlistDropFlag().store (on, std::memory_order_relaxed); }
// whether the Playlist takes MIDI files: always in the Live-style arrangement, in the Pattern layout with the extra on
// (and in Track-only mode, core/TrackOnly.h, where the Playlist works as the Live-style arrangement does)
inline bool playlistTakes (bool liveStyle) noexcept { return liveStyle || trackonly::active() || playlistDrop(); }

struct Result
{
    bool ok = false;
    std::vector<int> patterns, channels, tracks; // what was made / used, in order
    std::string error;                         // "no notes", "audio track", "no track"
};

// The channel LYNX makes for a MIDI track with no channel to go to: LYNX Synth, as the MIDI import makes them.
inline ChannelDef newChannel (const std::string& name)
{
    ChannelDef c;
    c.name = name;
    c.instrument = InstrumentKind::synth;
    c.volume = 0.6f;
    return c;
}

// The instrument channel that plays track `track`, -1 for none: an instrument track's channel; in the Live-style
// views (`liveStyle`) also the single instrument channel its clips play (detailchain::forTrack).
inline int channelUnder (const Song& song, int track, bool liveStyle)
{
    if (track < 0)
        return -1;
    if (track < (int) song.playlistTracks.size())
    {
        const auto& t = song.playlistTracks[(std::size_t) track];
        if (t.mode == PlaylistTrackMode::instrument && t.channel >= 0 && t.channel < (int) song.channels.size())
            return t.channel;
    }
    if (! liveStyle)
        return -1;
    const auto chain = detailchain::forTrack (song, track);
    if (chain.channel >= 0 && chain.channel < (int) song.channels.size()
        && song.channels[(std::size_t) chain.channel].instrument != InstrumentKind::audioClip
        && song.channels[(std::size_t) chain.channel].instrument != InstrumentKind::controller)
        return chain.channel;
    return -1;
}

inline bool isAudioTrack (const Song& song, int track)
{
    return track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::audio;
}

// The MIDI tracks with notes (LYNX's import groups: one per file track; all channels of a track together).
inline std::vector<midiimport::Group> noteGroups (const midiimport::ParsedMidi& midi)
{
    auto gs = midiimport::groups (midi, midiimport::Options {});
    gs.erase (std::remove_if (gs.begin(), gs.end(), [] (const midiimport::Group& g) { return g.notes.empty(); }), gs.end());
    return gs;
}

namespace detail
{
inline int addPattern (Song& song, const std::string& name, std::vector<Note> notes)
{
    Pattern p;
    p.name = name;
    p.notes = std::move (notes);
    p.sortNotes();
    song.patterns.push_back (std::move (p));
    return (int) song.patterns.size() - 1;
}

inline void appendNotes (std::vector<Note>& out, const std::vector<Note>& notes, int channel)
{
    for (auto n : notes)
    {
        n.channel = channel;
        out.push_back (n);
    }
}

inline Clip patternClip (const Song& song, int pattern, int track, std::int64_t start)
{
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = pattern;
    clip.track = track;
    clip.start = std::max<std::int64_t> (0, start);
    clip.length = std::max<std::int64_t> (ppq, song.patterns[(std::size_t) pattern].effectiveLength (song.meter()));
    return clip;
}

// a channel's / track's name: the file track's own name, else the file's (numbered when the file has several)
inline std::string groupName (const midiimport::ParsedMidi& midi, const midiimport::Group& g, const std::string& fileName, std::size_t index,
                              std::size_t count)
{
    if (g.track >= 0 && g.track < (int) midi.trackNames.size() && ! midi.trackNames[(std::size_t) g.track].empty())
        return midi.trackNames[(std::size_t) g.track];
    return count <= 1 ? fileName : fileName + " " + std::to_string (index + 1);
}

// the channel of a Live-style track, made when it has none (an instrument track of a new LYNX Synth); -1: an audio
// track, or no room
inline int instrumentOf (Song& song, int track, const std::string& name)
{
    if (const auto c = channelUnder (song, track, true); c >= 0)
        return c;
    if (isAudioTrack (song, track))
        return -1;
    return trackmodes::addInstrumentTrack (song, newChannel (name), track);
}

// a track the next MIDI track of a file can take in the Live-style arrangement: `from` and down, the first one with no
// clips that is not an audio track
inline int nextTakingTrack (const Song& song, int from)
{
    for (int t = std::max (0, from); t < 500; ++t)
    {
        if (isAudioTrack (song, t))
            continue;
        const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [t] (const Clip& c) { return c.track == t; });
        const auto instrument = t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::instrument;
        if (! busy && ! instrument)
            return t;
    }
    return -1;
}
} // namespace detail

// A drop on the Playlist at (`track`, `start`). `liveStyle`: the Clip-launch layout's arrangement. `fileName`: the
// file's name without its extension.
inline Result dropOnPlaylist (Song& song, const midiimport::ParsedMidi& midi, const std::string& fileName, int track, std::int64_t start,
                              bool liveStyle)
{
    Result r;
    const auto gs = noteGroups (midi);
    if (gs.empty())
    {
        r.error = "no notes";
        return r;
    }
    if (track < 0 || track >= 500)
    {
        r.error = "no track";
        return r;
    }
    if (! liveStyle)
    {
        std::vector<Note> notes;
        if (const auto under = channelUnder (song, track, false); under >= 0)
        {
            for (const auto& g : gs)
                detail::appendNotes (notes, g.notes, under);
            r.channels.push_back (under);
        }
        else
            for (std::size_t i = 0; i < gs.size(); ++i)
            {
                const auto& g = gs[i];
                auto c = newChannel (detail::groupName (midi, g, fileName, i, gs.size()));
                c.mixerTrack = song.claimFreeInsert();
                song.channels.push_back (c);
                const auto channel = (int) song.channels.size() - 1;
                detail::appendNotes (notes, g.notes, channel);
                r.channels.push_back (channel);
            }
        const auto pattern = detail::addPattern (song, fileName, std::move (notes));
        song.playlist.push_back (detail::patternClip (song, pattern, track, start));
        r.patterns.push_back (pattern);
        r.tracks.push_back (track);
    }
    else
    {
        if (isAudioTrack (song, track))
        {
            r.error = "audio track";
            return r;
        }
        const auto before = song;
        int next = track;
        for (std::size_t i = 0; i < gs.size(); ++i)
        {
            const auto& g = gs[i];
            const auto name = detail::groupName (midi, g, fileName, i, gs.size());
            const auto t = i == 0 ? track : detail::nextTakingTrack (song, next + 1);
            const auto channel = t >= 0 ? detail::instrumentOf (song, t, name) : -1;
            if (channel < 0)
            {
                if (i == 0)
                {
                    song = before;
                    r.error = "no track";
                    return r;
                }
                break; // (no more room: the tracks placed so far stay)
            }
            std::vector<Note> notes;
            detail::appendNotes (notes, g.notes, channel);
            const auto pattern = detail::addPattern (song, gs.size() > 1 ? name : fileName, std::move (notes));
            song.playlist.push_back (detail::patternClip (song, pattern, t, start));
            r.patterns.push_back (pattern);
            r.channels.push_back (channel);
            r.tracks.push_back (t);
            next = t;
        }
    }
    song.mode = PlayMode::song;
    r.ok = true;
    return r;
}

// A drop on the clip grid's slot (`track`, `scene`): one clip, every MIDI track of the file merged on the track's
// instrument (made when the track has none).
inline Result dropOnSlot (Song& song, const midiimport::ParsedMidi& midi, const std::string& fileName, int track, int scene)
{
    Result r;
    const auto gs = noteGroups (midi);
    if (gs.empty())
    {
        r.error = "no notes";
        return r;
    }
    if (track < 0 || track >= launcher::maxTracks || scene < 0 || scene >= launcher::maxScenes)
    {
        r.error = "no track";
        return r;
    }
    if (isAudioTrack (song, track))
    {
        r.error = "audio track";
        return r;
    }
    const auto before = song;
    const auto channel = detail::instrumentOf (song, track, fileName);
    if (channel < 0)
    {
        song = before;
        r.error = "no track";
        return r;
    }
    std::vector<Note> notes;
    for (const auto& g : gs)
        detail::appendNotes (notes, g.notes, channel);
    const auto pattern = detail::addPattern (song, fileName, std::move (notes));
    auto clip = detail::patternClip (song, pattern, track, 0);
    if (launcher::setClip (song, track, scene, clip) < 0)
    {
        song = before;
        r.error = "no track";
        return r;
    }
    r.patterns.push_back (pattern);
    r.channels.push_back (channel);
    r.tracks.push_back (track);
    r.ok = true;
    return r;
}
} // namespace lynx::mididrop
