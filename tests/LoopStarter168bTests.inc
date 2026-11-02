// Round 168b: the local Loop Starter (core/LoopStarter.h, REA_rodada168b-loopstarter.md).
#include "core/LoopStarter.h"

namespace
{
std::string ls168bRead (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace

TEST (loopstarter168b_fl_random_bit_exact)
{
    using lynx::loopstarter::FlRand;
    // reference values from FL's own toolbox.py (rand_int / rand_uniform / rand_tri) run in Python 3.12
    FlRand r (12345);
    const std::uint32_t expect[] = { 556608163u, 323275282u, 1178251266u, 1799102494u, 2023681743u };
    for (const auto e : expect)
        CHECK (r.nextInt() == e);
    FlRand big (17300000000000000ull);
    CHECK (big.nextInt() == 1545453568u);
    FlRand u (7);
    CHECK (std::abs (u.uniform() - 0.6524758473969996) < 1e-15);
    CHECK (std::abs (u.uniform() - 0.806957297027111) < 1e-15);
    FlRand t (99);
    CHECK (std::abs (t.triangular (0.4545, 2) - 0.43193142499968346) < 1e-12);
    CHECK (std::abs (t.triangular (0.4545, 2) - 0.5472468796865708) < 1e-12);
}

TEST (loopstarter168b_step_tokens)
{
    using namespace lynx::loopstarter;
    // drum_setup.py: velocity range(72, 125, 4) / 128 (default state 7), shift a..x / 24, octave q r s, offset 0..b
    CHECK (std::abs (velocityOfToken ('0') - 72.0f / 128.0f) < 1e-6f);
    CHECK (std::abs (velocityOfToken ('d') - 124.0f / 128.0f) < 1e-6f);
    CHECK (std::abs (velocityOfToken ('`') - 100.0f / 128.0f) < 1e-6f);
    CHECK (shiftOfToken ('a') == 0.0 && std::abs (shiftOfToken ('m') - 0.5) < 1e-12 && shiftOfToken ('`') == 0.0);
    CHECK (octaveOfToken ('q') == -1 && octaveOfToken ('r') == 0 && octaveOfToken ('s') == 1);
    CHECK (semitoneOfToken ('7') == 7 && semitoneOfToken ('b') == 11 && semitoneOfToken ('`') == 0);
    CHECK (repeatProportionOfToken ('0') == 1.0 && repeatProportionOfToken ('3') == 0.5 && std::abs (repeatProportionOfToken ('7') - 1.0 / 6.0) < 1e-12);
    CHECK (presetFileName ("trap") == "trap presets.lstj");
}

TEST (loopstarter168b_presets_and_notes)
{
    using namespace lynx::loopstarter;
    const std::string json = R"({"P1": {"genre": "trap", "weight": 2, "kit": ["kick", "ghost snare|clap"],
        "rhythm_strs": [">```>```>```>```", "````>```````>```"], "vel_strs": ["dddddddddddddddd", "0000000000000000"],
        "repeat_strs": ["0000000000000000", "0000300000000000"],
        "refs": {"kick": {"rhythm": [">```````````````", ">```>```````````"]}}}})";
    const auto presets = parsePresets (json);
    CHECK (presets.size() == 1);
    if (presets.size() != 1)
        return;
    const auto& p = presets[0];
    CHECK (p.name == "P1" && p.genre == "trap" && p.weight == 2.0 && p.lanes.size() == 2);
    CHECK ((p.lanes[1].kit == std::vector<std::string> { "ghost snare", "clap" }));
    CHECK (p.laneFor ("clap") == &p.lanes[1] && p.laneFor ("ride") == nullptr);
    // the kick: 4 bars alternate its two variants (1 hit, 2 hits)
    const auto kick = laneNotes (p.lanes[0], 4, 3, 60);
    CHECK (kick.size() == 6);
    CHECK (kick[0].start == 0 && kick[1].start == 16 * lynx::ticksPerStep && kick[2].start == 20 * lynx::ticksPerStep);
    CHECK (kick[0].channel == 3 && kick[0].key == 60 && kick[0].length <= lynx::ticksPerStep);
    // the snare's repeat '3' (every half step) fills up to the next hit, at most a beat
    const auto snare = laneNotes (p.lanes[1], 1, 0, 60);
    CHECK (snare.size() == 8 + 1);
    CHECK (snare[1].start - snare[0].start == lynx::ticksPerStep / 2);
}

TEST (loopstarter168b_config)
{
    using namespace lynx::loopstarter;
    const std::string json = R"({"default_genre": "trap", "same_sample_block_count": {"oneshot": 3},
      "tag_map": {"trap": {"label": "Trap"}, "lofi": {"label": "Lo-Fi"}},
      "genres": {
        "trap": {"weight": 1, "kit": {"kick": 1, "snare": 0.5}, "size": [3, 6], "default_tempo": 140, "tempo_range": [130, 152],
                 "content": ["loops", "oneshots"],
                 "templates": [{"host": "FLS", "channels": [{"tool": "Audio", "label": "Drums"}, {"tool": "Drum", "label": "Kit 1"}, {"tool": "Drum", "label": "Kit 2"}]}],
                 "oneshot_channels": {"Kit 1": {"options": ["kick"]}, "Kit 2": {"options": ["snare", "clap"]}}},
        "lofi": {"content": ["Raw"]}}})";
    const auto c = parseConfig (json);
    CHECK (c.has_value());
    if (! c)
        return;
    CHECK (c->fromFl && c->defaultGenre == "trap" && c->genres.size() == 2);
    const auto* trap = c->find ("trap");
    CHECK (trap != nullptr && trap->label == "Trap" && trap->tempoMin == 130 && trap->tempoMax == 152);
    CHECK (trap->kitSlots.size() == 2 && trap->kitSlots[1].label == "Kit 2" && trap->kitSlots[1].options.size() == 2);
    // FL lists only the genres with loops / one-shots content
    CHECK (c->listed().size() == 1);
    CHECK (! parseConfig ("[1, 2]").has_value());
    // update_pattern_tempo: an integer in the range
    FlRand rng (5);
    for (int i = 0; i < 50; ++i)
    {
        const auto bpm = pickTempo (*trap, rng);
        CHECK (bpm >= 130 && bpm < 152 && bpm == std::floor (bpm));
    }
    CHECK (builtinConfig().find (builtinConfig().defaultGenre) != nullptr);
}

TEST (loopstarter168b_set_blanks)
{
    using namespace lynx::loopstarter;
    Genre g;
    g.kit = { { "kick", 1.0 }, { "snare", 1.0 }, { "rim", 0.0 }, { "shaker", 0.0 }, { "tom", 0.0 } };
    g.sizeMin = 3;
    std::vector<ChannelPlan> base;
    for (const char* c : { "kick", "snare", "rim", "shaker", "tom" })
    {
        ChannelPlan p;
        p.category = c;
        p.notes.resize (2);
        base.push_back (p);
    }
    for (std::uint64_t seed = 1; seed < 200; ++seed)
    {
        auto chans = base;
        FlRand rng (seed);
        setBlanks (g, chans, rng);
        int blanks = 0;
        for (const auto& c : chans)
            blanks += c.blank ? 1 : 0;
        CHECK (blanks <= 2);                                // DRUMS_MAX_BLANK
        CHECK (! chans[0].blank && ! chans[1].blank);        // weight 1: never blank
    }
    // below the kit's minimum size nothing is blank; locked slots are never blank
    auto two = std::vector<ChannelPlan> (base.begin(), base.begin() + 2);
    FlRand rng (3);
    setBlanks (g, two, rng);
    CHECK (! two[0].blank && ! two[1].blank);
    for (std::uint64_t seed = 1; seed < 100; ++seed)
    {
        auto chans = base;
        FlRand r (seed);
        setBlanks (g, chans, r, { false, false, true, true, true });
        for (const auto& c : chans)
            CHECK (! c.blank);
    }
}

TEST (loopstarter168b_classifier)
{
    using namespace lynx::loopstarter;
    CHECK (classifySample ("Packs/Drums/Kicks/Big Kick.wav") == "kick");
    CHECK (classifySample ("Packs/Drums/Hats/707 OH.wav") == "open hat");
    CHECK (classifySample ("Packs/Legacy/Collider Sub Kick.wav") == "sub kick");
    CHECK (classifySample ("Packs/Kit/tom1_oh.wav").empty()); // a kit's overhead mic, not a one-shot
    CHECK (classifySample ("Packs/Loops/Drum Loop 120 bpm.wav").empty());
    Library lib;
    lib.add ("a/Kick 1.wav");
    lib.add ("a/Kick 2.wav");
    lib.add ("a/Loop 90bpm.wav");
    CHECK (lib.size() == 2);
    FlRand rng (1);
    // a re-roll avoids the recent samples
    for (int i = 0; i < 20; ++i)
        CHECK (pickSample (lib, "kick", rng, { "a/Kick 1.wav" }) == "a/Kick 2.wav");
    CHECK (pickSample (lib, "ride", rng).empty() || ! fallbackCategories ("ride").empty());
}

TEST (loopstarter168b_generate_apply_send)
{
    using namespace lynx::loopstarter;
    const auto config = builtinConfig();
    Sources src;
    src.config = &config;
    const auto a = generate (src, "trap", 42), b = generate (src, "trap", 42);
    CHECK (a.genre == "trap" && a.channels.size() == 6 && a.bars == loopBars);
    CHECK (a.bpm == b.bpm && a.channels.size() == b.channels.size()); // same seed, same loop
    CHECK (a.bpm >= 135 && a.bpm < 146);
    int withNotes = 0;
    for (const auto& c : a.channels)
        withNotes += c.notes.empty() ? 0 : 1;
    CHECK (withNotes >= 2);

    lynx::Song song;
    song.mixer.resize (5); // master + 4 inserts
    const auto patternsBefore = (int) song.patterns.size();
    const auto channelsBefore = (int) song.channels.size();
    const auto p = apply (song, a);
    CHECK (p == patternsBefore && song.currentPattern == p);
    CHECK (song.patterns[(std::size_t) p].name == "Loop Starter #1" && song.patterns[(std::size_t) p].loopStarterGenre == "trap");
    CHECK (nextPatternName (song) == "Loop Starter #2");
    const auto chans = channelsOf (song, p);
    CHECK ((int) chans.size() == 6 && chans.front() == channelsBefore);
    CHECK (song.bpm == a.bpm);
    const auto insert = song.channels[(std::size_t) chans.front()].mixerTrack;
    CHECK (insert > 0 && song.mixer[(std::size_t) insert].name == "LS Steps");
    for (const auto& n : song.patterns[(std::size_t) p].notes)
        CHECK (n.channel >= channelsBefore);
    // a second loop shares the "LS Steps" insert
    const auto p2 = apply (song, generate (src, "house", 7));
    CHECK (song.channels[(std::size_t) channelsOf (song, p2).front()].mixerTrack == insert);

    // new steps for one channel replace only its notes
    const auto ch = chans[0];
    setChannelNotes (song, p, ch, {}, 4);
    for (const auto& n : song.patterns[(std::size_t) p].notes)
        CHECK (n.channel != ch);

    // send to playlist: first free track at the position; the channels leave Loop Starter
    lynx::Clip blocker;
    blocker.track = 0;
    blocker.start = 0;
    blocker.length = 1000000;
    blocker.pattern = p2;
    song.playlist.push_back (blocker);
    const auto t = sendToPlaylist (song, p, 0);
    CHECK (t == 1);
    CHECK (song.playlist.back().pattern == p && song.playlist.back().track == 1);
    CHECK (channelsOf (song, p).empty() && ! channelsOf (song, p2).empty());

    // removing a pattern keeps the other loop's channels pointing at it
    song.removePattern (p);
    CHECK (! channelsOf (song, p2 - 1).empty());
}

TEST (loopstarter168b_fl_install)
{
    using namespace lynx::loopstarter;
    if (realFlpDir.empty())
        return;
    const auto notes = std::filesystem::path (realFlpDir).parent_path() / "System" / "Tools" / "Chord progression" / "Notes";
    if (! std::filesystem::exists (notes / "genres.lstj"))
        return;
    const auto config = parseConfig (ls168bRead (notes / "genres.lstj"));
    CHECK (config.has_value());
    if (! config)
        return;
    CHECK (config->genres.size() >= 10 && config->find (config->defaultGenre) != nullptr);
    const auto* trap = config->find ("trap");
    CHECK (trap != nullptr && trap->kitSlots.size() >= 4 && trap->tempoMax > trap->tempoMin);
    Sources src;
    src.config = &*config;
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator (notes / "content"))
    {
        auto name = e.path().filename().string();
        for (auto& c : name)
            c = (char) std::tolower ((unsigned char) c);
        if (name == "trap presets.lstj")
        {
            src.presets["trap"] = parsePresets (ls168bRead (e.path()));
            ++files;
        }
    }
    CHECK (files == 1 && src.presets["trap"].size() >= 5);
    for (const auto& p : src.presets["trap"])
        for (const auto& l : p.lanes)
            CHECK (l.main.rhythm.size() == 16);
    const auto plan = generate (src, "trap", 1234);
    CHECK (plan.channels.size() == trap->kitSlots.size());
}
