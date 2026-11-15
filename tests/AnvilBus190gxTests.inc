// --- Round 190gx: Anvil Bus refitted on Live 12.4's second Drum Buss render set (round 190gu: 46 cases at 44.1 / 48 /
// 96 kHz; /mnt/e/lynx-work/livefacts190gx/db/REPORT.md). The numbers in the checks are Live's own measurements.

namespace anvil190gx
{
using namespace lynx;

// the engine on a stereo-identical signal (256-sample blocks) at `rate`; the output, latency removed
inline std::vector<float> run (const EffectParams& p, std::vector<float> x, double rate = 48000.0)
{
    anvil::Engine e;
    e.prepare (rate);
    auto r = x;
    for (int at = 0; at < (int) x.size(); at += 256)
        e.process (x.data() + at, r.data() + at, std::min (256, (int) x.size() - at), p.data());
    x.erase (x.begin(), x.begin() + anvil::Engine::latency());
    return x;
}
inline double rmsDb (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (auto i = from; i < to && i < x.size(); ++i)
        s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double) std::max<std::size_t> (1, to - from) + 1e-30);
}
// a sustained chord of detuned band-limited saws (A3 / C#4 / E4, like the measurement's pad), peak about 0.5
inline std::vector<float> pad (double rate, double seconds)
{
    std::vector<float> x ((std::size_t) (rate * seconds));
    const double roots[3] { 220.0, 277.18, 329.63 }, detune[3] { 0.997, 1.0, 1.003 };
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double v = 0.0;
        for (const auto f0 : roots)
            for (const auto d : detune)
                for (int h = 1; h <= 20; ++h)
                    v += std::sin (2.0 * 3.141592653589793 * f0 * d * h * (double) i / rate) / h;
        x[i] = (float) (0.02 * v);
    }
    return x;
}
} // namespace anvil190gx

TEST (anvil190gx_comp_sits_before_the_curve)
{
    using namespace lynx;
    namespace a = anvil;
    using anvil190fk::params;
    using anvil190fk::sineOut;
    CHECK (a::Tuning {}.compPre == 1);
    // Comp + Drive 50 % (Soft), the 1 kHz staircase [Live 190gu: −36 → −16.03, −24 → −5.03, −12 → −1.16, 0 → −0.57 dBFS]
    const auto p = params ({ { a::comp, 1.0f }, { a::drive, 0.5f } });
    CHECK (std::abs (sineOut (p, -36.0) - (-16.03)) < 0.5);
    CHECK (std::abs (sineOut (p, -24.0) - (-5.03)) < 0.5);
    CHECK (std::abs (sineOut (p, -12.0) - (-1.16)) < 0.5);
    // Comp + Trim −12 dB [Live: −36 → −34.90, −12 → −11.08, 0 → −4.96 dBFS]
    const auto t = params ({ { a::comp, 1.0f }, { a::trim, a::trimValue (-12.0) } });
    CHECK (std::abs (sineOut (t, -36.0) - (-34.90)) < 0.3 && std::abs (sineOut (t, -12.0) - (-11.08)) < 0.3 && std::abs (sineOut (t, 0.0) - (-4.96)) < 0.4);
}

TEST (anvil190gx_dry_wet_blends_the_raw_input_with_comp_on)
{
    // Live 190gu: Comp on, Dry/Wet 0.5 = 0.5 · input + 0.5 · the Comp-on device, sample for sample (within 0.005 dB on
    // every step of the staircase); LYNX's blend is that exactly
    using namespace lynx;
    namespace a = anvil;
    std::vector<float> x (48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.4 * std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) i / 48000.0) * std::min (1.0, (double) i / 4800.0));
    const auto wet = anvil190gx::run (anvil190fk::params ({ { a::comp, 1.0f } }), x);
    const auto half = anvil190gx::run (anvil190fk::params ({ { a::comp, 1.0f }, { a::dryWet, 0.5f } }), x);
    double worst = 0.0;
    for (std::size_t i = 1000; i < half.size(); ++i)
        worst = std::max (worst, std::abs ((double) half[i] - (0.5 * x[i] + 0.5 * wet[i])));
    CHECK (worst < 1.0e-5);
}

TEST (anvil190gx_boom_amount_sets_the_ring)
{
    using namespace lynx;
    namespace a = anvil;
    using anvil190fk::params;
    using anvil190fk::relGain;
    // small-signal lift at 50 Hz (decay 1.0) grows with the amount [Live noise TF re the neutral device: +4.0 dB at 0.25,
    // +14.1 at 0.5, ≥ +20.8 at 1.0 (the measurement's 2.9 Hz bins under-read the 1.0 peak)]
    const auto at = [] (float amount) {
        return relGain (params ({ { a::boom, amount }, { a::boomFreq, a::boomHzValue (50.0) }, { a::boomDecay, 1.0f } }), 50.0);
    };
    const auto g25 = at (0.25f), g50 = at (0.5f), g100 = at (1.0f);
    CHECK (g25 > 2.0 && g25 < 7.0);
    CHECK (std::abs (g50 - 14.1) < 2.5);
    CHECK (g100 > 20.0);
    // the ring after an impulse lasts at 1.0 [Live: 50 Hz still −42 → −50 dB from 0.02 to 0.4 s after it, about −20 dB/s]
    std::vector<float> x (48000 * 2, 0.0f);
    x[4800] = 1.0f;
    const auto y = anvil190gx::run (params ({ { a::boom, 1.0f }, { a::boomFreq, a::boomHzValue (50.0) }, { a::boomDecay, 1.0f } }), x);
    const auto early = anvil190gx::rmsDb (y, 4800 + 960, 4800 + 960 + 2400), late = anvil190gx::rmsDb (y, 4800 + 14400, 4800 + 14400 + 2400);
    CHECK (early - late < 25.0); // 0.25 s apart: Live −6 dB; 190fk's fixed Q fell ~60 dB
}

TEST (anvil190gx_soft_75_and_type_eq_depth)
{
    using namespace lynx;
    namespace a = anvil;
    using anvil190fk::params;
    using anvil190fk::sineOut;
    using anvil190fk::relGain;
    // Soft 75 %: small signal +10.22 dB, 0 dBFS folds to −12.1 dBFS [Live 190gu a075s]
    const auto s75 = params ({ { a::drive, 0.75f } });
    CHECK (std::abs (sineOut (s75, -60.0) - (-60.0 + 10.22)) < 0.15);
    CHECK (sineOut (s75, 0.0) < -5.0);
    // Medium 50 %: no dip around 180 Hz yet (Live's noise TF keeps the neutral shape; the −16 dB bell is a 100 % thing)
    const auto m50 = params ({ { a::drive, 0.5f }, { a::driveType, 0.5f } }), m0 = params ({ { a::driveType, 0.5f } });
    const auto dip = (relGain (m50, 180.0) - relGain (m0, 180.0)) - (relGain (m50, 1000.0) - relGain (m0, 1000.0));
    CHECK (std::abs (dip) < 1.5);
}

TEST (anvil190gx_rate_independent)
{
    // Live's three render rates agree within 0.25 dB rms; LYNX's engine: the same settings on the pad at 44.1 / 48 / 96 kHz
    using namespace lynx;
    namespace a = anvil;
    const auto p = anvil190fk::params ({ { a::drive, 0.5f }, { a::comp, 1.0f }, { a::boom, 0.5f }, { a::transients, a::transientsValue (-0.5) } });
    double level[3] {};
    const double rates[3] { 44100.0, 48000.0, 96000.0 };
    for (int k = 0; k < 3; ++k)
    {
        const auto y = anvil190gx::run (p, anvil190gx::pad (rates[k], 1.5), rates[k]);
        level[k] = anvil190gx::rmsDb (y, (std::size_t) (rates[k] * 0.5), (std::size_t) (rates[k] * 1.4));
    }
    CHECK (std::abs (level[0] - level[1]) < 0.25 && std::abs (level[2] - level[1]) < 0.25);
}

TEST (anvil190gx_transients_move_a_steady_pad)
{
    // Live 190gu: a sustained pad's rms re the neutral device: −1.0 → −3.92 dB, +0.25 → +1.15 dB. Anvil Bus holds the
    // direction with part of the depth (a deeper level term moves the drum loop's measured segments off)
    using namespace lynx;
    namespace a = anvil;
    const auto x = anvil190gx::pad (48000.0, 1.5);
    const auto level = [&] (double t) {
        const auto y = anvil190gx::run (anvil190fk::params ({ { a::transients, a::transientsValue (t) } }), x);
        return anvil190gx::rmsDb (y, 24000, 67200);
    };
    const auto neutral = level (0.0);
    CHECK (level (-1.0) - neutral < -0.4);
    CHECK (level (0.25) - neutral > 0.3);
}
