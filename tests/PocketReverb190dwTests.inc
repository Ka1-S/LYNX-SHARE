// Round 190dw: FL Studio Mobile's reverbs on FLM's own plate (core/PocketReverb.cpp: Pocket Reverb Classic / Reverb 2 /
// Reverb, modules 12 / 35 / 32). Expected values are worked from FLM's laws (docs/analise/FLM_FORMAT.md §7, round 190dw); the
// engines' match with FLM's own renders (16-bit null, -70...-77 dB) is measured outside the tests (round 190dw HANDOFF).

namespace rv190dw
{
struct Stereo
{
    std::vector<float> l, r;
};

inline Stereo run (lynx::EffectKind kind, std::initializer_list<std::pair<int, float>> values, Stereo io, double fs = 48000.0,
                   double bpm = 120.0, int block = 256)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (fs);
    auto p = makeEffectSlot (kind).params;
    for (const auto& [k, v] : values)
        p[(std::size_t) k] = v;
    const int n = (int) io.l.size();
    for (int at = 0; at < n; at += block)
        fx->process (io.l.data() + at, io.r.data() + at, std::min (block, n - at), p, bpm);
    return io;
}
inline Stereo impulse (int n, int at)
{
    Stereo s { std::vector<float> ((std::size_t) n, 0.0f), std::vector<float> ((std::size_t) n, 0.0f) };
    s.l[(std::size_t) at] = s.r[(std::size_t) at] = 1.0f;
    return s;
}
// the slope (dB per second) of the energy decay curve of x between -10 and -40 dB
inline double decaySlope (const std::vector<float>& x, int from, double fs)
{
    std::vector<double> e (x.size() - (std::size_t) from + 1, 0.0);
    for (std::size_t i = x.size(); i-- > (std::size_t) from;)
        e[i - (std::size_t) from] = e[i - (std::size_t) from + 1] + (double) x[i] * x[i];
    const double e0 = e[0];
    int a = -1, b = -1;
    for (std::size_t i = 0; i + 1 < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (e[i] / e0 + 1e-300);
        if (a < 0 && db <= -10.0) a = (int) i;
        if (b < 0 && db <= -40.0) { b = (int) i; break; }
    }
    return a < 0 || b < 0 ? 0.0 : -30.0 / ((double) (b - a) / fs);
}
} // namespace rv190dw

TEST (pocket190dw_reverb_info_and_texts)
{
    using namespace lynx;
    CHECK ((int) effectInfo (EffectKind::pocketReverbClassic).params.size() == pocket::reverbClassicParams);
    CHECK ((int) effectInfo (EffectKind::pocketReverb2).params.size() == pocket::reverb2Params);
    CHECK ((int) effectInfo (EffectKind::pocketReverb).params.size() == pocket::reverbParams);
    // DECAY: v³ (legacy and 32) or v (Reverb 2) → ⌊199·x + 1.5⌋ tenths of a second
    CHECK (pocket::reverbClassicParamText (0, 0.4f) == "1.4 s");
    CHECK (pocket::reverbParamText (0, 0.4f) == "1.4 s");
    CHECK (pocket::reverb2ParamText (3, 0.068f) == "1.5 s");
    CHECK (pocket::reverb2ParamText (0, 1.0f) == "Off");      // HIGH CUT at its top: off
    CHECK (pocket::reverb2ParamText (10, 0.1525f) == "153 ms"); // DELAY 0…1000 ms
    CHECK (pocket::reverbClassicParamText (3, 1.0f) == "250 ms"); // the legacy PREDELAY stops at 250 ms
    CHECK (pocket::reverbClassicParamText (2, 0.0f) == "-inf dB");
}

TEST (pocket190dw_reverb_silence_and_dry)
{
    using namespace lynx;
    using rv190dw::run;
    const int n = 48000;
    const rv190dw::Stereo zero { std::vector<float> ((std::size_t) n, 0.0f), std::vector<float> ((std::size_t) n, 0.0f) };
    for (auto k : { EffectKind::pocketReverbClassic, EffectKind::pocketReverb2, EffectKind::pocketReverb })
    {
        const auto out = run (k, {}, zero);
        CHECK (std::all_of (out.l.begin(), out.l.end(), [] (float v) { return v == 0.0f; }));
        CHECK (std::all_of (out.r.begin(), out.r.end(), [] (float v) { return v == 0.0f; }));
    }
    // legacy: the plate adds onto the buffer, so the input passes at unity (DRY 0 = off) and the wet comes later
    const auto c = run (EffectKind::pocketReverbClassic, {}, rv190dw::impulse (n, 1000));
    CHECK (c.l[1000] == 1.0f && c.r[1000] == 1.0f);
    // Reverb 2 with ER and WET at 0: out = 1.25·DRY·in
    const auto d = run (EffectKind::pocketReverb2, { { 6, 0.8f }, { 7, 0.0f }, { 8, 0.0f } }, rv190dw::impulse (n, 1000));
    CHECK (std::abs (d.l[1000] - 1.0f) < 1e-6f && std::abs (d.r[1000] - 1.0f) < 1e-6f);
    double rest = 0.0;
    for (int i = 0; i < n; ++i)
        if (i != 1000) rest += std::abs (d.l[(std::size_t) i]) + std::abs (d.r[(std::size_t) i]);
    CHECK (rest < 1e-20);
    // Reverb (32) with MIX 0: dry only (× dry level); MIX 1: no dry
    const auto e0 = run (EffectKind::pocketReverb, { { 2, 0.0f }, { 4, 0.0f } }, rv190dw::impulse (n, 1000));
    CHECK (std::abs (e0.l[1000] - 1.0f) < 1e-6f);
    const auto e1 = run (EffectKind::pocketReverb, { { 2, 1.0f }, { 4, 0.0f } }, rv190dw::impulse (n, 1000));
    CHECK (std::abs (e1.l[1000]) < 1e-6f);
}

// The raw early reflections of Reverb 2: the impulse reaches the early-reflection line after FLM's two first input
// diffusers (y0 = −c4, y1 = c4·c8: the second feeds forward through c8), and each side sounds one tap, at
// ⌊E·3.4013603e-5·⌊(room + 3)·fs/103⌋⌋ samples, weighted 0.5·m/oL and 0.36·m/oR (m = the shorter tap).
TEST (pocket190dw_reverb2_early_reflection_law)
{
    using namespace lynx;
    const double fs = 48000.0;
    const int n = 4096, at = 300;
    // ER 1 (× 1.25), DRY 0, WET 0, HIGH / LOW CUT off, SIZE 0.5 (room 51), DIFFUSION 1
    const auto out = rv190dw::run (EffectKind::pocketReverb2, { { 0, 1.0f }, { 1, 0.0f }, { 2, 0.5f }, { 4, 1.0f }, { 6, 0.0f }, { 7, 1.0f }, { 8, 0.0f } },
                                   rv190dw::impulse (n, at), fs);
    const int fsI = 48000, room = 51;
    const int rs = ((room + 15) * fsI) / 115;
    const int L0 = rs * 142 / 20000, L1 = rs * 107 / 20000, L2 = rs * 379 / 20000, L3 = rs * 277 / 20000;
    const float inv = 1.0f / (float) fs;
    const float c4 = std::exp ((float) (L0 + L1) * -49.34111f * inv), c8 = std::exp (-31.398888f * (float) (L2 + L3) * inv);
    const float x8 = (float) (((room + 3) * fsI) / 103);
    const int oL = (int) (1102.0f * 3.4013603e-05f * x8), oR = (int) (837.0f * 3.4013603e-05f * x8);
    const float m = (float) std::min (oL, oR);
    const float wantL = 1.25f * c4 * c8 * (m * 0.5f / (float) oL), wantR = 1.25f * c4 * c8 * (m * 0.36f / (float) oR);
    CHECK (oL == 943 && oR == 716);
    CHECK (std::abs (out.l[(std::size_t) (at + oL)] - wantL) < 1e-6f * std::abs (wantL) + 1e-9f);
    CHECK (std::abs (out.r[(std::size_t) (at + oR)] - wantR) < 1e-6f * std::abs (wantR) + 1e-9f);
    // nothing before the taps (the line holds FLM's ~1e-30 fill)
    for (int i = 0; i < at + oR; ++i)
        CHECK (std::abs (out.r[(std::size_t) i]) < 1e-20f);
}

// The plate's decay law: a gain of e^(ln 0.001·ΣL/(fs·T)) per pass through the tank's four delay lines (ΣL), T =
// DECAY's tenths of a second; the four tank all-passes lengthen the pass (by 6036/15553), so the tail falls
// 60·15553/21589 dB per T — at any rate
TEST (pocket190dw_reverb_decay_law_and_rates)
{
    using namespace lynx;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = (int) (fs * 3.0), at = 100;
        // Reverb (32): MIX 1 (wet only), EARLY 0, HIGH DAMP and HI-CUT off, DECAY ∛(19/199) → control 20 = 2.0 s
        const float dec = std::cbrt (19.0f / 199.0f);
        const auto out = rv190dw::run (EffectKind::pocketReverb, { { 0, dec }, { 1, 1.0f }, { 2, 1.0f }, { 4, 0.0f }, { 10, 1.0f } },
                                       rv190dw::impulse (n, at), fs);
        CHECK (pocket::reverbParamText (0, dec) == "2.0 s");
        std::vector<float> mono (out.l.size());
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = out.l[i] + out.r[i];
        const double slope = rv190dw::decaySlope (mono, at + (int) (0.1 * fs), fs);
        // the loop's four delay lines carry the gain; its four all-passes add their length to the round trip
        const double want = -60.0 / 2.0 * 15553.0 / (15553.0 + 6036.0);
        CHECK (std::abs (slope - want) < 0.05 * std::abs (want));
        CHECK (std::all_of (out.l.begin(), out.l.end(), [] (float v) { return std::isfinite (v); }));
    }
}

// Reverb 2's DELAY with TEMPO: a whole number of 1/16 beats, shortened while above 1000 ms; then as the free DELAY
TEST (pocket190dw_reverb2_tempo_delay)
{
    using namespace lynx;
    const int n = 96000;
    // 4/16 beat at 120 BPM = 125 ms = the free DELAY 0.125
    const auto synced = rv190dw::run (EffectKind::pocketReverb2, { { 10, 0.25f }, { 11, 1.0f } }, rv190dw::impulse (n, 10), 48000.0, 120.0);
    const auto free = rv190dw::run (EffectKind::pocketReverb2, { { 10, 0.125f }, { 11, 0.0f } }, rv190dw::impulse (n, 10), 48000.0, 120.0);
    CHECK (synced.l == free.l && synced.r == free.r);
    // 16/16 beat at 30 BPM = 2000 ms → shortened to 8/16 = 1000 ms = the free DELAY 1
    const auto slow = rv190dw::run (EffectKind::pocketReverb2, { { 10, 1.0f }, { 11, 1.0f } }, rv190dw::impulse (n, 10), 48000.0, 30.0);
    const auto top = rv190dw::run (EffectKind::pocketReverb2, { { 10, 1.0f }, { 11, 0.0f } }, rv190dw::impulse (n, 10), 48000.0, 30.0);
    CHECK (slow.l == top.l);
    // and not the same as 125 ms
    CHECK (! (slow.l == synced.l));
}

TEST (pocket190dw_reverb_extremes_finite)
{
    using namespace lynx;
    std::mt19937 rng (190);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    rv190dw::Stereo noise { std::vector<float> (48000), std::vector<float> (48000) };
    for (auto& v : noise.l) v = u (rng);
    for (auto& v : noise.r) v = u (rng);
    for (auto k : { EffectKind::pocketReverbClassic, EffectKind::pocketReverb2, EffectKind::pocketReverb })
        for (float v : { 0.0f, 1.0f })
        {
            auto fx = makeEffect (k);
            fx->prepare (44100.0);
            auto p = makeEffectSlot (k).params;
            for (int i = 0; i < (int) effectInfo (k).params.size(); ++i)
                p[(std::size_t) i] = v;
            auto io = noise;
            for (int at = 0; at < 48000; at += 333)
                fx->process (io.l.data() + at, io.r.data() + at, std::min (333, 48000 - at), p, 140.0);
            CHECK (std::all_of (io.l.begin(), io.l.end(), [] (float x) { return std::isfinite (x) && std::abs (x) < 1e4f; }));
            CHECK (std::all_of (io.r.begin(), io.r.end(), [] (float x) { return std::isfinite (x) && std::abs (x) < 1e4f; }));
        }
}
