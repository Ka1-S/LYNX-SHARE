// Round 190nb: Kilohearts parity on the licensed kHs Essentials — the five snapins without a LYNX effect of their own
// (Ring Mod, Reverser, Resonator, Phase Distortion, Shaper) as hidden laws of Effector (core/KiloheartsDsp.h), and the
// measured maps of the earlier stand-ins (core/KiloheartsStates.inc, docs/analise/REA_rodada190nb.md).
// Uses parity190lnkhs::state / load / text / named (tests/Parity190lnKhsTests.inc).
#include "core/KiloheartsDsp.h"

namespace parity190nbkhs
{
inline std::vector<float> run (const lynx::EffectSlot& s, std::vector<float> l, std::vector<float>* rOut = nullptr, double songBeats = -1.0)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (48000.0);
    fx->reset();
    std::vector<float> r = l;
    for (std::size_t off = 0; off < l.size(); off += 512)
    {
        const int n = (int) std::min<std::size_t> (512, l.size() - off);
        if (songBeats >= 0.0)
            fx->setSongPosition ((songBeats + (double) off / 24000.0) * 960.0); // 120 BPM: two beats a second
        fx->process (l.data() + off, r.data() + off, n, s.params, 120.0);
    }
    if (rOut != nullptr)
        *rOut = r;
    return l;
}
} // namespace parity190nbkhs

TEST (parity190nbkhs_five_snapins_open_on_effector_laws_and_effector_stays_fl)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    namespace k = khsdsp;
    int n = 0;
    auto ring = load ("kHs Ring Mod", state ("\"mix\": \"1\", \"frequency\": \"440\", \"spread\": \"0.5\", \"bias\": \"0.25\", \"rectify\": \"-0.5\", \"$type\": \"ring_mod\""), n);
    CHECK (ring.kind == EffectKind::effector && n > 5);
    CHECK (k::lawOf (ring.params[(std::size_t) k::lawParam]) == k::Law::ringMod);
    CHECK (std::abs (k::enc::ringHz (ring.params[(std::size_t) k::valueParam]) - 440.0) < 0.01);
    CHECK (std::abs (ring.params[(std::size_t) k::valueParam + 3] - 0.25f) < 1e-6f); // rectify −0.5 → (r + 1) / 2
    CHECK (text (ring, 0) == "Ring");
    auto rev = load ("kHs Reverser", state ("\"delay_numerator\": \"4\", \"delay_time\": \"0.2\", \"delay_denominator\": \"denominator_8t\", "
                                            "\"sync_mode\": \"sync\", \"mix\": \"0.5\", \"crossfade\": \"0.1\", \"$type\": \"reverser\""), n);
    CHECK (k::lawOf (rev.params[(std::size_t) k::lawParam]) == k::Law::reverser);
    CHECK (std::abs (rev.params[(std::size_t) k::valueParam + 3] - 0.5f) < 1e-6f); // 8t = index 3 of 0 … 6
    CHECK (std::abs (rev.params[(std::size_t) named (rev, "Wet")] - 0.5f) < 1e-6f);
    auto res = load ("kHs Resonator", state ("\"mix\": \"1\", \"pitch\": \"60\", \"decay\": \"0.1\", \"intensity\": \"1\", \"harmonics\": \"true\", \"$type\": \"resonator\""), n);
    CHECK (k::lawOf (res.params[(std::size_t) k::lawParam]) == k::Law::resonator);
    CHECK (std::abs (k::enc::resonatorDecay (res.params[(std::size_t) k::valueParam + 1]) - 0.1) < 1e-4);
    auto pd = load ("kHs Phase Distortion", state ("\"drive\": \"0.5\", \"spread\": \"0.25\", \"tone\": \"640\", \"mix\": \"1\", \"bias\": \"-0.25\", \"normalize\": \"0.5\", \"$type\": \"phase_distortion\""), n);
    CHECK (k::lawOf (pd.params[(std::size_t) k::lawParam]) == k::Law::phaseDistortion);
    CHECK (std::abs (pd.params[(std::size_t) k::valueParam + 1] - 1.0f) < 1e-6f); // spread 0.25 (90°) → 1
    CHECK (std::abs (k::enc::toneHz (pd.params[(std::size_t) k::valueParam + 3]) - 640.0) < 0.1);
    auto sh = load ("kHs Shaper", state ("\"drive\": \"2\", \"mix\": \"1\", \"dcFilter\": \"false\", \"loop\": \"mirror\", \"curve\": {\"$type\": \"data_slot\", "
                                         "\"data\": {\"$type\": \"segmented_curve\", \"segments\": [{\"x\": \"-1\", \"y\": \"-1\"}, {\"x\": \"0\", \"y\": \"0.5\"}, {\"x\": \"1\", \"y\": \"1\"}]}}, "
                                         "\"$type\": \"shaper\""), n);
    CHECK (k::lawOf (sh.params[(std::size_t) k::lawParam]) == k::Law::shaper);
    CHECK (std::lround (16.0f * sh.params[(std::size_t) k::shaperCountParam]) == 3);
    CHECK (std::abs (sh.params[(std::size_t) k::shaperPointParam + 3] - 0.75f) < 1e-6f); // the middle point's y 0.5
    // a new Effector (and every project saved before) has no law and saves its 12 named parameters only
    const auto e = makeEffectSlot (EffectKind::effector);
    CHECK (k::lawOf (e.params[(std::size_t) k::lawParam]) == k::Law::none);
    CHECK (savedEffectParamCount (e) == 12);
    CHECK (savedEffectParamCount (ring) > 12);
}

TEST (parity190nbkhs_resonator_is_a_comb_of_one_period_and_its_allpass_blend)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // 480 Hz (a whole period of 100 samples: the interpolation is exact), intensity 0 = an all-pass (energy kept)
    auto ap = load ("kHs Resonator", state ("\"mix\": \"1\", \"pitch\": \"70.50637\", \"decay\": \"0.05\", \"intensity\": \"0\", \"harmonics\": \"false\", \"$type\": \"resonator\""), n);
    std::vector<float> x (48000, 0.0f);
    x[1000] = 1.0f;
    const auto y = parity190nbkhs::run (ap, x);
    double e = 0.0;
    for (float v : y) e += (double) v * v;
    CHECK (std::abs (e - 1.0) < 0.02);
    // intensity 1: the comb's taps only — nothing at the impulse, the first echo one period later (inverted: the saw
    // series' wet sign), each further echo 2^(−P / decay) of the one before
    auto full = load ("kHs Resonator", state ("\"mix\": \"1\", \"pitch\": \"69\", \"decay\": \"0.05\", \"intensity\": \"1\", \"harmonics\": \"false\", \"$type\": \"resonator\""), n);
    const auto z = parity190nbkhs::run (full, x);
    CHECK (std::abs (z[1000]) < 1e-6f);
    double e1 = 0.0, e2 = 0.0;
    for (int i = 1095; i < 1125; ++i) e1 += (double) z[(std::size_t) i] * z[(std::size_t) i];
    for (int i = 1204; i < 1234; ++i) e2 += (double) z[(std::size_t) i] * z[(std::size_t) i];
    const double g = std::exp2 (-(1.0 / 440.0) / 0.05);
    CHECK (std::abs (std::sqrt (e2 / e1) - g) < 0.02);
    double s1 = 0.0;
    for (int i = 1095; i < 1125; ++i) s1 += z[(std::size_t) i];
    CHECK (s1 < -0.5);
}

TEST (parity190nbkhs_ring_mod_reverser_shaper_and_phase_distortion_laws)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // Ring Mod at 100 Hz, rectify +100 %: a constant input becomes 0.5 · |sin|, never negative, peak 0.5
    auto ring = load ("kHs Ring Mod", state ("\"mix\": \"1\", \"frequency\": \"100\", \"spread\": \"0\", \"bias\": \"0\", \"rectify\": \"1\", \"$type\": \"ring_mod\""), n);
    const auto y = parity190nbkhs::run (ring, std::vector<float> (24000, 0.5f));
    float lo = 1.0f, hi = -1.0f;
    for (float v : y) lo = std::min (lo, v), hi = std::max (hi, v);
    CHECK (lo > -1e-6f && std::abs (hi - 0.5f) < 1e-3f);
    // Reverser, 100 ms sections (ms mode), mix 100 %, no crossfade: a ramp comes back reversed, two samples behind
    auto rev = load ("kHs Reverser", state ("\"delay_numerator\": \"4\", \"delay_time\": \"0.1\", \"delay_denominator\": \"denominator_16\", "
                                            "\"sync_mode\": \"ms\", \"mix\": \"1\", \"crossfade\": \"0\", \"$type\": \"reverser\""), n);
    std::vector<float> ramp (24000);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = 0.001f + (float) i * 1e-5f;
    const auto rr = parity190nbkhs::run (rev, ramp);
    // section 2 starts at sample 4800: there it plays x[4800 − 2 − c] for c = 0 …
    CHECK (std::abs (rr[4800 + 100] - ramp[4800 - 2 - 100]) < 1e-6f);
    CHECK (std::abs (rr[4800 + 2000] - ramp[4800 - 2 - 2000]) < 1e-6f);
    // Shaper, +20 dB into "hold" (clipped at ±1), DC filter off: a 0.5 sine comes out at most 1
    auto sh = load ("kHs Shaper", state ("\"drive\": \"10\", \"mix\": \"1\", \"dcFilter\": \"false\", \"loop\": \"hold\", \"$type\": \"shaper\""), n);
    std::vector<float> sine (9600);
    for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = 0.5f * (float) std::sin (2.0 * M_PI * 100.0 * (double) i / 48000.0);
    const auto c = parity190nbkhs::run (sh, sine);
    float peak = 0.0f;
    for (float v : c) peak = std::max (peak, std::abs (v));
    CHECK (peak > 0.99f && peak < 1.01f);
    // Phase Distortion at drive 0: a rotation by the bias only — the analytic pair keeps the input's energy
    auto pd = load ("kHs Phase Distortion", state ("\"drive\": \"0\", \"spread\": \"0\", \"tone\": \"640\", \"mix\": \"1\", \"bias\": \"0.25\", \"normalize\": \"0\", \"$type\": \"phase_distortion\""), n);
    std::vector<float> tone (48000);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.3f * (float) std::sin (2.0 * M_PI * 1000.0 * (double) i / 48000.0);
    const auto p = parity190nbkhs::run (pd, tone);
    double ex = 0.0, ey = 0.0;
    for (std::size_t i = 4800; i < tone.size(); ++i) ex += (double) tone[i] * tone[i], ey += (double) p[i] * p[i];
    CHECK (std::abs (10.0 * std::log10 (ey / ex)) < 0.3);
}

TEST (parity190nbkhs_measured_maps_of_the_earlier_stand_ins)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // kHs Delay: a linear mix (dry 1 − m, wet m) on Echoform's equal-power Mix + Output (the echoes only); one sample late
    auto d = load ("kHs Delay", state ("\"delay_time\": \"0.2\", \"sync_mode\": \"ms\", \"feedback\": \"0.5\", \"mix\": \"0.5\", \"$type\": \"delay\""), n);
    CHECK (std::abs (d.params[(std::size_t) named (d, "Mix")] - 2.0f / 3.0f) < 1e-5f);              // cos (60°) = 0.5 dry
    CHECK (text (d, named (d, "Output")) == "-4.7 dB" || text (d, named (d, "Output")) == "-4.8 dB"); // 0.5 / sin 60° = −4.77 dB
    CHECK (std::abs (echosuite::echoform::timeMs (d.params[(std::size_t) named (d, "Time L")]) - (200.0 + 1000.0 / 48000.0)) < 1e-3);
    // kHs Formant Filter: two cookbook peaks of 20·log10 (Q) dB at Q (Q 4: +12 dB), no output trim
    auto f = load ("kHs Formant Filter", state ("\"q\": \"4\", \"cutoff_1\": \"1500\", \"cutoff_2\": \"550\", \"lows\": \"true\", \"highs\": \"false\", \"$type\": \"vocal_filter\""), n);
    CHECK (std::abs (facet::hz (f.params[(std::size_t) facet::param (0, 1)]) - 550.0) < 1.0);
    CHECK (facet::shape (f.params[(std::size_t) facet::param (1, 0)]) == facet::Shape::highCut);
    CHECK (std::abs (facet::gainDb (f.params[(std::size_t) facet::param (0, 2)]) - 12.04) < 0.05);
    // kHs Flanger: dry cos / wet sin of mix × 45°, no cross-feed
    auto fl = load ("kHs Flanger", state ("\"delay\": \"0.001\", \"mix\": \"1\", \"mod_depth\": \"0.001\", \"mod_rate\": \"0.3\", \"feedback\": \"0\", \"$type\": \"flanger\""), n);
    CHECK (text (fl, named (fl, "Dry")) == "71%" && text (fl, named (fl, "Wet")) == "71%" && text (fl, named (fl, "Cross")) == "0%");
    // kHs Transient Shaper: Sustain is a steady-state gain of 17.93 dB × sustain, on Strike Shaper's Output
    auto t = load ("kHs Transient Shaper", state ("\"attack\": \"0\", \"pump\": \"0\", \"clip\": \"false\", \"sustain\": \"-0.5\", \"speed\": \"1\", \"$type\": \"transient_shaper\""), n);
    CHECK (text (t, named (t, "Sustain")) == "+0.0 dB");
    CHECK (text (t, named (t, "Output")) == "-8.9 dB" || text (t, named (t, "Output")) == "-9.0 dB"); // −8.97 dB
}
