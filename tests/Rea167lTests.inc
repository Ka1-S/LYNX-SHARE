// Round 167l: the Sampler's legacy precomputed effects, the export's extra data, Alt solo, the channel's "Lock
// active/mute state", CC 123 on a transport reset (docs/analise/REA_rodada167l.md)

#define REA167L_REQUIRE(x) \
    do { if (! (x)) { CHECK (x); return; } } while (false)

namespace rea167l
{
inline std::shared_ptr<lynx::SampleData> impulse (int frames, bool stereo, float level = 0.5f)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    d->left.assign ((std::size_t) frames, 0.0f);
    d->left[0] = level;
    if (stereo)
        d->right = d->left;
    return d;
}
} // namespace rea167l

// §1 the events: 0x4A boost, 0x45 EQ, 0x83 ring mod mix | freq << 16, 0x55 stereo delay, 0x47 / 0x53 low-pass, 0x8B
// reverb | type << 16, 0x56 pogo, 0x46 bit 2 clip — the channel form's handlers write them [DECOMP FUN_00f66710 …]
TEST (rea167l_flp_legacy_precompute_events)
{
    const auto p = rea167h::channelProject ([] (FlpBuilder& b) {
        b.u8 (0x4A); b.u16 (64);
        b.u8 (0x45); b.u16 (200);
        b.u8 (0x83); b.u32 (128u | (64u << 16));
        b.u8 (0x55); b.u16 (2148);
        b.u8 (0x47); b.u16 (600);
        b.u8 (0x53); b.u16 (300);
        b.u8 (0x8B); b.u32 (100u);
        b.u8 (0x56); b.u16 (384);
        b.u8 (0x46); b.u16 (4);
        b.u8 (0x20); b.u8 (1);
    });
    REA167L_REQUIRE (p.channels.size() == 1);
    const auto& c = p.channels[0];
    CHECK (c.boost == 64 && c.eq == 200 && c.ringMix == 128 && c.ringFreq == 64 && c.stereoDelay == 2148);
    CHECK (c.cutoff == 600 && c.resonance == 300 && c.reverb == 100 && c.reverbType == 0 && c.pogo == 384 && c.soloLock);
    const auto song = lynx::importFlp (p);
    REA167L_REQUIRE (! song.channels.empty());
    const auto& q = song.channels[0].sampler.precompute;
    CHECK (q.boost == 64 && q.eq == 200 && q.ringMix == 128 && q.ringFreq == 64 && q.stereoDelay == 2148 && q.cutoff == 600);
    CHECK (q.resonance == 300 && q.reverb == 100 && q.reverbType == 0 && q.pogo == 384 && q.clip && q.legacy());
    CHECK (song.channels[0].soloLock);

    // FL's defaults (no events): nothing to precompute
    const auto none = lynx::importFlp (rea167h::channelProject ([] (FlpBuilder&) {}));
    REA167L_REQUIRE (! none.channels.empty());
    CHECK (! none.channels[0].sampler.precompute.legacy() && ! none.channels[0].soloLock);
    CHECK (! lynx::SamplePrecompute {}.any());
}

// §1 FL's formulas, effect by effect
TEST (rea167l_legacy_precompute_formulas)
{
    namespace pc = lynx::precompute;
    lynx::SamplerSettings s;
    // stereo delay 2148: the left channel starts 100 frames later (FL: v > 2048 delays the left), the sample is 100
    // frames longer; 1748 delays the right one by 300
    {
        s.precompute = {};
        s.precompute.stereoDelay = 2148;
        auto out = pc::apply (*rea167l::impulse (500, false), s);
        REA167L_REQUIRE (out != nullptr && out->isStereo() && out->frames() == 600);
        CHECK (out->right[0] == 0.5f && out->left[0] == 0.0f && out->left[100] == 0.5f);
        s.precompute.stereoDelay = 1748;
        out = pc::apply (*rea167l::impulse (500, true), s);
        REA167L_REQUIRE (out != nullptr && out->frames() == 800);
        CHECK (out->left[0] == 0.5f && out->right[300] == 0.5f && out->right[0] == 0.0f);
    }
    // boost 64 = × 5, clip at ±1
    {
        s.precompute = {};
        s.precompute.boost = 64;
        auto in = rea167l::impulse (100, false, 0.3f);
        auto out = pc::apply (*in, s);
        REA167L_REQUIRE (out != nullptr);
        CHECK (std::abs (out->left[0] - 1.5f) < 1e-6f);
        s.precompute.clip = true;
        out = pc::apply (*in, s);
        REA167L_REQUIRE (out != nullptr);
        CHECK (out->left[0] == 1.0f);
    }
    // the EQ: y = x + y₋₁·k, k = (v·192/256 − 96)/128 → v = 200: k = 54/128
    {
        s.precompute = {};
        s.precompute.eq = 200;
        auto out = pc::apply (*rea167l::impulse (100, false, 1.0f), s);
        REA167L_REQUIRE (out != nullptr);
        const auto k = 54.0f / 128.0f;
        CHECK (out->left[0] == 1.0f && std::abs (out->left[1] - k) < 1e-6f && std::abs (out->left[3] - k * k * k) < 1e-6f);
    }
    // ring modulation, full mix: x · sin(2π·phase/16384), the phase advancing (freq + 8)·16384/5760 a frame at 44.1 kHz;
    // the second channel a quarter turn later
    {
        s.precompute = {};
        s.precompute.ringMix = 256;
        s.precompute.ringFreq = 200;
        auto in = std::make_shared<lynx::SampleData>();
        in->sampleRate = 44100.0;
        in->left.assign (400, 1.0f);
        in->right.assign (400, 1.0f);
        auto out = pc::apply (*in, s);
        REA167L_REQUIRE (out != nullptr && out->frames() == 400);
        const auto step = 208.0 * 16384.0 / 5760.0;
        for (int i : { 0, 1, 77, 399 })
        {
            const auto ph = std::fmod (step * i, 16384.0);
            CHECK (std::abs (out->left[(std::size_t) i] - (float) std::sin (2.0 * 3.141592653589793 * std::floor (ph) / 16384.0)) < 1e-5f);
            const auto ph2 = std::fmod (4096.0 + step * i, 16384.0);
            CHECK (std::abs (out->right[(std::size_t) i] - (float) std::sin (2.0 * 3.141592653589793 * std::floor (ph2) / 16384.0)) < 1e-5f);
        }
    }
    // the low-pass: a DC step settles at its level (unity gain at DC), slower with a lower cutoff
    {
        auto step = std::make_shared<lynx::SampleData>();
        step->sampleRate = 44100.0;
        step->left.assign (20000, 0.5f);
        s.precompute = {};
        s.precompute.cutoff = 600;
        auto slow = pc::apply (*step, s);
        s.precompute.cutoff = 900;
        auto fast = pc::apply (*step, s);
        REA167L_REQUIRE (slow != nullptr && fast != nullptr);
        CHECK (slow->left[20] < fast->left[20] && fast->left[20] < 0.5f);
        CHECK (std::abs (slow->left[19999] - 0.5f) < 1e-3f);
    }
    // pogo 512: the read position follows sin(i/(N−1)·π/2)·(N−1) — the sample's start is played fast
    {
        auto ramp = std::make_shared<lynx::SampleData>();
        ramp->sampleRate = 44100.0;
        for (int i = 0; i < 1001; ++i)
            ramp->left.push_back ((float) i / 1000.0f);
        s.precompute = {};
        s.precompute.pogo = 512;
        auto out = pc::apply (*ramp, s);
        REA167L_REQUIRE (out != nullptr && out->frames() == 1001);
        CHECK (std::abs (out->left[500] - (float) std::sin (0.5 * 1.5707963267948966)) < 1e-4f);
        CHECK (std::abs (out->left[1000] - 1.0f) < 1e-4f && out->left[0] == 0.0f);
    }
    // the reverbs: a tail after the sample, its silent end cut below 1/512
    for (int type : { 0, 1 })
    {
        s.precompute = {};
        s.precompute.reverb = 128;
        s.precompute.reverbType = type;
        auto out = pc::apply (*rea167l::impulse (2000, false, 1.0f), s);
        REA167L_REQUIRE (out != nullptr && out->isStereo());
        CHECK (out->frames() > 2000 && out->frames() <= 2000 + 22050);
        const auto last = out->frames() - 1;
        CHECK (std::max (std::abs (out->left[last]), std::abs (out->right[last])) >= 0.001953125f);
    }
}

// §5 0x20 "Lock active/mute state" [DECOMP SoloLockMenuClick 0x1213320 → FUN_00ffac40 toggles channel +0x892;
// FUN_00ffaaa0 / FUN_00ffa980: the solo skips locked channels] and 0x32 the ghost channels (+0x1fc)
TEST (rea167l_channel_solo_lock_and_ghosts)
{
    auto song = lynx::Song::makeDefault();
    REA167L_REQUIRE (song.channels.size() >= 4);
    for (auto& c : song.channels)
        c.muted = false, c.soloLock = false;
    song.channels[2].soloLock = true;
    lynx::noteedit::soloChannel (song, 0);
    CHECK (! song.channels[0].muted && song.channels[1].muted && ! song.channels[2].muted && song.channels[3].muted);
    // again: the unlocked channels play again; the locked one was never touched
    song.channels[2].muted = true;
    lynx::noteedit::soloChannel (song, 0);
    CHECK (! song.channels[0].muted && ! song.channels[1].muted && song.channels[2].muted && ! song.channels[3].muted);
    // a locked channel muted by hand does not stop the solo from being "alone"
    lynx::noteedit::soloChannel (song, 1);
    CHECK (song.channels[0].muted && ! song.channels[1].muted && song.channels[2].muted);
    lynx::noteedit::soloChannel (song, 1);
    CHECK (! song.channels[0].muted && ! song.channels[3].muted);

    // .lynx round trip, .flp 0x32
    const auto ghost = lynx::importFlp (rea167h::channelProject ([] (FlpBuilder& b) { b.u8 (0x32); b.u8 (0); }));
    REA167L_REQUIRE (! ghost.channels.empty());
    CHECK (ghost.channels[0].ghostHidden);
    const auto shown = lynx::importFlp (rea167h::channelProject ([] (FlpBuilder& b) { b.u8 (0x32); b.u8 (1); }));
    REA167L_REQUIRE (! shown.channels.empty());
    CHECK (! shown.channels[0].ghostHidden);
}

// §4 "Solo selected track(s)" (S) and "Alt solo selected track(s)" (Alt+S) [DECOMP FUN_0127c2a0 / FUN_0127bb10]
TEST (rea167l_mixer_alt_solo)
{
    auto song = lynx::Song::makeDefault();
    REA167L_REQUIRE (song.mixer.size() > 4);
    // 1 → 2 → Master; 3 → Master
    song.mixer[1].toMaster = false;
    song.mixer[1].sends = { lynx::MixerSend { 2, 1.0f } };
    lynx::mixerops::soloSelected (song, { 2 }, false);
    CHECK (song.mixer[2].solo && ! song.mixer[2].soloSources && ! song.mixer[1].solo);
    auto heard = song.soloAudible();
    CHECK (heard[2] && ! heard[1] && ! heard[3]);
    // Alt+S on a plain solo: the feeding track is heard too
    lynx::mixerops::soloSelected (song, { 2 }, true);
    heard = song.soloAudible();
    CHECK (song.mixer[2].solo && song.mixer[2].soloSources && heard[1] && heard[2] && ! heard[3]);
    // S on an alt solo: back to the plain one; S again: no solo
    lynx::mixerops::soloSelected (song, { 2 }, false);
    CHECK (song.mixer[2].solo && ! song.mixer[2].soloSources);
    lynx::mixerops::soloSelected (song, { 2 }, false);
    CHECK (! song.mixer[2].solo);
    // Alt+S twice: alt solo, then none
    lynx::mixerops::soloSelected (song, { 2 }, true);
    CHECK (song.mixer[2].solo && song.mixer[2].soloSources);
    lynx::mixerops::soloSelected (song, { 2 }, true);
    CHECK (! song.mixer[2].solo);
    // a track nothing feeds: S again clears it (both kinds are the same there)
    lynx::mixerops::soloSelected (song, { 3 }, true);
    lynx::mixerops::soloSelected (song, { 3 }, false);
    CHECK (! song.mixer[3].solo);
    // soloing others clears the old solo (the kind is saved in .lynx: ProjectFile "soloSources")
    lynx::mixerops::soloSelected (song, { 2 }, false);
    lynx::mixerops::soloSelected (song, { 3, 2 }, false);
    CHECK (song.mixer[3].solo && song.mixer[2].solo && ! song.mixer[2].soloSources);
    // the shortcut: Alt+S in the mixer
    const auto* alt = lynx::keys::findAction ("mixer.altSoloSelected");
    REA167L_REQUIRE (alt != nullptr && ! alt->defaults.empty());
    CHECK (alt->context == lynx::keys::Context::mixer && lynx::keys::toString (alt->defaults[0]) == "Alt+S");
}

// §6 CC 123 to the "Send all notes off" outputs on a transport reset [DECOMP FUN_011c89c0]
TEST (rea167l_transport_notes_off_bytes)
{
    std::array<std::uint8_t, 48> b {};
    lynx::midisync::transportNotesOff (b);
    for (int ch = 0; ch < 16; ++ch)
        CHECK (b[(std::size_t) ch * 3] == 0xb0 + ch && b[(std::size_t) ch * 3 + 1] == 123 && b[(std::size_t) ch * 3 + 2] == 0);
}

// §3 the export's extra data: FL 2026's render of r167l/fl/l_all.flp (take 1: 120 BPM, 44.1 kHz, three plain markers
// at beats 8 / 16 / 30, the second unnamed, AcidBox on) — every byte after "data" up to FL's LIST INFO
TEST (rea167l_export_wav_extras_match_fl)
{
    static const char* fl =
        "736d706c3c0000000000000000000000945800003c000000000000000000000000000000010000000000000000000200000400000000000089e43b000000"
        "0000000000006163696418000000040000003c00800000000000b2000000040004000000f042637565204c000000030000000000010010b10200646174"
        "61000000000000000010b1020001000100206205006461746100000000000000002062050002000100fc170a00646174610000000000000000fc170a00"
        "4c495354400000006164746c6c61626c0a000000000001005665727365006c61626c0d000000010001004d61726b6572203100006c61626c0b00000002"
        "00010044726f7020410000746c73744c0000000300000063756520ffff000001000000ff3c90000000000000000000637565200000010001000000ff3d"
        "90000000000000000000637565200100010001000000ff3e90000000000000000000";
    std::vector<std::uint8_t> want;
    for (const char* p = fl; p[0] != 0 && p[1] != 0; p += 2)
        want.push_back ((std::uint8_t) std::stoi (std::string (p, 2), nullptr, 16));
    lynx::exportopts::WavExtras x;
    x.loop = true;
    x.musicFrames = 3925130; // (FL's loop end 3925129 = the last frame of the music)
    x.acid = true;
    x.tempo = 120.0f;
    x.markers = { { 176400, "Verse" }, { 352800, "" }, { 661500, "Drop A" } };
    const auto got = lynx::exportopts::wavExtraChunks (x, 44100.0, 3925359);
    CHECK (got.size() == want.size());
    CHECK (got == want);

    // from a song: the plain markers only, at their time; tempo and meter
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.beatsPerBar = 3;
    song.markers().clear();
    lynx::Marker a;
    a.tick = 8 * lynx::ppq;
    a.name = "Verse";
    song.addMarker (a);
    lynx::Marker sig;
    sig.tick = 12 * lynx::ppq;
    sig.numerator = 7;
    sig.action = lynx::Marker::timeSignature;
    song.addMarker (sig);
    lynx::Marker loop;
    loop.tick = 14 * lynx::ppq;
    loop.action = lynx::Marker::markerLoop;
    song.addMarker (loop);
    lynx::exportopts::Options o;
    o.range = lynx::exportopts::Range::pattern;
    auto e = lynx::exportopts::wavExtras (song, o, 44100.0);
    CHECK (e.loop && ! e.acid && e.numerator == 3 && e.denominator == 4 && e.tempo == 120.0f);
    o.saveTempo = true;
    o.loopMarkers = false;
    o.playlistMarkers = false;
    e = lynx::exportopts::wavExtras (song, o, 44100.0);
    CHECK (e.acid && ! e.loop && e.markers.empty());
    // the defaults are FL's (tempo off, loop and playlist markers on) and survive the settings text
    const lynx::exportopts::Options d;
    CHECK (! d.saveTempo && d.loopMarkers && d.playlistMarkers);
    CHECK (lynx::exportopts::fromText (lynx::exportopts::toText (o)) == o);
    CHECK (lynx::exportopts::wavExtraChunks ({}, 44100.0, 100).empty());
}

TEST (rea167l_export_wav_extras_song_markers)
{
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.markers().clear();
    lynx::Marker a;
    a.tick = 2 * lynx::ppq; // 1 s
    a.name = "A";
    song.addMarker (a);
    lynx::Marker b;
    b.tick = 6 * lynx::ppq; // 3 s
    song.addMarker (b);
    // the song as the range: a clip of 16 beats so both markers are inside
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 16 * lynx::ppq;
    song.playlist.push_back (c);
    lynx::exportopts::Options o;
    const auto e = lynx::exportopts::wavExtras (song, o, 44100.0);
    REA167L_REQUIRE (e.markers.size() == 2);
    CHECK (e.markers[0].frame == 44100 && e.markers[0].name == "A" && e.markers[1].frame == 132300 && e.markers[1].name.empty());
    // a selection from beat 4: the second marker 1 s in, the first left out
    song.loopStart = 4 * lynx::ppq;
    song.loopEnd = 12 * lynx::ppq;
    o.range = lynx::exportopts::Range::selection;
    const auto s = lynx::exportopts::wavExtras (song, o, 44100.0);
    REA167L_REQUIRE (s.markers.size() == 1);
    CHECK (s.markers[0].frame == 44100);

    // appended to a WAV file: the RIFF size follows, the frames are read back
    const auto path = (std::filesystem::temp_directory_path() / ("lynx_rea167l_extras_" + processTempTag() + ".wav")).string();
    {
        std::vector<std::uint8_t> w;
        auto put = [&w] (const char* t) { w.insert (w.end(), t, t + 4); };
        auto u32 = [&w] (std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back ((std::uint8_t) (v >> (8 * i))); };
        put ("RIFF"); u32 (4 + 24 + 8 + 10); put ("WAVE");
        put ("fmt "); u32 (16); u32 (0x00020001); u32 (44100); u32 (176400); u32 (0x00100004);
        put ("data"); u32 (10); for (int i = 0; i < 10; ++i) w.push_back (0);
        std::ofstream (path, std::ios::binary).write ((const char*) w.data(), (std::streamsize) w.size());
    }
    CHECK (lynx::exportopts::wavDataFrames (path) == 2);
    lynx::exportopts::WavExtras x;
    x.markers = { { 1, "m" } };
    CHECK (lynx::exportopts::appendWavExtras (path, x, 44100.0));
    std::ifstream in (path, std::ios::binary);
    std::vector<std::uint8_t> back ((std::istreambuf_iterator<char> (in)), {});
    REA167L_REQUIRE (back.size() > 54);
    const auto riff = (std::uint32_t) back[4] | (std::uint32_t) back[5] << 8 | (std::uint32_t) back[6] << 16 | (std::uint32_t) back[7] << 24;
    CHECK (riff == back.size() - 8);
    CHECK (std::string (back.begin() + 54, back.begin() + 58) == "cue ");
    CHECK (lynx::exportopts::wavDataFrames (path) == 2);
    std::filesystem::remove (path);
}

// §2 FL 2026's per-clip stretch (+0x40) [FL render r167l/fl/l_all.flp take 2: a 2 s 440 Hz ramp, 6-beat clips]
namespace rea167l
{
inline std::vector<float> renderSine (const lynx::Song& s, double seconds)
{
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (96000);
        for (std::size_t i = 0; i < d->left.size(); ++i)
            d->left[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / 48000.0);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256), out;
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    for (int b = 0; b < (int) (seconds * 48000.0 / 256.0); ++b)
    {
        r.process (state.get(), true, (double) b * 256.0 * tps, 256, l.data(), rr.data());
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace rea167l

TEST (rea167l_clip_stretch_import)
{
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0xA9); b.u32 (7);
    b.u8 (0x9C); b.u32 (120000);
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (4);
    b.utf16 (0xC4, u"C:\\samples\\ramp.wav");
    std::vector<std::uint8_t> items (80, 0);
    std::uint32_t pos = 0, len = 576;
    std::uint16_t base = 20480, idx = 0, rv = 499;
    const float gain = 1.0f;
    const double stretch = 1.5;
    std::memcpy (items.data() + 0, &pos, 4);
    std::memcpy (items.data() + 4, &base, 2);
    std::memcpy (items.data() + 6, &idx, 2);
    std::memcpy (items.data() + 8, &len, 4);
    std::memcpy (items.data() + 12, &rv, 2);
    std::memcpy (items.data() + 0x34, &gain, 4);
    std::memcpy (items.data() + 0x40, &stretch, 8);
    b.data (0xE9, items);
    const auto p = flp::parseProject (b.bytes);
    REA167L_REQUIRE (p.arrangements.size() == 1 && p.arrangements[0].items.size() == 1);
    CHECK (p.arrangements[0].items[0].stretch == 1.5);
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    o.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto song = lynx::importFlp (p, o);
    REA167L_REQUIRE (song.playlist.size() == 1);
    // FL: the 6-beat item stretched 1.5 ends 9 beats after its start (the song's end with it)
    CHECK (song.playlist[0].stretch == 1.5 && song.playlist[0].length == 9 * lynx::ppq);
    // the Playlist reads the file at 1 / stretch
    const auto& ch = song.channels[(std::size_t) song.playlist[0].channel];
    CHECK (std::abs (lynx::clipops::framesPerTick (ch, song.playlist[0], 96000.0, 48000.0, 120.0) * 1.5
                     - lynx::clipops::framesPerTick (ch, 96000.0, 48000.0, 120.0)) < 1.0e-9);
}

TEST (rea167l_clip_stretch_plays_like_fl)
{
    using namespace lynx;
    auto s = rea167k::rampSong();
    s.playlist[0].length = 12 * ppq; // 6 s at 120 BPM; after the file's end the clip holds its last value (FL, round 168j)
    // Resample: ×2 = 4 s at 220 Hz, ×0.5 = 1 s at 880 Hz (FL: st_r_2 / st_r_05)
    s.playlist[0].stretch = 2.0;
    auto x = rea167l::renderSine (s, 5.0);
    CHECK (std::abs (rea167k::frequencyOf (x, 0.5, 1.5) - 220.0) < 1.5);
    CHECK (rea167k::peakOf (x, 3.7, 3.95) > 0.2f && rea167k::spreadOf (x, 4.15, 4.9) < 1.0e-4f);
    s.playlist[0].stretch = 0.5;
    x = rea167l::renderSine (s, 2.0);
    CHECK (std::abs (rea167k::frequencyOf (x, 0.2, 0.8) - 880.0) < 3.0);
    CHECK (rea167k::peakOf (x, 0.85, 0.97) > 0.2f && rea167k::spreadOf (x, 1.1, 1.9) < 1.0e-4f);
    // a stretch mode (FL: st_e3_2 / st_rt_2): ×2 = 4 s, the pitch stays 440 Hz
    s.channels[0].sampler.keepPitch = true;
    s.playlist[0].stretch = 2.0;
    x = rea167l::renderSine (s, 5.0);
    CHECK (std::abs (rea167k::frequencyOf (x, 0.5, 3.0) - 440.0) < 3.0);
    CHECK (rea167k::peakOf (x, 3.6, 3.9) > 0.1f && rea167k::spreadOf (x, 4.3, 4.9) < 1.0e-4f);
}
