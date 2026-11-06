// Round 177c: comping on take lanes — swipe, equal-power crossfades, used ranges, audition, flatten, more takes over a
// comp (docs/analise/REA_rodada177c.md). Included at the end of tests.cpp (after Round166eTests.inc: its render helper).

#include "core/Comping.h"
#include "core/TakeRecording.h"

namespace comp177
{
// three passes of a loop recording over [1000, 2000) on insert 2 ("Vox"): comp track 0, lanes 1..3 (take 3..1)
inline lynx::Song loopTakes()
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.bpm = 120.0; // 5 ms = 10 ticks
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    song.mixer[2].name = "Vox";
    song.loopStart = 1000;
    song.loopEnd = 2000;
    const auto parts = takes::loopPasses (takes::place (1000.0, 2900.0, 0.04, 0), 1000, 2000);
    takes::placeTakeClips (song, { { "/rec/Vox_Take1.wav", "Vox_Take1", 2 } }, parts);
    return song;
}

inline std::vector<lynx::Clip> onTrack (const lynx::Song& song, int track)
{
    std::vector<lynx::Clip> out;
    for (const auto& c : song.playlist)
        if (c.track == track)
            out.push_back (c);
    std::sort (out.begin(), out.end(), [] (const lynx::Clip& a, const lynx::Clip& b) { return a.start < b.start; });
    return out;
}

// the audio-to-song alignment of a clip (which take it plays)
inline std::int64_t alignment (const lynx::Clip& c) { return c.offset - c.start; }
} // namespace comp177

TEST (comp177_groups_and_loop_takes)
{
    using namespace lynx;
    auto song = comp177::loopTakes();
    const auto g = comp::groupOf (song, 2);
    CHECK (g.valid() && g.comp == 0 && g.first == 1 && g.last == 3 && g.lanes() == 3);
    CHECK (comp::groupOf (song, 0).comp == 0 && comp::groupOf (song, 0).isLane (3) && ! comp::groupOf (song, 0).isLane (0));
    CHECK (! comp::groupOf (song, 4).valid() && ! comp::groupOf (song, -1).valid());
    CHECK (comp::crossfadeTicks (song) == 10);
    song.compCrossfadeMs = 0.0f;
    CHECK (comp::crossfadeTicks (song) == 0);
    song.compCrossfadeMs = Song::defaultCompCrossfadeMs;

    // every pass on a lane (newest first), the last one also on the comp; the comp plays the last pass
    const auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 1 && ! c[0].muted && c[0].offset == 2000);
    CHECK (comp177::onTrack (song, 1).size() == 1 && comp177::onTrack (song, 1)[0].offset == 2000 && comp177::onTrack (song, 1)[0].muted);
    CHECK (comp177::onTrack (song, 3).size() == 1 && comp177::onTrack (song, 3)[0].offset == 0);
    const auto used = comp::usedRanges (song, g);
    CHECK (used.size() == 1 && used[0].lane == 1 && used[0].from == 1000 && used[0].to == 1900);
}

TEST (comp177_swipe_crossfades_and_back)
{
    using namespace lynx;
    auto song = comp177::loopTakes();
    const auto original = comp177::onTrack (song, 0);

    // take 1 (lane 3) for [1200, 1500): three comp pieces, equal-power crossfades of 10 ticks centred on 1200 and 1500
    CHECK (comp::swipe (song, 3, 1200, 1500));
    auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3);
    if (c.size() == 3)
    {
        CHECK (c[0].start == 1000 && c[0].end() == 1205 && c[0].fadeOut == 10 && c[0].fadeCurve == 2 && comp177::alignment (c[0]) == 1000);
        CHECK (c[1].start == 1195 && c[1].end() == 1505 && c[1].fadeIn == 10 && c[1].fadeOut == 10 && c[1].fadeCurve == 3
               && comp177::alignment (c[1]) == -1000 && ! c[1].muted);
        CHECK (c[2].start == 1495 && c[2].end() == 1900 && c[2].fadeIn == 10 && c[2].fadeCurve == 1 && comp177::alignment (c[2]) == 1000);
    }
    const auto g = comp::groupOf (song, 0);
    auto used = comp::usedRanges (song, g);
    std::sort (used.begin(), used.end(), [] (const comp::Used& a, const comp::Used& b) { return a.from < b.from; });
    CHECK (used.size() == 3);
    if (used.size() == 3)
        CHECK (used[0].lane == 1 && used[0].to == 1200 && used[1].lane == 3 && used[1].from == 1200 && used[1].to == 1500
               && used[2].lane == 1 && used[2].from == 1500 && used[2].to == 1900);

    // the same swipe again changes nothing; a swipe across a switch moves it
    CHECK (! comp::swipe (song, 3, 1200, 1500));
    CHECK (comp::swipe (song, 2, 1400, 1600)); // take 2 over the second switch
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 4 && c[1].end() == 1405 && c[2].start == 1395 && c[2].end() == 1605 && comp177::alignment (c[2]) == 0
           && c[3].start == 1595 && comp177::alignment (c[3]) == 1000);

    // another crossfade length re-applies to every switch; 0 = hard cuts
    comp::applyCrossfades (song, 0, 0);
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 4 && c[0].end() == 1200 && c[1].start == 1200 && c[1].fadeIn == 0 && c[0].fadeCurve == 0 && c[3].start == 1600);
    comp::applyCrossfades (song, 0, 40);
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 4 && c[0].end() == 1220 && c[1].start == 1180 && c[1].fadeIn == 40 && c[1].fadeCurve == 3);

    // the last take over the whole range: back to the original comp
    CHECK (comp::swipe (song, 1, 1000, 1900));
    CHECK (comp177::onTrack (song, 0).size() == 1 && comp177::onTrack (song, 0)[0].start == original[0].start
           && comp177::onTrack (song, 0)[0].length == original[0].length && comp177::onTrack (song, 0)[0].offset == original[0].offset
           && comp177::onTrack (song, 0)[0].fadeCurve == 0);

    // invalid: the comp track itself, an empty range, a track outside any group
    CHECK (! comp::swipe (song, 0, 1000, 1200) && ! comp::swipe (song, 3, 1500, 1500) && ! comp::swipe (song, 7, 1000, 1200));
}

TEST (comp177_crossfade_limits_and_gaps)
{
    using namespace lynx;
    auto song = comp177::loopTakes();
    // a 4-tick piece gets at most a 2-tick crossfade on each side
    CHECK (comp::swipe (song, 3, 1300, 1304));
    auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3 && c[1].fadeIn == 4 && c[1].fadeOut == 4 && c[1].start == 1298 && c[1].end() == 1306);

    // a lane with a gap: the comp is cleared where the lane has no take
    song = comp177::loopTakes();
    for (auto& clip : song.playlist)
        if (clip.track == 3)
            clip.length = 300; // take 1 = [1000, 1300)
    CHECK (comp::swipe (song, 3, 1200, 1500));
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3 && c[1].start == 1195 && c[1].end() == 1300 && c[1].fadeOut == 0 && c[2].start == 1500 && c[2].fadeIn == 0);

    // a comp crossfade keeps its curve while FL's automatic crossfades are on
    song = comp177::loopTakes();
    song.autoCrossfades = true;
    CHECK (comp::swipe (song, 3, 1200, 1500));
    clipops::updateCrossfades (song);
    for (const auto& clip : comp177::onTrack (song, 0))
        CHECK (clip.crossfadeIn == 0 && clip.crossfadeOut == 0);
}

TEST (comp177_equal_power_render)
{
    using namespace lynx;
    for (int k = 0; k <= 10; ++k)
    {
        const auto x = (float) k / 10.0f;
        CHECK (std::abs (clipops::equalPowerCurve (x) * clipops::equalPowerCurve (x) + clipops::equalPowerCurve (1.0f - x) * clipops::equalPowerCurve (1.0f - x) - 1.0f) < 1e-5f);
    }

    // one take channel (constant 1.0), two lanes of it, the second swiped in from beat 2: a 100 ms crossfade
    auto make = [] (bool muteA, bool muteB) {
        auto s = round166e::overlapSong (false);
        s.playlist.clear();
        s.playlistTracks.resize (3);
        s.playlistTracks[1].grouped = s.playlistTracks[2].grouped = true;
        s.playlist.push_back (round166e::audioClip (1, 0, 4 * ppq));
        s.playlist.push_back (round166e::audioClip (2, 0, 4 * ppq));
        s.playlist[0].muted = s.playlist[1].muted = true;
        s.compCrossfadeMs = 100.0f; // 192 ticks = 4800 samples
        comp::swipe (s, 1, 0, 4 * ppq);
        comp::swipe (s, 2, 2 * ppq, 4 * ppq);
        for (auto& c : s.playlist)
            if (c.track == 0)
                c.muted = (c.start == 0 && muteA) || (c.start != 0 && muteB);
        return s;
    };
    const auto a = make (false, true), b = make (true, false);
    const auto full = round166e::levelAt (a, 1.0);
    CHECK (full > 0.1f);
    // levelAt(2.0) reads sample 187·256 + 255 = 48127; the crossfade runs over samples [45600, 50400)
    const auto t = (48127.0f - 45600.0f) / 4800.0f;
    const auto la = round166e::levelAt (a, 2.0) / full, lb = round166e::levelAt (b, 2.0) / full;
    CHECK (std::abs (la - clipops::equalPowerCurve (1.0f - t)) < 0.01f);
    CHECK (std::abs (lb - clipops::equalPowerCurve (t)) < 0.01f);
    CHECK (std::abs (la * la + lb * lb - 1.0f) < 0.02f); // constant power
}

TEST (comp177_audition_flatten_and_more_takes)
{
    using namespace lynx;
    auto song = comp177::loopTakes();
    const auto tps = ticksPerSample (120.0, 48000.0);
    auto playing = [&] (const Song& s) {
        EventBuffer ev;
        scheduleSong (s, 1100.0, 1000, tps, ev, true);
        std::vector<int> tracks;
        for (const auto& e : ev)
            if (e.type == NoteEvent::Type::on && e.key >= audioClipKeyBase)
                tracks.push_back (s.playlist[(std::size_t) (e.key - audioClipKeyBase)].track);
        return tracks;
    };
    CHECK (playing (song) == std::vector<int> { 0 });
    comp::setAudition (song, 3);
    CHECK (song.auditionLane == 3 && song.auditionComp == 0);
    CHECK (playing (song) == std::vector<int> { 3 }); // the lane (its take is muted) instead of the comp
    comp::setAudition (song, 0); // the comp track is no lane: off
    CHECK (song.auditionLane == -1 && playing (song) == std::vector<int> { 0 });

    // a second loop recording over the comp: three more lanes under it (newest first), its last pass on the comp
    const auto parts = takes::loopPasses (takes::place (1000.0, 2900.0, 0.04, 0), 1000, 2000);
    takes::placeTakeClips (song, { { "/rec/Vox_Take2.wav", "Vox_Take2", 2 } }, parts);
    const auto g = comp::groupOf (song, 0);
    CHECK (g.valid() && g.lanes() == 6);
    CHECK (song.playlistTracks[1].name == "Vox (take 6)" && song.playlistTracks[3].name == "Vox (take 4)" && song.playlistTracks[4].name == "Vox (take 3)");
    const auto newChannel = (int) song.channels.size() - 1;
    auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 1 && c[0].channel == newChannel && c[0].offset == 2000 && ! c[0].muted);

    // flatten: the comp stays, the lanes go, the tracks below move up
    Clip below;
    below.pattern = 0;
    below.track = 9;
    below.length = 100;
    song.playlist.push_back (below);
    CHECK (comp::flatten (song, 0));
    CHECK (! comp::groupOf (song, 0).valid() && comp177::onTrack (song, 0).size() == 1 && comp177::onTrack (song, 3).size() == 1
           && comp177::onTrack (song, 3)[0].kind == ClipKind::pattern && song.playlist.size() == 2);
    CHECK (! comp::flatten (song, 0));

    // a single take (punch) over a track made a comp with "insert take lanes": one more lane, its part on the comp
    CHECK (comp::makeLanes (song, 0) == 1 && comp::groupOf (song, 0).lanes() == 1);
    CHECK (comp::makeLanes (song, 0) == -1 && comp::makeLanes (song, 1) == -1);
    takes::Part punch;
    punch.start = 1200;
    punch.length = 300;
    punch.offset = 500; // after a pre-roll: the file has audio before the punch-in, the crossfade starts in it
    takes::placeTakeClips (song, { { "/rec/Vox_Take3.wav", "Vox_Take3", 2 } }, { punch });
    CHECK (comp::groupOf (song, 0).lanes() == 2 && song.playlistTracks[1].name == "Vox (take 2)");
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3 && c[1].channel == (int) song.channels.size() - 1 && c[1].start == 1195 && c[1].offset == 495 && c[1].fadeCurve == 1
           && c[1].fadeOut == 5 && c[2].fadeIn == 5); // (the punch-out: no audio known after it, short linear fades)
}

TEST (comp177_a_take_never_replaces_clips_of_an_ordinary_group)
{
    using namespace lynx;
    // a track group the user made: the header's clip is on no lane, so a take recorded over it must not touch it
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    song.playlistTracks.resize (2);
    song.playlistTracks[0].mode = PlaylistTrackMode::audio;
    song.playlistTracks[0].mixerTrack = 2;
    song.playlistTracks[1].grouped = true;
    ChannelDef own;
    own.instrument = InstrumentKind::audioClip;
    own.samplePath = "/rec/own.wav";
    song.channels.push_back (own);
    Clip mine;
    mine.kind = ClipKind::audio;
    mine.channel = (int) song.channels.size() - 1;
    mine.track = 0;
    mine.start = 1000;
    mine.length = 900;
    song.playlist.push_back (mine);

    takes::Part part;
    part.start = 1200;
    part.length = 300;
    takes::placeTakeClips (song, { { "/rec/t.wav", "t", 2 } }, { part });
    const auto header = comp177::onTrack (song, 0);
    CHECK (header.size() == 1 && header[0].start == 1000 && header[0].length == 900 && header[0].channel == mine.channel);
    CHECK (comp::groupOf (song, 0).lanes() == 1);       // no lane was added
    CHECK (song.playlist.size() == 2 && song.playlist.back().track >= 2); // the take went to a new track, as before
}
