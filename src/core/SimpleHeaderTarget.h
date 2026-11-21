#pragma once

// The Simple headers on an FL project's tracks (round 190oo, design review 2 CS-24; owner decision: "Simple shows S /
// volume / pan on an FL project's tracks, acting on the mixer track the clips play through").
//
// A Simple track made in the Simple layout owns its mixer insert (core/SimpleTracks.h insertOf): its header's M, S,
// volume, pan and "+FX" are that insert's. A track of an FL project has no insert of its own; its clips' channels play
// through inserts that other tracks may use too. Its header now follows the Clip-launch Session mixer's rule
// (core/SessionMixer.h stripOf, core/DetailChain.h forTrack): the one insert all the track's clips play through
// (one channel: that channel's insert; several channels on one insert: that insert). With several inserts, or only the
// Master, or none, there is nothing to choose: S, volume, pan and "+FX" stay hidden and M is kept, as the Session mixer
// shows no controls for such a column. The controls never act on the Master (insert 0).
//
// Mute and solo, so that what the header shows is what is heard (a lit M always means this track is silent):
//  - own insert (Simple's own tracks, unchanged): M = the insert's mute, S = the insert's solo.
//  - borrowed insert (an FL track, the Session mixer's insert): M = the Playlist track's own mute, as before this round
//    (only this track's clips go silent; the other tracks on the same insert keep playing, their M stays unlit); S = the
//    insert's solo, as the Session mixer's S (every track that plays through that insert stays audible, the others go
//    silent; S lights on every header that shares the insert); volume / pan = the insert's fader and pan (they move the
//    sound of every track on it); "+FX" = the insert's effects.
//  - no insert (several, the Master, none): M = the Playlist track's mute; nothing else.
// A header whose insert also plays other tracks shows a small "shared" mark (PlaylistComponent) saying which.
//
// Header only, no JUCE; tested in tests/SimpleFl190ooTests.inc.

#include <algorithm>
#include <vector>

#include "core/DetailChain.h"
#include "core/SimpleTracks.h"
#include "core/Song.h"

namespace lynx::simplehdr
{
struct HeaderTarget
{
    int insert = -1;      // the insert S, volume, pan and "+FX" act on (> 0), -1: none (the header shows M only)
    bool own = false;     // the track's own insert (simpletracks::insertOf): M is the insert's mute
    int insertCount = 0;  // how many inserts the track's clips play through
    int sharedWith = 0;   // how many other tracks play through `insert` too
    int firstOther = -1;  // the first of them, -1: none
    std::vector<int> others; // all of them, ascending (the mark's hint names them)

    bool hasControls() const noexcept { return insert > 0; }
    bool muteActsOnInsert() const noexcept { return own && insert > 0; }
};

// Every track's target, tracks 0 .. max (playlistTracks, the highest clip track + 1) - 1, in one pass over the clips
// (the Playlist and the clip grid, as detailchain::forTrack reads them) — the Simple headers ask once per published song.
inline std::vector<HeaderTarget> headerTargets (const Song& song)
{
    int tracks = (int) song.playlistTracks.size();
    const auto grow = [&tracks] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            tracks = std::max (tracks, c.track + 1);
    };
    grow (song.playlist);
    if (song.clipGrid)
        grow (song.clipGrid->clips);
    tracks = std::clamp (tracks, 0, 4096);

    std::vector<HeaderTarget> out ((std::size_t) tracks);
    std::vector<std::vector<int>> channels ((std::size_t) tracks);
    std::vector<std::vector<int>> patternChannels (song.patterns.size());
    std::vector<char> patternRead (song.patterns.size(), 0);
    const auto scan = [&] (const std::vector<Clip>& clips) {
        for (const auto& clip : clips)
        {
            if (clip.track < 0 || clip.track >= tracks || clip.length <= 0)
                continue;
            auto& list = channels[(std::size_t) clip.track];
            if (clip.kind == ClipKind::audio)
            {
                if (clip.channel >= 0 && clip.channel < (int) song.channels.size())
                    list.push_back (clip.channel);
            }
            else if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size())
            {
                const auto p = (std::size_t) clip.pattern;
                if (! patternRead[p])
                {
                    patternRead[p] = 1;
                    for (const auto& n : song.patterns[p].notes)
                        if (n.channel >= 0 && n.channel < (int) song.channels.size())
                            patternChannels[p].push_back (n.channel);
                    std::sort (patternChannels[p].begin(), patternChannels[p].end());
                    patternChannels[p].erase (std::unique (patternChannels[p].begin(), patternChannels[p].end()), patternChannels[p].end());
                }
                list.insert (list.end(), patternChannels[p].begin(), patternChannels[p].end());
            }
        }
    };
    scan (song.playlist);
    if (song.clipGrid)
        scan (song.clipGrid->clips);

    const auto mixerSize = (int) song.mixer.size();
    const auto validInsert = [mixerSize] (int i) { return i > 0 && i < mixerSize; };
    // the inserts each track plays through (its own insert too), for the "shared" mark
    std::vector<std::vector<int>> tracksOfInsert ((std::size_t) std::max (0, mixerSize));
    for (int t = 0; t < tracks; ++t)
    {
        auto& h = out[(std::size_t) t];
        auto& list = channels[(std::size_t) t];
        std::sort (list.begin(), list.end());
        list.erase (std::unique (list.begin(), list.end()), list.end());
        std::vector<int> inserts;
        for (const auto ch : list)
            if (const auto i = detailchain::insertOfChannel (song, ch); i >= 0)
                inserts.push_back (i);
        std::sort (inserts.begin(), inserts.end());
        inserts.erase (std::unique (inserts.begin(), inserts.end()), inserts.end());
        h.insertCount = (int) inserts.size();

        if (const auto own = simpletracks::insertOf (song, t); validInsert (own))
        {
            h.insert = own;
            h.own = true;
        }
        else
        {
            // the Session mixer's rule (detailchain::forTrack, then sessionmixer::validInsert): a track in FL's track mode
            // decides by its mode; insertOf found no insert of its own there, so it is the Master or none (-1)
            const auto* pt = t < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) t] : nullptr;
            const auto byMode = pt != nullptr
                                && ((pt->mode == PlaylistTrackMode::instrument && pt->channel >= 0 && pt->channel < (int) song.channels.size())
                                    || (pt->mode == PlaylistTrackMode::audio && pt->mixerTrack >= 0 && pt->mixerTrack < mixerSize));
            int insert = -1;
            if (! byMode)
            {
                if (list.size() == 1)
                    insert = detailchain::insertOfChannel (song, list.front());
                else if (inserts.size() == 1)
                    insert = inserts.front();
            }
            h.insert = validInsert (insert) ? insert : -1;
        }
        if (h.insert > 0 && std::find (inserts.begin(), inserts.end(), h.insert) == inserts.end())
            inserts.push_back (h.insert);
        for (const auto i : inserts)
            if (i >= 0 && i < mixerSize)
                tracksOfInsert[(std::size_t) i].push_back (t);
    }
    for (int t = 0; t < tracks; ++t)
    {
        auto& h = out[(std::size_t) t];
        if (h.insert <= 0)
            continue;
        for (const auto other : tracksOfInsert[(std::size_t) h.insert])
            if (other != t)
            {
                if (h.firstOther < 0)
                    h.firstOther = other;
                h.others.push_back (other);
                ++h.sharedWith;
            }
    }
    return out;
}

// One track's target (the tests; the headers keep headerTargets' list per published song)
inline HeaderTarget headerTarget (const Song& song, int track)
{
    const auto all = headerTargets (song);
    return track >= 0 && track < (int) all.size() ? all[(std::size_t) track] : HeaderTarget {};
}

// --- what the header's M and S show and do (the rules above) ------------------------------------------------------------
// M lit: the track is silent through what M switches.
inline bool muteLit (const Song& song, int track, const HeaderTarget& h) noexcept
{
    if (h.muteActsOnInsert() && h.insert < (int) song.mixer.size())
        return song.mixer[(std::size_t) h.insert].muted;
    return song.isTrackMuted (track);
}
inline void toggleMute (Song& song, int track, const HeaderTarget& h)
{
    if (h.muteActsOnInsert() && h.insert < (int) song.mixer.size())
    {
        song.mixer[(std::size_t) h.insert].muted = ! song.mixer[(std::size_t) h.insert].muted;
        return;
    }
    if (track < 0)
        return;
    if (track >= (int) song.playlistTracks.size())
        song.playlistTracks.resize ((std::size_t) track + 1);
    song.playlistTracks[(std::size_t) track].muted = ! song.playlistTracks[(std::size_t) track].muted;
}
// S: the insert's solo (own or borrowed); nothing without an insert (never the Master's).
inline bool soloLit (const Song& song, const HeaderTarget& h) noexcept
{
    return h.insert > 0 && h.insert < (int) song.mixer.size() && song.mixer[(std::size_t) h.insert].solo;
}
inline void toggleSolo (Song& song, const HeaderTarget& h)
{
    if (h.insert > 0 && h.insert < (int) song.mixer.size())
        song.mixer[(std::size_t) h.insert].solo = ! song.mixer[(std::size_t) h.insert].solo;
}
// Volume (fader position 0..1) and pan (-1..1) of the insert; nothing without one.
inline bool setLevel (Song& song, const HeaderTarget& h, float volume, float pan)
{
    if (h.insert <= 0 || h.insert >= (int) song.mixer.size())
        return false;
    auto& m = song.mixer[(std::size_t) h.insert];
    m.volume = std::clamp (volume, 0.0f, 1.0f);
    m.pan = std::clamp (pan, -1.0f, 1.0f);
    return true;
}
} // namespace lynx::simplehdr
