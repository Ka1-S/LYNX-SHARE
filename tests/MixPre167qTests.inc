// Round 167q: issue #25 (a stretched pattern clip plays once, as in FL; no crash) and FL mixer presets.

namespace mixpre167q
{
// The "on" events of the song in [0, ticks), scheduled in blocks of `block` samples (with or without the index):
// (song tick, key), sorted.
inline std::vector<std::pair<long, int>> songOns (const lynx::Song& s, std::int64_t ticks, int block, bool withIndex)
{
    const auto tps = lynx::ticksPerSample (120.0, 48000.0); // 1 beat = 24000 samples, 250 samples per tick
    const auto index = lynx::makeScheduleIndex (s);
    std::vector<std::pair<long, int>> out;
    const auto total = (long) std::llround ((double) ticks / tps);
    for (long at = 0; at < total; at += block)
    {
        lynx::EventBuffer ev;
        const auto n = (int) std::min<long> (block, total - at);
        lynx::scheduleSong (s, (double) at * tps, n, tps, ev, false, withIndex ? &index : nullptr, false);
        CHECK (ev.droppedCount() == 0);
        for (const auto& e : ev)
            if (e.type == lynx::NoteEvent::Type::on)
                out.emplace_back (std::lround ((double) (at + e.sampleOffset) * tps), e.key);
    }
    std::sort (out.begin(), out.end());
    return out;
}

inline lynx::Song oneBarSong()
{
    lynx::Song s;
    s.channels.resize (1);
    auto& p = s.patterns.emplace_back(); // 1 bar: C5 on beat 0, C6 on beat 2
    for (auto [beat, key] : { std::pair { 0, 60 }, std::pair { 2, 72 } })
    {
        lynx::Note n;
        n.start = beat * lynx::ppq;
        n.length = lynx::ppq / 4;
        n.key = key;
        p.notes.push_back (n);
    }
    return s;
}
} // namespace mixpre167q

// Issue #25 (Disar): "create a clip, extend it beyond its length, play" — FL 2026 plays a pattern clip stretched past
// its pattern ONCE: the extended part is silent (round 168k: FL command-line render of a 1-bar pattern clip stretched to
// 4 bars: onsets at 0.0 / 1.0 s only, silence after). LYNX does the same (round 167q briefly made it repeat; reverted).
TEST (mixpre167q_stretched_pattern_clip_plays_once)
{
    auto s = mixpre167q::oneBarSong();
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 16 * lynx::ppq; // 4 bars of a 1-bar pattern
    s.playlist.push_back (c);
    lynx::Clip o = c; // a clip that starts 1 beat into the pattern, 8 beats long, at song beat 20
    o.start = 20 * lynx::ppq;
    o.offset = lynx::ppq;
    o.length = 8 * lynx::ppq;
    s.playlist.push_back (o);

    const auto ppq = (long) lynx::ppq;
    const std::vector<std::pair<long, int>> expected { { 0, 60 }, { 2 * ppq, 72 }, { 21 * ppq, 72 } };
    for (const auto block : { 64, 480, 512, 4096 })
        for (const auto withIndex : { false, true })
            CHECK (mixpre167q::songOns (s, 32 * lynx::ppq, block, withIndex) == expected);
}

// Many clips of many shapes, scheduled block by block: the same events with and without the index, nothing dropped,
// every event inside its block, no crash (issue #25 was reported as a crash).
TEST (mixpre167q_repeats_stress)
{
    std::mt19937 rng (25);
    auto pick = [&rng] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    for (int round = 0; round < 40; ++round)
    {
        lynx::Song s;
        s.channels.resize (3);
        for (int p = 0; p < 3; ++p)
        {
            auto& pat = s.patterns.emplace_back();
            if (p == 2 && round % 3 == 0)
                pat.lengthTicks = lynx::ppq * pick (1, 3); // an explicit length shorter than its notes
            for (int k = pick (0, 12); k > 0; --k)
            {
                lynx::Note n;
                n.start = pick (0, 8 * lynx::ppq);
                n.length = pick (1, 3 * lynx::ppq);
                n.key = pick (36, 84);
                n.channel = pick (0, 2);
                n.slide = pick (0, 9) == 0;
                pat.notes.push_back (n);
            }
            pat.sortNotes();
        }
        s.playTruncatedNotes = round % 2 == 0;
        for (int k = pick (1, 8); k > 0; --k)
        {
            lynx::Clip c;
            c.pattern = pick (0, 2);
            c.track = pick (0, 3);
            c.start = pick (0, 32) * lynx::ppq / 4;
            c.length = pick (1, 64 * lynx::ppq);
            c.offset = pick (0, 3) == 0 ? pick (0, 20 * lynx::ppq) : 0;
            s.playlist.push_back (c);
        }
        const auto length = s.songLength() + 2 * lynx::ppq;
        const auto block = pick (16, 2048);
        const auto a = mixpre167q::songOns (s, length, block, false), b = mixpre167q::songOns (s, length, block, true);
        CHECK (a == b);
        for (const auto& [tick, key] : a)
            CHECK (tick >= 0 && tick <= length && key >= 36 && key <= 84);
    }
}

namespace mixpre167q
{
inline std::vector<std::uint8_t> le32s (std::initializer_list<std::uint32_t> words)
{
    std::vector<std::uint8_t> out;
    for (auto w : words)
        for (int i = 0; i < 4; ++i)
            out.push_back ((std::uint8_t) (w >> (8 * i)));
    return out;
}

// {u32 0, u32 id, i32 value} records of an 0xE1 event
inline std::vector<std::uint8_t> controls (std::initializer_list<std::pair<std::uint32_t, std::int32_t>> list)
{
    std::vector<std::uint8_t> out;
    for (auto [id, value] : list)
    {
        const auto r = le32s ({ 0u, id, (std::uint32_t) value });
        out.insert (out.end(), r.begin(), r.end());
    }
    return out;
}

inline std::vector<std::uint8_t> pluginRecord (std::uint32_t insert, std::uint32_t slot)
{
    auto r = le32s ({ insert, slot, 2u });
    r.resize (52, 0);
    return r;
}
} // namespace mixpre167q

// FL's mixer track presets (REA round 167q, Data\Patches\Mixer presets\*.fst, docs/analise/REA_rodada167q.md §A): one
// insert's events. The newer layout (Big meter 8.0.2 / Default 21.1): name and colour, the 0xEC, the slots, then the
// controls of the track it was saved from (track 2 here, 0x2... ids before 24.2.99).
TEST (mixpre167q_fl_mixer_preset_new_layout)
{
    FlpBuilder b;
    b.ascii (0xC7, "21.1.0.3682");
    b.u8 (0x95); b.u32 (0x00332211);                       // colour 0x00BBGGRR = RGB 112233
    b.utf16 (0xCC, u"Drum bus");
    b.data (0xEC, mixpre167q::le32s ({ 0u, 0x4Cu | 0x01u, 0u })); // enabled + invert phase
    b.utf16 (0xC9, u"Fruity Limiter");
    b.data (0xD4, mixpre167q::pluginRecord (2, 0));
    b.data (0xD5, {});
    b.u8 (0x62); b.u16 (0);
    for (std::uint16_t k = 1; k < 10; ++k) { b.u8 (0x62); b.u16 (k); }
    const auto track = [] (std::uint32_t t, std::uint32_t slot, std::uint32_t param) { return 0x20000000u + (t << 22) + (slot << 16) + param; };
    b.data (0xE1, mixpre167q::controls ({ { track (2, 0, 0x1FC0), 8000 },  // level: half of FL's 16000
                                          { track (2, 0, 0x1FC1), -3200 }, // pan half left
                                          { track (2, 0, 0x1F01), 6400 },  // slot 1's mix 50 %
                                          { track (5, 0, 0x1FC0), 16000 } })); // another track's: not part of it

    CHECK (flp::looksLikeMixerPreset (b.bytes));
    const auto preset = lynx::mixerTrackFromFlPreset (b.bytes);
    CHECK (preset.has_value());
    if (! preset)
        return;
    CHECK (preset->name == "Drum bus");
    CHECK (preset->colour == 0xFF112233u);
    CHECK (preset->invertPhase && ! preset->swapChannels);
    CHECK (std::abs (preset->volume - 0.5f) < 1e-4f);
    CHECK (std::abs (preset->pan + 0.5f) < 1e-4f);
    CHECK (preset->effects.size() == 1);
    if (! preset->effects.empty())
    {
        CHECK (preset->effects[0].kind == lynx::EffectKind::limiter);
        CHECK (std::abs (preset->effects[0].mix - 0.5f) < 0.01f);
    }

    // a project, a channel preset and garbage are not mixer presets
    FlpBuilder project;
    project.ascii (0xC7, "21.1.0.3682");
    project.u8 (0x40); project.u16 (0); // new channel
    project.data (0xEC, mixpre167q::le32s ({ 0u, 0x4Cu, 0u }));
    CHECK (! flp::looksLikeMixerPreset (project.bytes));
    CHECK (! lynx::mixerTrackFromFlPreset (project.bytes).has_value());
    const std::vector<std::uint8_t> junk { 'F', 'L', 'h', 'd', 1, 2 };
    CHECK (! flp::looksLikeMixerPreset (junk));
    CHECK (! lynx::mixerTrackFromFlPreset (junk).has_value());
}

// The old layout (Reverb/Chorus/Flanger send 3.6.5, the mastering presets 5.3.3): no 0xEC, the slots first (the slot
// index from the 0xD4), then colour and name; the level/pan/EQ block at 0x1F60 and levels in 0..128 (×100 later)
TEST (mixpre167q_fl_mixer_preset_old_layout)
{
    FlpBuilder b;
    b.ascii (0xC7, "3.6.5");
    b.ascii (0xC9, "Fruity Reeverb");
    b.data (0xD4, mixpre167q::pluginRecord (65, 1));
    b.data (0xD5, std::vector<std::uint8_t> (40, 0));
    b.u8 (0x95); b.u32 (0x00CAC8BA);
    b.ascii (0xCC, "Reverb");
    const auto track = [] (std::uint32_t t, std::uint32_t slot, std::uint32_t param) { return 0x20000000u + (t << 22) + (slot << 16) + param; };
    b.data (0xE1, mixpre167q::controls ({ { track (65, 0, 0x1F00), 1 }, { track (65, 1, 0x1F01), 64 }, { track (65, 0, 0x1F60), 64 },
                                          { track (65, 0, 0x1F61), 0 }, { track (65, 0, 0x1F78), 5777 } }));
    CHECK ((track (65, 0, 0) >> 28) == 3); // (track 65 overflows into the 0x3... class, as in FL's file)

    const auto project = flp::parseMixerPreset (b.bytes);
    CHECK (project.mixer.size() == 2 && project.mixer[1].slots.size() == 1);
    if (project.mixer.size() == 2 && project.mixer[1].slots.size() == 1)
    {
        CHECK (project.mixer[1].slots[0].index == 1 && project.mixer[1].slots[0].plugin == "Fruity Reeverb");
        CHECK (project.mixer[1].name == "Reverb" && project.mixer[1].colour == 0xBAC8CA);
    }
    for (const auto& c : project.controls)
        CHECK (c.id.isMixer() && c.id.track() == 1);

    const auto preset = lynx::mixerTrackFromFlPreset (b.bytes);
    CHECK (preset.has_value());
    if (! preset)
        return;
    CHECK (preset->name == "Reverb");
    CHECK (std::abs (preset->volume - 0.4f) < 1e-4f); // 64 → 6400 of 16000
    CHECK (preset->effects.size() == 1 && ! preset->effects.empty() && preset->effects[0].kind == lynx::EffectKind::reverb);
    if (! preset->effects.empty())
        CHECK (std::abs (preset->effects[0].mix - 0.5f) < 0.01f);
}

// FL's own factory presets, when FL is installed here: every one reads, its plugins become effects
TEST (mixpre167q_fl_factory_mixer_presets)
{
    const std::filesystem::path folder = "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data/Patches/Mixer presets";
    std::error_code ec;
    if (! std::filesystem::is_directory (folder, ec))
        return;
    int read = 0;
    for (const auto& entry : std::filesystem::directory_iterator (folder, ec))
    {
        if (entry.path().extension() != ".fst")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (in), {} };
        CHECK (flp::looksLikeMixerPreset (bytes));
        const auto preset = lynx::mixerTrackFromFlPreset (bytes);
        CHECK (preset.has_value());
        if (! preset)
            continue;
        ++read;
        CHECK (preset->volume > 0.7f && preset->volume <= 0.81f); // all at 0 dB (12800 of 16000)
        const auto name = entry.path().stem().string();
        if (name == "Default")
            CHECK (preset->effects.empty() && preset->name.empty());
        if (name == "Reverb send")
            CHECK (preset->name == "Reverb" && preset->effects.size() == 1);
        if (name == "General purpose mastering")
            CHECK (preset->effects.size() == 3);
    }
    CHECK (read >= 15);
}
