#pragma once

// Round 190kx (core/AraLink.h): the ARA model of an audio clip channel edited in an ARA plugin, worked out from the song.
// Header only, no JUCE (the app's engine/AraHost.cpp turns it into ARA objects; tests check it on any platform).
//
// LYNX's model → ARA's model (ARA 2 "model graph", Celemony's ARA SDK documentation):
//   the song's tempo and meter          → one MusicalContext (tempo entries + bar signatures)
//   an ARA channel's sample             → one AudioSource (persistent id "lynx-ch-<channel uid>")
//   that channel                        → one AudioModification ("lynx-mod-<channel uid>") and one RegionSequence: the
//                                         channel is FL's "sample", so its clips share the edits, as editing a channel's
//                                         sample in Edison or Newtone changes every clip of it (FL's "Make unique"
//                                         gives a clip a channel, and so edits, of its own)
//   each audio clip of the Playlist     → one PlaybackRegion: where it plays on the timeline (seconds) and which part of
//                                         the sample it plays (seconds of the source)
// Clips the song does not play are left out: muted clips, clips on muted Playlist tracks, clips of the clip grid
// (Clip-launch workflow: not on the timeline), empty clips. The plugin plays the raw sample: the channel's own stretch /
// pitch / reverse and the clip's pitch, fades and reverse are what an ARA plugin does itself, so they are not applied
// to an ARA channel (the clip's stretch is handed over as ARA time-stretching).
// Timeline seconds are at the project's tempo (as the hosted plugins' play head reports them: engine/PluginHost.cpp
// TimelinePlayHead); tempo automation is not followed.

#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace lynx::ara
{
struct Region
{
    std::uint64_t clipUid = 0;
    int clipIndex = -1;     // in Song::playlist
    double playStart = 0.0, playDuration = 0.0;     // timeline seconds
    double sourceStart = 0.0, sourceDuration = 0.0; // seconds of the source (the sample)
    bool stretched() const noexcept { return std::abs (playDuration - sourceDuration) > 1.0e-9; }
    bool operator== (const Region&) const = default;
};

inline double secondsPerTick (const Song& song) noexcept
{
    const auto bpm = song.bpm > 0.0 ? song.bpm : 120.0;
    return 60.0 / (bpm * (double) ppq);
}

inline std::string sourceId (const ChannelDef& c) { return "lynx-ch-" + std::to_string (c.uid); }
inline std::string modificationId (const ChannelDef& c) { return "lynx-mod-" + std::to_string (c.uid); }

// Whether channel `channel` is an audio clip channel edited in an ARA plugin
inline bool isAraChannel (const Song& song, int channel) noexcept
{
    return channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].instrument == InstrumentKind::audioClip
           && song.channels[(std::size_t) channel].ara.active();
}

inline bool anyAraChannel (const Song& song) noexcept
{
    return std::any_of (song.channels.begin(), song.channels.end(),
                        [] (const ChannelDef& c) { return c.instrument == InstrumentKind::audioClip && c.ara.active(); });
}

// The playback regions of channel `channel`, in Playlist order. `sourceSeconds` = the sample's length (a region never
// reads past it; 0 = unknown, not clipped).
inline std::vector<Region> regionsOf (const Song& song, int channel, double sourceSeconds = 0.0)
{
    std::vector<Region> out;
    if (channel < 0 || channel >= (int) song.channels.size())
        return out;
    const auto spt = secondsPerTick (song);
    for (std::size_t i = 0; i < song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[i];
        if (c.kind != ClipKind::audio || c.channel != channel || c.length <= 0 || c.muted || c.gridSlot >= 0 || song.isTrackMuted (c.track))
            continue;
        const auto stretch = c.stretch > 0.0 && std::isfinite (c.stretch) ? c.stretch : 1.0;
        Region r;
        r.clipUid = c.uid;
        r.clipIndex = (int) i;
        r.playStart = (double) c.start * spt;
        r.playDuration = (double) c.length * spt;
        r.sourceStart = std::max (0.0, ((double) c.offset + (double) c.offsetFraction) * spt / stretch);
        r.sourceDuration = r.playDuration / stretch;
        if (sourceSeconds > 0.0)
        {
            if (r.sourceStart >= sourceSeconds)
                continue; // (the clip starts past the end of its sample: nothing to play)
            if (r.sourceStart + r.sourceDuration > sourceSeconds)
            {
                // the sample ends inside the clip: the region ends with it (the rest of the clip is silent, as it is when
                // the channel's sampler plays it)
                r.sourceDuration = sourceSeconds - r.sourceStart;
                r.playDuration = r.sourceDuration * stretch;
            }
        }
        out.push_back (r);
    }
    return out;
}

// What the plugin is told about the tempo: one entry at 0 and one a bar later at the project's tempo (ARA wants two
// entries to define a constant tempo), and the project's meter at 0.
struct TempoEntry
{
    double seconds = 0.0, quarters = 0.0;
};
inline std::vector<TempoEntry> tempoEntries (const Song& song)
{
    const auto bpm = song.bpm > 0.0 ? song.bpm : 120.0;
    return { { 0.0, 0.0 }, { 4.0 * 60.0 / bpm, 4.0 } };
}
} // namespace lynx::ara
