// Round 190q: FPC as one channel (issue #135, core/FpcInstrument.h) — it plays what the .flp import's channel per pad
// plays, the kit goes back to FL byte for byte

namespace fpc190q
{
// a sample per path: a decaying tone whose pitch, length and stereo come from the path (no files needed)
inline std::shared_ptr<const lynx::SampleData> sampleFor (const std::string& path)
{
    if (path.empty())
        return nullptr;
    std::uint32_t h = 2166136261u;
    for (const auto ch : path)
        h = (h ^ (std::uint8_t) ch) * 16777619u;
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    const auto frames = (std::size_t) (6000 + h % 30000);
    const auto hz = 90.0 + (double) ((h >> 8) % 900);
    d->left.resize (frames);
    if ((h >> 20) & 1)
        d->right.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto env = std::exp (-4.0 * (double) i / (double) frames);
        d->left[i] = (float) (0.6 * env * std::sin (6.283185307179586 * hz * (double) i / 44100.0));
        if (! d->right.empty())
            d->right[i] = (float) (0.5 * env * std::sin (6.283185307179586 * hz * 1.01 * (double) i / 44100.0));
    }
    return d;
}

inline std::u16string wide (const std::string& s) { return lynx::fpc::fromUtf8 (s); }

// A kit on FL's empty one: kick (two velocity layers), snare (panned, tuned, three layers that take turns), closed
// hat (cuts group 0), open hat (cut by group 0, quieter), a pad that cuts itself, a muted pad
inline lynx::fpc::Kit testKit()
{
    using namespace lynx;
    auto kit = fpc::emptyKit();
    auto layer = [] (const std::string& path, int volume, int low, int high, int tune = 0) {
        fpc::Layer l;
        l.path = wide (path);
        l.volume = volume;
        l.velocityLow = low;
        l.velocityHigh = high;
        l.tune = tune;
        return l;
    };
    auto& kick = kit.pads[0];
    kick.name = u"Kick";
    kick.note = 36;
    kick.volume = 128;
    kick.layers = { layer ("%FLStudioFactoryData%/Data/Patches/Packs/Drums/Kicks/kick soft.wav", 99, 0, 80),
                    layer ("%FLStudioFactoryData%/Data/Patches/Packs/Drums/Kicks/kick hard.wav", 127, 80, 127) };
    auto& snare = kit.pads[1];
    snare.name = u"Snare";
    snare.note = 38;
    snare.volume = 99;
    snare.pan = -40;
    snare.tune = 24;
    snare.flags = 8; // one layer at a time
    snare.layers = { layer ("snare a.wav", 127, 0, 127), layer ("snare b.wav", 110, 0, 127, -12), layer ("snare c.wav", 127, 0, 127, 30) };
    auto& closed = kit.pads[2];
    closed.name = u"Closed hat";
    closed.note = 42;
    closed.pan = 64;
    closed.cut = 0;
    closed.layers = { layer ("hat closed.wav", 127, 0, 127) };
    auto& open = kit.pads[3];
    open.name = u"Open hat";
    open.note = 46;
    open.volume = 64;
    open.cutBy = 0;
    open.layers = { layer ("hat open.wav", 127, 0, 127) };
    auto& self = kit.pads[4];
    self.name = u"Crash";
    self.note = 49;
    self.cut = 3;
    self.cutBy = 3;
    self.pan = -128;
    self.layers = { layer ("crash.wav", 100, 0, 127) };
    auto& muted = kit.pads[5];
    muted.name = u"Muted";
    muted.note = 50;
    muted.flags = 1;
    muted.layers = { layer ("tom.wav", 127, 0, 127) };
    return kit;
}

// one channel with the kit and a busy pattern on its keys (also a key without a pad and the muted pad's)
inline lynx::Song kitSong (const lynx::fpc::Kit& kit)
{
    using namespace lynx;
    Song s;
    s.bpm = 128.0;
    s.mixer = Song::defaultMixer (5); // Master + 5 inserts, no effects
    while (s.mixer.size() < 6)
        s.mixer.emplace_back();
    for (auto& t : s.mixer)
        t.effects.clear();
    ChannelDef c;
    c.name = "FPC";
    c.instrument = InstrumentKind::fpc;
    c.volume = 0.7f;
    c.pan = 0.2f;
    c.mixerTrack = 2;
    c.sampler.pitch = 1.0f;
    setFpcKit (c, kit, {});
    s.channels.clear();
    s.channels.push_back (c);
    s.patterns.clear();
    auto& p = s.patterns.emplace_back();
    auto note = [&p] (int step, int key, float velocity, int steps = 1, float pan = 0.0f) {
        Note n;
        n.start = (std::int64_t) step * ticksPerStep;
        n.length = (std::int64_t) steps * ticksPerStep;
        n.key = key;
        n.velocity = velocity;
        n.pan = pan;
        p.notes.push_back (n);
    };
    for (int bar = 0; bar < 2; ++bar)
    {
        const auto at = bar * 16;
        note (at + 0, 36, 0.9f);
        note (at + 0, 42, 0.7f);
        note (at + 0, 46, 0.8f, 4); // with the closed hat, on the same sample: both sound
        note (at + 2, 42, 0.5f);
        note (at + 3, 36, 0.3f);
        note (at + 4, 38, 0.8f);
        note (at + 4, 46, 1.0f, 3);
        note (at + 5, 42, 0.9f, 1, -0.5f); // cuts the open hat
        note (at + 6, 49, 0.7f, 2);
        note (at + 7, 49, 0.9f, 2);        // cuts itself
        note (at + 8, 36, 1.0f);
        note (at + 8, 50, 1.0f);           // muted pad
        note (at + 9, 61, 1.0f);           // no pad
        note (at + 10, 38, 0.6f);
        note (at + 11, 38, 0.7f);
        note (at + 12, 38, 0.2f, 1, 0.8f);
        note (at + 13, 46, 0.6f, 2);
        note (at + 14, 42, 0.6f);
        note (at + 15, 36, 0.55f);
    }
    p.sortNotes();
    Clip clip;
    clip.pattern = 0;
    clip.length = 8 * ppq;
    s.playlist.clear();
    s.playlist.push_back (clip);
    s.mode = PlayMode::song;
    return s;
}

// the same song as the import made it before this round: a sampler channel per pad
inline lynx::Song asPadChannels (lynx::Song s)
{
    using namespace lynx;
    for (int c = (int) s.channels.size() - 1; c >= 0; --c)
        if (s.channels[(std::size_t) c].instrument == InstrumentKind::fpc)
        {
            const auto pads = fpcChannelPads (s.channels[(std::size_t) c]);
            s.channels[(std::size_t) c].zones.clear();
            splitDrumChannel (s, c, pads);
        }
    return s;
}

struct Rendered
{
    std::vector<float> left, right;
};

inline Rendered render (const lynx::Song& s, double seconds, int block = 256)
{
    using namespace lynx;
    const double rate = 44100.0;
    auto state = RackState::build (s, nullptr, rate, [] (const std::string& path) { return sampleFor (path); });
    RackRenderer r;
    r.prepare (rate, block);
    const auto blocks = (int) (seconds * rate / block);
    Rendered out;
    out.left.resize ((std::size_t) blocks * (std::size_t) block);
    out.right.resize (out.left.size());
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int b = 0; b < blocks; ++b)
        r.process (state.get(), true, (double) b * block * tps, block, out.left.data() + (std::size_t) b * (std::size_t) block,
                   out.right.data() + (std::size_t) b * (std::size_t) block);
    return out;
}

inline float peak (const Rendered& a)
{
    float p = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i)
        p = std::max ({ p, std::abs (a.left[i]), std::abs (a.right[i]) });
    return p;
}

inline float difference (const Rendered& a, const Rendered& b)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < a.left.size() && i < b.left.size(); ++i)
        d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
    return a.left.size() == b.left.size() ? d : 1.0e9f;
}

inline lynx::FlpImportOptions flOptions (const std::string& dataDir)
{
    lynx::FlpImportOptions options;
    const std::filesystem::path data (dataDir);
    options.roots = { data.parent_path().string(), data.string(), "" };
    options.fileExists = [] (const std::string& p) {
        auto q = p;
        std::replace (q.begin(), q.end(), '\\', '/');
        return ! lynx::existingPathIgnoringCase (q).empty();
    };
    return options;
}
} // namespace fpc190q

TEST (fpc190q_one_channel_plays_what_the_pad_channels_play)
{
    // The null test behind the import's switch: the FPC channel against a sampler channel per pad (the import before
    // this round), same kit, same notes — pad and layer volumes, pad pan and tuning, velocity layers, layers that take
    // turns, cut groups (also two pads of a group on the same sample), a muted pad, a key without a pad
    using namespace lynx;
    const auto one = fpc190q::kitSong (fpc190q::testKit());
    const auto split = fpc190q::asPadChannels (one);
    CHECK (one.channels.size() == 1 && split.channels.size() == 5);
    const auto a = fpc190q::render (one, 4.2);
    const auto b = fpc190q::render (split, 4.2);
    const auto level = fpc190q::peak (b);
    CHECK (level > 0.05f);
    CHECK (fpc190q::difference (a, b) <= 2.0e-6f * std::max (1.0f, level));

    // other block sizes (notes in the middle of a block, cuts between blocks)
    for (const auto block : { 64, 1024 })
        CHECK (fpc190q::difference (fpc190q::render (one, 4.2, block), fpc190q::render (split, 4.2, block)) <= 2.0e-6f * std::max (1.0f, level));

    // the project's triangular panning law and "Fast declick for cut groups"
    auto tri = one;
    tri.panTriangular = true;
    tri.fastDeclick = true;
    const auto c = fpc190q::render (tri, 4.2);
    CHECK (fpc190q::difference (c, fpc190q::render (fpc190q::asPadChannels (tri), 4.2)) <= 2.0e-6f * std::max (1.0f, level));
    CHECK (fpc190q::difference (a, c) > 1.0e-3f); // (they do something)

    // the channel's pan, volume and pitch reach every pad
    auto moved = one;
    moved.channels[0].pan = -0.6f;
    moved.channels[0].volume = 0.4f;
    moved.channels[0].sampler.pitch = -3.0f;
    CHECK (fpc190q::difference (fpc190q::render (moved, 4.2), fpc190q::render (fpc190q::asPadChannels (moved), 4.2)) <= 2.0e-6f * std::max (1.0f, level));
}

TEST (fpc190q_pad_outputs_reach_their_inserts)
{
    // a pad with an output offset plays into the insert that many after the channel's, as its own channel would
    using namespace lynx;
    auto kit = fpc190q::testKit();
    kit.pads[1].output = 1;
    kit.pads[2].output = 3;
    kit.pads[3].output = 3;
    auto one = fpc190q::kitSong (kit);
    one.mixer[3].volume = 0.5f;
    one.mixer[5].pan = -1.0f;
    const auto split = fpc190q::asPadChannels (one);
    CHECK (split.channels[1].mixerTrack == 3 && split.channels[2].mixerTrack == 5 && split.channels[0].mixerTrack == 2);
    const auto a = fpc190q::render (one, 4.2);
    const auto b = fpc190q::render (split, 4.2);
    CHECK (fpc190q::peak (b) > 0.05f);
    CHECK (fpc190q::difference (a, b) <= 2.0e-6f * std::max (1.0f, fpc190q::peak (b)));
    auto plain = fpc190q::kitSong (fpc190q::testKit());
    plain.mixer[3].volume = 0.5f;
    plain.mixer[5].pan = -1.0f;
    CHECK (fpc190q::difference (a, fpc190q::render (plain, 4.2)) > 1.0e-3f); // (the inserts' settings are heard)
}

TEST (fpc190q_mute_solo_and_missing_files)
{
    using namespace lynx;
    auto kit = fpc190q::testKit();
    ChannelDef c;
    c.instrument = InstrumentKind::fpc;
    setFpcKit (c, kit, {});
    CHECK (c.zones.size() == 9 && fpcLayerPaths (kit).size() == 9);
    CHECK (c.pluginState == fpc::write (kit));
    auto pads = fpcChannelPads (c);
    CHECK (pads.size() == 5); // (the muted pad and the 26 empty ones do not play)
    CHECK (pads[0].name == "Kick" && pads[0].key == 36 && pads[0].layers.size() == 2);
    CHECK (pads[0].layers[1].path == "%FLStudioFactoryData%/Data/Patches/Packs/Drums/Kicks/kick hard.wav");

    // FPC's solo: with a pad soloed only the soloed ones play (REA_rodada9 §1.3)
    kit.pads[1].flags |= 2;
    kit.pads[3].flags |= 2;
    setFpcKit (c, kit, {});
    pads = fpcChannelPads (c);
    CHECK (pads.size() == 2 && pads[0].name == "Snare" && pads[1].name == "Open hat");
    kit.pads[5].flags |= 2; // a muted pad's solo: the others are silenced, it stays muted
    setFpcKit (c, kit, {});
    CHECK (fpcChannelPads (c).size() == 2);

    // the files: each layer on what `locate` found; a layer whose file is missing does not play, a pad without any is out
    kit = fpc190q::testKit();
    setFpcKit (c, kit, [] (const std::string& path) {
        return path.find ("kick hard") != std::string::npos || path.find ("snare") != std::string::npos ? std::string() : "/found/" + path;
    });
    CHECK (c.zones.size() == 9 && c.zones[0].path.rfind ("/found/", 0) == 0 && c.zones[1].path.empty());
    pads = fpcChannelPads (c);
    CHECK (pads.size() == 4 && pads[0].layers.size() == 1 && pads[1].name == "Closed hat");
    CHECK (fpc::read (c.pluginState).has_value() && *fpc::read (c.pluginState) == kit); // (the state keeps FL's own paths)

    // an unreadable state, or no kit at all: no pads, and an instrument that stays silent
    ChannelDef broken;
    broken.instrument = InstrumentKind::fpc;
    broken.pluginState = { 1, 2, 3 };
    CHECK (fpcChannelPads (broken).empty());
    auto silent = makeFpcInstrument (broken, {});
    CHECK (silent != nullptr);
    if (silent != nullptr)
    {
        silent->prepare (44100.0);
        silent->configure (broken, 120.0);
        silent->noteOn (36, 1.0f);
        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        silent->render (l.data(), r.data(), 256, 1.0f, 1.0f);
        CHECK (silent->idle() && fpc190q::peak ({ l, r }) == 0.0f);
    }
}

TEST (fpc190q_flp_round_trip_is_byte_exact)
{
    // LYNX → .flp → LYNX → .flp: the FPC channel stays one FPC channel, its state the same bytes, the two files equal
    using namespace lynx;
    const auto kit = fpc190q::testKit();
    const auto song = fpc190q::kitSong (kit);
    const auto state = fpc::write (kit);
    flpexport::Options options;
    flpexport::Report report;
    const auto file = flpexport::exportFlp (song, options, report);
    CHECK (! file.empty());
    const auto project = flp::parseProject (file);
    const flp::Channel* fpcChannel = nullptr;
    for (const auto& c : project.channels)
        if (c.pluginName == "FPC")
            fpcChannel = &c;
    CHECK (fpcChannel != nullptr);
    if (fpcChannel == nullptr)
        return;
    CHECK (fpcChannel->pluginState == state);

    FlpImportOptions import;
    import.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, import);
    const ChannelDef* channel = nullptr;
    for (const auto& c : back.channels)
        if (c.instrument == InstrumentKind::fpc)
            channel = &c;
    CHECK (channel != nullptr);
    if (channel == nullptr)
        return;
    CHECK (channel->pluginState == state);
    CHECK (channel->zones.size() == song.channels[0].zones.size());
    CHECK (fpcChannelPads (*channel).size() == 5);
    std::size_t notes = 0;
    for (const auto& p : back.patterns)
        notes += p.notes.size();
    CHECK (notes == song.patterns[0].notes.size()); // (the notes stay on the channel, on their keys)
    flpexport::Report again;
    CHECK (flpexport::exportFlp (back, options, again) == file);

    // the import before this round is still there (FlpImportOptions::fpcAsPads), and a kit with a pad on another
    // insert keeps it (a channel per pad)
    import.fpcAsPads = true;
    const auto pads = importFlp (project, import);
    CHECK (std::none_of (pads.channels.begin(), pads.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::fpc; }));
    CHECK (pads.channels.size() == back.channels.size() + 4);
    import.fpcAsPads = false;
    auto routed = kit;
    routed.pads[2].output = 1;
    auto routedSong = song;
    setFpcKit (routedSong.channels[0], routed, {});
    flpexport::Report third;
    const auto routedBack = importFlp (flp::parseProject (flpexport::exportFlp (routedSong, options, third)), import);
    CHECK (std::none_of (routedBack.channels.begin(), routedBack.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::fpc; }));
}

TEST (fpc190q_fl_projects_sound_the_same_and_go_back_byte_exact)
{
    // Every project of the FL install with an FPC: the import with one FPC channel against the import with a channel
    // per pad, only those channels playing (no effects), from the song's start — and the FPC states an export writes
    // are FL's own bytes
    using namespace lynx;
    if (realFlpDir.empty() || realFlpDir[0] != '/')
        return;
    auto options = fpc190q::flOptions (realFlpDir);
    int projects = 0, kits = 0, sounding = 0, states = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir, std::filesystem::directory_options::skip_permission_denied, error))
    {
        if (entry.path().extension() != ".flp")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
        flp::Project project;
        try
        {
            project = flp::parseProject (bytes);
        }
        catch (const std::exception&)
        {
            continue;
        }
        std::vector<std::vector<std::uint8_t>> fl;
        for (const auto& c : project.channels)
            if (c.pluginName == "FPC" && ! c.pluginState.empty())
                fl.push_back (c.pluginState);
        if (fl.empty())
            continue;
        options.fpcAsPads = false;
        auto one = importFlp (project, options);
        const auto fpcChannels = (int) std::count_if (one.channels.begin(), one.channels.end(), [] (const ChannelDef& c) { return c.instrument == InstrumentKind::fpc; });
        if (fpcChannels == 0)
            continue; // (every kit of it has pads on other inserts: imported as before)
        ++projects;
        kits += fpcChannels;

        // the export: each FPC channel's state is one of FL's, byte for byte
        {
            flpexport::Options eo;
            flpexport::Report report;
            const auto written = flp::parseProject (flpexport::exportFlp (one, eo, report));
            for (const auto& c : written.channels)
                if (c.pluginName == "FPC")
                {
                    CHECK (std::find (fl.begin(), fl.end(), c.pluginState) != fl.end());
                    ++states;
                }
        }

        options.fpcAsPads = true;
        auto split = importFlp (project, options);
        CHECK (split.channels.size() > one.channels.size());
        // only the kits play, dry
        for (auto* s : { &one, &split })
        {
            for (auto& t : s->mixer)
                t.effects.clear();
            for (auto& c : s->channels)
                c.muted = ! (c.instrument == InstrumentKind::fpc
                             || (c.instrument == InstrumentKind::sampler && ! c.zones.empty() && c.sampler.flVelocityLaw && c.name.find (" - ") != std::string::npos));
            s->mode = PlayMode::song;
        }
        const auto a = fpc190q::render (one, 12.0, 512);
        const auto b = fpc190q::render (split, 12.0, 512);
        const auto level = fpc190q::peak (b);
        const auto diff = fpc190q::difference (a, b);
        if (diff > 2.0e-6f * std::max (1.0f, level))
            std::printf ("  %s: FPC channel vs pad channels differ by %g (peak %g)\n", entry.path().filename().string().c_str(), (double) diff, (double) level);
        CHECK (diff <= 2.0e-6f * std::max (1.0f, level));
        sounding += level > 1.0e-3f ? 1 : 0;
    }
    std::printf ("  FPC as one channel: %d projects, %d kits, %d sounding in the first 12 s, %d states written back\n", projects, kits, sounding, states);
    CHECK (projects >= 3 && sounding >= 1 && states == kits); // (FL 2026: 7 of its 21 demo kits have every pad on the channel's insert)
}

// ---- kits (.fst presets of FPC) ----

TEST (fpc190q_new_channel_and_kit_folders)
{
    using namespace lynx;
    // FPC is one of the engines a project can add; without FL Studio a new one is FL's empty kit (silent, as FL's)
    const auto* engine = natives::instrumentEngine (InstrumentKind::fpc);
    CHECK (engine != nullptr);
    if (engine == nullptr)
        return;
    CHECK (std::string (engine->name) == "FPC" && natives::presetFolderOf (InstrumentKind::fpc) == "FPC");
    natives::FlAccess none;
    CHECK (natives::available (*engine, none));
    const auto made = natives::makeChannel (InstrumentKind::fpc, none);
    CHECK (made.has_value());
    if (made)
    {
        CHECK (made->instrument == InstrumentKind::fpc && made->name == "FPC");
        CHECK (made->pluginState == fpc::write (fpc::emptyKit()) && made->zones.empty());
        CHECK (fpcChannelPads (*made).empty());
        CHECK (flpexport::flGeneratorName (*made) == "FPC"); // (it goes to FL as an FPC)
    }

    // the kits come from FL's own folder and from the user's (nothing of FL is stored in LYNX)
    FlPathRoots roots;
    roots.data = "C:/FL/Data";
    roots.userData = "C:/Users/u/Documents/Image-Line/FL Studio";
    const auto folders = presetcycle::pluginPresetFolders (roots, true, natives::presetFolderOf (InstrumentKind::fpc));
    CHECK (folders.size() == 2);
    if (folders.size() == 2)
    {
        CHECK (folders[0] == "C:/FL/Data/Patches/Plugin presets/Generators/FPC");
        CHECK (folders[1] == "C:/Users/u/Documents/Image-Line/FL Studio/Presets/Plugin presets/Generators/FPC");
    }
}

TEST (fpc190q_fl_kits_load_onto_a_channel)
{
    // Every FPC kit of the FL install (.fst): it loads onto an FPC channel — also the "multi out" kits, whose pads play
    // into other inserts —, the state is the file's own bytes, its samples are found in the install
    using namespace lynx;
    if (realFlpDir.empty() || realFlpDir[0] != '/')
        return;
    const auto fl = natives142::flAccess();
    const auto folder = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "FPC";
    std::error_code error;
    if (! std::filesystem::is_directory (folder, error))
        return;
    int kits = 0, playing = 0, routed = 0, layers = 0, found = 0;
    ChannelDef current;
    current.instrument = InstrumentKind::fpc;
    current.name = "My drums";
    current.mixerTrack = 7;
    current.volume = 0.5f;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (folder, std::filesystem::directory_options::skip_permission_denied, error))
    {
        if (entry.path().extension() != ".fst")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
        std::vector<std::uint8_t> state;
        try
        {
            for (const auto& c : flp::parseProject (file, true).channels)
                if (c.pluginName == "FPC")
                    state = c.pluginState;
        }
        catch (const std::exception&)
        {
        }
        if (state.empty())
            continue;
        ++kits;
        const auto name = entry.path().stem().string();
        const auto loaded = natives::withPreset (current, entry.path().string(), name, fl);
        if (! loaded)
            std::printf ("  kit not loaded: %s\n", entry.path().filename().string().c_str());
        CHECK (loaded.has_value());
        if (! loaded)
            continue;
        CHECK (loaded->instrument == InstrumentKind::fpc && loaded->pluginState == state);
        CHECK (loaded->name == name && loaded->mixerTrack == 7 && loaded->volume == 0.5f); // (the channel's own settings stay)
        const auto kit = fpc::read (state);
        CHECK (kit.has_value());
        if (! kit)
            continue;
        CHECK (loaded->zones.size() == fpcLayerPaths (*kit).size());
        layers += (int) loaded->zones.size();
        for (const auto& z : loaded->zones)
            found += z.path.empty() ? 0 : 1;
        const auto pads = fpcChannelPads (*loaded);
        playing += pads.empty() ? 0 : 1;
        routed += std::any_of (pads.begin(), pads.end(), [] (const DrumPad& p) { return p.mixerOffset != 0; }) ? 1 : 0;
    }
    std::printf ("  FPC kits: %d, %d with pads that play, %d with pads on other inserts; %d of %d layer files found\n", kits, playing, routed, found, layers);
    CHECK (kits >= 19 && playing >= kits - 1 && routed >= 1); // (Empty.fst has no sample)
    CHECK (layers > 0 && found >= layers * 9 / 10);

    // a new FPC channel on a PC with FL: FL's own new FPC (its Plugin database entry, else the Default kit), with pads
    const auto made = natives::makeChannel (InstrumentKind::fpc, fl);
    CHECK (made.has_value() && made->instrument == InstrumentKind::fpc && ! fpcChannelPads (*made).empty());
}
