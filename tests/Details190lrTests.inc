// Round 190lr (details pass 3): the Simple dock's Drum Machine (BandLab's step grid, DS-39) and Live's scene follow
// actions (DL-14).
#include "core/SimpleSteps.h"
#include "core/SimpleTracks.h"

namespace details190lr
{
using namespace lynx;

// A Drums track with a three-sound kit and a one-bar starter (kick on 0 and 8)
inline Song drumSong (int* trackOut)
{
    Song song;
    song.mixer = Song::defaultMixer();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    q.name = "Drums";
    for (const char* n : { "Kick", "Snare", "Hat" })
    {
        ChannelDef c;
        c.name = n;
        c.instrument = InstrumentKind::sampler;
        q.kit.push_back (c);
    }
    Pattern p;
    p.lengthTicks = 16 * ticksPerStep;
    for (int step : { 0, 8 })
    {
        Note n;
        n.start = step * ticksPerStep;
        n.channel = 0;
        p.notes.push_back (n);
    }
    q.pattern = p;
    q.fillBars = 2;
    const auto r = simpletracks::addTrack (song, q);
    *trackOut = r.track;
    return song;
}
} // namespace details190lr

// BandLab's drum machine is a view of the Drums track: one row per kit sound (kick at the bottom, key A), pattern A is the
// track's own pattern, B … H are made on demand as "<track> B" …, a rename keeps them, "Add" places a clip on a bar
TEST (details190lr_drum_machine_rows_letters_and_clips)
{
    using namespace lynx;
    int track = -1;
    auto song = details190lr::drumSong (&track);
    CHECK (track >= 0);
    if (track < 0)
        return;
    const auto rows = simplesteps::kitRows (song, track);
    CHECK (rows.size() == 3);
    CHECK (song.channels[(std::size_t) rows[0]].name == "Kick");
    CHECK (simplesteps::rowKey (0) == 'A' && simplesteps::rowKey (1) == 'O' && simplesteps::rowKey (7) == 'L' && simplesteps::rowKey (8) == 0);
    CHECK (simplesteps::rowForKey ('f') == 2 && simplesteps::rowForKey ('L') == 7 && simplesteps::rowForKey ('Z') == -1);

    const auto a = simplesteps::letterPattern (song, track, 0);
    CHECK (a >= 0 && song.patterns[(std::size_t) a].name == "Drums");
    CHECK (simplesteps::stepCount (song, a) == 16 && simplesteps::lengthBars (song, a) == 1);
    CHECK (song.patterns[(std::size_t) a].hasStep (rows[0], 8));
    CHECK (simplesteps::letterPattern (song, track, 1) < 0);
    CHECK (simplesteps::lettersWithPattern (song, track) == 1u);

    const auto b = simplesteps::ensureLetter (song, track, 1);
    CHECK (b >= 0 && song.patterns[(std::size_t) b].name == "Drums B" && song.patterns[(std::size_t) b].notes.empty());
    CHECK (simplesteps::ensureLetter (song, track, 1) == b); // (made once)
    CHECK (simplesteps::stepCount (song, b) == 16);
    CHECK (simplesteps::letterOf (song, track, b) == 1 && simplesteps::lettersWithPattern (song, track) == 3u);

    // Length: bars of the meter; the steps follow
    simplesteps::setLengthBars (song, b, 2);
    CHECK (simplesteps::stepCount (song, b) == 32 && simplesteps::lengthBars (song, b) == 2);

    // Add B at the playhead, unsnapped (BandLab): it overwrites the part of A's second clip under it (A keeps its left part)
    const auto bar = song.barTicks();
    const auto before = song.playlist.size();
    const auto clip = simplesteps::addLetterClip (song, track, 1, bar + 500);
    CHECK (clip >= 0 && song.playlist.size() == before + 1 && clip == (int) song.playlist.size() - 1);
    CHECK (song.playlist[(std::size_t) clip].start == bar + 500 && song.playlist[(std::size_t) clip].length == 2 * bar);
    const auto lengthAt = [&song, track] (std::int64_t start) {
        for (const auto& c : song.playlist)
            if (c.track == track && c.start == start)
                return c.length;
        return (std::int64_t) -1;
    };
    CHECK (lengthAt (0) == bar && lengthAt (bar) == 500);
    CHECK (simplesteps::addLetterClip (song, track, 5, 0) == -1); // (F has no pattern)

    // the playing step: in the clip of B (song mode), and nothing outside the track's clips of that pattern
    CHECK (simplesteps::playingStep (song, track, b, (double) (bar + 500 + 5 * ticksPerStep + 10), true) == 5);
    CHECK (simplesteps::playingStep (song, track, b, (double) (bar + 500 + 5 * ticksPerStep), false) == -1);
    CHECK (simplesteps::playingStep (song, track, b, 10.0, true) == -1);
    CHECK (simplesteps::playingStep (song, track, a, (double) (bar / 2 + 3 * ticksPerStep), true) == 11); // (A's first clip)

    // A one bar long put in the middle of B's clip: B keeps its left part, and goes on after A from where it was
    const auto inside = bar + 500 + bar / 2;
    simplesteps::addLetterClip (song, track, 0, inside);
    CHECK (lengthAt (bar + 500) == bar / 2 && lengthAt (inside) == bar);
    bool right = false;
    for (const auto& c : song.playlist)
        if (c.track == track && c.pattern == b && c.start == inside + bar)
            right = c.offset == bar / 2 + bar && c.end() == 3 * bar + 500;
    CHECK (right);

    song.mode = PlayMode::pattern;
    song.currentPattern = a;
    CHECK (simplesteps::playingStep (song, track, a, (double) (7 * song.barTicks() + 9 * ticksPerStep), true) == 9);
    CHECK (simplesteps::playingStep (song, track, b, 0.0, true) == -1);

    // a rename of the track keeps the letters
    simpletracks::rename (song, track, "Beat");
    CHECK (simplesteps::letterPattern (song, track, 0) == a && simplesteps::letterPattern (song, track, 1) == b);
    CHECK (song.patterns[(std::size_t) b].name == "Beat B");

    // velocity shade: the default velocity is the full colour, softer is darker, never below 0.45
    CHECK (simplesteps::shade (0.78f) == 1.0f && simplesteps::shade (1.0f) == 1.0f);
    CHECK (simplesteps::shade (0.39f) < 0.8f && simplesteps::shade (0.0f) == 0.45f);

    // not a Drums track: nothing
    CHECK (simplesteps::kitRows (song, track + 1).empty() && simplesteps::letterPattern (song, track + 1, 0) == -1);
    CHECK (simplesteps::ensureLetter (song, track + 1, 1) == -1);
}

// Live's scene follow action (manual 16.7): the scene's Follow Action Time after the scene starts, its action launches
// another scene (Next wraps to the first); a new scene launch replaces it, Stop All Clips ends it (the .lynx round trip: --details190lrshot)
TEST (details190lr_scene_follow_action_launches_the_next_scene)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 0, 1, grid181a::patternClip (1));
    launcher::setClip (s, 0, 2, grid181a::patternClip (2));
    auto& grid = *s.clipGrid;
    while (grid.scenes.size() < 3)
        grid.scenes.emplace_back();
    grid.scenes[0].follow.enabled = true;
    grid.scenes[0].follow.a = FollowKind::next;
    grid.scenes[0].follow.ticks = 2 * bar;
    CHECK (launcher::sceneFollows (grid, 0) && ! launcher::sceneFollows (grid, 1));
    CHECK (launcher::sceneFollowTarget (grid, 2, FollowKind::next, 0, 0).scene == 0);
    CHECK (launcher::sceneFollowTarget (grid, 0, FollowKind::previous, 0, 0).scene == 2);
    CHECK (launcher::sceneFollowTarget (grid, 1, FollowKind::jump, 2, 0).scene == 2);
    CHECK (launcher::sceneFollowTarget (grid, 1, FollowKind::stop, 0, 0).stops);
    for (std::uint32_t r = 0; r < 20; ++r)
        CHECK (launcher::sceneFollowTarget (grid, 1, FollowKind::other, 0, r).scene != 1);

    grid181a::Run r (std::move (s));
    r.command (launcher::Command::Type::launchScene, 0, 0);
    r.until (bar / 2);
    CHECK (r.runtime.player.sceneFollowScene() == 0 && closeTo (r.runtime.player.sceneFollowTime(), 2 * bar));
    r.until (2 * bar + bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == 1); // (scene 2 = row 1 launched by the follow action)
    CHECK (closeTo (r.onsOf (61).at (0), 2 * bar));
    CHECK (r.runtime.player.sceneFollowScene() == -1); // (row 1 has none)

    // a hand launch of a scene with a follow action, then Stop All Clips: nothing waits any more
    r.command (launcher::Command::Type::launchScene, 0, 0);
    r.until (3 * bar + bar / 2);
    CHECK (r.runtime.player.sceneFollowScene() == 0);
    r.command (launcher::Command::Type::stopAll);
    r.until (4 * bar);
    CHECK (r.runtime.player.sceneFollowScene() == -1);
}

// Live's single-track Back to Arrangement: only that track gives its Playlist clips back; the other stays with the grid
TEST (details190lr_one_track_back_to_arrangement)
{
    using namespace lynx;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 1, 0, grid181a::patternClip (1));
    grid181a::Run r (std::move (s));
    r.command (launcher::Command::Type::launchScene, 0, 0);
    r.until (bar / 2);
    CHECK (r.runtime.player.taken()[0] && r.runtime.player.taken()[1]);
    r.command (launcher::Command::Type::trackBackToArrangement, 0, 0);
    r.until (bar / 2 + bar / 4);
    CHECK (! r.runtime.player.taken()[0] && r.runtime.player.taken()[1]);
    CHECK (r.runtime.player.playingOn (0) == -1 && r.runtime.player.playingOn (1) >= 0);
}
