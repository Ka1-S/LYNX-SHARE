#pragma once

// Mix snapshots (round 190hp, slice B16 of the 0.1.28 pain report, complaint #30: "Mixer: hide unused tracks, snapshots").
// LYNX's own: FL keeps one insert's settings in a file (the track menu's Mixer states, .fst; LYNX has them too) and the
// reference DAW has no mixer snapshots. The Mixer menu's "Mix snapshots (LYNX)" submenu:
//   - Take snapshot: the whole mix as it is now — every insert's fader, pan, separation, mute, its route to the Master
//     (on / level) and its sends (target, level, pre / post) — named "Snapshot n"; at most maxMixSnapshots per project;
//   - a snapshot's own submenu: Recall, Update (to the mix as it is now), Rename..., Delete;
//   - Compare A/B: recalls the snapshot recalled before the current one (A, B, A...).
// A recall is one undo step. The rest of the mix (effects, EQ, colours, names, solo) stays as it is. Insert numbers
// follow the mixer's reordering and deletion (remap, from mixerops::remapTrackReferences); an insert added after the
// snapshot keeps its settings, and a send that would now make a loop is left out (Song::canRoute). No JUCE here.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::mixsnap
{
// The mix as it is now.
inline MixSnapshot capture (const Song& song, std::string name = {})
{
    MixSnapshot s;
    s.name = std::move (name);
    for (int t = 0; t < (int) song.mixer.size(); ++t)
    {
        const auto& m = song.mixer[(std::size_t) t];
        MixSnapshot::Insert e;
        e.track = t;
        e.volume = m.volume;
        e.pan = m.pan;
        e.stereo = m.stereo;
        e.muted = m.muted;
        e.toMaster = m.toMaster;
        e.masterLevel = m.masterLevel;
        e.sends = m.sends;
        s.inserts.push_back (std::move (e));
    }
    return s;
}

// The mix now is the snapshot's (what it holds of it).
inline bool matches (const Song& song, const MixSnapshot& snap)
{
    for (const auto& e : snap.inserts)
    {
        if (e.track < 0 || e.track >= (int) song.mixer.size())
            continue;
        const auto& m = song.mixer[(std::size_t) e.track];
        if (m.volume != e.volume || m.pan != e.pan || m.stereo != e.stereo || m.muted != e.muted || m.masterLevel != e.masterLevel
            || (e.track > 0 && (m.toMaster != e.toMaster || ! (m.sends == e.sends))))
            return false;
    }
    return true;
}

// A name no snapshot of the project has: "Snapshot 1", "Snapshot 2"...
inline std::string freeName (const Song& song, const std::string& stem = "Snapshot")
{
    for (int n = 1;; ++n)
    {
        const auto name = stem + " " + std::to_string (n);
        if (std::none_of (song.mixSnapshots.begin(), song.mixSnapshots.end(), [&] (const MixSnapshot& s) { return s.name == name; }))
            return name;
    }
}

// Take snapshot: the index of the new snapshot, -1 when the project already has maxMixSnapshots. It becomes the current
// one (the mix is that snapshot now).
inline int take (Song& song, const std::string& stem = "Snapshot")
{
    if ((int) song.mixSnapshots.size() >= maxMixSnapshots)
        return -1;
    song.mixSnapshots.push_back (capture (song, freeName (song, stem)));
    const auto index = (int) song.mixSnapshots.size() - 1;
    if (song.mixSnapshotCurrent != index)
    {
        song.mixSnapshotPrevious = song.mixSnapshotCurrent;
        song.mixSnapshotCurrent = index;
    }
    return index;
}

// Update: the snapshot becomes the mix as it is now (its name stays).
inline bool update (Song& song, int index)
{
    if (index < 0 || index >= (int) song.mixSnapshots.size())
        return false;
    auto& s = song.mixSnapshots[(std::size_t) index];
    s.inserts = capture (song).inserts;
    return true;
}

// Recall: the mix takes the snapshot's settings.
inline bool recall (Song& song, int index)
{
    if (index < 0 || index >= (int) song.mixSnapshots.size())
        return false;
    const auto snap = song.mixSnapshots[(std::size_t) index];
    const auto n = (int) song.mixer.size();
    for (const auto& e : snap.inserts) // the levels first, and the sends of the snapshot's inserts cleared
    {
        if (e.track < 0 || e.track >= n)
            continue;
        auto& m = song.mixer[(std::size_t) e.track];
        m.volume = std::clamp (e.volume, 0.0f, 1.0f);
        m.pan = std::clamp (e.pan, -1.0f, 1.0f);
        m.stereo = std::clamp (e.stereo, -1.0f, 1.0f);
        m.muted = e.muted;
        m.masterLevel = std::clamp (e.masterLevel, 0.0f, maxFaderGain);
        if (e.track > 0)
        {
            m.toMaster = e.toMaster;
            m.sends.clear();
        }
    }
    for (const auto& e : snap.inserts) // then the routes, each only where it makes no loop
    {
        if (e.track <= 0 || e.track >= n)
            continue;
        for (const auto& send : e.sends)
            if (send.target > 0 && send.target < n && send.target != e.track && song.canRoute (e.track, send.target))
            {
                auto kept = send;
                kept.level = std::clamp (send.level, 0.0f, maxFaderGain);
                song.mixer[(std::size_t) e.track].sends.push_back (kept);
            }
    }
    if (song.mixSnapshotCurrent != index)
    {
        song.mixSnapshotPrevious = song.mixSnapshotCurrent;
        song.mixSnapshotCurrent = index;
    }
    return true;
}

// Compare A/B: recalls the snapshot recalled before the current one (they swap).
inline bool canCompare (const Song& song) noexcept
{
    const auto n = (int) song.mixSnapshots.size();
    return song.mixSnapshotPrevious >= 0 && song.mixSnapshotPrevious < n && song.mixSnapshotCurrent >= 0 && song.mixSnapshotCurrent < n
           && song.mixSnapshotPrevious != song.mixSnapshotCurrent;
}
inline bool compareAB (Song& song)
{
    return canCompare (song) && recall (song, song.mixSnapshotPrevious);
}

inline bool rename (Song& song, int index, std::string name)
{
    if (index < 0 || index >= (int) song.mixSnapshots.size() || name.empty())
        return false;
    song.mixSnapshots[(std::size_t) index].name = std::move (name);
    return true;
}

inline bool remove (Song& song, int index)
{
    if (index < 0 || index >= (int) song.mixSnapshots.size())
        return false;
    song.mixSnapshots.erase (song.mixSnapshots.begin() + index);
    for (auto* i : { &song.mixSnapshotCurrent, &song.mixSnapshotPrevious })
        *i = *i == index ? -1 : *i > index ? *i - 1 : *i;
    return true;
}

// The inserts were renumbered (`map`: old number -> new, < 0 = deleted; the Master stays 0).
inline void remap (Song& song, const std::function<int (int)>& map)
{
    for (auto& s : song.mixSnapshots)
    {
        for (auto& e : s.inserts)
        {
            if (e.track > 0)
                e.track = map (e.track);
            for (auto& send : e.sends)
                send.target = map (send.target);
            e.sends.erase (std::remove_if (e.sends.begin(), e.sends.end(), [] (const MixerSend& x) { return x.target <= 0; }), e.sends.end());
        }
        s.inserts.erase (std::remove_if (s.inserts.begin(), s.inserts.end(), [] (const MixSnapshot::Insert& e) { return e.track < 0; }), s.inserts.end());
        std::sort (s.inserts.begin(), s.inserts.end(), [] (const MixSnapshot::Insert& a, const MixSnapshot::Insert& b) { return a.track < b.track; });
    }
}
} // namespace lynx::mixsnap
