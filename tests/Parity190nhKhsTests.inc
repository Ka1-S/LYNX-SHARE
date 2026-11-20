// Round 190nh: Kilohearts part 2 — the kHs snapins whose DSP round 190nb decompiled but did not build: Comb Filter,
// Chorus, Trance Gate, Phaser, Distortion, Bitcrush, Frequency Shifter and Ensemble as hidden laws of Effector
// (core/KiloheartsDsp.h, codes 41 … 48 / 64), translated from their states (core/KiloheartsStates.inc).
// docs/analise/REA_rodada190nh.md. Uses parity190lnkhs::state / load / text / named and parity190nbkhs::run.

namespace parity190nhkhs
{
inline double energy (const std::vector<float>& v)
{
    double e = 0.0;
    for (float x : v)
        e += (double) x * x;
    return e;
}
inline bool finite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}
inline std::vector<float> tone (int n, double hz, float amp)
{
    std::vector<float> x ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        x[(std::size_t) i] = amp * (float) std::sin (6.283185307179586 * hz * i / 48000.0);
    return x;
}
} // namespace parity190nhkhs

TEST (parity190nhkhs_law_codes_keep_round_190nb_values_and_map_the_new_snapins)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    namespace k = khsdsp;
    // round 190nb's five stay k / 8 (the same floats every saved slot holds)
    CHECK (k::lawValue (k::Law::ringMod) == 0.125f && k::lawValue (k::Law::shaper) == 0.625f);
    for (int c = 1; c <= 5; ++c)
        CHECK ((int) k::lawOf ((float) c / 8.0f) == c);
    CHECK (k::lawOf (0.0f) == k::Law::none && k::lawOf (0.3f) == k::Law::none && k::lawOf (1.0f) == k::Law::none);
    for (auto l : { k::Law::comb, k::Law::chorus, k::Law::tranceGate, k::Law::phaser, k::Law::distortion, k::Law::bitcrush,
                    k::Law::freqShift, k::Law::ensemble })
        CHECK (k::lawOf (k::lawValue (l)) == l);
    int n = 0;
    struct Case { const char* name; std::string body; k::Law law; };
    const Case cases[] {
        { "kHs Comb Filter", "\"cutoff\": \"440\", \"polarity\": \"true\", \"mix\": \"0.5\", \"stereo\": \"true\", \"$type\": \"comb_filter\"", k::Law::comb },
        { "kHs Chorus", "\"delay\": \"0.004\", \"rate\": \"0.6\", \"depth\": \"0.004\", \"spread\": \"1\", \"mix\": \"1\", \"multiTap\": \"false\", \"$type\": \"chorus\"", k::Law::chorus },
        { "kHs Phaser", "\"rate\": \"0.6\", \"order\": \"3\", \"cutoff\": \"500\", \"depth\": \"0.5\", \"spread\": \"0.5\", \"mix\": \"1\", \"$type\": \"phaser\"", k::Law::phaser },
        { "kHs Distortion", "\"dc_filter\": \"true\", \"dynamics\": \"0.5\", \"drive\": \"2\", \"bias\": \"0\", \"spread\": \"0\", \"type\": \"hardclip\", \"mix\": \"1\", \"$type\": \"distortion\"", k::Law::distortion },
        { "kHs Bitcrush", "\"dither_amount\": \"0\", \"bit_depth\": \"8\", \"sample_rate\": \"6000\", \"adc_quality\": \"1\", \"mix2\": \"1\", \"dac_quality\": \"0\", \"quantize\": \"1\", \"$type\": \"bitcrush\"", k::Law::bitcrush },
        { "kHs Frequency Shifter", "\"shift\": \"-250\", \"$type\": \"frequency_shifter\"", k::Law::freqShift },
        { "kHs Ensemble", "\"spread\": \"0.5\", \"voices\": \"6\", \"motion\": \"symmetric\", \"detune\": \"0.25\", \"mix\": \"1\", \"$type\": \"unison\"", k::Law::ensemble },
    };
    for (const auto& c : cases)
    {
        const auto s = load (c.name, state (c.body), n);
        CHECK (s.kind == EffectKind::effector && n > 3);
        CHECK (k::lawOf (s.params[(std::size_t) k::lawParam]) == c.law);
    }
    const auto comb = load ("kHs Comb Filter", state (cases[0].body), n);
    CHECK (std::abs (k::enc::logv (comb.params[(std::size_t) k::valueParam], k::enc::combLo, k::enc::combHi) - 440.0) < 0.01);
    CHECK (comb.params[(std::size_t) k::valueParam + 1] == 1.0f && comb.params[(std::size_t) k::valueParam + 2] == 1.0f);
    CHECK (std::abs (comb.params[(std::size_t) named (comb, "Wet")] - 0.5f) < 1e-6f);
    const auto dist = load ("kHs Distortion", state (cases[3].body), n);
    CHECK (std::lround (5.0f * dist.params[(std::size_t) k::valueParam + 3]) == 4); // hard clip
    CHECK (std::abs (k::enc::logv (dist.params[(std::size_t) k::valueParam], k::enc::driveLo, k::enc::driveHi) - 2.0) < 1e-4);
    const auto fsh = load ("kHs Frequency Shifter", state (cases[5].body), n);
    CHECK (std::abs (10000.0 * (fsh.params[(std::size_t) k::valueParam] - 0.5) + 250.0) < 0.01);
    // the Comb Filter reports its read's 7 samples of latency; every other law and FL's Effector none
    CHECK (makeEffect (comb)->latencySamples() == 7);
    CHECK (makeEffect (dist)->latencySamples() == 0 && makeEffect (makeEffectSlot (EffectKind::effector))->latencySamples() == 0);
}

TEST (parity190nhkhs_comb_filter_is_the_dry_and_one_tap_a_period_later)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // 480 Hz: a whole period of 100 samples, so the windowed sinc is one tap; mix 0.5, Minus polarity, Stereo
    auto c = load ("kHs Comb Filter", state ("\"cutoff\": \"480\", \"polarity\": \"true\", \"mix\": \"0.5\", \"stereo\": \"true\", \"$type\": \"comb_filter\""), n);
    std::vector<float> x (4000, 0.0f), r;
    x[1000] = 1.0f;
    const auto y = parity190nbkhs::run (c, x, &r);
    CHECK (std::abs (y[1007] - 1.0f) < 1e-6f);    // the dry, 7 samples late
    CHECK (std::abs (y[1107] + 0.5f) < 1e-5f);    // −mix one period later
    CHECK (std::abs (r[1107] - 0.5f) < 1e-5f);    // Stereo: the right channel's tap inverted
    double rest = 0.0;
    for (int i = 0; i < 4000; ++i)
        if (i != 1007 && i != 1107)
            rest += (double) y[(std::size_t) i] * y[(std::size_t) i];
    CHECK (rest < 1e-9);
}

TEST (parity190nhkhs_trance_gate_follows_the_song_and_its_steps)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    std::string steps;
    for (int i = 0; i < 16; ++i)
        steps += std::string (i ? ", " : "") + "{\"on\": \"" + (i % 2 == 0 ? "true" : "false") + "\", \"tie\": \"false\"}";
    int n = 0;
    auto t = load ("kHs Trance Gate", state ("\"attack\": \"0.001\", \"decay\": \"0.01\", \"sustain\": \"1\", \"depth\": \"1\", \"release\": \"0.001\", "
                                             "\"rate\": \"one_16th\", \"swing\": \"0.5\", \"pattern\": \"1\", \"$type\": \"trance_gate\", "
                                             "\"patterns\": [{\"length\": \"16\", \"steps\": [" + steps + "]}]"), n);
    // 1/16 at 120 BPM: a step is 6000 samples; even steps open (sustain 1), odd ones close (release 1 ms)
    std::vector<float> x (48000, 0.5f);
    const auto out = parity190nbkhs::run (t, x, nullptr, 0.0);
    CHECK (std::abs (out[3000] - 0.5f) < 1e-3f);   // inside step 0 (open)
    CHECK (std::abs (out[9000]) < 1e-3f);          // inside step 1 (closed)
    CHECK (std::abs (out[15000] - 0.5f) < 1e-3f);  // step 2
    CHECK (out[6010] < out[5990]);                 // the release starts at the step's edge
}

TEST (parity190nhkhs_modulation_laws_and_shapers_stay_finite_and_bounded)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    using namespace parity190nhkhs;
    int n = 0;
    const auto x = tone (96000, 997.0, 0.5f);
    const char* names[] { "kHs Chorus", "kHs Phaser", "kHs Distortion", "kHs Bitcrush", "kHs Frequency Shifter", "kHs Ensemble" };
    const char* bodies[] {
        "\"delay\": \"0.004\", \"rate\": \"0.6\", \"depth\": \"0.004\", \"spread\": \"1\", \"mix\": \"1\", \"multiTap\": \"true\", \"$type\": \"chorus\"",
        "\"rate\": \"2\", \"order\": \"7\", \"cutoff\": \"500\", \"depth\": \"1\", \"spread\": \"0.5\", \"mix\": \"1\", \"$type\": \"phaser\"",
        "\"dc_filter\": \"true\", \"dynamics\": \"1\", \"drive\": \"10\", \"bias\": \"0.3\", \"spread\": \"0.2\", \"type\": \"sine\", \"mix\": \"1\", \"$type\": \"distortion\"",
        "\"dither_amount\": \"0.5\", \"bit_depth\": \"4\", \"sample_rate\": \"2000\", \"adc_quality\": \"0.5\", \"mix2\": \"1\", \"dac_quality\": \"0.5\", \"quantize\": \"1\", \"$type\": \"bitcrush\"",
        "\"shift\": \"300\", \"$type\": \"frequency_shifter\"",
        "\"spread\": \"1\", \"voices\": \"16\", \"motion\": \"random\", \"detune\": \"1\", \"mix\": \"1\", \"$type\": \"unison\"" };
    for (int i = 0; i < 6; ++i)
    {
        const auto s = load (names[i], state (bodies[i]), n);
        std::vector<float> r;
        const auto y = parity190nbkhs::run (s, x, &r);
        CHECK (finite (y) && finite (r));
        CHECK (energy (y) > 0.05 * energy (x) && energy (y) < 8.0 * energy (x));
    }
    // the shift moves a tone by its amount: 1 kHz + 300 Hz → the energy sits at 1.3 kHz (Goertzel), not at 1 kHz
    const auto fsh = load ("kHs Frequency Shifter", state ("\"shift\": \"300\", \"$type\": \"frequency_shifter\""), n);
    const auto in = tone (48000, 1000.0, 0.5f);
    const auto y = parity190nbkhs::run (fsh, in);
    auto power = [&] (double hz) {
        double c = 0.0, s = 0.0;
        for (int i = 24000; i < 48000; ++i)
        {
            c += y[(std::size_t) i] * std::cos (6.283185307179586 * hz * i / 48000.0);
            s += y[(std::size_t) i] * std::sin (6.283185307179586 * hz * i / 48000.0);
        }
        return c * c + s * s;
    };
    CHECK (power (1300.0) > 100.0 * power (1000.0));
}
