// Round 168j: the .flp events of the 168c gap plan — icons (channel 0x9B, mixer 0x5F, playlist track 0xEE +8), the
// "own colour" flags 0x2A / 0x2B, Monitor external input 0xA6, PDC Automatic 0x1D / Compensate automations 0x27, Time
// settings 0x23, mixer track count 0x67, scale highlighting 0xF3, data folder 0xCA, pattern length 0xA4 — and FL's DC
// hold at an audio clip's end (docs/analise/REA_rodada168j.md)

#define REA168J_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace rea168j
{
inline std::vector<std::uint8_t> le32 (std::initializer_list<std::int32_t> values)
{
    std::vector<std::uint8_t> out;
    for (auto v : values)
        for (int i = 0; i < 4; ++i)
            out.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * i)));
    return out;
}

// a project in FL's event order: globals, one channel, one pattern, one Playlist track, two mixer inserts
inline std::vector<std::uint8_t> project (bool flags)
{
    FlpBuilder b;
    b.ascii (0xC7, "25.2.5.5319");
    if (flags)
    {
        b.u8 (0x1D); b.u8 (0);  // PDC Automatic off
        b.u8 (0x27); b.u8 (0);  // Compensate automations off
        b.u8 (0x23); b.u8 (1);  // Time settings: time signature
        b.u8 (0x67); b.u16 (20); // 20 mixer track records: Master + 18 inserts + "current"
        b.data (0xF3, le32 ({ 10, 5 })); // Minor Natural (Aeolian), root F
        b.utf16 (0xCA, u"D:\\Data");
    }
    b.u8 (0x40); b.u16 (0); // channel 0
    b.u8 (0x15); b.u8 (0);
    b.utf16 (0xCB, u"Kick");
    if (flags)
    {
        b.u8 (0x9B);
        b.u32 (75);
    }
    b.u8 (0x41); b.u16 (1); // pattern 1
    if (flags)
    {
        b.u8 (0xA4);
        b.u32 (1536); // 16 beats at PPQ 96
    }
    b.u8 (0x63); b.u16 (0); // arrangement 0
    std::vector<std::uint8_t> track (70, 0);
    const std::uint32_t index = 3, colour = 0x00FF8040, icon = flags ? 30 : 0;
    std::memcpy (&track[0], &index, 4);
    std::memcpy (&track[4], &colour, 4);
    std::memcpy (&track[8], &icon, 4);
    track[12] = 1;
    b.data (0xEE, track);
    if (flags)
    {
        b.u8 (0x2B); b.u8 (0); // the track's own colour off: FL puts the default back
    }
    // insert 0 (Master), then insert 1 with a colour, the flag off, an icon and monitoring "When armed"
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));
    b.u8 (0x95); b.u32 (0x00102030);
    if (flags)
    {
        b.u8 (0x2A); b.u8 (0);
        b.u8 (0x5F); b.u16 (71);
    }
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));
    if (flags)
    {
        b.u8 (0xA6); b.u32 (1);
    }
    return b.bytes;
}
} // namespace rea168j

// §1 the parser: every event to its field [DECOMP loader FUN_00f46490 / savers FUN_00f40840, FUN_00f3d620]
TEST (rea168j_flp_events_parsed)
{
    const auto bytes = rea168j::project (true);
    const auto p = flp::parseProject (bytes);
    CHECK (! p.autoPdc && ! p.compensateAutomations && p.timeSignatureMode && p.mixerTrackCount == 20);
    CHECK (p.hasScaleHighlight && p.scaleHighlight == 10 && p.scaleRoot == 5 && p.dataFolder == "D:\\Data");
    REA168J_REQUIRE (p.channels.size() == 1 && p.patterns.size() == 1 && p.arrangements.size() == 1 && p.mixer.size() == 2);
    CHECK (p.channels[0].icon == 75);
    CHECK (p.patterns[0].fixedLength == 1536);
    REA168J_REQUIRE (p.arrangements[0].tracks.size() == 1);
    CHECK (p.arrangements[0].tracks[0].icon == 30 && p.arrangements[0].tracks[0].colour == 0x485156); // 0x2B = 0: default colour
    CHECK (p.mixer[1].colour == 0x302010 && p.mixer[1].colourSet == 0 && p.mixer[1].icon == 71 && p.mixer[1].monitor == 1);
    CHECK (p.mixer[0].monitor == -1 && p.mixer[0].colourSet == -1 && p.mixer[0].icon == 0);

    // without the events: FL's defaults
    const auto plain = flp::parseProject (rea168j::project (false));
    CHECK (plain.autoPdc && plain.compensateAutomations && ! plain.timeSignatureMode && plain.mixerTrackCount == 0);
    CHECK (! plain.hasScaleHighlight && plain.dataFolder.empty());
    REA168J_REQUIRE (plain.arrangements.size() == 1 && plain.arrangements[0].tracks.size() == 1 && plain.mixer.size() == 2);
    CHECK (plain.arrangements[0].tracks[0].colour == 0x4080FF && plain.mixer[1].colourSet == -1 && plain.mixer[1].monitor == -1);
}

// §1 the import: icons, PDC flags, time settings, scale, data folder, mixer count, pattern length, monitoring
TEST (rea168j_flp_import)
{
    using namespace lynx;
    const auto song = importFlp (flp::parseProject (rea168j::project (true)));
    CHECK (! song.autoPdc && ! song.compensateAutomations && song.timeSignatureMode);
    CHECK (song.scaleHighlight.scale == 10 && song.scaleHighlight.root == 5 && song.dataFolder == "D:\\Data");
    CHECK (song.mixer.size() == 19); // 20 records − FL's "current" track = Master + 18 inserts
    REA168J_REQUIRE (! song.channels.empty() && song.mixer.size() > 1);
    CHECK (song.channels[0].icon == 75);
    CHECK (song.mixer[1].icon == 71 && song.mixer[1].colour == 0 && song.mixer[1].monitor == InputMonitor::whenArmed);
    REA168J_REQUIRE (song.playlistTracks.size() == 3); // FL's track 3 (1-based)
    CHECK (song.playlistTracks[2].icon == 30 && song.playlistTracks[2].colour == 0);

    const auto plain = importFlp (flp::parseProject (rea168j::project (false)));
    CHECK (plain.autoPdc && plain.compensateAutomations && ! plain.timeSignatureMode && plain.scaleHighlight == Song::ScaleHighlight {});
    CHECK (plain.mixer.size() == 17); // no 0x67: 16 inserts as before
    REA168J_REQUIRE (plain.mixer.size() > 1);
    CHECK (plain.mixer[1].colour == (0xFF000000u | 0x302010) && plain.mixer[1].monitor == InputMonitor::automatic);
}

// §1 0xA4: the pattern length selector's ticks (file PPQ) become the pattern's fixed length
TEST (rea168j_flp_pattern_length)
{
    using namespace lynx;
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (0);
    b.u8 (0x41); b.u16 (1);
    b.u8 (0xA4);
    b.u32 (1536);
    b.data (0xE0, noteRecord (0, 0, 24, 60, 100));
    const auto song = importFlp (flp::parseProject (b.bytes));
    REA168J_REQUIRE (song.patterns.size() == 1);
    CHECK (song.patterns[0].lengthTicks == 16 * ppq);
    CHECK (song.patterns[0].effectiveLength (lynx::Meter (4, 4)) == 16 * ppq);
}

// §2 FL's reference scales: the 37 scales in FL's order; the ones LYNX lists map onto its own
TEST (rea168j_reference_scales)
{
    namespace nt = lynx::notetools;
    const auto& fl = nt::flReferenceScales();
    CHECK (fl.size() == 37);
    CHECK (std::string (fl[10].name) == "Minor Natural (Aeolian)");
    CHECK (nt::scales()[(std::size_t) nt::scaleForFlReference (0)].intervals == fl[0].intervals); // Major
    CHECK (std::string (nt::scales()[(std::size_t) nt::scaleForFlReference (10)].name) == "Natural minor");
    CHECK (std::string (nt::scales()[(std::size_t) nt::scaleForFlReference (7)].name) == "Harmonic minor");
    CHECK (std::string (nt::scales()[(std::size_t) nt::scaleForFlReference (12)].name) == "Minor pentatonic");
    CHECK (nt::scaleForFlReference (-2) == 0 && nt::scaleForFlReference (-1) == 0 && nt::scaleForFlReference (37) == 0);
    CHECK (nt::scaleForFlReference (2) == 0); // Major Bulgarian: not in LYNX's list
    for (const auto& s : fl)
        CHECK (! s.intervals.empty() && s.intervals[0] == 0 && std::is_sorted (s.intervals.begin(), s.intervals.end()));

    // with FL's installation: the table equals FL's own .fsc files (the keys of each, from C5)
    if (realFlpDir.empty())
        return;
    const auto dir = std::filesystem::path (realFlpDir) / "Patches" / "Scores" / "Reference scales";
    if (! std::filesystem::exists (dir))
        return;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator (dir))
        if (e.path().extension() == ".fsc")
            files.push_back (e.path());
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b) {
        auto lower = [] (std::string s) { std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); }); return s; };
        return lower (a.filename().string()) < lower (b.filename().string());
    });
    REA168J_REQUIRE (files.size() == fl.size());
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        std::ifstream in (files[i], std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto score = flp::parseProject (bytes, true);
        std::set<int> degrees;
        for (const auto& pattern : score.patterns)
            for (const auto& n : pattern.notes)
                degrees.insert (((int) n.key - 60) % 12 + (n.key < 60 ? 12 : 0));
        CHECK (files[i].stem().string() == fl[i].name);
        CHECK (std::vector<int> (degrees.begin(), degrees.end()) == fl[i].intervals);
    }
}

// §3 PDC Automatic off: the plan leaves out the instruments' and effects' latencies, the manual latencies stay
TEST (rea168j_pdc_automatic_off)
{
    using namespace lynx;
    Song s = latency165::impulses (3, true);
    const std::vector<int> channels { 100, 0, 0 };
    const std::vector<std::vector<int>> slots { {}, { 50 } };
    const auto on = computePdc (s, channels, slots, 48000.0);
    CHECK (on.total == 150 && on.sendDelay[2][0] == 150 && on.sendDelay[1][0] == 0);
    REA168J_REQUIRE (on.slotInput.size() == s.mixer.size() && on.slotInput[1].size() == 1);
    CHECK (on.slotInput[1][0] == 100); // the slot's input: its track's alignment (the channel's latency)

    s.autoPdc = false;
    const auto off = computePdc (s, channels, slots, 48000.0);
    CHECK (off.total == 0 && ! off.any() && off.slotInput[1][0] == 0);

    // a manual latency still plans (FL: +0x1cc is added outside the Automatic branch)
    s.mixer[2].latencyMs = 100.0f * 1000.0f / 48000.0f;
    const auto manual = computePdc (s, channels, slots, 48000.0);
    CHECK (manual.total == 100 && manual.sendDelay[1][0] == 100 && manual.sendDelay[2][0] == 0);
}

// §3 Compensate automations: an automated effect behind a delayed input gets the value at its delayed position
TEST (rea168j_compensate_automations)
{
    using namespace lynx;
    // insert 1: a 4800-sample latent plugin, then a LYNX effect whose input waits for it
    Song s = latency165::impulses (1, true);
    s.bpm = 120.0; // 1 beat = 24000 samples at 48 kHz: 4800 samples = 0.2 beat
    s.mode = PlayMode::song;
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::utility));
    Automation a;
    a.target = { AutomationTargetKind::effectParam, 1, 1, 1 };
    a.points = { { 0, 0.0f }, { ppq, 1.0f } }; // a ramp over beat 1
    s.automations.push_back (a);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.start = 0;
    c.length = 4 * ppq;
    s.playlist.push_back (c);
    auto value = [&] (bool compensate) {
        s.compensateAutomations = compensate;
        auto state = RackState::build (s, nullptr, 48000.0, latency165::factories (4800));
        RackRenderer r;
        r.prepare (48000.0, 256);
        std::vector<float> l (256), rr (256);
        // half a beat in: the automation is at 0.5; the delayed slot sees the song 0.2 beat earlier (0.3)
        r.process (state.get(), true, 0.5 * ppq, 256, l.data(), rr.data());
        return r.compensatedAutomation (*state, 0, 1, 1, 0.5f);
    };
    CHECK (std::abs (value (false) - 0.5f) < 1e-6f);
    CHECK (std::abs (value (true) - 0.3f) < 0.02f);
    s.autoPdc = false; // nothing to compensate without the plan
    CHECK (std::abs (value (true) - 0.5f) < 1e-6f);
}

// §4 FL holds an audio clip's last sample as DC to the clip's end (FL renders r167l take 2, r167k c_base)
TEST (rea168j_audio_clip_holds_its_last_value)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0; // 1 beat = 24000 samples at 48 kHz
    ChannelDef ch;
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = "short";
    s.channels.push_back (ch);
    s.patterns.emplace_back();
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.start = 0;
    c.length = 4 * ppq; // 2 s; the sample lasts 1 s
    s.playlist.push_back (c);
    Clip later; // an empty pattern clip from 4 s: the song (and its loop) goes on past the audio clip
    later.kind = ClipKind::pattern;
    later.pattern = 0;
    later.start = 8 * ppq;
    later.length = 4 * ppq;
    s.playlist.push_back (later);
    s.mode = PlayMode::song;
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (48000, 0.25f);
        d->right.assign (48000, -0.125f);
        return std::shared_ptr<const SampleData> (d);
    };
    auto state = RackState::build (s, nullptr, 48000.0, provider);
    RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    const auto tps = ticksPerSample (120.0, 48000.0);
    float during = 0.0f, after = 0.0f, afterR = 0.0f, past = 1.0f;
    for (int b = 0; b * 256 < 48000 * 3; ++b)
    {
        r.process (state.get(), true, (double) b * 256.0 * tps, 256, l.data(), rr.data());
        const auto t = b * 256;
        if (t >= 24000 && t < 24256)
            during = l[100];
        if (t >= 72000 && t < 72256) // 1.5 s: past the sample, inside the clip
            after = l[100], afterR = rr[100];
        if (t >= 120000 && t < 120256) // 2.5 s: past the clip
            past = l[100];
    }
    CHECK (std::abs (during) > 0.05f);
    CHECK (std::abs (after - during) < 1e-4f * std::abs (during) + 1e-6f); // the DC holds the sample's last value
    CHECK (afterR < -0.01f);
    CHECK (std::abs (past) < 1e-6f); // and stops with the clip
}
