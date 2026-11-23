// Round 190oo (design review 2, CS-24): the Simple headers on an FL project's tracks (core/SimpleHeaderTarget.h). The
// controls follow the Clip-launch Session mixer's insert (sessionmixer::stripOf); M stays the Playlist track's mute on
// a borrowed insert (only that track goes silent), S is the insert's solo, volume / pan the insert's; several inserts,
// the Master or nothing: M only, never an edit of the Master.

#include "core/SessionMixer.h"
#include "core/SimpleHeaderTarget.h"
#include "core/SimpleTracks.h"

namespace simplefl190oo
{
// An FL-like project: channels 0 KICK and 1 CLAP on insert 2 (shared), 2 SNARE on insert 3, 3 RIDE on the Master (0),
// 4 TOM on insert 4. Track 0 plays KICK, 1 plays CLAP, 2 plays SNARE, 3 a pattern with SNARE and TOM (two inserts),
// 4 plays RIDE (the Master), 5 is empty; no track mode anywhere (FL's default tracks).
inline lynx::Song song()
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer();
    const int routes[] = { 2, 2, 3, 0, 4 };
    s.channels.resize (5);
    for (int c = 0; c < 5; ++c)
    {
        s.channels[(std::size_t) c].instrument = lynx::InstrumentKind::synth;
        s.channels[(std::size_t) c].mixerTrack = routes[c];
    }
    s.playlistTracks.resize (6);
    const auto pattern = [&s] (std::initializer_list<int> channels) {
        lynx::Pattern p;
        int at = 0;
        for (const auto ch : channels)
        {
            lynx::Note n;
            n.channel = ch;
            n.start = (std::int64_t) (at++) * lynx::ppq;
            n.length = lynx::ppq / 2;
            p.notes.push_back (n);
        }
        s.patterns.push_back (p);
        return (int) s.patterns.size() - 1;
    };
    const auto place = [&s] (int track, int p) {
        lynx::Clip c;
        c.kind = lynx::ClipKind::pattern;
        c.pattern = p;
        c.track = track;
        c.start = 0;
        c.length = 4 * lynx::ppq;
        s.playlist.push_back (c);
    };
    place (0, pattern ({ 0 }));
    place (1, pattern ({ 1 }));
    place (2, pattern ({ 2 }));
    place (3, pattern ({ 2, 4 }));
    place (4, pattern ({ 3 }));
    return s;
}
} // namespace simplefl190oo

TEST (simplefl190oo_fl_track_gets_the_session_mixer_insert)
{
    using namespace lynx;
    const auto s = simplefl190oo::song();
    const auto all = simplehdr::headerTargets (s);
    CHECK (all.size() >= 6);
    // one track whose clips all play through one insert: that insert (KICK -> 2, SNARE -> 3), borrowed, not its own
    CHECK (all[0].insert == 2 && ! all[0].own && all[0].insertCount == 1 && all[0].hasControls());
    CHECK (all[2].insert == 3 && ! all[2].own);
    // the same rule as the Session mixer's strip, track by track
    for (int t = 0; t < 6; ++t)
        CHECK (all[(std::size_t) t].insert == sessionmixer::stripOf (s, t).insert);
    // a track past the end of the list: nothing
    CHECK (simplehdr::headerTarget (s, 40).insert == -1 && ! simplehdr::headerTarget (s, 40).hasControls());
}

TEST (simplefl190oo_shared_insert_mute_solo_volume)
{
    using namespace lynx;
    auto s = simplefl190oo::song();
    const auto kick = simplehdr::headerTarget (s, 0), clap = simplehdr::headerTarget (s, 1);
    // KICK and CLAP play through insert 2: both headers act on it and each names the other
    CHECK (kick.insert == 2 && clap.insert == 2);
    CHECK (kick.sharedWith == 1 && kick.firstOther == 1 && kick.others == std::vector<int> { 1 });
    CHECK (clap.sharedWith == 1 && clap.firstOther == 0);
    // SNARE's insert 3 is also played by track 3 (whose clips use inserts 3 and 4)
    const auto snare = simplehdr::headerTarget (s, 2);
    CHECK (snare.sharedWith == 1 && snare.firstOther == 3);

    // M on KICK: the Playlist track's mute. KICK is silent (lit), CLAP keeps playing (unlit), the insert is not muted
    CHECK (! kick.muteActsOnInsert());
    simplehdr::toggleMute (s, 0, kick);
    CHECK (s.isTrackMuted (0) && simplehdr::muteLit (s, 0, kick));
    CHECK (! s.isTrackMuted (1) && ! simplehdr::muteLit (s, 1, clap));
    CHECK (! s.mixer[2].muted);
    simplehdr::toggleMute (s, 0, kick);
    CHECK (! s.isTrackMuted (0) && ! simplehdr::muteLit (s, 0, kick));
    // a mute of the insert from the Mixer does not light the borrowed M (M says what M switches)
    s.mixer[2].muted = true;
    CHECK (! simplehdr::muteLit (s, 0, kick));
    s.mixer[2].muted = false;

    // S on KICK: the insert's solo, lit on both headers that share it (both stay audible)
    simplehdr::toggleSolo (s, kick);
    CHECK (s.mixer[2].solo && simplehdr::soloLit (s, kick) && simplehdr::soloLit (s, clap));
    CHECK (! simplehdr::soloLit (s, snare));
    simplehdr::toggleSolo (s, clap);
    CHECK (! s.mixer[2].solo);

    // volume / pan: the insert's, so both headers read the new level
    const auto masterVolume = s.mixer[0].volume;
    CHECK (simplehdr::setLevel (s, clap, 0.5f, -0.25f));
    CHECK (s.mixer[2].volume == 0.5f && s.mixer[2].pan == -0.25f);
    CHECK (s.mixer[0].volume == masterVolume); // (the Master untouched)
}

TEST (simplefl190oo_two_inserts_and_the_master_keep_only_m)
{
    using namespace lynx;
    auto s = simplefl190oo::song();
    // track 3: SNARE (3) and TOM (4) — several inserts: nothing chosen, as the Session mixer
    const auto both = simplehdr::headerTarget (s, 3);
    CHECK (both.insert == -1 && both.insertCount == 2 && ! both.hasControls() && both.sharedWith == 0);
    // track 4: RIDE on the Master — no controls; nothing ever edits insert 0
    const auto master = simplehdr::headerTarget (s, 4);
    CHECK (master.insert == -1 && master.insertCount == 1 && ! master.hasControls());
    const auto before = s.mixer[0];
    simplehdr::toggleSolo (s, master);
    CHECK (! simplehdr::setLevel (s, master, 0.1f, 1.0f));
    simplehdr::toggleMute (s, 4, master); // M: the Playlist track
    CHECK (s.mixer[0].solo == before.solo && s.mixer[0].muted == before.muted && s.mixer[0].volume == before.volume && s.mixer[0].pan == before.pan);
    CHECK (s.isTrackMuted (4) && simplehdr::muteLit (s, 4, master));
    // the empty track
    CHECK (simplehdr::headerTarget (s, 5).insert == -1 && simplehdr::headerTarget (s, 5).insertCount == 0);
    // an FL audio track linked to the Master (track mode): the Master again, no controls
    s.playlistTracks[5].mode = PlaylistTrackMode::audio;
    s.playlistTracks[5].mixerTrack = 0;
    CHECK (simplehdr::headerTarget (s, 5).insert == -1 && sessionmixer::stripOf (s, 5).insert == -1);
    // still the Session mixer's rule everywhere
    for (int t = 0; t < 6; ++t)
        CHECK (simplehdr::headerTarget (s, t).insert == sessionmixer::stripOf (s, t).insert);
}

TEST (simplefl190oo_simple_tracks_keep_their_own_insert)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    q.channel = c;
    q.emptyBars = 4;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.track >= 0 && r.insert > 0);
    const auto own = simplehdr::headerTarget (s, r.track);
    CHECK (own.own && own.insert == r.insert && own.muteActsOnInsert() && own.sharedWith == 0);
    // M on a Simple track is its insert's mute, as before this round
    simplehdr::toggleMute (s, r.track, own);
    CHECK (s.mixer[(std::size_t) r.insert].muted && simplehdr::muteLit (s, r.track, own) && ! s.isTrackMuted (r.track));
    simplehdr::toggleSolo (s, own);
    CHECK (s.mixer[(std::size_t) r.insert].solo);
}
