// Round 167q: issue #25 (a stretched pattern clip loops its pattern: LYNX extra, on by default; off = FL: once) and FL
// mixer presets.

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
// 4 bars: onsets at 0.0 / 1.0 s only, silence after). LYNX loops the pattern instead, as a LYNX extra the user asked for
// (Options › General "Stretched pattern clips loop their pattern (LYNX)", on by default; off = FL's behaviour).
namespace mixpre167q
{
struct LoopFlag // sets the LYNX extra for one test and puts the app's setting back
{
    explicit LoopFlag (bool on) : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (on); }
    ~LoopFlag() { lynx::setStretchedClipsLoop (was); }
    bool was;
};

// a 1-bar pattern clip stretched to 4 bars, and a clip that starts 1 beat into the pattern, 8 beats long, at song beat 20
inline lynx::Song stretchedSong()
{
    auto s = oneBarSong();
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 16 * lynx::ppq;
    s.playlist.push_back (c);
    lynx::Clip o = c;
    o.start = 20 * lynx::ppq;
    o.offset = lynx::ppq;
    o.length = 8 * lynx::ppq;
    s.playlist.push_back (o);
    return s;
}
} // namespace mixpre167q

TEST (mixpre167q_stretched_pattern_clip_loops_its_pattern)
{
    const mixpre167q::LoopFlag loop (true);
    auto s = mixpre167q::stretchedSong();
    const auto ppq = (long) lynx::ppq;
    const std::vector<std::pair<long, int>> expected {
        { 0, 60 }, { 2 * ppq, 72 }, { 4 * ppq, 60 }, { 6 * ppq, 72 }, { 8 * ppq, 60 }, { 10 * ppq, 72 }, { 12 * ppq, 60 }, { 14 * ppq, 72 },
        // the offset clip: pattern beat 2 at song beat 21, then the pattern again from song beat 23
        { 21 * ppq, 72 }, { 23 * ppq, 60 }, { 25 * ppq, 72 }, { 27 * ppq, 60 },
    };
    for (const auto block : { 64, 480, 512, 4096 })
        for (const auto withIndex : { false, true })
            CHECK (mixpre167q::songOns (s, 32 * lynx::ppq, block, withIndex) == expected);

    // a clip within its pattern plays it once, as before
    s.playlist.resize (1);
    s.playlist[0].length = 3 * lynx::ppq;
    CHECK ((mixpre167q::songOns (s, 8 * lynx::ppq, 512, true) == std::vector<std::pair<long, int>> { { 0, 60 }, { 2 * ppq, 72 } }));

    // an offset past the pattern's length (a piece split from a long clip): the pass it falls in
    s.playlist[0].offset = 6 * lynx::ppq; // pattern beat 2 of the second pass
    s.playlist[0].length = 4 * lynx::ppq;
    CHECK ((mixpre167q::songOns (s, 4 * lynx::ppq, 512, true) == std::vector<std::pair<long, int>> { { 0, 72 }, { 2 * ppq, 60 } }));

    // MIDI export takes the repeats too
    s.mode = lynx::PlayMode::song;
    s.playlist[0].offset = 0;
    s.playlist[0].length = 8 * lynx::ppq;
    const auto notes = lynx::exportopts::midiExportNotes (s);
    CHECK (notes.size() == 4);
    if (notes.size() == 4)
        CHECK (notes[2].start == 4 * lynx::ppq && notes[2].key == 60 && notes[3].start == 6 * lynx::ppq && notes[3].key == 72);

    // the Piano roll's playhead wraps with the passes
    CHECK (lynx::patternTickInClip (s.playlist[0], 5.0 * lynx::ppq, 4 * lynx::ppq) == 1.0 * lynx::ppq);
    CHECK (lynx::transportedit::patternPlayhead (s, 0, 5.0 * lynx::ppq) == 1.0 * lynx::ppq);
    const auto passes = lynx::clipPasses (s.playlist[0], 4 * lynx::ppq);
    CHECK (passes.loops && passes.first == 0 && passes.last == 1 && passes.length == 4 * lynx::ppq);

    // a pattern whose length changes later: the clip follows the new length
    s.patterns[0].lengthTicks = 8 * lynx::ppq; // 2 bars: the 8-beat clip is within it, it plays once
    CHECK ((mixpre167q::songOns (s, 8 * lynx::ppq, 512, true) == std::vector<std::pair<long, int>> { { 0, 60 }, { 2 * ppq, 72 } }));
    s.patterns[0].lengthTicks = 2 * lynx::ppq; // half a bar: the note on beat 2 is past it, the clip repeats beats 0-1
    CHECK ((mixpre167q::songOns (s, 8 * lynx::ppq, 512, true)
            == std::vector<std::pair<long, int>> { { 0, 60 }, { 2 * ppq, 60 }, { 4 * ppq, 60 }, { 6 * ppq, 60 } }));
}

// The extra off: FL's behaviour, the pattern plays once and the rest of the clip is silent.
TEST (mixpre167q_stretched_pattern_clip_plays_once_with_the_option_off)
{
    const mixpre167q::LoopFlag loop (false);
    auto s = mixpre167q::stretchedSong();
    const auto ppq = (long) lynx::ppq;
    const std::vector<std::pair<long, int>> expected { { 0, 60 }, { 2 * ppq, 72 }, { 21 * ppq, 72 } };
    for (const auto block : { 64, 480, 512, 4096 })
        for (const auto withIndex : { false, true })
            CHECK (mixpre167q::songOns (s, 32 * lynx::ppq, block, withIndex) == expected);
    s.mode = lynx::PlayMode::song;
    s.playlist.resize (1);
    CHECK (lynx::exportopts::midiExportNotes (s).size() == 2);
    CHECK (lynx::transportedit::patternPlayhead (s, 0, 5.0 * lynx::ppq) == 5.0 * lynx::ppq);
    CHECK (! lynx::clipPasses (s.playlist[0], 4 * lynx::ppq).loops);
}

// Many clips of many shapes, scheduled block by block: the same events with and without the index, nothing dropped,
// every event inside its block, no crash (issue #25 was reported as a crash).
TEST (mixpre167q_repeats_stress)
{
    const mixpre167q::LoopFlag keep (lynx::stretchedClipsLoop());
    std::mt19937 rng (25);
    auto pick = [&rng] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    for (int round = 0; round < 80; ++round)
    {
        lynx::setStretchedClipsLoop (round % 4 != 3); // both modes of the LYNX extra
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

// ---- Issue #25, the sound: 0.1.21 drew the repeats but did not play them. The renders below are the rack's audio
// (RackRenderer, the synth of a new channel: deterministic), Song mode, 120 BPM, 48 kHz: 1 bar = 96000 samples.
namespace mixpre167q
{
constexpr int barSamples = 96000;

// a synth channel (the default instrument) on Insert 1 with the 1-bar pattern; `clips` on the Playlist
inline lynx::Song synthSong (std::vector<lynx::Clip> clips)
{
    auto s = oneBarSong();
    s.mixer = lynx::Song::defaultMixer (2);
    s.bpm = 120.0;
    s.channels[0].mixerTrack = 1;
    s.mode = lynx::PlayMode::song;
    s.playlist = std::move (clips);
    return s;
}
inline lynx::Clip clipAt (std::int64_t start, std::int64_t length, std::int64_t offset = 0)
{
    lynx::Clip c;
    c.pattern = 0;
    c.start = start;
    c.length = length;
    c.offset = offset;
    return c;
}

// Renders `samples` of the left output in blocks of `block`, the way AudioEngine's device callback moves the position
// (a block ends where a jump or the loop region's end is due; the position is then set there). `change` (optional) is
// applied to the song at sample `changeAt` (the rack state is rebuilt from the edited song, as an edit while playing).
struct Jump // when the position reaches `at`, it goes to `to` (once, or each time: a loop region); the block is split there
{
    double at = -1.0, to = 0.0;
    bool repeat = false;
};
inline std::vector<float> render (lynx::Song s, double from, int samples, int block, Jump jump = {}, int changeAt = -1,
                                  const std::function<void (lynx::Song&)>& change = {})
{
    using namespace lynx;
    std::shared_ptr<const RackState> state = RackState::build (s, nullptr, 48000.0);
    RackRenderer r;
    r.prepare (48000.0, 4096);
    const auto tps = ticksPerSample (120.0, 48000.0);
    std::vector<float> out;
    out.reserve ((std::size_t) samples);
    auto position = from;
    for (int done = 0; done < samples;)
    {
        if (changeAt >= 0 && done >= changeAt && change)
        {
            change (s);
            state = RackState::build (s, state.get(), 48000.0);
            changeAt = -1;
        }
        auto n = std::min (block, samples - done);
        if (changeAt > done)
            n = std::min (n, changeAt - done);
        if (jump.at >= 0.0 && position < jump.at)
            n = std::clamp ((int) std::ceil ((jump.at - position) / tps - 1e-9), 1, n);
        std::vector<float> l ((std::size_t) n), rr ((std::size_t) n);
        r.process (state.get(), true, position, n, l.data(), rr.data(), nullptr);
        out.insert (out.end(), l.begin(), l.end());
        position += n * tps;
        if (jump.at >= 0.0 && position >= jump.at - 1e-6)
        {
            position = jump.to;
            if (! jump.repeat)
                jump.at = -1.0;
        }
        done += n;
    }
    return out;
}

inline double rms (const std::vector<float>& x, std::size_t from, std::size_t count)
{
    double sum = 0.0;
    for (auto i = from; i < from + count && i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) std::max<std::size_t> (1, count));
}
inline double maxDiff (const std::vector<float>& a, std::size_t aFrom, const std::vector<float>& b, std::size_t bFrom, std::size_t count)
{
    double m = 0.0;
    for (std::size_t i = 0; i < count && aFrom + i < a.size() && bFrom + i < b.size(); ++i)
        m = std::max (m, (double) std::abs (a[aFrom + i] - b[bFrom + i]));
    return m;
}
inline bool printNumbers() { return std::getenv ("LYNX_PRINT_CLIPLOOP") != nullptr; }
inline void dump (const std::vector<float>& x, const char* name)
{
    if (const char* dir = std::getenv ("LYNX_DUMP_CLIPLOOP"))
        if (auto* f = std::fopen ((std::string (dir) + "/" + name + ".f32").c_str(), "wb"))
        {
            std::fwrite (x.data(), sizeof (float), x.size(), f);
            std::fclose (f);
        }
}
} // namespace mixpre167q

// (1) the stretched clip's bars 2-4 sound as bar 1: same RMS, the same samples (the synth is deterministic); the same
// render as four 1-bar clips placed back to back. With the extra off (FL), bars 2-4 are silent.
TEST (mixpre167q_stretched_clip_repeats_sound)
{
    using namespace mixpre167q;
    const auto bar = 4 * (std::int64_t) lynx::ppq;
    const auto stretched = synthSong ({ clipAt (0, 4 * bar) });
    const auto copies = synthSong ({ clipAt (0, bar), clipAt (bar, bar), clipAt (2 * bar, bar), clipAt (3 * bar, bar) });
    for (const auto block : { 512, 480, 4096 })
    {
        const LoopFlag loop (true);
        const auto a = render (stretched, 0.0, 4 * barSamples, block);
        const auto b = render (copies, 0.0, 4 * barSamples, block);
        const auto bar1 = rms (a, 0, barSamples);
        CHECK (bar1 > 0.01);
        for (int k = 1; k < 4; ++k)
        {
            const auto rk = rms (a, (std::size_t) k * barSamples, barSamples);
            const auto diff = maxDiff (a, (std::size_t) k * barSamples, a, 0, barSamples);
            if (printNumbers())
                std::printf ("cliploop block %d: bar 1 RMS %.6f, bar %d RMS %.6f (ratio %.6f), max |bar %d - bar 1| = %.3g\n", block, bar1,
                             k + 1, rk, rk / bar1, k + 1, diff);
            CHECK (std::abs (rk / bar1 - 1.0) < 1.0e-3);
            CHECK (diff < 1.0e-4); // (-66 dB: the events' sub-sample place against the blocks; 0 with blocks of 480)
        }
        const auto vsCopies = maxDiff (a, 0, b, 0, a.size());
        if (printNumbers())
            std::printf ("cliploop block %d: stretched vs 4 copies max diff %.3g\n", block, vsCopies);
        CHECK (vsCopies < 1.0e-6);

        const LoopFlag once (false);
        const auto c = render (stretched, 0.0, 4 * barSamples, block);
        CHECK (maxDiff (c, 0, a, 0, (std::size_t) barSamples) < 1.0e-6); // bar 1 the same
        for (int k = 1; k < 4; ++k)
        {
            const auto rk = rms (c, (std::size_t) k * barSamples, barSamples);
            if (printNumbers())
                std::printf ("cliploop off block %d: bar %d RMS %.3g\n", block, k + 1, rk);
            CHECK (rk < 1.0e-6);
        }
    }
}

// Clips that start mid-pattern (an offset), an offset past the pattern (a piece split from a long clip) and patterns
// whose length changes (before playing and while the song plays): the stretched clip sounds as the same pattern placed
// pass by pass with plain clips.
TEST (mixpre167q_stretched_clip_offsets_and_length_changes_sound)
{
    using namespace mixpre167q;
    const LoopFlag loop (true);
    const auto bar = 4 * (std::int64_t) lynx::ppq, beat = (std::int64_t) lynx::ppq;
    auto same = [] (const lynx::Song& a, const lynx::Song& b, int samples, const char* what, int changeAt = -1,
                    const std::function<void (lynx::Song&)>& changeA = {}, const std::function<void (lynx::Song&)>& changeB = {}) {
        const auto x = render (a, 0.0, samples, 512, {}, changeAt, changeA);
        const auto y = render (b, 0.0, samples, 512, {}, changeAt, changeB);
        const auto d = maxDiff (x, 0, y, 0, x.size());
        if (printNumbers())
            std::printf ("cliploop %s: RMS %.6f, vs plain clips max diff %.3g\n", what, rms (x, 0, x.size()), d);
        CHECK (rms (x, 0, x.size()) > 0.01);
        CHECK (d < 1.0e-6);
    };
    // 1 beat into the pattern, 8 beats long: pattern beats 1-3, then a whole pass, then beat 0
    same (synthSong ({ clipAt (0, 8 * beat, beat) }), synthSong ({ clipAt (0, 3 * beat, beat), clipAt (3 * beat, bar), clipAt (7 * beat, beat) }),
          2 * barSamples, "offset 1 beat");
    // an offset of 6 beats (beat 2 of the second pass), 7 beats long
    same (synthSong ({ clipAt (bar, 7 * beat, 6 * beat) }), synthSong ({ clipAt (bar, 2 * beat, 2 * beat), clipAt (bar + 2 * beat, bar), clipAt (bar + 6 * beat, beat) }),
          3 * barSamples, "offset past the pattern");
    // the pattern is 2 bars long (an explicit length): a 4-bar clip plays it twice
    {
        auto a = synthSong ({ clipAt (0, 4 * bar) }), b = synthSong ({ clipAt (0, 2 * bar), clipAt (2 * bar, 2 * bar) });
        a.patterns[0].lengthTicks = b.patterns[0].lengthTicks = 2 * bar;
        same (a, b, 4 * barSamples, "pattern of 2 bars");
    }
    // half a bar: the note on beat 2 is past the pattern's end; the clip repeats beats 0-1
    {
        auto a = synthSong ({ clipAt (0, bar) }), b = synthSong ({ clipAt (0, 2 * beat), clipAt (2 * beat, 2 * beat) });
        a.patterns[0].lengthTicks = b.patterns[0].lengthTicks = 2 * beat;
        same (a, b, barSamples, "pattern of half a bar");
    }
    // the pattern's length changes while the song plays (after bar 2): 1 bar -> 2 bars, the passes follow the new length
    same (synthSong ({ clipAt (0, 4 * bar) }), synthSong ({ clipAt (0, bar), clipAt (bar, bar), clipAt (2 * bar, bar), clipAt (3 * bar, bar) }),
          4 * barSamples, "length change while playing", 2 * barSamples,
          [bar] (lynx::Song& s) { s.patterns[0].lengthTicks = 2 * bar; },
          [bar] (lynx::Song& s) {
              s.patterns[0].lengthTicks = 2 * bar;
              s.playlist = { clipAt (0, 2 * bar), clipAt (2 * bar, 2 * bar) };
          });
}

// (2) the transport over the repeats: starting inside a repeat, a jump of the position mid-clip, and a loop region
// inside the stretched clip. Every played beat sounds as the same beat of bar 1.
TEST (mixpre167q_stretched_clip_repeats_sound_through_jumps_and_loops)
{
    using namespace mixpre167q;
    const LoopFlag loop (true);
    const auto ppq = (double) lynx::ppq;
    const auto beatSamples = barSamples / 4;
    const auto song = synthSong ({ clipAt (0, 16 * lynx::ppq) });
    const auto bar = 16 * lynx::ppq / 4;
    const auto copies = synthSong ({ clipAt (0, bar), clipAt (bar, bar), clipAt (2 * bar, bar), clipAt (3 * bar, bar) });
    const auto reference = render (song, 0.0, barSamples, 512); // bar 1
    dump (reference, "reference");
    auto report = [] (const char* what, double r, double d, double vsCopies) {
        if (printNumbers())
            std::printf ("cliploop %s: RMS %.6f, max diff vs bar 1 %.3g, vs the same over 4 plain clips %.3g\n", what, r, d, vsCopies);
    };

    // playback starts at bar 3, beat 2 (inside the third pass): beats 2-3, then bar 4 as bar 1
    {
        const auto x = render (song, 10 * ppq, barSamples / 2 + barSamples, 512);
        const auto y = render (copies, 10 * ppq, barSamples / 2 + barSamples, 512);
        const auto d = std::max (maxDiff (x, 0, reference, 2 * (std::size_t) beatSamples, (std::size_t) barSamples / 2),
                                 maxDiff (x, (std::size_t) barSamples / 2, reference, 0, (std::size_t) barSamples));
        const auto c = maxDiff (x, 0, y, 0, x.size());
        report ("start inside a repeat", rms (x, 0, x.size()), d, c);
        CHECK (rms (x, 0, (std::size_t) barSamples / 2) > 0.01);
        CHECK (d < 1.0e-4);
        CHECK (c < 1.0e-6);
    }
    // a jump mid-clip: after 1 bar + 3 beats, the position jumps to bar 4, beat 2 (the user clicks the ruler)
    {
        const auto x = render (song, 0.0, 2 * barSamples, 512, { 7 * ppq, 14 * ppq, false });
        const auto y = render (copies, 0.0, 2 * barSamples, 512, { 7 * ppq, 14 * ppq, false });
        dump (x, "jump");
        // what played after the jump: bar 4 beat 3 (= bar 1 beat 3). After any jump (plain clips too) a note on the jump's
        // target tick starts 1 sample late (21 µs): compared 1 sample later
        const auto at = (std::size_t) (7 * beatSamples);
        const auto d = maxDiff (x, at + 1, reference, 2 * (std::size_t) beatSamples, (std::size_t) beatSamples - 1);
        const auto c = maxDiff (x, 0, y, 0, x.size());
        const auto ratio = rms (x, at, (std::size_t) beatSamples) / rms (reference, 2 * (std::size_t) beatSamples, (std::size_t) beatSamples);
        report ("jump mid-clip", rms (x, at, (std::size_t) beatSamples), d, c);
        if (printNumbers())
            std::printf ("cliploop jump mid-clip: RMS ratio vs bar 1 beat 3 %.6f\n", ratio);
        CHECK (std::abs (ratio - 1.0) < 1.0e-3);
        CHECK (d < 1.0e-4);
        CHECK (c < 1.0e-6);
    }
    // a loop region over bars 2-3 (both repeats): played three times, every bar sounds as bar 1
    {
        const auto x = render (song, 0.0, 7 * barSamples, 512, { 12 * ppq, 4 * ppq, true });
        const auto y = render (copies, 0.0, 7 * barSamples, 512, { 12 * ppq, 4 * ppq, true });
        dump (x, "loop");
        double d = 0.0; // (bars 4-7 follow a wrap: 1 sample late, as after any jump)
        for (int k = 1; k < 7; ++k)
            d = std::max (d, maxDiff (x, (std::size_t) k * barSamples + (k >= 3 ? 1 : 0), reference, 0, (std::size_t) barSamples - 1));
        const auto c = maxDiff (x, 0, y, 0, x.size());
        report ("loop region bars 2-3", rms (x, (std::size_t) barSamples, 6 * (std::size_t) barSamples), d, c);
        for (int k = 1; k < 7; ++k)
        {
            const auto ratio = rms (x, (std::size_t) k * barSamples, (std::size_t) barSamples) / rms (reference, 0, (std::size_t) barSamples);
            if (printNumbers())
                std::printf ("cliploop loop region: played bar %d RMS ratio vs bar 1 %.6f\n", k + 1, ratio);
            CHECK (std::abs (ratio - 1.0) < 1.0e-3);
        }
        CHECK (d < 1.0e-4);
        CHECK (c < 1.0e-6);
    }
}
