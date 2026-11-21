// --- Round 190ll: GitHub issues #199 (a hosted synth's piano-roll playback), #200 (a folder dropped on the Browser),
// #201 (colour several selected tracks), #203 (the Piano roll's key and scale chosen apart) ----------------------------

#include "core/SelectionRename.h"
#include "core/KeyMap.h"
#include "core/UserFolders.h"
#include "core/FixedBlock.h"

// #201: FL's Mixer rule (manual: "Set the selected Mixer Track name, color & icon [...] For a selection of tracks - The
// name/icon will only apply to unnamed tracks"): the colour goes to every selected track, the name to the unnamed ones
TEST (issues190ll_201_mixer_selection_takes_the_colour)
{
    using namespace lynx;
    Song s;
    s.mixer.resize (6);
    s.mixer[2].name = "Kick";
    s.mixer[3].colour = 0xff112233u;
    const auto targets = selrename::targets (2, { 4, 2, 3 });
    CHECK ((targets == std::vector<int> { 2, 3, 4 }));
    CHECK ((selrename::targets (1, { 2, 3 }) == std::vector<int> { 1 })); // (a track outside the selection: itself alone)

    selrename::applyToMixer (s, 2, targets, { "Kick", "Drums", 0u, 0xffaa0000u });
    CHECK (s.mixer[2].name == "Drums" && s.mixer[2].colour == 0xffaa0000u);
    CHECK (s.mixer[3].name == "Drums" && s.mixer[3].colour == 0xffaa0000u); // (unnamed: takes the name)
    CHECK (s.mixer[4].name == "Drums" && s.mixer[4].colour == 0xffaa0000u);
    CHECK (s.mixer[1].name.empty() && s.mixer[1].colour == 0u && s.mixer[5].colour == 0u); // (not selected)

    // a named track keeps its name; a rename alone keeps the others' colours
    s.mixer[3].name = "Snare";
    s.mixer[4].colour = 0xff00ff00u;
    selrename::applyToMixer (s, 2, targets, { "Drums", "Perc", 0xffaa0000u, 0xffaa0000u });
    CHECK (s.mixer[2].name == "Perc" && s.mixer[3].name == "Snare" && s.mixer[4].name == "Drums");
    CHECK (s.mixer[3].colour == 0xffaa0000u && s.mixer[4].colour == 0xff00ff00u);

    selrename::colourMixer (s, targets, 0xff0000ffu); // "Change color..." / "Random color"
    for (const auto t : targets)
        CHECK (s.mixer[(std::size_t) t].colour == 0xff0000ffu);
    CHECK (s.mixer[1].colour == 0u);
}

TEST (issues190ll_201_playlist_tracks_and_channels_take_the_colour)
{
    using namespace lynx;
    Song s;
    s.playlistTracks.resize (2);
    s.playlistTracks[1].name = "Bass";
    // the dialog opened on track 1 with tracks 1, 3 and 5 selected (5 is past the stored list: it is made)
    selrename::applyToPlaylistTracks (s, 1, { 1, 3, 5 }, { "Bass", "Low end", 0u, 0xff445566u });
    CHECK (s.playlistTracks.size() >= 6);
    CHECK (s.playlistTracks[1].name == "Low end" && s.playlistTracks[1].colour == 0xff445566u);
    CHECK (s.playlistTracks[3].colour == 0xff445566u && s.playlistTracks[3].name.empty()); // (the name stays the track's own)
    CHECK (s.playlistTracks[5].colour == 0xff445566u && s.playlistTracks[0].colour == 0u);
    selrename::applyToPlaylistTracks (s, 1, { 1, 3, 5 }, { "Low end", "Sub", 0xff445566u, 0xff445566u }); // (a rename alone)
    CHECK (s.playlistTracks[1].name == "Sub" && s.playlistTracks[3].name.empty());

    s.channels.resize (4);
    s.channels[0].name = "Kick";
    s.channels[2].name = "Hat";
    s.channels[3].colour = 0xff999999u;
    selrename::applyToChannels (s, 0, selrename::targets (0, { 0, 2 }), { "Kick", "Kick 2", 0u, 0xff123456u });
    CHECK (s.channels[0].name == "Kick 2" && s.channels[0].colour == 0xff123456u);
    CHECK (s.channels[2].name == "Hat" && s.channels[2].colour == 0xff123456u);
    CHECK (s.channels[1].colour == 0u && s.channels[3].colour == 0xff999999u);
    selrename::colourChannels (s, { 1, 3 }, 0xff000001u);
    CHECK (s.channels[1].colour == 0xff000001u && s.channels[3].colour == 0xff000001u && s.channels[0].colour == 0xff123456u);
}

TEST (issues190ll_201_f2_renames_the_selected_channel_in_the_rack)
{
    // the channel menu shows "Rename and color... F2": with the Channel Rack focused F2 opens it (elsewhere F2 stays the
    // pattern's rename, in the Mixer the track's)
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ f2, none }, Context::channelRack, false) == "rack.rename");
    CHECK (map.find ({ f2, none }, Context::global, false) == "pattern.rename");
    CHECK (map.find ({ f2, none }, Context::mixer, false) == "mixer.rename");
}

// #200: a folder dropped on the Browser joins the list Add folder... writes (FL: "drop any folder from an external File
// Browser onto the FL Studio Browser and it will be added to the list"); listed already: nothing changes
TEST (issues190ll_200_dropped_folders_join_the_list)
{
    using namespace lynx;
    std::vector<std::string> list { "/home/u/Samples", "", "  ", "/home/u/Loops/" };
    auto added = userfolders::add (list, { "/home/u/Drums", "/home/u/Samples/", "/home/u/Loops", "/home/u/Drums" }, false);
    CHECK ((added == std::vector<std::string> { "/home/u/Drums" }));
    CHECK ((list == std::vector<std::string> { "/home/u/Samples", "/home/u/Loops/", "/home/u/Drums" }));
    // Windows: the case and the separator do not make another folder; a drive root keeps its backslash
    std::vector<std::string> win { "C:\\Samples" };
    CHECK (userfolders::add (win, { "c:\\samples\\" }, true).empty());
    CHECK (userfolders::add (win, { "D:\\" }, true).front() == "D:\\");
    CHECK (userfolders::key ("D:\\", true) == "d:\\" && userfolders::key ("/", false) == "/");
    CHECK (userfolders::add (win, { "D:\\" }, true).empty());
}

// #199: with "Use fixed size buffers" a hosted plugin's play head said the same position for every fixed block that
// completed inside one stretch of the chunk (and the stretch's start for a block begun in the stretch before): the song
// stood still, then jumped. A synth that keeps its LFOs / chorus / flanger / phaser in step with the host position
// (Vital resets them to it before every 128 samples) clicks there. The blocks now say where they began.
TEST (issues190ll_199_fixed_blocks_tell_where_they_began)
{
    using namespace lynx;
    FixedBlock fixed (96, 2);
    std::vector<float> l (1024), r (1024);
    float* io[2] = { l.data(), r.data() };
    std::vector<int> starts;
    int at = 0; // where the current run starts on the song's sample line
    for (const auto n : { 1024, 37, 987, 5, 200, 1, 1024 })
    {
        fixed.run (io, n, false, [&] (float* const*, int count, int end) {
            CHECK (count == 96);
            starts.push_back (at + end - count);
        });
        at += n;
    }
    CHECK (starts.size() == (std::size_t) (at / 96));
    for (std::size_t i = 0; i < starts.size(); ++i)
        CHECK (starts[i] == (int) i * 96); // (each block begins exactly where the one before ended)
    // the two-argument form is still taken (the other callers)
    int calls = 0;
    fixed.run (io, 192, false, [&] (float* const*, int) { ++calls; });
    CHECK (calls == 2);
}

// round 190od (issue #199): a chunk that runs over the pattern's end is one chunk; the plugin block that starts after
// the wrap (the pattern's first note) reports the pattern's start, not its end + a few samples
TEST (issues190od_199_blocks_after_the_pattern_wrap_report_its_start)
{
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::pattern;
    const auto length = (double) s.patterns[0].effectiveLength (s.meter());
    const auto wrap = lynx::hostPositionWrap (s, s.songLength());
    CHECK (wrap.at == length && wrap.to == 0.0);

    lynx::HostTimeline t;
    const auto tps = 960.0 * 130.0 / 60.0 / 48000.0; // 130 BPM at 48 kHz
    t.ticksPerSample.store (tps);
    t.tick.store (length - 79.0 * tps);
    t.wrapAt.store (wrap.at);
    t.wrapTo.store (wrap.to);
    CHECK (std::abs (t.tickAt (78) - (length - tps)) < 1e-6);         // before the wrap: as before
    CHECK (std::abs (t.tickAt (79)) < 1e-6);                          // the first note's block: the start
    CHECK (std::abs (t.tickAt (512) - 433.0 * tps) < 1e-6);           // and on from there
    t.wrapAt.store (0.0);
    CHECK (std::abs (t.tickAt (79) - length) < 1e-6);                 // no wrap known: unchanged

    // Song mode: the loop region's end goes back to its start; without a region, the song's end to 0
    s.mode = lynx::PlayMode::song;
    const auto bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.playlist = { lynx::Clip { 0, 0, 0, 4 * bar, 0 } };
    auto w = lynx::hostPositionWrap (s, s.songLength());
    CHECK (w.at == (double) s.songLength() && w.to == 0.0);
    s.loopStart = bar;
    s.loopEnd = 3 * bar;
    w = lynx::hostPositionWrap (s, s.songLength());
    CHECK (! s.loopPlays() || (w.at == (double) (3 * bar) && w.to == (double) bar));
}
