// --- Round 181a: the clip grid's model and launch engine (core/ClipLauncher.h). Pattern workflow never touches any of it.

#include "core/ClipLauncher.h"
#include "core/RackOps.h"

namespace grid181a
{
using namespace lynx;

constexpr int bar = 4 * ppq;

// Four patterns: pattern p has one note of `noteLength` ticks with key 60 + p, on its first tick, in a one-bar pattern.
inline Song patternSong (std::int64_t noteLength = ppq / 2)
{
    Song s;
    s.bpm = 120.0;
    s.channels.resize (2);
    s.mode = PlayMode::song;
    for (int p = 0; p < 4; ++p)
    {
        auto& pattern = s.patterns.emplace_back();
        Note n;
        n.key = 60 + p;
        n.length = noteLength;
        pattern.notes.push_back (n);
        pattern.lengthTicks = bar;
    }
    s.playlistTracks.resize (3);
    return s;
}

inline Clip patternClip (int pattern, std::int64_t length = bar)
{
    Clip c;
    c.pattern = pattern;
    c.length = length;
    return c;
}

struct Event
{
    double tick = 0.0;
    NoteEvent e;
};

// Plays the song's grid block by block through playBlock, collecting what comes out.
struct Run
{
    Song song;
    launcher::Runtime runtime;
    launcher::GridIndex index;
    ScheduleIndex schedule;
    ScheduledNotes notes;
    EventBuffer buffer;
    std::vector<Event> events;
    double tick = 0.0;
    static constexpr int block = 256;
    static constexpr double tps = 1.6; // whole ticks would hide rounding

    explicit Run (Song s) : song (std::move (s))
    {
        for (auto& p : song.patterns)
            std::stable_sort (p.notes.begin(), p.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        index = launcher::makeIndex (*song.clipGrid);
        schedule = makeScheduleIndex (song);
        runtime.clock = 0.0;
    }

    void command (launcher::Command::Type type, int track = 0, int scene = 0) { runtime.commands.push ({ type, track, scene }); }
    void launch (int track, int scene) { command (launcher::Command::Type::launch, track, scene); }

    void until (double end)
    {
        while (tick < end)
        {
            buffer.clear();
            launcher::playBlock (runtime, song, *song.clipGrid, index, tick, block, tps, buffer, false, &schedule, true, false, notes);
            notes.process (buffer, block, tps);
            for (const auto& e : buffer)
                events.push_back ({ tick + e.sampleOffset * tps, e });
            tick += block * tps;
        }
    }

    // ons of `key` (in order), to within a block's rounding
    std::vector<double> onsOf (int key) const
    {
        std::vector<double> out;
        for (const auto& ev : events)
            if (ev.e.type == NoteEvent::Type::on && ev.e.key == key)
                out.push_back (ev.tick);
        return out;
    }
    std::vector<double> offsOf (int key) const
    {
        std::vector<double> out;
        for (const auto& ev : events)
            if (ev.e.type == NoteEvent::Type::off && ev.e.key == key)
                out.push_back (ev.tick);
        return out;
    }
};

inline bool closeTo (double a, double b, double tolerance = 2.0) { return std::abs (a - b) <= tolerance; }
} // namespace grid181a

TEST (grid181a_song_has_no_grid_until_it_is_used)
{
    using namespace lynx;
    Song s = Song::makeDefault();
    CHECK (! s.clipGrid.has_value());
    int lists = 0;
    s.forEachClipList ([&] (const std::vector<Clip>&) { ++lists; });
    CHECK (lists == 1 + (int) s.arrangements.size() - (s.currentArrangement >= 0 && s.currentArrangement < (int) s.arrangements.size() ? 1 : 0));
    CHECK (Clip {}.gridSlot == -1);

    // a rack state without a grid has no launcher, and a grid without the engine's runtime (an export) has none either
    auto plain = RackState::build (s, nullptr, 48000.0);
    CHECK (plain->clipLauncher == nullptr);
    CHECK (! plain->launchers);
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    auto exported = RackState::build (s, nullptr, 48000.0);
    CHECK (exported->clipLauncher == nullptr);
    CHECK (! exported->launchers);
    launcher::Runtime runtime;
    RackState::Factories factories;
    factories.clipLauncher = &runtime;
    auto live = RackState::build (s, nullptr, 48000.0, factories);
    CHECK (live->clipLauncher == &runtime);
    CHECK (live->launchers);
}

TEST (grid181a_model_slots_and_scenes)
{
    using namespace lynx;
    auto s = grid181a::patternSong();
    CHECK (launcher::setClip (s, 1, 2, grid181a::patternClip (1)) >= 0);
    CHECK (s.clipGrid->scenes.size() == 3); // scenes appear as far as the clip's row
    const auto* c = launcher::clipAt (*s.clipGrid, 1, 2);
    CHECK (c != nullptr && c->pattern == 1 && c->track == 1 && c->start == 0);
    CHECK (launcher::clipAt (*s.clipGrid, 1, 1) == nullptr);
    CHECK (s.playlist.empty()); // nothing lands in the Playlist

    // replacing a slot keeps one clip there, its settings back to the defaults
    launcher::settingsAt (*s.clipGrid, 1, 2)->legato = true;
    launcher::setClip (s, 1, 2, grid181a::patternClip (2));
    CHECK (s.clipGrid->clips.size() == 1);
    CHECK (launcher::clipAt (*s.clipGrid, 1, 2)->pattern == 2);
    CHECK (! launcher::settingsAt (*s.clipGrid, 1, 2)->legato);
    CHECK (s.clipGrid->slots.size() == 1); // the freed id is used again

    // only pattern and audio clips, only with a length
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.length = grid181a::bar;
    CHECK (launcher::setClip (s, 0, 0, automation) == -1);
    CHECK (launcher::setClip (s, 0, 0, grid181a::patternClip (0, 0)) == -1);
    CHECK (launcher::setClip (s, -1, 0, grid181a::patternClip (0)) == -1);
    CHECK (launcher::setClip (s, 0, launcher::maxScenes, grid181a::patternClip (0)) == -1);

    // scenes move their clips; removing one takes its clips
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    CHECK (launcher::insertScene (s, 1) == 1);
    CHECK (launcher::clipAt (*s.clipGrid, 0, 0) != nullptr);
    CHECK (launcher::clipAt (*s.clipGrid, 1, 3) != nullptr);
    CHECK (launcher::removeScene (s, 3));
    CHECK (launcher::clipAt (*s.clipGrid, 1, 3) == nullptr);
    CHECK (s.clipGrid->clips.size() == 1);
    while (s.clipGrid->scenes.size() > 1)
        CHECK (launcher::removeScene (s, 0));
    CHECK (! launcher::removeScene (s, 0)); // a grid keeps one scene
    CHECK (launcher::clearClip (s, 0, 0) == false || s.clipGrid->clips.empty());
}

TEST (grid181a_references_follow_edits_like_playlist_clips)
{
    using namespace lynx;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 2, 0, grid181a::patternClip (3));
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 1;
    audio.length = grid181a::bar;
    launcher::setClip (s, 1, 1, audio);

    s.removePattern (1); // pattern 3 becomes 2
    CHECK (launcher::clipAt (*s.clipGrid, 2, 0)->pattern == 2);
    s.removePattern (2); // a slot on a removed pattern goes, as a Playlist clip does
    CHECK (launcher::clipAt (*s.clipGrid, 2, 0) == nullptr);

    noteedit::insertTrack (s, 0); // the column moves
    CHECK (launcher::clipAt (*s.clipGrid, 2, 1) != nullptr);
    CHECK (launcher::clipAt (*s.clipGrid, 2, 1)->channel == 1);
    s.channels.insert (s.channels.begin(), ChannelDef {});
    s.removeChannel (0);
    noteedit::removeTrack (s, 2); // and goes with its track
    CHECK (s.clipGrid->clips.empty());
}

TEST (grid181a_launch_waits_for_the_quantisation)
{
    using namespace lynx;
    using grid181a::closeTo;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 1, 0, grid181a::patternClip (1));
    launcher::settingsAt (*s.clipGrid, 1, 0)->quant = LaunchQuant::quarter;
    grid181a::Run r (std::move (s));

    r.until (grid181a::bar / 8); // an eighth into the first bar
    r.launch (0, 0);             // the global 1 bar
    r.launch (1, 0);             // its own 1/4
    r.until (2 * grid181a::bar - 2 * r.block * r.tps);
    const auto first = r.onsOf (60), second = r.onsOf (61);
    CHECK (first.size() == 1 && closeTo (first[0], grid181a::bar)); // not before the bar, and it loops on it
    CHECK (! second.empty() && closeTo (second[0], ppq));
    CHECK (r.runtime.player.playingOn (0) >= 0);
    CHECK (r.runtime.tracks[0].playing.load() == 0);
    CHECK (r.runtime.anyPlaying.load());

    // the table: each value's ticks
    CHECK (launcher::quantTicks (LaunchQuant::none, lynx::Meter (4, 4)) == 0.0);
    CHECK (launcher::quantTicks (LaunchQuant::bars8, lynx::Meter (4, 4)) == 32.0 * ppq);
    CHECK (launcher::quantTicks (LaunchQuant::bar, lynx::Meter (3, 4)) == 3.0 * ppq); // a bar is the song's own
    CHECK (closeTo (launcher::quantTicks (LaunchQuant::eighthTriplet, lynx::Meter (4, 4)) * 3.0, ppq, 1.0e-9));
    CHECK (launcher::quantTicks (LaunchQuant::thirtySecond, lynx::Meter (4, 4)) == ppq / 8.0);
}

TEST (grid181a_no_quantisation_and_exact_grid_point)
{
    using namespace lynx;
    using grid181a::closeTo;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    s.clipGrid->globalQuant = LaunchQuant::none;
    grid181a::Run r (std::move (s));
    r.until (100.0);
    const auto at = r.tick;
    r.launch (0, 0);
    r.until (at + 200.0);
    CHECK (r.onsOf (60).size() == 1 && closeTo (r.onsOf (60)[0], at));

    // launched exactly on a grid point, it starts there
    auto s2 = grid181a::patternSong();
    launcher::setClip (s2, 0, 0, grid181a::patternClip (2));
    grid181a::Run r2 (std::move (s2));
    r2.launch (0, 0); // tick 0 is on the bar
    r2.until (100.0);
    CHECK (r2.onsOf (62).size() == 1 && closeTo (r2.onsOf (62)[0], 0.0));
}

TEST (grid181a_a_track_plays_one_clip_and_repeated_keys_survive)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong (2 * bar); // notes longer than the clip's bar: only a cut ends them
    s.patterns[0].lengthTicks = bar;
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 0, 1, grid181a::patternClip (1));
    grid181a::Run r (std::move (s));
    r.launch (0, 0);
    r.until (bar + bar / 2);
    r.launch (0, 1);
    r.until (2 * bar + bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == 1);
    CHECK (r.onsOf (61).size() == 1 && closeTo (r.onsOf (61)[0], 2 * bar));
    // the old clip's note ended at the switch, not at its own end
    bool endedAtSwitch = false;
    for (const auto t : r.offsOf (60))
        endedAtSwitch = endedAtSwitch || closeTo (t, 2 * bar, 8.0);
    CHECK (endedAtSwitch);
    CHECK (r.notes.sounding() == 1); // only the new clip's note

    // launching the same clip restarts it on the grid: the note on the same key is not cut by the old one's end
    auto s2 = grid181a::patternSong (bar / 2);
    launcher::setClip (s2, 0, 0, grid181a::patternClip (0));
    grid181a::Run r2 (std::move (s2));
    r2.launch (0, 0);
    r2.until (bar / 4);
    r2.launch (0, 0);
    r2.until (bar + bar / 4);
    const auto ons = r2.onsOf (60);
    CHECK (ons.size() == 2 && closeTo (ons[1], bar));
    int soundingOnKey = r2.notes.sounding();
    CHECK (soundingOnKey == 1);
}

TEST (grid181a_legato_keeps_the_position)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    // a second note in the middle of each bar, to hear where the new clip starts
    for (auto& p : s.patterns)
    {
        Note n = p.notes[0];
        n.start = bar / 2;
        n.key += 20;
        p.notes.push_back (n);
    }
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 0, 1, grid181a::patternClip (1));
    launcher::settingsAt (*s.clipGrid, 0, 1)->legato = true;
    launcher::settingsAt (*s.clipGrid, 0, 1)->quant = LaunchQuant::quarter;
    grid181a::Run r (std::move (s));
    r.launch (0, 0);
    r.until (ppq + ppq / 2); // 1.5 beats into the first clip
    r.launch (0, 1);         // on beat 2 it takes over at position 2 beats: its middle note, its first note not
    r.until (bar - 2 * r.block * r.tps);
    CHECK (r.onsOf (61).empty());
    CHECK (r.onsOf (81).size() == 1 && closeTo (r.onsOf (81)[0], 2 * ppq));
    CHECK (r.onsOf (80).empty()); // the first clip's own middle note never came
}

TEST (grid181a_one_pass_clip_and_stops)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    {
        auto s = grid181a::patternSong();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        launcher::settingsAt (*s.clipGrid, 0, 0)->loop = false;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (3 * bar);
        CHECK (r.onsOf (60).size() == 1);
        CHECK (r.runtime.player.playingOn (0) == -1);
        CHECK (r.runtime.tracks[0].playing.load() == -1);
        CHECK (! r.runtime.anyPlaying.load());
    }
    {
        // a track's stop waits for the global quantisation; so does stop all
        auto s = grid181a::patternSong();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        launcher::setClip (s, 1, 0, grid181a::patternClip (1));
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.launch (1, 0);
        r.until (bar / 4);
        r.command (launcher::Command::Type::stopTrack, 0);
        r.until (bar - 2 * r.block * r.tps);
        CHECK (r.runtime.player.playingOn (0) >= 0); // not yet
        r.until (bar + bar / 4);
        CHECK (r.runtime.player.playingOn (0) == -1);
        CHECK (r.onsOf (60).size() == 1);
        CHECK (r.onsOf (61).size() == 2);
        r.command (launcher::Command::Type::stopAll);
        r.until (2 * bar + bar / 4);
        CHECK (r.runtime.player.playingOn (1) == -1);
        CHECK (r.onsOf (61).size() == 2);
    }
}

TEST (grid181a_scene_launches_its_row_and_stops_the_rest)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 1, 0, grid181a::patternClip (1));
    launcher::setClip (s, 0, 1, grid181a::patternClip (2));
    grid181a::Run r (std::move (s));
    r.command (launcher::Command::Type::launchScene, 0, 0);
    r.until (bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == 0 && r.runtime.tracks[1].playing.load() == 0);
    r.command (launcher::Command::Type::launchScene, 0, 1);
    r.until (bar + bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == 1);
    CHECK (r.runtime.player.playingOn (1) == -1); // no clip in that row: the track stops
    CHECK (closeTo (r.onsOf (62).at (0), bar));
}

TEST (grid181a_a_clip_takes_its_track_from_the_playlist)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong (3 * bar);
    s.patterns[2].lengthTicks = 4 * bar;
    // the Playlist: pattern 2 on track 0 (a note held for 3 bars of its 4), pattern 3 on track 1
    Clip a = grid181a::patternClip (2, 8 * bar);
    a.track = 0;
    Clip b = grid181a::patternClip (3, 8 * bar);
    b.track = 1;
    s.playlist = { a, b };
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    grid181a::Run r (std::move (s));
    r.until (bar / 2);
    r.launch (0, 0);
    r.until (3 * bar + bar / 2);
    // the song's track 0 stopped at the launch and started nothing after it; track 1 went on
    CHECK (r.onsOf (62).size() == 1);
    bool cut = false;
    for (const auto t : r.offsOf (62))
        cut = cut || closeTo (t, bar, 8.0);
    CHECK (cut);
    CHECK (r.onsOf (63).size() >= 2); // (the clip loops its one-bar pattern)
    CHECK (r.runtime.tracks[0].taken.load());
    CHECK (! r.runtime.tracks[1].taken.load());

    // a stopped clip leaves the track silent: taken until "back to arrangement"
    r.command (launcher::Command::Type::stopTrack, 0);
    r.until (5 * bar);
    CHECK (r.runtime.player.playingOn (0) == -1);
    CHECK (r.runtime.player.taken()[0]);
    const auto before = r.onsOf (62).size();
    r.until (6 * bar);
    CHECK (r.onsOf (62).size() == before);

    // back: the note in progress starts where the song is
    r.command (launcher::Command::Type::backToArrangement);
    const auto backAt = r.tick;
    r.until (backAt + 100.0);
    CHECK (! r.runtime.player.taken()[0]);
    CHECK (! r.runtime.tracks[0].taken.load());
    CHECK (r.onsOf (62).size() == before + 1 && closeTo (r.onsOf (62).back(), backAt));
}

TEST (grid181a_transport_stop_ends_clips_and_keeps_tracks_taken)
{
    using namespace lynx;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    grid181a::Run r (std::move (s));
    r.launch (0, 0);
    r.until (100.0);
    CHECK (r.runtime.anyPlaying.load());
    r.runtime.player.stopAll (&r.runtime);
    CHECK (r.runtime.player.playingOn (0) == -1);
    CHECK (r.runtime.tracks[0].playing.load() == -1);
    CHECK (! r.runtime.anyPlaying.load());
    CHECK (r.runtime.player.taken()[0]); // (a user's "back" gives it back, or another project)
    CHECK (r.runtime.tracks[0].taken.load());

    r.command (launcher::Command::Type::reset);
    r.until (r.tick + 10.0);
    CHECK (! r.runtime.player.taken()[0]);

    // the option: tracks come back with the stop
    r.launch (0, 0);
    r.until (r.tick + 2.0 * grid181a::bar);
    CHECK (r.runtime.player.taken()[0]);
    r.runtime.player.stopAll (&r.runtime, true);
    CHECK (! r.runtime.player.taken()[0]);
    CHECK (! r.runtime.tracks[0].taken.load());
}

TEST (grid181a_audio_slots_have_their_own_voices)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    s.channels[1].instrument = InstrumentKind::audioClip;
    Clip song;
    song.kind = ClipKind::audio;
    song.channel = 1;
    song.track = 0;
    song.length = 8 * bar;
    s.playlist = { song };
    Clip slot = song;
    slot.length = bar;
    const auto id = launcher::setClip (s, 0, 0, slot);
    grid181a::Run r (std::move (s));
    const auto songKey = audioClipKey (0);
    const auto slotKey = audioClipKey (launcher::slotKeyBase + (std::size_t) id);
    CHECK (songKey != slotKey);
    r.until (bar / 2);
    r.launch (0, 0);
    r.until (2 * bar + bar / 2);
    // the Playlist's clip started at 0 and was cut at the launch; the slot plays from the bar and loops
    CHECK (r.onsOf (songKey).size() == 1);
    bool cut = false; // (an end at the song's own wrap is there too, at tick 0: the scheduler's, harmless)
    for (const auto t : r.offsOf (songKey))
        cut = cut || closeTo (t, bar, 2.0);
    CHECK (cut);
    const auto ons = r.onsOf (slotKey);
    CHECK (ons.size() == 2 && closeTo (ons[0], bar) && closeTo (ons[1], 2 * bar));
    CHECK (closeTo (r.offsOf (slotKey).at (0), 2 * bar, 4.0)); // its pass ended before the next began
}

TEST (grid181a_renderer_plays_the_grid_only_through_the_runtime)
{
    using namespace lynx;
    constexpr auto bar = grid181a::bar;
    // the same song, with and without a grid (a grid nobody launches): identical output
    auto s = grid181a::patternSong();
    s.channels[0].instrument = InstrumentKind::synth;
    Clip a = grid181a::patternClip (0, 4 * bar);
    s.playlist = { a };
    auto withGrid = s;
    launcher::setClip (withGrid, 1, 0, grid181a::patternClip (1));
    launcher::Runtime runtime;
    RackState::Factories factories;
    factories.clipLauncher = &runtime;
    const auto before = launcher::Runtime::instances.load();

    auto render = [&] (const Song& song, const RackState::Factories& f, bool launchIt) {
        auto state = RackState::build (song, nullptr, 48000.0, f);
        RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> left (512), right (512);
        std::vector<float> out;
        double tick = 0.0;
        const auto tps = ticksPerSample (song.bpm, 48000.0);
        for (int b = 0; b < 100; ++b)
        {
            if (launchIt && b == 2)
                runtime.commands.push ({ launcher::Command::Type::launch, 1, 0 });
            r.process (state.get(), true, tick, 512, left.data(), right.data(), nullptr);
            out.insert (out.end(), left.begin(), left.end());
            tick += 512 * tps;
        }
        return out;
    };
    const auto plain = render (s, {}, false);
    const auto idle = render (withGrid, factories, false);
    CHECK (plain == idle);
    CHECK (std::any_of (plain.begin(), plain.end(), [] (float x) { return std::abs (x) > 1.0e-4f; })); // it did sound
    CHECK (launcher::Runtime::instances.load() == before); // the renderer never makes one
    (void) render (withGrid, factories, true);
    CHECK (runtime.tracks[1].playing.load() == 0 || runtime.tracks[1].pending.load() == 0);
}

TEST (grid181a_launch_modes)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    using T = launcher::Command::Type;
    {
        // gate: up stops the clip
        auto s = grid181a::patternSong();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        launcher::settingsAt (*s.clipGrid, 0, 0)->mode = LaunchMode::gate;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (bar / 4);
        CHECK (r.runtime.player.playingOn (0) >= 0);
        r.command (T::release, 0, 0);
        r.until (bar + bar / 4); // (the global 1 bar)
        CHECK (r.runtime.player.playingOn (0) == -1);
    }
    {
        // toggle: up is ignored, the next down stops it
        auto s = grid181a::patternSong();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        launcher::settingsAt (*s.clipGrid, 0, 0)->mode = LaunchMode::toggle;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.command (T::release, 0, 0);
        r.until (bar + bar / 4);
        CHECK (r.runtime.player.playingOn (0) >= 0);
        r.launch (0, 0);
        r.until (2 * bar + bar / 4);
        CHECK (r.runtime.player.playingOn (0) == -1);
    }
    {
        // repeat: while held the clip starts over at its quantisation rate; let go, it plays on
        auto s = grid181a::patternSong (ppq / 8);
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        auto* slot = launcher::settingsAt (*s.clipGrid, 0, 0);
        slot->mode = LaunchMode::repeat;
        slot->quant = LaunchQuant::quarter;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (2 * ppq + ppq / 2);
        const auto held = r.onsOf (60);
        CHECK (held.size() == 3 && closeTo (held[1], ppq) && closeTo (held[2], 2 * ppq));
        r.command (T::release, 0, 0);
        r.until (2 * bar + ppq);
        CHECK (r.runtime.player.playingOn (0) >= 0);
    }
    {
        // trigger: up is ignored
        auto s = grid181a::patternSong();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.command (T::release, 0, 0);
        r.until (2 * bar);
        CHECK (r.runtime.player.playingOn (0) >= 0);
    }
}

TEST (grid181a_follow_actions)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    auto threeInAColumn = [] {
        auto s = grid181a::patternSong ();
        launcher::setClip (s, 0, 0, grid181a::patternClip (0));
        launcher::setClip (s, 0, 1, grid181a::patternClip (1));
        launcher::setClip (s, 0, 2, grid181a::patternClip (2));
        launcher::setClip (s, 0, 4, grid181a::patternClip (3)); // after a gap: another group
        return s;
    };
    {
        // the groups and what each action leads to
        auto s = threeInAColumn();
        const auto& g = *s.clipGrid;
        int rows[8];
        CHECK (launcher::followGroup (g, 0, 1, rows, 8) == 3 && rows[0] == 0 && rows[2] == 2);
        CHECK (launcher::followGroup (g, 0, 4, rows, 8) == 1 && rows[0] == 4);
        CHECK (launcher::followGroup (g, 0, 3, rows, 8) == 0);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::next, 0, 0).scene == 2);
        CHECK (launcher::followTarget (g, 0, 2, FollowKind::next, 0, 0).scene == 0);
        CHECK (launcher::followTarget (g, 0, 0, FollowKind::previous, 0, 0).scene == 2);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::first, 0, 0).scene == 0);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::last, 0, 0).scene == 2);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::playAgain, 0, 0).scene == 1);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::stop, 0, 0).stops);
        CHECK (launcher::followTarget (g, 0, 1, FollowKind::none, 0, 0).scene == -1);
        CHECK (launcher::followTarget (g, 0, 0, FollowKind::jump, 4, 0).scene == 4);
        CHECK (launcher::followTarget (g, 0, 0, FollowKind::jump, 3, 0).scene == -1);
        for (std::uint32_t k = 0; k < 16; ++k)
        {
            CHECK (launcher::followTarget (g, 0, 1, FollowKind::other, 0, k).scene != 1);
            CHECK (launcher::followTarget (g, 0, 1, FollowKind::any, 0, k).scene >= 0);
        }
        CHECK (launcher::followTarget (g, 0, 4, FollowKind::other, 0, 5).scene == 4); // alone in its group
    }
    {
        // next, after the clip's length: at once, not waiting for the global quantisation
        auto s = threeInAColumn();
        s.clipGrid->globalQuant = LaunchQuant::bars4;
        auto* first = launcher::settingsAt (*s.clipGrid, 0, 0);
        first->follow.enabled = true;
        first->follow.iterations = 2;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (2 * bar + bar / 2);
        CHECK (r.onsOf (60).size() == 2);
        CHECK (r.onsOf (61).size() == 1 && closeTo (r.onsOf (61)[0], 2 * bar));
    }
    {
        // unlinked time; the target's own quantisation still applies
        auto s = threeInAColumn();
        auto* first = launcher::settingsAt (*s.clipGrid, 0, 0);
        first->follow.enabled = true;
        first->follow.linked = false;
        first->follow.ticks = ppq + ppq / 2;
        launcher::settingsAt (*s.clipGrid, 0, 1)->quant = LaunchQuant::half;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (bar - 2 * r.block * r.tps);
        CHECK (r.onsOf (61).size() == 1 && closeTo (r.onsOf (61)[0], 2 * ppq));
    }
    {
        // stop; chances; the grid-wide switch
        auto s = threeInAColumn();
        auto* first = launcher::settingsAt (*s.clipGrid, 0, 0);
        first->follow.enabled = true;
        first->follow.a = FollowKind::stop;
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (bar + bar / 2);
        CHECK (r.runtime.player.playingOn (0) == -1);

        auto s2 = threeInAColumn();
        auto* f2 = launcher::settingsAt (*s2.clipGrid, 0, 0);
        f2->follow.enabled = true;
        f2->follow.chanceA = 0;
        f2->follow.chanceB = 100;
        f2->follow.b = FollowKind::jump;
        f2->follow.jumpB = 2;
        grid181a::Run r2 (std::move (s2));
        r2.launch (0, 0);
        r2.until (bar + bar / 2);
        CHECK (r2.runtime.tracks[0].playing.load() == 2);

        auto s3 = threeInAColumn();
        launcher::settingsAt (*s3.clipGrid, 0, 0)->follow.enabled = true;
        s3.clipGrid->followActions = false;
        grid181a::Run r3 (std::move (s3));
        r3.launch (0, 0);
        r3.until (3 * bar - 2 * r3.block * r3.tps);
        CHECK (r3.runtime.tracks[0].playing.load() == 0);
        CHECK (r3.onsOf (60).size() == 3);
    }
}

TEST (grid181a_stop_buttons_decide_what_a_scene_leaves_alone)
{
    using namespace lynx;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0));
    launcher::setClip (s, 1, 0, grid181a::patternClip (1));
    launcher::setClip (s, 2, 0, grid181a::patternClip (2));
    launcher::setClip (s, 0, 1, grid181a::patternClip (3));
    launcher::setStopButton (s, 1, 1, false); // the row leaves track 1 alone
    CHECK (! launcher::hasStopButton (*s.clipGrid, 1, 1));
    CHECK (launcher::hasStopButton (*s.clipGrid, 2, 1));
    CHECK (launcher::clipAt (*s.clipGrid, 1, 1) == nullptr); // it is not a clip
    launcher::setStopButton (s, 0, 0, false);                // a filled slot has none to remove
    CHECK (launcher::hasStopButton (*s.clipGrid, 0, 0));

    // the mark follows its track, as clips do, and comes back
    auto moved = s;
    noteedit::insertTrack (moved, 0);
    CHECK (! launcher::hasStopButton (*moved.clipGrid, 2, 1));
    CHECK (launcher::hasStopButton (*moved.clipGrid, 1, 1));
    launcher::setStopButton (moved, 2, 1, true);
    CHECK (launcher::hasStopButton (*moved.clipGrid, 2, 1));

    grid181a::Run r (std::move (s));
    r.command (launcher::Command::Type::launchScene, 0, 0);
    r.until (bar / 2);
    r.command (launcher::Command::Type::launchScene, 0, 1);
    r.until (bar + bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == 1);
    CHECK (r.runtime.player.playingOn (1) >= 0);  // left alone
    CHECK (r.runtime.player.playingOn (2) == -1); // stopped
}

TEST (grid181a_recording_writes_passes_for_the_playlist)
{
    using namespace lynx;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 1, 0, grid181a::patternClip (2));
    const auto grid = *s.clipGrid;
    grid181a::Run r (std::move (s));
    r.runtime.recording.store (true);
    r.launch (1, 0);
    r.until (2 * bar + bar / 2);
    r.runtime.player.recordStop (*r.song.clipGrid, r.index, &r.runtime);
    r.runtime.player.stopAll (&r.runtime);

    std::vector<Clip> clips;
    launcher::RecordedPass pass;
    while (r.runtime.recorded.pop (pass))
        if (Clip c; launcher::recordedClip (grid, pass, c))
            clips.push_back (c);
    // two whole bars, then the part the stop cut
    CHECK (clips.size() == 3);
    if (clips.size() == 3)
    {
        CHECK (clips[0].start == 0 && clips[0].length == bar && clips[0].track == 1 && clips[0].pattern == 2);
        CHECK (clips[0].gridSlot == -1); // a Playlist clip now
        CHECK (clips[1].start == bar && clips[1].length == bar);
        CHECK (clips[2].start == 2 * bar && std::abs (clips[2].length - r.tick + 2 * bar) < 2.0);
    }
}

// A note longer than its pattern's period, or crossing the pattern's wrap, is sounding from an earlier pass of the
// pattern at the moment of a takeover or a stop. It must end there: a hanging note is the first thing one would hear.
TEST (grid181a_a_note_longer_than_its_pattern_does_not_hang)
{
    using namespace lynx;
    using grid181a::closeTo;
    constexpr auto bar = grid181a::bar;
    const auto endsAt = [] (const grid181a::Run& r, int key, double at) {
        for (const auto t : r.offsOf (key))
            if (closeTo (t, at, 8.0))
                return true;
        return false;
    };
    // the Playlist's looping clip, taken over by a slot playing pattern 1 (a short note on another key)
    const auto takeover = [&] (std::int64_t start, std::int64_t length, LaunchQuant quant, double launchAt, double cutAt) {
        auto s = grid181a::patternSong();
        s.patterns[0].notes[0].start = start;
        s.patterns[0].notes[0].length = length;
        s.playlist = { grid181a::patternClip (0, 8 * bar) };
        launcher::setClip (s, 0, 0, grid181a::patternClip (1));
        s.clipGrid->globalQuant = quant;
        grid181a::Run r (std::move (s));
        r.until (launchAt);
        const auto soundingBefore = r.notes.sounding() > 0;
        r.launch (0, 0);
        r.until (cutAt + bar / 2);
        const auto cut = endsAt (r, 60, cutAt);
        const auto silent = ! r.notes.shorten (0, 60, 0.0);
        return soundingBefore && cut && silent;
    };
    // held for 3 bars of a one-bar pattern, cut on the pattern's wrap
    CHECK (takeover (0, 3 * bar, LaunchQuant::bar, bar + bar / 2, 2 * bar));
    // starts late in the pattern and crosses its wrap: the cut's position is before the note's start
    CHECK (takeover (3 * bar / 4, 2 * bar, LaunchQuant::bar, bar + bar / 2, 2 * bar));
    // the same, cut in the middle of a pass by a quarter note quantisation
    CHECK (takeover (3 * bar / 4, 2 * bar, LaunchQuant::quarter, bar + bar / 8, bar + bar / 4));

    // a slot whose clip loops its pattern, stopped: the note from the pass before ends at the stop
    {
        auto s = grid181a::patternSong (3 * bar);
        launcher::setClip (s, 0, 0, grid181a::patternClip (0, 4 * bar));
        grid181a::Run r (std::move (s));
        r.launch (0, 0);
        r.until (bar + bar / 2);
        CHECK (r.notes.sounding() > 0);
        r.command (launcher::Command::Type::stopTrack, 0);
        r.until (2 * bar + bar / 2);
        CHECK (endsAt (r, 60, 2 * bar));
        CHECK (! r.notes.shorten (0, 60, 0.0));
    }

    // FL's way, "stretched clips loop" off: the pattern plays once, and the one long note is still cut
    {
        setStretchedClipsLoop (false);
        auto s = grid181a::patternSong (3 * bar);
        s.playlist = { grid181a::patternClip (0, 8 * bar) };
        launcher::setClip (s, 0, 0, grid181a::patternClip (1));
        grid181a::Run r (std::move (s));
        r.until (bar + bar / 2);
        r.launch (0, 0);
        r.until (2 * bar + bar / 2);
        setStretchedClipsLoop (true);
        CHECK (endsAt (r, 60, 2 * bar));
        CHECK (! r.notes.shorten (0, 60, 0.0));
    }
}

TEST (grid181a_a_stop_shows_until_its_grid_point)
{
    using namespace lynx;
    constexpr auto bar = grid181a::bar;
    auto s = grid181a::patternSong();
    launcher::setClip (s, 0, 0, grid181a::patternClip (0, 4 * bar));
    grid181a::Run r (std::move (s));
    r.launch (0, 0);
    r.until (bar + bar / 2);
    CHECK (! r.runtime.tracks[0].stopping.load());
    r.command (launcher::Command::Type::stopTrack, 0);
    r.until (bar + bar * 3 / 4);
    CHECK (r.runtime.tracks[0].playing.load() == 0);
    CHECK (r.runtime.tracks[0].stopping.load());
    r.until (2 * bar + bar / 2);
    CHECK (r.runtime.tracks[0].playing.load() == -1);
    CHECK (! r.runtime.tracks[0].stopping.load());
}
