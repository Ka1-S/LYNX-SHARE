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
// Timeline seconds follow the song's tempo (round 190oq, issue #243): with tempo automation / pattern Tempo events (song
// mode) the timeline is the tempo map's (TimeMap below: the regions, the musical context's tempo entries and the ARA
// instances' play head all use it); without, the project's tempo, exactly as before.
//
// Round 190oq (issue #243), ARA on a mixer insert (as REAPER, Logic and Studio One place it on a track): an ARA plugin in
// a mixer slot with EffectSlot::ara on plays and edits the audio clips of every audio clip channel routed to that mixer
// track (ChannelDef::mixerTrack). projectInserts gives those channels the slot's plugin as their ARA link in the song the
// rack plays (the channel's own link, from the clip menu, wins) and leaves the slot itself empty there: the clips go
// through the same per-channel ARA path as before, and the insert's chain after the slot hears them. A bypassed slot
// plays the clips unedited (A/B), as in those hosts.

#include "core/Song.h"
#include "core/TempoMap.h"

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

// --- ARA on a mixer insert (round 190oq, issue #243) -------------------------------------------------------------------
// The slot of mixer track `track` that is an ARA insert (a plugin slot with `ara` on, switched on), or -1. The first one
// counts (a second ARA slot on the same track does nothing).
inline int araInsertSlot (const Song& song, int track) noexcept
{
    if (track < 0 || track >= (int) song.mixer.size())
        return -1;
    const auto& fx = song.mixer[(std::size_t) track].effects;
    for (int i = 0; i < (int) fx.size() && i < maxEffectSlots; ++i)
        if (fx[(std::size_t) i].kind == EffectKind::plugin && fx[(std::size_t) i].ara && ! fx[(std::size_t) i].plugin.id.empty())
            return fx[(std::size_t) i].enabled ? i : -1;
    return -1;
}

inline bool anyAraInsert (const Song& song) noexcept
{
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        if (araInsertSlot (song, t) >= 0)
            return true;
    return false;
}

// The link channel `channel` gets from an ARA insert on its mixer track (inactive: none, or the channel has its own)
inline AraLink insertLink (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return {};
    const auto& c = song.channels[(std::size_t) channel];
    if (c.instrument != InstrumentKind::audioClip || c.ara.active())
        return {};
    const auto slot = araInsertSlot (song, c.mixerTrack);
    if (slot < 0)
        return {};
    const auto& ref = song.mixer[(std::size_t) c.mixerTrack].effects[(std::size_t) slot].plugin;
    return { ref.id, ref.name.empty() ? ref.id : ref.name };
}

// The link channel `channel` plays with: its own (clip menu), else its ARA insert's
inline AraLink effectiveLink (const Song& song, int channel)
{
    if (channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].ara.active())
        return song.channels[(std::size_t) channel].ara;
    return insertLink (song, channel);
}

// The audio clip channels an ARA insert of mixer track `track` plays (routed there, without a link of their own)
inline std::vector<int> insertChannels (const Song& song, int track)
{
    std::vector<int> out;
    if (araInsertSlot (song, track) < 0)
        return out;
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (song.channels[(std::size_t) i].mixerTrack == track && insertLink (song, i).active())
            out.push_back (i);
    return out;
}

// The audio clip channels routed to `track` (what an ARA slot there would play), whether or not a slot is in ARA mode
inline int routedAudioClipChannels (const Song& song, int track)
{
    int n = 0;
    for (const auto& c : song.channels)
        n += c.instrument == InstrumentKind::audioClip && c.mixerTrack == track && ! c.ara.active() ? 1 : 0;
    return n;
}

// The song the rack plays: the channels routed to an ARA insert take its plugin as their link, and the ARA slots are
// left empty (their plugin plays inside the channels, not as a plain insert effect). Returns how many channels.
inline int projectInserts (Song& song)
{
    std::vector<AraLink> links (song.channels.size());
    int n = 0;
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if ((links[(std::size_t) i] = insertLink (song, i)).active())
            ++n;
    for (int t = 0; t < (int) song.mixer.size(); ++t)
        if (const auto slot = araInsertSlot (song, t); slot >= 0)
        {
            auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
            fx.plugin.id.clear(); // (an empty plugin slot: the rack passes the sound through)
            fx.ara = false;
        }
    for (std::size_t i = 0; i < links.size(); ++i)
        if (links[i].active())
            song.channels[i].ara = links[i];
    return n;
}

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
// --- the timeline (round 190oq, issue #243: the tempo map) -------------------------------------------------------------
// Seconds on the song's timeline at each tick: breakpoints where the tempo changes, linear between them (ARA's own model
// of a tempo map: "tempo entries", constant tempo between two entries). Without tempo changes: two points at the
// project's tempo.
struct TimeMap
{
    std::vector<double> ticks, seconds; // (same size, ≥ 2, ticks rising)
    bool constant() const noexcept { return ticks.size() <= 2; }
    bool operator== (const TimeMap&) const = default;

    double secondsAt (double tick) const noexcept
    {
        if (ticks.size() < 2)
            return 0.0;
        auto i = (std::size_t) (std::upper_bound (ticks.begin(), ticks.end(), tick) - ticks.begin());
        i = std::clamp<std::size_t> (i, 1, ticks.size() - 1);
        const auto t0 = ticks[i - 1], t1 = ticks[i], s0 = seconds[i - 1], s1 = seconds[i];
        return s0 + (tick - t0) * (s1 - s0) / (t1 - t0);
    }
    double ticksAt (double s) const noexcept
    {
        if (ticks.size() < 2)
            return 0.0;
        auto i = (std::size_t) (std::upper_bound (seconds.begin(), seconds.end(), s) - seconds.begin());
        i = std::clamp<std::size_t> (i, 1, ticks.size() - 1);
        const auto t0 = ticks[i - 1], t1 = ticks[i], s0 = seconds[i - 1], s1 = seconds[i];
        return t0 + (s - s0) * (t1 - t0) / (s1 - s0);
    }
};

// The song's time map up to `untilTick` (past it the last tempo goes on). Tempo automation and pattern Tempo events count
// in song mode (core/TempoMap.h), read per 1/16 step as playback and Song::secondsForTicks do.
inline TimeMap timeMap (const Song& song, double untilTick = 0.0)
{
    TimeMap m;
    const auto bpm = song.bpm > 0.0 ? song.bpm : 120.0;
    if (song.mode != PlayMode::song || ! tempomap::active (song))
    {
        m.ticks = { 0.0, 4.0 * ppq };
        m.seconds = { 0.0, 4.0 * 60.0 / bpm };
        return m;
    }
    constexpr std::int64_t step = ticksPerStep;
    const auto until = std::max<double> ({ untilTick, (double) song.songLength(), 4.0 * ppq }) + 4.0 * ppq;
    double at = 0.0, last = -1.0;
    m.ticks.push_back (0.0);
    m.seconds.push_back (0.0);
    constexpr std::size_t maxPoints = 200000;
    for (std::int64_t t = 0; (double) t < until && m.ticks.size() < maxPoints; t += step)
    {
        const auto tempo = std::max (1.0, song.tempoAt ((double) t));
        if (last > 0.0 && tempo != last && (double) t > m.ticks.back())
        {
            m.ticks.push_back ((double) t);
            m.seconds.push_back (at);
        }
        last = tempo;
        at += (double) step * 60.0 / (tempo * ppq);
    }
    // the last stretch (its tempo goes on past the end)
    const auto end = (double) ((std::int64_t) until / step) * (double) step;
    if (end > m.ticks.back())
    {
        m.ticks.push_back (end);
        m.seconds.push_back (at);
    }
    if (m.ticks.size() < 2)
    {
        m.ticks = { 0.0, 4.0 * ppq };
        m.seconds = { 0.0, 4.0 * 60.0 / bpm };
    }
    return m;
}

inline std::vector<Region> regionsOf (const Song& song, int channel, double sourceSeconds = 0.0, const TimeMap* map = nullptr)
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
        if (map != nullptr && ! map->constant())
        {
            r.playStart = map->secondsAt ((double) c.start);
            r.playDuration = map->secondsAt ((double) c.end()) - r.playStart;
        }
        else
        {
            r.playStart = (double) c.start * spt;
            r.playDuration = (double) c.length * spt;
        }
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

// What the plugin is told about the tempo: ARA's tempo entries (seconds, quarters), one per breakpoint of the time map
// (at a constant tempo: one at 0 and one a bar later, as ARA wants two entries to define a constant tempo); round 190oq:
// the tempo map, so a plugin (Melodyne) draws its bar ruler where the song's bars are.
struct TempoEntry
{
    double seconds = 0.0, quarters = 0.0;
    bool operator== (const TempoEntry&) const = default;
};
inline std::vector<TempoEntry> tempoEntries (const TimeMap& map)
{
    std::vector<TempoEntry> out;
    for (std::size_t i = 0; i < map.ticks.size(); ++i)
        out.push_back ({ map.seconds[i], map.ticks[i] / (double) ppq });
    return out;
}
inline std::vector<TempoEntry> tempoEntries (const Song& song) { return tempoEntries (timeMap (song)); }

// ARA's bar signatures (numerator, denominator, position in quarters): the project's meter at 0, then each time signature
// marker of the song (round 190oq; FL's time signature markers start a new bar where they are). A marker at 0 replaces
// the project's meter there.
struct BarSignature
{
    int numerator = 4, denominator = 4;
    double quarters = 0.0;
    bool operator== (const BarSignature&) const = default;
};
inline std::vector<BarSignature> barSignatures (const Song& song)
{
    const auto m = song.meter();
    std::vector<BarSignature> out { { std::max (1, m.beats), meterUnit (m.unit), 0.0 } };
    std::vector<const Marker*> signs;
    for (const auto& mk : song.markers())
        if (mk.isSignature() && mk.tick >= 0)
            signs.push_back (&mk);
    std::stable_sort (signs.begin(), signs.end(), [] (const Marker* a, const Marker* b) { return a->tick < b->tick; });
    for (const auto* mk : signs)
    {
        const BarSignature b { std::max (1, mk->numerator), meterUnit (mk->denominator), (double) mk->tick / (double) ppq };
        if (b.quarters <= out.back().quarters)
            out.back() = b; // (two at one place: the later one)
        else if (b.numerator != out.back().numerator || b.denominator != out.back().denominator)
            out.push_back (b);
    }
    return out;
}
} // namespace lynx::ara
