// Round 177c: comping on take lanes — swipe, equal-power crossfades, used ranges, audition, flatten, more takes over a
// comp (docs/analise/REA_rodada177c.md). Included at the end of tests.cpp (after Round166eTests.inc: its render helper).

#include "core/Comping.h"
#include "core/TakeRecording.h"

namespace comp177
{
// three passes of a loop recording over [1000, 2000) on insert 2 ("Vox"), after "Comp these takes": comp track 0, lanes
// 1..3 (take 1..3)
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
    comp::compTakes (song, 0);
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

    // every pass on a lane (oldest first), the last one also on the comp; the comp plays the last pass
    const auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 1 && ! c[0].muted && c[0].offset == 2000);
    CHECK (comp177::onTrack (song, 3).size() == 1 && comp177::onTrack (song, 3)[0].offset == 2000 && comp177::onTrack (song, 3)[0].muted);
    CHECK (comp177::onTrack (song, 1).size() == 1 && comp177::onTrack (song, 1)[0].offset == 0);
    const auto used = comp::usedRanges (song, g);
    CHECK (used.size() == 1 && used[0].lane == 3 && used[0].from == 1000 && used[0].to == 1900);
}

TEST (comp177_swipe_crossfades_and_back)
{
    using namespace lynx;
    auto song = comp177::loopTakes();
    const auto original = comp177::onTrack (song, 0);

    // take 1 (lane 1) for [1200, 1500): three comp pieces, equal-power crossfades of 10 ticks centred on 1200 and 1500
    CHECK (comp::swipe (song, 1, 1200, 1500));
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
        CHECK (used[0].lane == 3 && used[0].to == 1200 && used[1].lane == 1 && used[1].from == 1200 && used[1].to == 1500
               && used[2].lane == 3 && used[2].from == 1500 && used[2].to == 1900);

    // the same swipe again changes nothing; a swipe across a switch moves it
    CHECK (! comp::swipe (song, 1, 1200, 1500));
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
    CHECK (comp::swipe (song, 3, 1000, 1900));
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
    CHECK (comp::swipe (song, 1, 1300, 1304));
    auto c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3 && c[1].fadeIn == 4 && c[1].fadeOut == 4 && c[1].start == 1298 && c[1].end() == 1306);

    // a lane with a gap: the comp is cleared where the lane has no take
    song = comp177::loopTakes();
    for (auto& clip : song.playlist)
        if (clip.track == 1)
            clip.length = 300; // take 1 = [1000, 1300)
    CHECK (comp::swipe (song, 1, 1200, 1500));
    c = comp177::onTrack (song, 0);
    CHECK (c.size() == 3 && c[1].start == 1195 && c[1].end() == 1300 && c[1].fadeOut == 0 && c[2].start == 1500 && c[2].fadeIn == 0);

    // a comp crossfade keeps its curve while FL's automatic crossfades are on
    song = comp177::loopTakes();
    song.autoCrossfades = true;
    CHECK (comp::swipe (song, 1, 1200, 1500));
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

    // flatten: the comp stays, the lanes go, the tracks below move up
    Clip below;
    below.pattern = 0;
    below.track = 9;
    below.length = 100;
    song.playlist.push_back (below);
    CHECK (comp::isComp (song, 0));
    CHECK (comp::flatten (song, 0));
    CHECK (! comp::isComp (song, 0) && ! song.playlistTracks[0].compTrack);
    CHECK (! comp::groupOf (song, 0).valid() && comp177::onTrack (song, 0).size() == 1 && comp177::onTrack (song, 6).size() == 1
           && comp177::onTrack (song, 6)[0].kind == ClipKind::pattern && song.playlist.size() == 2);
    CHECK (! comp::flatten (song, 0));
}

TEST (comp177_fl_loop_take_layout)
{
    using namespace lynx;
    // what FL 26 leaves after a loop recording (REA round 177c §2.1)
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.bpm = 120.0;
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    song.mixer[2].name = "Vox";
    const auto clean = song;
    const auto parts = takes::loopPasses (takes::place (1000.0, 2900.0, 0.04, 0), 1000, 2000);
    CHECK (parts.size() == 3);

    // Blend off: one track per pass, the first on top as an open group's header; every pass muted, the last too
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, parts);
    CHECK (song.playlist.size() == 3);
    for (const auto& c : song.playlist)
        CHECK (c.muted);
    for (int t = 0; t < 3; ++t)
        CHECK (comp177::onTrack (song, t).size() == 1 && comp177::onTrack (song, t)[0].offset == t * 1000);
    CHECK (! song.playlistTracks[0].grouped && ! song.playlistTracks[0].collapsed && song.playlistTracks[1].grouped && song.playlistTracks[2].grouped);
    CHECK (song.playlistTracks[0].mode == PlaylistTrackMode::unassigned && song.playlistTracks[0].mixerTrack == -1);
    CHECK (song.channels.back().mixerTrack == 2);
    CHECK (! song.playlistTracks[0].compTrack);
    // no comp yet: nothing can write over the first pass
    CHECK (takes::compTrackOf (song, 2) == -1);
    int lastPass = -1;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].track == 2)
            lastPass = i;
    CHECK (! takes::promoteTake (song, lastPass, 0, 1000, 1100));

    // recording again over it: a new group on the next tracks free in that range; nothing replaced, no track moved
    Clip other;
    other.pattern = 0;
    other.track = 5;
    other.length = 100;
    song.playlist.push_back (other);
    const auto before = song.playlist;
    const auto tracksBefore = song.playlistTracks.size();
    takes::placeTakeClips (song, { { "/rec/b.wav", "b", 2 } }, parts);
    CHECK (song.playlist.size() == before.size() + 3);
    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK (song.playlist[i].track == before[i].track && song.playlist[i].start == before[i].start);
    CHECK (comp177::onTrack (song, 3).size() == 1 && comp177::onTrack (song, 4).size() == 1 && comp177::onTrack (song, 3)[0].offset == 0);
    CHECK (comp177::onTrack (song, 5).size() == 2); // the pattern clip (ticks 0-100) is outside this range
    CHECK (! song.playlistTracks[3].grouped && song.playlistTracks[4].grouped && song.playlistTracks[5].grouped);
    CHECK (comp::groupOf (song, 0).last == 2 && tracksBefore <= song.playlistTracks.size());

    // rows stay "Track N": the insert's name is not used (R10)
    CHECK (song.playlistTracks[0].name.empty() && song.playlistTracks[1].name.empty() && song.playlistTracks[2].name.empty());

    // the first track free in the range, not the first after everything (R11, R12): nothing plays at tick 5000 yet
    {
        auto later = song;
        takes::Part far;
        far.start = 5000;
        far.length = 100;
        takes::TakeOptions loop;
        loop.loopRecord = true;
        const auto count = later.playlist.size();
        takes::placeTakeClips (later, { { "/rec/c.wav", "c", 2 } }, { far }, loop);
        CHECK (later.playlist.size() == count + 1 && later.playlist.back().track == 0);
    }

    // a named first track: the other passes take its name (R13)
    {
        auto named = clean;
        named.playlistTracks.resize (1);
        named.playlistTracks[0].name = "REC";
        takes::placeTakeClips (named, { { "/rec/a.wav", "a", 2 } }, parts);
        CHECK (named.playlistTracks[0].name == "REC" && named.playlistTracks[1].name == "REC" && named.playlistTracks[2].name == "REC");
    }

    // started in the middle of the loop (R11): the first pass goes to the first track free in its part of the loop and stays
    // what that track was, the next passes follow below it and join its group
    {
        auto mid = clean;
        Clip early;
        early.kind = ClipKind::audio;
        early.channel = 0;
        early.track = 2;
        early.start = 1000;
        early.length = 200;
        Clip a = early, b = early;
        a.track = 0, a.length = 1000;
        b.track = 1, b.length = 1000;
        mid.playlist = { a, b, early };
        mid.playlistTracks.resize (3);
        mid.playlistTracks[1].grouped = mid.playlistTracks[2].grouped = true;
        std::vector<takes::Part> passes (2);
        passes[0].start = 1500, passes[0].length = 500, passes[0].offset = 0;
        passes[1].start = 1000, passes[1].length = 1000, passes[1].offset = 500;
        takes::placeTakeClips (mid, { { "/rec/m.wav", "m", 2 } }, passes);
        const auto row2 = comp177::onTrack (mid, 2), row3 = comp177::onTrack (mid, 3);
        CHECK (row2.size() == 2 && row2[1].start == 1500 && row3.size() == 1 && row3[0].start == 1000);
        CHECK (mid.playlistTracks[2].grouped && mid.playlistTracks[3].grouped && comp::groupOf (mid, 0).last == 3);
    }

    // passes that don't overlap (R12): still one track per pass, in a new group
    {
        auto apart = clean;
        std::vector<takes::Part> passes (2);
        passes[0].start = 1500, passes[0].length = 500;
        passes[1].start = 1000, passes[1].length = 100, passes[1].offset = 500;
        takes::TakeOptions loop;
        loop.loopRecord = true;
        takes::placeTakeClips (apart, { { "/rec/n.wav", "n", 2 } }, passes, loop);
        CHECK (comp177::onTrack (apart, 0).size() == 1 && comp177::onTrack (apart, 1).size() == 1 && ! apart.playlistTracks[0].grouped && apart.playlistTracks[1].grouped);
        for (const auto& c : apart.playlist)
            CHECK (c.muted);
    }

    // never onto a comp group: it must keep what it plays
    {
        auto comped = clean;
        takes::placeTakeClips (comped, { { "/rec/a.wav", "a", 2 } }, parts);
        comp::compTakes (comped, 0);
        takes::Part far;
        far.start = 5000;
        far.length = 100;
        takes::TakeOptions loop;
        loop.loopRecord = true;
        takes::placeTakeClips (comped, { { "/rec/c.wav", "c", 2 } }, { far }, loop);
        CHECK (comped.playlist.back().track == 4);
    }

    // stopped just past a pass boundary: FL keeps even a 1-tick pass, on its own track, muted
    song = clean;
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, takes::loopPasses (takes::place (1000.0, 2010.0, 0.04, 0), 1000, 2000));
    CHECK (song.playlist.size() == 3 && comp177::onTrack (song, 2).size() == 1 && comp177::onTrack (song, 2)[0].length == 10 && comp177::onTrack (song, 2)[0].muted);

    // Blend on: every pass plays
    song = clean;
    takes::TakeOptions blend;
    blend.blend = blend.loopRecord = true;
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, parts, blend);
    CHECK (song.playlist.size() == 3);
    for (const auto& c : song.playlist)
        CHECK (! c.muted);

    // a lone pass: muted when loop recording with Blend off (R1), playing without Loop record (R4); no group either way
    takes::Part part;
    part.start = 1000;
    part.length = 226;
    song = clean;
    takes::TakeOptions loop;
    loop.loopRecord = true;
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, { part }, loop);
    CHECK (song.playlist.size() == 1 && song.playlist[0].muted && ! comp::groupOf (song, 0).valid());
    song = clean;
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, { part });
    CHECK (song.playlist.size() == 1 && ! song.playlist[0].muted);
}

TEST (comp177_comp_these_takes)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.bpm = 120.0;
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    song.mixer[2].name = "Vox";
    Clip above, below;
    above.pattern = below.pattern = 0;
    above.track = 0;
    above.start = 1000;
    above.length = 1000;
    below.track = 5;
    below.length = 100;
    song.playlist = { above, below };
    song.playlistTracks.resize (1);
    song.playlistTracks[0].name = "Drums";
    const auto parts = takes::loopPasses (takes::place (1000.0, 2900.0, 0.04, 0), 1000, 2000);
    takes::placeTakeClips (song, { { "/rec/a.wav", "a", 2 } }, parts);
    CHECK (comp177::onTrack (song, 1).size() == 1 && comp177::onTrack (song, 3).size() == 1);

    CHECK (! comp::canComp (song, 0) && comp::canComp (song, 2));
    CHECK (comp::compTakes (song, 0) == -1); // no group there
    const auto beforeComp = song;
    CHECK (comp::compTakes (song, 3) == 1);
    // the undo step is named for what it does
    CHECK (std::string (describeChange (beforeComp, song).text) == "Comp takes");

    // one comp track above the group: what is above it stays, the group moves down by one
    CHECK (song.playlistTracks[1].compTrack && song.playlistTracks[1].name == "Track 2 comp" && ! song.playlistTracks[1].grouped
           && ! song.playlistTracks[1].collapsed);
    CHECK (comp177::onTrack (song, 0).size() == 1 && comp177::onTrack (song, 6).size() == 1);
    CHECK (song.playlistTracks[0].name == "Drums");
    for (int k = 0; k < 3; ++k)
    {
        const auto lane = comp177::onTrack (song, 2 + k);
        CHECK (lane.size() == 1 && lane[0].muted && lane[0].offset == k * 1000);
        CHECK (song.playlistTracks[(std::size_t) (2 + k)].grouped && song.playlistTracks[(std::size_t) (2 + k)].name == "Track 2 (take " + std::to_string (k + 1) + ")");
    }
    const auto c = comp177::onTrack (song, 1);
    CHECK (c.size() == 1 && ! c[0].muted && c[0].offset == 2000 && c[0].length == 900);
    CHECK (comp::isComp (song, 1) && ! comp::canComp (song, 3) && takes::compTrackOf (song, 3) == 1);
    CHECK (comp::compTakes (song, 3) == -1);

    // a take recorded over it does not join it
    const auto lanes = comp::groupOf (song, 1).lanes();
    takes::placeTakeClips (song, { { "/rec/b.wav", "b", 2 } }, parts);
    CHECK (comp::groupOf (song, 1).lanes() == lanes && comp177::onTrack (song, 1).size() == 1 && comp177::onTrack (song, 1)[0].offset == 2000);

    // every pass was playing (Blend on), and FL's own takes have one channel per pass: all muted, the newest channel plays
    song = Song::makeDefault();
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    for (int k = 0; k < 2; ++k)
    {
        ChannelDef take;
        take.instrument = InstrumentKind::audioClip;
        take.samplePath = "/rec/fl" + std::to_string (k) + ".wav";
        song.channels.push_back (take);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = (int) song.channels.size() - 1;
        clip.track = k;
        clip.start = 1000;
        clip.length = 500;
        song.playlist.push_back (clip);
    }
    song.playlistTracks.resize (2);
    song.playlistTracks[1].grouped = true;
    const auto newest = (int) song.channels.size() - 1;
    CHECK (comp::compTakes (song, 0) == 0);
    CHECK (comp177::onTrack (song, 0).size() == 1 && comp177::onTrack (song, 0)[0].channel == newest);
    CHECK (comp177::onTrack (song, 1)[0].muted && comp177::onTrack (song, 2)[0].muted);
    CHECK (song.playlistTracks[0].name == "Track 1 comp" && song.playlistTracks[1].name == "Track 1 (take 1)" && song.playlistTracks[2].name == "Track 1 (take 2)");
}

TEST (comp177_existing_projects_do_not_change)
{
    using namespace lynx;
    // a project saved before round 177c: loop takes in round 163's layout, with no comp mark
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.bpm = 120.0;
    song.playlist.clear();
    while (song.mixer.size() < 4)
        song.mixer.emplace_back();
    song.mixer[2].name = "Gtr";
    takes::TakeOptions old;
    old.lastPassPlays = true;
    takes::placeTakeClips (song, { { "/rec/g.wav", "g", 2 } }, takes::loopPasses (takes::place (1000.0, 2900.0, 0.04, 0), 1000, 2000), old);
    CHECK (song.playlistTracks[0].collapsed && song.playlistTracks[1].grouped && ! song.playlistTracks[0].compTrack);
    // (saving and loading it without a comp mark: --selftest, comp_crossfades)
    // its promote still works, without a comp mark
    CHECK (takes::compTrackOf (song, 1) == 0 && ! comp::isComp (song, 0));

    // "Comp these takes" on it keeps every pass: the last pass was only on the header, it becomes a lane too
    auto comped = song;
    CHECK (comp::compTakes (comped, 1) == 0);
    CHECK (comped.playlist.size() == 4 && comp::groupOf (comped, 0).lanes() == 3);
    CHECK (comp177::onTrack (comped, 0).size() == 1 && comp177::onTrack (comped, 0)[0].offset == 2000 && ! comp177::onTrack (comped, 0)[0].muted);
    CHECK (comp177::onTrack (comped, 1)[0].offset == 2000 && comp177::onTrack (comped, 1)[0].muted);
    CHECK (comped.playlistTracks[1].mode == PlaylistTrackMode::unassigned);
    CHECK (comped.playlistTracks[1].name == "Track 1 (take 3)" && comped.playlistTracks[3].name == "Track 1 (take 1)");

    // a header the user named gives its name: "<name> comp", "<name> (take N)"
    auto named = song;
    named.playlistTracks[0].name = "Lead";
    CHECK (comp::compTakes (named, 1) == 0);
    CHECK (named.playlistTracks[0].name == "Lead comp" && named.playlistTracks[1].name == "Lead (take 3)");
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

TEST (comp177_fl_clip_mute_survives_import_and_export)
{
    using namespace lynx;
    // FL's item flag 0x2000 = muted clip (FL's Mute tool toggled it, and FL leaves loop-recorded takes muted when "Blend
    // recorded notes" is off): such a clip must stay silent in LYNX and come back muted in FL
    auto song = comp177::loopTakes();
    int muted = 0, playing = 0;
    for (const auto& c : song.playlist)
        ++(c.muted ? muted : playing);
    CHECK (muted == 3 && playing == 1);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.arrangements.size() == 1);
    if (project.arrangements.size() == 1)
    {
        CHECK (project.arrangements[0].items.size() == 4);
        int flagged = 0;
        for (const auto& item : project.arrangements[0].items)
            flagged += item.muted() ? 1 : 0;
        CHECK (flagged == 3);
    }
    const auto back = importFlp (project, {});
    int mutedBack = 0;
    for (const auto& c : back.playlist)
        mutedBack += c.muted ? 1 : 0;
    CHECK (back.playlist.size() == 4 && mutedBack == 3);
}

TEST (comp177_blend_starts_off_only_for_new_installs)
{
    using lynx::RecordMode;
    // a new install: FL's factory default
    CHECK (lynx::startingRecordMode (false, false, false) == RecordMode::replace);
    // (the engine's own default, AudioEngine::RecordOptions, is not in the test binary: the guarded GUI session checked it)
    // settings from before 0.1.25 have no answer, and those sessions always started on: nothing changes for them
    CHECK (lynx::startingRecordMode (true, false, false) == RecordMode::overdub);
    // from then on, the saved answer is what counts
    CHECK (lynx::startingRecordMode (true, true, true) == RecordMode::overdub);
    CHECK (lynx::startingRecordMode (true, true, false) == RecordMode::replace);
}

TEST (comp177_audition_ends_when_rows_move_and_before_a_render)
{
    using namespace lynx;
    Song song;
    song.playlistTracks.resize (4);
    song.playlistTracks[0].compTrack = true;
    song.playlistTracks[1].grouped = song.playlistTracks[2].grouped = song.playlistTracks[3].grouped = true;
    song.playlistTracks[1].name = "take 1";
    song.playlistTracks[2].name = "take 2";
    song.playlistTracks[3].name = "take 3";

    comp::setAudition (song, 2);
    CHECK (song.auditionLane == 2 && song.auditionComp == 0);
    {
        // an edit that leaves the rows alone keeps it, one that changes it is not undone
        auto s = song;
        auto before = comp::auditionedTrack (s);
        s.bpm = 90.0;
        comp::checkAudition (s, before);
        CHECK (s.auditionLane == 2);
        before = comp::auditionedTrack (s);
        comp::setAudition (s, 3);
        comp::checkAudition (s, before);
        CHECK (s.auditionLane == 3);
    }
    {
        // a row removed above it: that row now holds another take
        auto s = song;
        const auto before = comp::auditionedTrack (s);
        s.playlistTracks.erase (s.playlistTracks.begin() + 1);
        comp::checkAudition (s, before);
        CHECK (s.auditionLane == -1 && s.auditionComp == -1);
    }
    {
        // a row inserted above the comp: the lane's row is another row now
        auto s = song;
        const auto before = comp::auditionedTrack (s);
        s.playlistTracks.insert (s.playlistTracks.begin(), PlaylistTrack {});
        comp::checkAudition (s, before);
        CHECK (s.auditionLane == -1);
    }
    {
        // nothing auditioned: nothing to compare
        Song s;
        CHECK (comp::auditionedTrack (s).lane == -1);
        comp::checkAudition (s, comp::auditionedTrack (s));
        CHECK (s.auditionLane == -1);
    }

    // a render plays the song, not the audition
    std::int64_t music = 0, loop = 0;
    song.playlist.push_back ([] { Clip c; c.kind = ClipKind::audio; c.track = 0; c.length = 960; return c; }());
    const auto rendered = exportopts::makeRenderSong (song, exportopts::Options {}, 0, music, loop);
    CHECK (rendered.auditionLane == -1 && rendered.auditionComp == -1);
}

TEST (comp177_undo_labels_for_the_comp_steps)
{
    using namespace lynx;
    Song before;
    before.playlistTracks.resize (3);
    before.playlistTracks[0].compTrack = true;
    before.playlistTracks[1].grouped = before.playlistTracks[2].grouped = true;
    for (int t = 0; t < 3; ++t)
    {
        Clip c;
        c.kind = ClipKind::audio;
        c.track = t;
        c.length = 960;
        before.playlist.push_back (c);
    }
    auto after = before;
    CHECK (comp::flatten (after, 0));
    CHECK (std::string (describeChange (before, after).text) == "Flatten comp");

    // a swipe: only the comp track's clips change
    auto swiped = before;
    swiped.playlist[0].length = 480;
    CHECK (std::string (describeChange (before, swiped).text) == "Swipe to comp");
    // an edit of a lane clip is an ordinary clip edit
    auto edited = before;
    edited.playlist[1].length = 480;
    CHECK (std::string (describeChange (before, edited).text) == "Edit clips");
}
