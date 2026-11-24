// Round 190qz (Kilohearts part 5): kHs Ring Mod with the plug-in's own 2× path, and kHs Formant Filter, kHs Delay and
// kHs Clipper as hidden laws of Effector (core/KiloheartsDsp.h, codes 53 … 56 / 64): RingModDsp, VocalFilterDsp,
// DelayDsp and ClipperDsp read in HeartCore (docs/analise/REA_rodada190qz-khs.md), translated from their states
// (core/KiloheartsStates.inc). Uses parity190lnkhs::state / load / named and parity190nbkhs::run.

namespace parity190qzkhs
{
inline std::string delayBody (const char* time, const char* feedback, const char* mix, const char* pan = "0", const char* bounce = "false",
                              const char* duck = "0", const char* tone = "0")
{
    return std::string ("\"pan\": \"") + pan + "\", \"delay_numerator\": \"3\", \"delay_time\": \"" + time + "\", \"delay_denominator\": \"denominator_16\", "
           "\"sync_mode\": \"ms\", \"ping_pong\": \"" + bounce + "\", \"duck\": \"" + duck + "\", \"feedback\": \"" + feedback + "\", \"mix\": \"" + mix
           + "\", \"tone\": \"" + tone + "\", \"$type\": \"delay\"";
}
inline double dftDb (const std::vector<float>& h, double hz)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = 0; i < h.size(); ++i)
        re += h[i] * std::cos (2.0 * M_PI * hz * (double) i / 48000.0), im -= h[i] * std::sin (2.0 * M_PI * hz * (double) i / 48000.0);
    return 10.0 * std::log10 (re * re + im * im);
}
} // namespace parity190qzkhs

TEST (parity190qzkhs_law_codes_translation_and_latency)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    namespace k = khsdsp;
    CHECK (k::lawCode (k::Law::ringMod2) == 53 && k::lawCode (k::Law::formant) == 54 && k::lawCode (k::Law::delay) == 55 && k::lawCode (k::Law::clipper) == 56);
    CHECK (k::lawOf (53.0f / 64.0f) == k::Law::ringMod2 && k::lawOf (56.0f / 64.0f) == k::Law::clipper && k::lawOf (57.0f / 64.0f) == k::Law::none);
    CHECK (k::lawOf (8.0f / 64.0f) == k::Law::ringMod && k::lawOf (52.0f / 64.0f) == k::Law::ensemble2); // earlier codes unchanged
    int n = 0;
    const auto ring = load ("kHs Ring Mod", state ("\"mix\": \"0.75\", \"frequency\": \"1810.193\", \"spread\": \"0.5\", \"bias\": \"0.25\", \"rectify\": \"-0.5\", \"$type\": \"ring_mod\""), n);
    CHECK (ring.kind == EffectKind::effector && k::lawOf (ring.params[(std::size_t) k::lawParam]) == k::Law::ringMod2);
    CHECK (ring.params[(std::size_t) k::valueParam] * 32768.0f == 1810.193f); // the snapin's own float, exactly
    CHECK (ring.params[(std::size_t) k::valueParam + 3] == 0.25f && ring.params[(std::size_t) named (ring, "Wet")] == 0.75f);
    const auto f = load ("kHs Formant Filter", state ("\"q\": \"6.5\", \"cutoff_1\": \"1500\", \"cutoff_2\": \"550\", \"lows\": \"false\", \"highs\": \"true\", \"$type\": \"vocal_filter\""), n);
    CHECK (f.kind == EffectKind::effector && k::lawOf (f.params[(std::size_t) k::lawParam]) == k::Law::formant);
    CHECK (f.params[(std::size_t) k::valueParam] * 32768.0f == 1500.0f && f.params[(std::size_t) k::valueParam + 2] * 64.0f == 6.5f);
    CHECK (f.params[(std::size_t) k::valueParam + 3] == 0.0f && f.params[(std::size_t) k::valueParam + 4] == 1.0f);
    const auto d = load ("kHs Delay", state (parity190qzkhs::delayBody ("0.2236068", "0.5", "0.5", "-0.5", "true", "0.25", "0.5")), n);
    CHECK (d.kind == EffectKind::effector && k::lawOf (d.params[(std::size_t) k::lawParam]) == k::Law::delay);
    CHECK (d.params[(std::size_t) k::valueParam] * k::enc::ddSeconds == 0.2236068f);
    CHECK (d.params[(std::size_t) k::valueParam + 5] == 0.25f && d.params[(std::size_t) k::valueParam + 6] == 1.0f
           && d.params[(std::size_t) k::valueParam + 7] == 0.25f && d.params[(std::size_t) k::valueParam + 8] == 0.75f);
    const auto c = load ("kHs Clipper", state ("\"threshold\": \"1.99526\", \"knee\": \"0.25\", \"pre_gain\": \"3.1622777\", \"post_gain\": \"0.1\", \"$type\": \"clipper\""), n);
    CHECK (c.kind == EffectKind::effector && k::lawOf (c.params[(std::size_t) k::lawParam]) == k::Law::clipper);
    CHECK (c.params[(std::size_t) k::valueParam] * 2.0f == 1.99526f && c.params[(std::size_t) k::valueParam + 2] * 16.0f == 3.1622777f
           && c.params[(std::size_t) k::valueParam + 3] * 16.0f == 0.1f);
    // the Ring Mod's 2× path makes dry and wet 7 samples late; the other three report none
    CHECK (makeEffect (ring)->latencySamples() == 7 && makeEffect (f)->latencySamples() == 0 && makeEffect (d)->latencySamples() == 0
           && makeEffect (c)->latencySamples() == 0);
}

TEST (parity190qzkhs_ring_mod_2x_path)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // mix 0: the input through the two half-bands and the 15 Hz high pass, 7 samples late — a 1 kHz tone comes back
    // within −30 dB of itself (the high pass turns it by about 1.2°: −33.5 dB)
    auto dry = load ("kHs Ring Mod", state ("\"mix\": \"0\", \"frequency\": \"440\", \"spread\": \"0\", \"bias\": \"0\", \"rectify\": \"0\", \"$type\": \"ring_mod\""), n);
    std::vector<float> t (24000);
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i] = 0.4f * (float) std::sin (2.0 * M_PI * 1000.0 * (double) i / 48000.0);
    const auto z = parity190nbkhs::run (dry, t);
    double e = 0.0, s = 0.0;
    for (std::size_t i = 12000; i < t.size(); ++i)
        e += ((double) z[i] - t[i - 7]) * ((double) z[i] - t[i - 7]), s += (double) t[i - 7] * t[i - 7];
    CHECK (10.0 * std::log10 (e / s) < -30.0);
    // mix 1 at 440 Hz on a constant: the 440 Hz modulator alone (the high pass takes no DC, the input has only DC)
    auto ring = load ("kHs Ring Mod", state ("\"mix\": \"1\", \"frequency\": \"440\", \"spread\": \"0\", \"bias\": \"0\", \"rectify\": \"0\", \"$type\": \"ring_mod\""), n);
    const auto y = parity190nbkhs::run (ring, std::vector<float> (24000, 0.5f));
    double p440 = 0.0, mean = 0.0;
    std::vector<float> tail (y.begin() + 12000, y.end());
    for (float v : tail)
        mean += v;
    p440 = parity190qzkhs::dftDb (tail, 440.0) - 20.0 * std::log10 ((double) tail.size());
    CHECK (std::abs (mean / (double) tail.size()) < 1e-3);
    CHECK (std::abs (p440 - 20.0 * std::log10 (0.25)) < 0.1); // |0.5 · sin| at 440 Hz: amplitude 0.5 → |X| / N = 0.25
    // the half-band's odd taps (LYNX's own Kaiser design) sum to ¼: unity at DC with the centre tap ½ and the other side
    double sum = 0.0;
    for (float v : khsdsp::Engine::ringMod2Taps())
        sum += v;
    CHECK (std::abs (sum - 0.25) < 1e-7);
}

TEST (parity190qzkhs_formant_filter_cookbook_pair)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // Lows and Highs on, Q 4: peaks of 20 log10 (4) = 12.04 dB at both cutoffs (far apart: each at its own); unity far away
    auto f = load ("kHs Formant Filter", state ("\"q\": \"4\", \"cutoff_1\": \"5000\", \"cutoff_2\": \"200\", \"lows\": \"true\", \"highs\": \"true\", \"$type\": \"vocal_filter\""), n);
    std::vector<float> x (48000, 0.0f);
    x[0] = 1.0f;
    const auto h = parity190nbkhs::run (f, x);
    CHECK (std::abs (parity190qzkhs::dftDb (h, 200.0) - 12.04) < 0.15);
    CHECK (std::abs (parity190qzkhs::dftDb (h, 5000.0) - 12.04) < 0.15);
    CHECK (std::abs (parity190qzkhs::dftDb (h, 1000.0)) < 1.0);
    // Highs off: a low pass at the higher cutoff (Q 4: +12 dB at it), −40 dB an octave and more above
    auto lp = load ("kHs Formant Filter", state ("\"q\": \"4\", \"cutoff_1\": \"2000\", \"cutoff_2\": \"200\", \"lows\": \"true\", \"highs\": \"false\", \"$type\": \"vocal_filter\""), n);
    const auto g = parity190nbkhs::run (lp, x);
    CHECK (parity190qzkhs::dftDb (g, 16000.0) < -30.0);
}

TEST (parity190qzkhs_delay_echoes_pan_bounce_and_duck)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // 0.2 s: each echo round (0.2 · 48 000 − 7) + 8 = 9 601 samples after the last; feedback 0.5; the mix is linear
    auto d = load ("kHs Delay", state (parity190qzkhs::delayBody ("0.2", "0.5", "0.5")), n);
    std::vector<float> x (40000, 0.0f), r;
    x[4800] = 0.5f;
    const auto y = parity190nbkhs::run (d, x, &r);
    CHECK (y[4800] == 0.25f && std::abs (y[4800 + 9601] - 0.25f) < 1e-6f && std::abs (y[4800 + 19202] - 0.125f) < 1e-6f);
    CHECK (std::abs (y[4800 + 9600]) < 1e-9f && std::abs (y[4800 + 9602]) < 1e-9f && std::abs (r[4800 + 9601] - 0.25f) < 1e-6f); // (the tone shelf's rounding: ~1e-18 around the echo)
    // pan hard left: √2 cos 0 into the left line, nothing into the right
    auto pl = load ("kHs Delay", state (parity190qzkhs::delayBody ("0.2", "0", "1", "-1")), n);
    const auto yl = parity190nbkhs::run (pl, x, &r);
    CHECK (std::abs (yl[4800 + 9601] - 0.5f * 1.41421356f) < 1e-5f && std::abs (r[4800 + 9601]) < 1e-6f);
    // bounce: the echoes cross the channels each pass (the left line takes the right input through the left pan gain:
    // a mono impulse panned hard left echoes left first, then right)
    auto bo = load ("kHs Delay", state (parity190qzkhs::delayBody ("0.2", "0.5", "1", "-1", "true")), n);
    const auto yb = parity190nbkhs::run (bo, x, &r);
    CHECK (yb[4800 + 9601] > 0.7f && std::abs (r[4800 + 9601]) < 1e-6f);
    CHECK (std::abs (yb[4800 + 19202]) < 1e-6f && r[4800 + 19202] > 0.3f);
    // duck 100 % (an impulse after 0.6 s of silence, as the references): the first echo is ducked away, the later ones
    // come back
    auto du = load ("kHs Delay", state (parity190qzkhs::delayBody ("0.2", "0.5", "0.5", "0", "false", "1")), n);
    std::vector<float> xd (60000, 0.0f);
    xd[28800] = 0.5f;
    const auto yd = parity190nbkhs::run (du, xd);
    CHECK (std::abs (yd[28800 + 9601]) < 1e-6f && yd[28800 + 19202] > 0.05f && yd[28800 + 19202] < 0.125f);
}

TEST (parity190qzkhs_clipper_knee_and_gains)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // threshold 0.5, knee 0.5: passes up to 0.25, holds 0.5 from 0.75, the quadratic knee between (at 0.5: 0.4375)
    auto c = load ("kHs Clipper", state ("\"threshold\": \"0.5\", \"knee\": \"0.5\", \"pre_gain\": \"1\", \"post_gain\": \"2\", \"$type\": \"clipper\""), n);
    const auto y = parity190nbkhs::run (c, std::vector<float> { 0.2f, -0.2f, 0.5f, -0.5f, 0.9f, -3.0f });
    CHECK (y[0] == 0.4f && y[1] == -0.4f);
    CHECK (std::abs (y[2] - 0.875f) < 1e-6f && std::abs (y[3] + 0.875f) < 1e-6f);
    CHECK (y[4] == 1.0f && y[5] == -1.0f);
    // pre gain 10 into threshold 1, knee 0: a hard clip at ±1
    auto h = load ("kHs Clipper", state ("\"threshold\": \"1\", \"knee\": \"0\", \"pre_gain\": \"10\", \"post_gain\": \"1\", \"$type\": \"clipper\""), n);
    const auto z = parity190nbkhs::run (h, std::vector<float> { 0.05f, 0.2f, -0.2f });
    CHECK (std::abs (z[0] - 0.5f) < 1e-6f && z[1] == 1.0f && z[2] == -1.0f);
}
