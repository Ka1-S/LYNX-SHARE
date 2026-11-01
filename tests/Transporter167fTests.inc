// Round 167f: FL's Transporter (transient looper) on FL's own engine (core/TransporterFl.h,
// docs/analise/REA_rodada167f-transporter.md). The goldens are FNV-1a hashes of the float bits of FL's own output:
// the whole plug-in (the effect path) and its DSP library's engine (four channels: L, R, send A, send B).

#include "core/TransporterFl.h"

namespace tp167f
{
std::vector<std::uint8_t> state (int version, std::initializer_list<std::int32_t> controls, bool tail, int source = 2, int stereo = 1)
{
    std::vector<std::uint8_t> out;
    auto put = [&out] (std::int32_t x) {
        const auto* b = reinterpret_cast<const std::uint8_t*> (&x);
        out.insert (out.end(), b, b + 4);
    };
    put (version);
    for (const auto c : controls)
        put (c);
    out.push_back (1); // flush when disabled
    if (tail)
    {
        out.push_back (1); // look-ahead (unused)
        put (source);
        put (stereo);
        if (version >= 8)
        {
            put (0); // send track
            put (0); // input track
        }
    }
    return out;
}
// 48000 stereo frames of LCG noise in eight loudness steps (FL's goldens were made from the same signal)
std::vector<float> signal()
{
    std::vector<float> s (96000);
    std::uint32_t z = 12345;
    const float amps[] { 0.3f, 1.5f, 0.05f, 3.0f, 0.8f, 0.0f, 2.0f, 0.4f };
    for (int i = 0; i < 48000; ++i)
        for (int c = 0; c < 2; ++c)
        {
            z = z * 1664525u + 1013904223u;
            s[(std::size_t) (2 * i + c)] = (float) ((z >> 8) / 8388608.0 - 1.0) * amps[(i / 3000) % 8];
        }
    return s;
}
std::uint64_t fnv (const std::vector<float>& v)
{
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto x : v)
    {
        std::uint32_t u;
        std::memcpy (&u, &x, 4);
        h = (h ^ u) * 0x100000001b3ull;
    }
    return h;
}
std::uint32_t bits (float x)
{
    std::uint32_t u;
    std::memcpy (&u, &x, 4);
    return u;
}
// the effect path: FL's state → effectFromFl → the mixer effect, 512-frame blocks at 44.1 kHz, 60 BPM
std::uint64_t runEffect (const std::vector<std::uint8_t>& st)
{
    const auto slot = lynx::effectFromFl ("Transporter", st, 60.0);
    if (! slot || slot->kind != lynx::EffectKind::flTransporter)
        return 0;
    auto fx = lynx::makeEffect (*slot);
    fx->prepare (44100.0);
    const auto in = signal();
    std::vector<float> l (48000), r (48000), out;
    for (int i = 0; i < 48000; ++i)
    {
        l[(std::size_t) i] = in[(std::size_t) (2 * i)];
        r[(std::size_t) i] = in[(std::size_t) (2 * i + 1)];
    }
    for (int i = 0; i < 48000; i += 512)
        fx->process (l.data() + i, r.data() + i, std::min (512, 48000 - i), slot->params, 60.0);
    for (int i = 0; i < 48000; ++i)
    {
        out.push_back (l[(std::size_t) i]);
        out.push_back (r[(std::size_t) i]);
    }
    return fnv (out);
}
struct Set
{
    long at; // −1: right after prepare
    int k;
    float v;
};
// the engine: settings by the library's index, a side-chain (L/R swapped, half level) or the input as the detector,
// a beat ramp of 22050-sample blocks at 120 BPM; 512-frame blocks, split at the timed settings
std::uint64_t runEngine (float rate, bool sideChain, bool beatRamp, std::initializer_list<Set> sets)
{
    lynx::transporter::Engine e;
    e.prepare (rate);
    for (const auto& s : sets)
        if (s.at < 0)
            e.setParam (s.k, s.v);
    const auto in = signal();
    std::vector<float> out (192000), l (512), r (512), ol (512), orr (512), s0 (512), s1 (512), c0 (512), c1 (512), ph (512);
    for (long n0 = 0; n0 < 48000;)
    {
        int len = (int) std::min (512L, 48000 - n0);
        for (const auto& s : sets)
            if (s.at > n0 && s.at < n0 + len)
                len = (int) (s.at - n0);
        for (const auto& s : sets)
            if (s.at == n0)
                e.setParam (s.k, s.v);
        for (int j = 0; j < len; ++j)
        {
            const auto i = (std::size_t) (n0 + j);
            l[(std::size_t) j] = in[2 * i];
            r[(std::size_t) j] = in[2 * i + 1];
            c0[(std::size_t) j] = sideChain ? in[2 * i + 1] * 0.5f : 0.0f;
            c1[(std::size_t) j] = sideChain ? in[2 * i] * 0.5f : 0.0f;
            ph[(std::size_t) j] = beatRamp ? (float) std::fmod ((double) ((i / 512) * 512) / 22050.0 * 2.0, 4.0) : 0.0f;
        }
        e.process (l.data(), r.data(), c0.data(), c1.data(), ph.data(), ol.data(), orr.data(), s0.data(), s1.data(), len);
        for (int j = 0; j < len; ++j)
        {
            const auto i = (std::size_t) (n0 + j);
            out[4 * i] = ol[(std::size_t) j];
            out[4 * i + 1] = orr[(std::size_t) j];
            out[4 * i + 2] = s0[(std::size_t) j];
            out[4 * i + 3] = s1[(std::size_t) j];
        }
        n0 += len;
    }
    return fnv (out);
}
} // namespace tp167f

TEST (tp167f_state_parsing_and_params)
{
    using namespace lynx::transporter;
    // version 8 (a demo song's): 22 controls, the flags, source, stereo, send and input tracks
    const auto v8 = parse (tp167f::state (8, { 11000, 5362, 5675, 4969, 0, 0, 499, 2000, 12, 1024, 512, 0, 0, 1, 0, 0, 0, 0, 0, 1, 21, 0 }, true));
    CHECK (v8 && v8->raw[0] == 11000 && v8->raw[7] == 2000 && v8->raw[13] == 1 && v8->raw[20] == 21 && v8->source == 2 && v8->stereo == 1);
    // version 4: 20 controls ([21] = 0), source / stereo, no tracks
    const auto v4 = parse (tp167f::state (4, { 12800, 8086, 8086, 5001, 10000, 0, 869, 2000, 2, 510, 512, 0, 0, 2, 0, 0, 0, 0, 1, 5 }, true, 1, 0));
    CHECK (v4 && v4->raw[18] == 1 && v4->raw[19] == 5 && v4->raw[21] == 0 && v4->source == 1 && v4->stereo == 0 && v4->sendTrack == 0);
    // version 3: the controls and one flag only (source / stereo keep their defaults)
    const auto v3 = parse (tp167f::state (3, { 12800, 12800, 12800, 0, 0, 0, 512, 500, 8, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, false));
    CHECK (v3 && v3->raw[8] == 8 && v3->source == 2 && v3->stereo == 1);
    CHECK (! parse ({}) && ! parse (tp167f::state (9, { 0 }, false)));

    // parameters: raw over the range, and back
    lynx::EffectParams p {};
    toParams (*v8, p.data());
    for (int k = 0; k < numControls; ++k)
        CHECK (rawOf (k, p[(std::size_t) k]) == v8->raw[(std::size_t) k]);
    CHECK (rawOf (sourceParam, p[sourceParam]) == 2 && rawOf (stereoParam, p[stereoParam]) == 1);
    const auto slot = lynx::effectFromFl ("Transporter", tp167f::state (8, { 11000, 5362, 5675, 4969, 0, 0, 499, 2000, 12, 1024, 512, 0, 0, 1, 0, 0, 0, 0, 0, 1, 21, 0 }, true), 120.0);
    CHECK (slot && slot->kind == lynx::EffectKind::flTransporter && slot->params[7] == 1.0f && slot->params[sendParam] == 0.0f);
    CHECK (lynx::effectForFlPlugin ("Transporter").has_value()); // the fallback when the state is unreadable
    const auto m = lynx::effectParamFromFl ("Transporter", 13);
    CHECK (m && m->param == 13 && m->convert (0.4f) == 1.0f / 3.0f);
}

TEST (tp167f_control_values_match_fl)
{
    using namespace lynx::transporter;
    using tp167f::bits;
    // the values FL's front end sends its engine (logged from the plug-in, float bits)
    CHECK (bits (engineValue (0, 11000)) == 0xc05233eau && bits (engineValue (1, 5362)) == 0xc173c87fu);
    CHECK (bits (engineValue (2, 5675)) == 0xc1672269u && bits (engineValue (0, 1)) == 0xc2bd17f4u);
    CHECK (bits (engineValue (0, 640)) == 0xc21797a7u && bits (engineValue (0, 6400)) == 0xc14b3f05u && bits (engineValue (0, 12799)) == 0xbaea9c6eu);
    CHECK (bits (engineValue (3, 4969)) == 0x3efe69adu && bits (engineValue (3, 1)) == 0x38d1b717u && bits (engineValue (3, 3333)) == 0x3eaaa64cu);
    CHECK (bits (engineValue (3, 9999)) == 0x3f7ff972u);
    CHECK (bits (engineValue (6, 499)) == 0xbcd00000u && bits (engineValue (6, 0)) == 0xbf800000u && bits (engineValue (6, 300)) == 0xbed40000u);
    CHECK (bits (engineValue (6, 511)) == 0xbb000000u && bits (engineValue (6, 513)) == 0x3b000000u && bits (engineValue (6, 1024)) == 0x3f800000u);
    CHECK (bits (engineValue (7, 20)) == 0x3ca3d70au && bits (engineValue (7, 21)) == 0x3cac0831u && bits (engineValue (7, 999)) == 0x3f7fbe76u);
    CHECK (bits (engineValue (7, 1980)) == 0x3ffd70a4u && engineValue (7, 2000) == 2.0f);
    CHECK (bits (engineValue (8, 1)) == 0x3fa00000u && bits (engineValue (8, 7)) == 0x40300000u && bits (engineValue (8, 35)) == 0x411c0000u);
    CHECK (engineValue (8, 12) == 4.0f && engineValue (9, 1024) == 1.0f && bits (engineValue (9, 3)) == 0x3b400000u && bits (engineValue (9, 700)) == 0x3f2f0000u);
    // the beat within the bar from the host's ticks
    CHECK (beatOf (0.0, 96) == 0.0f && beatOf (96.0 * 5, 96) == 1.0f && beatOf (96.0 * 4 + 48, 96) == 0.5f && beatOf (96.0 * 7, 96) == 3.0f);
}

TEST (tp167f_effect_matches_fl_plugin)
{
    using tp167f::runEffect;
    using tp167f::state;
    // FL's whole plug-in on the same input (its own input as the detector, no send), FNV-1a over L/R
    CHECK (runEffect (state (8, { 11000, 5362, 5675, 4969, 0, 0, 499, 2000, 12, 1024, 512, 0, 0, 1, 0, 0, 0, 0, 0, 1, 21, 0 }, true)) == 0x7d10984a49779bb5ull);
    CHECK (runEffect (state (3, { 12800, 12800, 12800, 0, 0, 0, 512, 500, 8, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, false)) == 0x6bbaf64ff2925971ull);
    CHECK (runEffect (state (4, { 0, 12800, 12800, 0, 2025, 4963, 1024, 1000, 0, 1024, 512, 0, 0, 3, 0, 0, 0, 0, 0, 1 }, true)) == 0x4a1508e9d88c40c3ull);
    CHECK (runEffect (state (4, { 12800, 8086, 8086, 5001, 10000, 0, 869, 2000, 2, 510, 512, 0, 0, 2, 0, 0, 0, 0, 1, 5 }, true)) == 0xc64d1c414699a0e2ull);
    CHECK (runEffect (state (8, { 9000, 11000, 10000, 3000, 4000, 6000, 100, 300, 20, 900, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 2500 }, true, 0, 0))
           == 0x607c6713a232beebull);
    CHECK (runEffect (state (8, { 12800, 12000, 12800, 5000, 0, 0, 700, 150, 30, 800, 900, 0, 0, 1, 0, 0, 0, 0, 0, 3, 4, 1000 }, true, 1)) == 0x84e077e042e34ad4ull);
}

TEST (tp167f_engine_matches_fl_library)
{
    using tp167f::runEngine;
    // side-chain detector, beat-grid trigger (Tempo mode), sends, probabilities and spread
    CHECK (runEngine (44100.0f, true, true,
                      { { -1, 13, 3 }, { -1, 19, 2 }, { -1, 20, 3 }, { -1, 22, 120 }, { -1, 3, 0.5f }, { -1, 4, 0.4f }, { -1, 5, 0.5f },
                        { -1, 6, -0.6f }, { -1, 1, -2 }, { -1, 2, -4 } })
           == 0xee654713452e2bd9ull);
    // 48 kHz, deterministic, freeze / update / bypass switches mid-stream
    CHECK (runEngine (48000.0f, false, false,
                      { { -1, 13, 0 }, { -1, 18, 1 }, { -1, 3, 0.6f }, { -1, 4, 0.6f }, { -1, 5, 0.3f }, { -1, 21, 0.2f }, { 20000, 12, 1 },
                        { 30000, 11, 1 }, { 36000, 11, 0 }, { 40000, 15, 1 }, { 41000, 15, 0 }, { -1, 14, 0 }, { 44000, 14, 1 } })
           == 0x245f32073398ef86ull);
}

TEST (tp167f_reset_latency_and_project_round_trip)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::flTransporter);
    fx->prepare (44100.0);
    CHECK (fx->latencySamples() == 220);
    fx->prepare (48000.0);
    CHECK (fx->latencySamples() == 240);
    // silence in → silence out, also after a reset (FL's flush)
    const auto slot = lynx::makeEffectSlot (lynx::EffectKind::flTransporter);
    std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
    fx->process (l.data(), r.data(), 2048, slot.params, 120.0);
    fx->reset();
    fx->process (l.data(), r.data(), 2048, slot.params, 120.0);
    bool silent = true;
    for (int i = 0; i < 2048; ++i)
        silent = silent && l[(std::size_t) i] == 0.0f && r[(std::size_t) i] == 0.0f;
    CHECK (silent);
    CHECK (lynx::effectInfo (lynx::EffectKind::flTransporter).name == std::string ("Transporter"));
}
