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
