#pragma once

// Round 190op (research190oe P4, LYNX extras): Hide tracks and Deactivate tracks. JUCE-free and header-only; every edit
// runs inside AudioEngine::editSong (one undo step).
//
// Hide (PlaylistTrack::hidden): a view state. A hidden Playlist track has no row (and in the Clip-launch layout no
// column) until "Show hidden tracks" is on; it still plays. Reference: Cubase / Studio Pro hide tracks; FL has only its
// "Hide collapsed grouped tracks" (round 190jf), Live none.
//
// Deactivate (ChannelDef::deactivated, MixerTrack::deactivated; owner decision O-3): the instrument / effects are not
// loaded at all (their RAM and CPU are free) and the unit carries silence; nothing is rendered (unlike Freeze, round
// 190ay). The settings and plugin states stay in the project as they were, so Reactivate brings the same sound back. A
// unit that plays only into a deactivated insert is deactivated with it (deactivate::effective). The engine plays the
// projection deactivate::project makes (as it plays standins::project); the UI keeps the song itself. References:
// Cubase "Disable Track", Studio Pro "Deactivate", REAPER offline FX. Live's Track Activator is a mute and stays one.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/DetailChain.h"
#include "core/Song.h"

namespace lynx::trackstates
{
// Settings › LYNX extras "Hide and deactivate tracks" (ui-options.txt "trackStates"): the Pattern layout's menus offer
// Hide track / Deactivate only while it is on (FL's menus stay FL's); the Clip-launch and Simple layouts always do. A
// project that holds hidden tracks always offers "Show hidden tracks", whatever the switch.
inline bool& patternMenus() noexcept
{
    static bool on = false;
    return on;
}

// --- Hide -------------------------------------------------------------------------------------------------------------

inline bool isHidden (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].hidden;
}

inline int hiddenCount (const Song& song) noexcept
{
    return (int) std::count_if (song.playlistTracks.begin(), song.playlistTracks.end(), [] (const PlaylistTrack& t) { return t.hidden; });
}

// Hides or shows `tracks` (rows are added up to the highest one hidden). Returns how many changed.
inline int setHidden (Song& song, const std::vector<int>& tracks, bool hidden)
{
    int changed = 0;
    for (const auto t : tracks)
    {
        if (t < 0 || t >= 10000)
            continue;
        if (t >= (int) song.playlistTracks.size())
        {
            if (! hidden)
                continue;
            song.playlistTracks.resize ((std::size_t) t + 1);
        }
        changed += std::exchange (song.playlistTracks[(std::size_t) t].hidden, hidden) != hidden ? 1 : 0;
    }
    return changed;
}

// Every hidden track shown again. Returns how many changed.
inline int showAll (Song& song)
{
    int changed = 0;
    for (auto& t : song.playlistTracks)
        changed += std::exchange (t.hidden, false) ? 1 : 0;
    return changed;
}

// The tracks that hold a clip in any arrangement or in the clip grid (by index; sized to cover them all).
inline std::vector<char> tracksWithClips (const Song& song)
{
    std::vector<char> used (song.playlistTracks.size(), 0);
    song.forEachClipList ([&used] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.track >= 0 && c.track < 10000)
            {
                if ((std::size_t) c.track >= used.size())
                    used.resize ((std::size_t) c.track + 1, 0);
                used[(std::size_t) c.track] = 1;
            }
    });
    return used;
}

// "Hide unused tracks": the tracks above the last used one with no clip in any arrangement or the clip grid, no
// automation lanes, and no part in a group (a header, a member, a comp, a take lane). The empty rows below the last used
// track stay: they are where new parts go. Returns how many were hidden.
inline int hideUnused (Song& song)
{
    const auto used = tracksWithClips (song);
    int last = -1;
    for (int t = 0; t < (int) used.size(); ++t)
        if (used[(std::size_t) t] != 0 || (t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].lanes.empty()))
            last = t;
    if (last < 0)
        return 0;
    if ((int) song.playlistTracks.size() <= last)
        song.playlistTracks.resize ((std::size_t) last + 1);
    int changed = 0;
    for (int t = 0; t < last; ++t)
    {
        auto& row = song.playlistTracks[(std::size_t) t];
        const auto next = t + 1 < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) t + 1] : nullptr;
        const auto inGroup = row.grouped || row.compTrack || row.takeLane || (next != nullptr && next->grouped);
        if (row.hidden || inGroup || (t < (int) used.size() && used[(std::size_t) t] != 0) || ! row.lanes.empty())
            continue;
        row.hidden = true;
        ++changed;
    }
    return changed;
}

} // namespace lynx::trackstates

namespace lynx::deactivate
{
// Which units are deactivated, the ones switched off by hand and the ones that play only into a deactivated insert.
struct Effective
{
    std::vector<char> channels, inserts;
    bool any = false;
    bool channel (int c) const noexcept { return c >= 0 && c < (int) channels.size() && channels[(std::size_t) c] != 0; }
    bool insert (int t) const noexcept { return t >= 0 && t < (int) inserts.size() && inserts[(std::size_t) t] != 0; }
};

// The quick answer for almost every project: nothing switched off by hand.
inline bool anyMarked (const Song& song) noexcept
{
    return std::any_of (song.channels.begin(), song.channels.end(), [] (const ChannelDef& c) { return c.deactivated; })
           || std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& t) { return t.deactivated; });
}

inline Effective effective (const Song& song)
{
    Effective e;
    if (! anyMarked (song))
        return e;
    const auto tracks = song.mixer.size();
    e.inserts.assign (tracks, 0);
    e.channels.assign (song.channels.size(), 0);
    for (std::size_t t = 1; t < tracks; ++t) // (the Master is never deactivated)
        e.inserts[t] = song.mixer[t].deactivated ? 1 : 0;
    // an insert whose every route leads into deactivated inserts (no route to the Master) is silent: deactivated too;
    // repeated until nothing changes (chains of inserts)
    for (bool changed = true; changed;)
    {
        changed = false;
        for (std::size_t t = 1; t < tracks; ++t)
        {
            const auto& m = song.mixer[t];
            if (e.inserts[t] != 0 || m.toMaster || m.sends.empty() || m.current)
                continue;
            const auto allOff = std::all_of (m.sends.begin(), m.sends.end(), [&] (const MixerSend& s) {
                return s.target > 0 && (std::size_t) s.target < tracks && e.inserts[(std::size_t) s.target] != 0;
            });
            if (allOff)
            {
                e.inserts[t] = 1;
                changed = true;
            }
        }
    }
    for (std::size_t c = 0; c < song.channels.size(); ++c)
    {
        const auto& ch = song.channels[c];
        e.channels[c] = (ch.deactivated || (ch.mixerTrack > 0 && (std::size_t) ch.mixerTrack < tracks && e.inserts[(std::size_t) ch.mixerTrack] != 0)) ? 1 : 0;
    }
    e.any = std::any_of (e.inserts.begin(), e.inserts.end(), [] (char v) { return v != 0; })
            || std::any_of (e.channels.begin(), e.channels.end(), [] (char v) { return v != 0; });
    return e;
}

// What a deactivated channel becomes for the engine: a muted, empty audio clip channel on the same insert (nothing is
// loaded, nothing sounds); its place, name, colour and id stay so every index and link still holds.
inline ChannelDef silentChannel (const ChannelDef& c)
{
    ChannelDef f;
    f.name = c.name;
    f.instrument = InstrumentKind::audioClip;
    f.mixerTrack = c.mixerTrack;
    f.colour = c.colour;
    f.group = c.group;
    f.uid = c.uid;
    f.muted = true;
    f.deactivated = true;
    return f;
}

// The song the engine plays: deactivated channels are silent placeholders, deactivated inserts have no effects and are
// muted (a muted insert's routes, sends included, carry nothing), and what drove their parameters drives nothing.
// The channels and inserts alone (AudioEngine also keeps its last built shape this way: a later build must never take a
// placeholder for the unit it stands for).
inline void projectUnits (std::vector<ChannelDef>& channels, std::vector<MixerTrack>& mixer, const Effective& e)
{
    if (! e.any)
        return;
    for (std::size_t c = 0; c < channels.size(); ++c)
        if (e.channel ((int) c))
            channels[c] = silentChannel (channels[c]);
    for (std::size_t t = 1; t < mixer.size(); ++t)
        if (e.insert ((int) t))
        {
            auto& m = mixer[t];
            m.effects.clear();
            m.muted = true;
            m.solo = false;
            m.standIns.clear();
            m.deactivated = true;
        }
}

inline void project (Song& s, const Effective& e)
{
    if (! e.any)
        return;
    projectUnits (s.channels, s.mixer, e);
    auto gone = [&e] (const AutomationTarget& t) {
        switch (t.kind)
        {
            case AutomationTargetKind::channelParam:
                return e.channel (t.index);
            case AutomationTargetKind::effectParam:
            case AutomationTargetKind::effectMix:
                return e.insert (t.index);
            default:
                return false;
        }
    };
    s.forEachTarget ([&gone] (AutomationTarget& t) {
        if (gone (t))
            t = {};
    });
}

inline Song projected (const Song& song)
{
    auto s = song;
    project (s, effective (song));
    return s;
}

// Sets the flag on channels / inserts. Returns how many changed.
inline int setChannels (Song& song, const std::vector<int>& channels, bool off)
{
    int changed = 0;
    for (const auto c : channels)
        if (c >= 0 && c < (int) song.channels.size())
            changed += std::exchange (song.channels[(std::size_t) c].deactivated, off) != off ? 1 : 0;
    return changed;
}

inline int setInserts (Song& song, const std::vector<int>& inserts, bool off)
{
    int changed = 0;
    for (const auto t : inserts)
        if (t > 0 && t < (int) song.mixer.size() && ! song.mixer[(std::size_t) t].current)
            changed += std::exchange (song.mixer[(std::size_t) t].deactivated, off) != off ? 1 : 0;
    return changed;
}

// A Playlist / clip grid track's units (the Clip-launch layout's "Deactivate track", the Simple track menu): the channels
// it plays (detailchain::forTrack) and its insert when nothing else plays into that insert (a shared insert stays on: it
// would silence other tracks).
struct TrackUnits
{
    std::vector<int> channels, inserts;
    bool empty() const noexcept { return channels.empty() && inserts.empty(); }
};

inline TrackUnits unitsOfTrack (const Song& song, int track)
{
    TrackUnits u;
    const auto chain = detailchain::forTrack (song, track);
    u.channels = chain.channels;
    if (chain.channel >= 0 && std::find (u.channels.begin(), u.channels.end(), chain.channel) == u.channels.end())
        u.channels.push_back (chain.channel);
    std::sort (u.channels.begin(), u.channels.end());
    const auto insert = chain.insert;
    if (insert > 0 && insert < (int) song.mixer.size() && ! song.mixer[(std::size_t) insert].current)
    {
        const auto own = std::all_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) {
            const auto index = (int) (&c - song.channels.data());
            return c.mixerTrack != insert || std::binary_search (u.channels.begin(), u.channels.end(), index);
        });
        if (own)
            u.inserts.push_back (insert);
    }
    return u;
}

// The track counts as deactivated when its own insert is, or every channel it plays is.
inline bool trackDeactivated (const Song& song, const TrackUnits& u) noexcept
{
    for (const auto t : u.inserts)
        if (t > 0 && t < (int) song.mixer.size() && song.mixer[(std::size_t) t].deactivated)
            return true;
    return ! u.channels.empty() && std::all_of (u.channels.begin(), u.channels.end(), [&song] (int c) {
        return c >= 0 && c < (int) song.channels.size() && song.channels[(std::size_t) c].deactivated;
    });
}
} // namespace lynx::deactivate

namespace lynx::trackstates
{
// (a track's units are what Hide's neighbour items act on: the same names in this namespace)
using deactivate::TrackUnits;
using deactivate::trackDeactivated;
using deactivate::unitsOfTrack;
} // namespace lynx::trackstates
