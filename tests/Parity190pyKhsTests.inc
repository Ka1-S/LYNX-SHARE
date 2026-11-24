// Round 190py (Kilohearts part 4): kHs Flanger and kHs Dual Delay as hidden laws of Effector (core/KiloheartsDsp.h,
// codes 50 and 51 / 64): FlangerDsp and DualdelayDsp read in HeartCore (docs/analise/REA_rodada190py-khs.md), translated
// from their states (core/KiloheartsStates.inc). Uses parity190lnkhs::state / load / named and parity190nbkhs::run.

namespace parity190pykhs
{
inline std::string flangerBody (const char* mix = "1")
{
    return std::string ("\"delay\": \"0.001\", \"phase\": \"0\", \"mix\": \"") + mix + "\", \"mod_depth\": \"0\", \"spread\": \"0.25\", "
                        "\"mod_rate\": \"0.3\", \"scroll\": \"0.5\", \"feedback\": \"0\", \"barberpole\": \"false\", \"$type\": \"flanger\"";
}
inline std::string dualBody (const char* feedback, const char* mix, const char* duck = "0")
{
    return std::string ("\"delay_time\": \"0.2\", \"ratio\": \"1.618034\", \"delay_numerator\": \"3\", \"delay_denominator\": \"denominator_16\", "
                        "\"duck\": \"") + duck + "\", \"sync_mode\": \"time\", \"feedback\": \"" + feedback + "\", \"crosstalk\": \"0.5\", "
                        "\"spread\": \"0.5\", \"tone\": \"0\", \"mix\": \"" + mix + "\", \"$type\": \"dualdelay\"";
}
} // namespace parity190pykhs

TEST (parity190pykhs_law_codes_and_translation)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    namespace k = khsdsp;
    CHECK (k::lawCode (k::Law::flanger) == 50 && k::lawCode (k::Law::dualDelay) == 51);
    CHECK (k::lawOf (50.0f / 64.0f) == k::Law::flanger && k::lawOf (51.0f / 64.0f) == k::Law::dualDelay);
    CHECK (k::lawOf (49.0f / 64.0f) == k::Law::nonlinearFilter && k::lawOf (52.0f / 64.0f) == k::Law::ensemble2);
    CHECK (k::lawOf (57.0f / 64.0f) == k::Law::none && k::lawCode (k::Law::ensemble2) == 52);
    int n = 0;
    const auto f = load ("kHs Flanger", state (parity190pykhs::flangerBody()), n);
    CHECK (f.kind == EffectKind::effector && n > 8 && k::lawOf (f.params[(std::size_t) k::lawParam]) == k::Law::flanger);
    // the times × 64 and the rate / 32 come back as the snapin's own floats
    CHECK (f.params[(std::size_t) k::valueParam] / k::enc::flSeconds == 0.001f);
    CHECK (f.params[(std::size_t) k::valueParam + 2] * k::enc::flRateHz == 0.3f);
    CHECK (f.params[(std::size_t) named (f, "Wet")] == 1.0f && std::lround (11.0f * f.params[(std::size_t) named (f, "Type")]) == 2); // Flanger
    const auto d = load ("kHs Dual Delay", state (parity190pykhs::dualBody ("0.5", "0.33333334")), n);
    CHECK (d.kind == EffectKind::effector && k::lawOf (d.params[(std::size_t) k::lawParam]) == k::Law::dualDelay);
    CHECK (d.params[(std::size_t) k::valueParam] * k::enc::ddSeconds == 0.2f && d.params[(std::size_t) k::valueParam + 1] * k::enc::ddRatio == 1.618034f);
    CHECK (std::lround (11.0f * d.params[(std::size_t) named (d, "Type")]) == 5); // Delay
    // the Flanger reports its up-sampler's 16 samples; the Dual Delay none
    CHECK (makeEffect (f)->latencySamples() == 16 && makeEffect (d)->latencySamples() == 0);
}

TEST (parity190pykhs_flanger_dry_and_wet_through_the_2x_line)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // depth 0, delay 1 ms, mix 100 %: dry cos 45° 16 samples late, wet sin 45° at 1 ms − 1.5 / fs + the read's sample (47)
    auto f = load ("kHs Flanger", state (parity190pykhs::flangerBody()), n);
    std::vector<float> x (4000, 0.0f), r;
    x[1000] = 1.0f;
    const auto y = parity190nbkhs::run (f, x, &r);
    CHECK (std::abs (y[1016] - 0.70710677f) < 1e-6f);
    CHECK (std::abs (y[1063] - 0.70710677f) < 1e-3f && std::abs (r[1063] - 0.70710677f) < 1e-3f);
    double early = 0.0;
    for (int i = 0; i < 1016; ++i)
        early += (double) y[(std::size_t) i] * y[(std::size_t) i];
    CHECK (early == 0.0);
    // mix 0: the input, 16 samples late, bit for bit
    auto dry = load ("kHs Flanger", state (parity190pykhs::flangerBody ("0")), n);
    std::vector<float> t (6000);
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = 0.4f * (float) std::sin (0.031 * (double) i);
    const auto z = parity190nbkhs::run (dry, t);
    bool same = true;
    for (std::size_t i = 16; i < t.size(); ++i)
        same = same && z[i] == t[i - 16];
    CHECK (same);
}

TEST (parity190pykhs_dual_delay_echoes_mix_and_spread)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // 0.2 s and × 1.618: echoes one sample after 9 600 and 15 533 samples; the mix is linear; spread 0.5 pans the first
    // line √2 sin 67.5° left / √2 cos 67.5° right (the second line the other way round)
    auto d = load ("kHs Dual Delay", state (parity190pykhs::dualBody ("0", "0.5")), n);
    std::vector<float> x (40000, 0.0f), r;
    x[100] = 0.5f;
    const auto y = parity190nbkhs::run (d, x, &r);
    const float a = 1.4142135f * std::sin (1.1780972f), b = 1.4142135f * std::cos (1.1780972f);
    CHECK (std::abs (y[100] - 0.25f) < 1e-6f);
    CHECK (std::abs (y[100 + 9601] - 0.25f * a) < 1e-5f && std::abs (r[100 + 9601] - 0.25f * b) < 1e-5f);
    CHECK (std::abs (y[100 + 15534] - 0.25f * b) < 1e-5f && std::abs (r[100 + 15534] - 0.25f * a) < 1e-5f);
    CHECK (std::abs (y[100 + 9600]) < 1e-9f && std::abs (y[100 + 9602]) < 1e-9f);
}

TEST (parity190pykhs_dual_delay_feedback_rings_through_silence_and_ducks)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // HeartCore's sleep: a block whose echoes are still audible counts as sound, so the repeats ring on through the
    // silence between them (round 190py's first draft fell asleep after the second echo)
    auto d = load ("kHs Dual Delay", state (parity190pykhs::dualBody ("0.5", "1")), n);
    std::vector<float> x (200000, 0.0f);
    x[100] = 0.5f;
    const auto y = parity190nbkhs::run (d, x);
    double late = 0.0;
    for (std::size_t i = 100000; i < y.size(); ++i)
        late = std::max (late, (double) std::abs (y[i]));
    CHECK (late > 1e-3 && late < 0.5);
    bool finite = true;
    for (float v : y)
        finite = finite && std::isfinite (v);
    CHECK (finite);
    // ducking: a loud steady input holds the echoes down
    auto loud = load ("kHs Dual Delay", state (parity190pykhs::dualBody ("0", "1", "1")), n);
    auto open = load ("kHs Dual Delay", state (parity190pykhs::dualBody ("0", "1", "0")), n);
    std::vector<float> s (40000);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = 0.3f * (float) std::sin (0.05 * (double) i);
    const auto yd = parity190nbkhs::run (loud, s), yf = parity190nbkhs::run (open, s);
    double ed = 0.0, ef = 0.0;
    for (std::size_t i = 20000; i < s.size(); ++i)
        ed += (double) yd[i] * yd[i], ef += (double) yf[i] * yf[i];
    CHECK (ed < 0.5 * ef);
}

TEST (parity190pykhs_ensemble_curves_and_the_dry_on_law_52)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    namespace k = khsdsp;
    // the generator's curves: random ones span 0 … 1 (to a float's rounding), a symmetric odd voice mirrors the voice before (delay
    // curve), every curve stays in 0 … 1 and is made the same way every time
    const auto r0 = k::Engine::ensembleCurve (0, 0, 0);
    CHECK (r0.size() == 1024);
    CHECK (*std::min_element (r0.begin(), r0.end()) == 0.0f && *std::max_element (r0.begin(), r0.end()) > 0.9999998f);
    const auto s2 = k::Engine::ensembleCurve (1, 2, 0), s3 = k::Engine::ensembleCurve (1, 3, 0);
    bool mirrored = true;
    for (std::size_t i = 0; i < 1024; ++i)
        mirrored = mirrored && s3[i] == 1.0f - s2[i];
    CHECK (mirrored);
    bool bounded = true;
    for (int m = 0; m < 3; ++m)
        for (int v = 0; v < 16; ++v)
            for (int c = 0; c < 2; ++c)
                for (float x : k::Engine::ensembleCurve (m, v, c))
                    bounded = bounded && x >= 0.0f && x <= 1.0f;
    CHECK (bounded);
    CHECK (k::Engine::ensembleCurve (0, 5, 1) == k::Engine::ensembleCurve (0, 5, 1) && k::Engine::ensembleCurve (0, 5, 1) != k::Engine::ensembleCurve (0, 6, 1));
    // the translator opens kHs Ensemble on law 52 (law 48 stays readable for slots saved with it)
    int n = 0;
    auto e = load ("kHs Ensemble", state ("\"spread\": \"0.5\", \"voices\": \"6\", \"motion\": \"symmetric\", \"detune\": \"0.25\", \"mix\": \"0\", \"$type\": \"unison\""), n);
    CHECK (k::lawOf (e.params[(std::size_t) k::lawParam]) == k::Law::ensemble2 && k::lawOf (48.0f / 64.0f) == k::Law::ensemble);
    // mix 0: the input 16 + ⌈0.016 fs⌉ − 1 samples late (784 at 48 kHz), bit for bit
    std::vector<float> t (6000);
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = 0.4f * (float) std::sin (0.031 * (double) i);
    const auto z = parity190nbkhs::run (e, t);
    bool same = true;
    for (std::size_t i = 784; i < t.size(); ++i)
        same = same && z[i] == t[i - 784];
    CHECK (same);
}
