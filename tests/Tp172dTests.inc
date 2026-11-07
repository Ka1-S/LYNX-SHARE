// Round tp172d: Auto-pan's Rhythm voicing gets PanMan's default shape, the ramp (measured black-box on the trial:
// right to left over the period, back over 45 ms + Smoothing × a quarter of the period)
namespace tp172d
{
// the pan angle (0 = left, 90 = right) of a steady 1 kHz tone through the Rhythm voicing, read over 50 ms at `at` (s)
inline std::vector<double> rampAngles (float smoothing, const std::vector<double>& at, float pattern = 0.5f)
{
    using namespace lynx;
    auto p = makeEffectSlot (EffectKind::autoPan).params;
    constexpr int rb = autopan::count;
    p[rb] = 1.0f;                    // the Rhythm voicing
    p[rb + 1] = 0.465980f;           // 0.25 Hz
    p[rb + 2] = 0.0f;                // free
    p[rb + 4] = 180.0f / 210.0f;     // full width
    p[rb + 5] = 0.5f;                // centred
    p[rb + 6] = smoothing;
    p[rb + 7] = pattern;             // the ramp (◀; 0.75 = ▶)
    p[rb + 9] = 0.0f;                // start phase
    p[rb + 10] = 0.0f;               // digital
    auto fx = makeEffect (EffectKind::autoPan);
    fx->prepare (48000.0);
    const int n = (int) (48000.0 * 4.5);
    std::vector<float> a ((std::size_t) n), b;
    for (int i = 0; i < n; ++i)
        a[(std::size_t) i] = (float) (0.1 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
    b = a;
    for (int i = 0; i < n; i += 256)
        fx->process (a.data() + i, b.data() + i, std::min (256, n - i), p, 120.0);
    std::vector<double> out;
    for (const auto t : at)
    {
        double sl = 0.0, sr = 0.0;
        for (int i = (int) ((t - 0.025) * 48000.0); i < (int) ((t + 0.025) * 48000.0); ++i)
        {
            sl += (double) a[(std::size_t) i] * a[(std::size_t) i];
            sr += (double) b[(std::size_t) i] * b[(std::size_t) i];
        }
        out.push_back (std::atan2 (std::sqrt (sr), std::sqrt (sl)) * 180.0 / 3.14159265358979);
    }
    return out;
}
} // namespace tp172d

TEST (autopan_ramp_pattern_tp172d)
{
    using namespace lynx;
    constexpr int rb = autopan::count;
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 7, 0.0f, 120.0) == "left / right");
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 7, 0.5f, 120.0) == "ramp");
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 7, 1.0f, 120.0) == "8 steps");
    CHECK (formatEffectParam (EffectKind::autoPan, rb + 7, 0.75f, 120.0) == "ramp right");
    // the ▶ key: the same ramp, left to right
    {
        auto r = tp172d::rampAngles (0.0f, { 1.0, 2.0, 3.0 }, 0.75f);
        CHECK (std::abs (r[0] - 21.7) < 1.5 && std::abs (r[1] - 44.7) < 1.5 && std::abs (r[2] - 67.2) < 1.5);
    }
    // Smoothing 0: a straight line from the right to the left over the 4 s period, then a quick return
    const auto a = tp172d::rampAngles (0.0f, { 1.0, 2.0, 3.0, 4.1 });
    std::printf ("  Auto-pan ramp: %.1f %.1f %.1f %.1f deg\n", a[0], a[1], a[2], a[3]);
    CHECK (std::abs (a[0] - 68.3) < 1.5 && std::abs (a[1] - 45.3) < 1.5 && std::abs (a[2] - 22.8) < 1.5);
    CHECK (a[3] > 85.0); // back on the right 0.1 s after the wrap
    // Smoothing 0.5: the return takes 45 ms + 0.5 s — a quarter of a second in, it is still on its way back
    const auto s = tp172d::rampAngles (0.5f, { 0.25, 1.0 });
    CHECK (s[0] > 30.0 && s[0] < 52.0);
    CHECK (s[1] > 60.0);
}

// round tp172d (look slice): PanMan's INPUT knob on the Rhythm voicing's level pod — the voicing now reads Auto-pan's
// Input (-24 … +24 dB; the default 0 dB leaves it as it was)
TEST (autopan_rhythm_input_tp172d)
{
    using namespace lynx;
    auto level = [] (float input)
    {
        auto p = makeEffectSlot (EffectKind::autoPan).params;
        constexpr int rb = autopan::count;
        p[rb] = 1.0f;          // the Rhythm voicing
        p[rb + 10] = 0.0f;     // digital
        p[autopan::inputGain] = input;
        auto fx = makeEffect (EffectKind::autoPan);
        fx->prepare (48000.0);
        const int n = 24000;
        std::vector<float> a ((std::size_t) n), b;
        for (int i = 0; i < n; ++i)
            a[(std::size_t) i] = (float) (0.01 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
        b = a;
        for (int i = 0; i < n; i += 256)
            fx->process (a.data() + i, b.data() + i, std::min (256, n - i), p, 120.0);
        double e = 0.0;
        for (int i = n / 2; i < n; ++i)
            e += (double) a[(std::size_t) i] * a[(std::size_t) i] + (double) b[(std::size_t) i] * b[(std::size_t) i];
        return 10.0 * std::log10 (e + 1.0e-30);
    };
    const auto at0 = level (0.5f), up = level (0.75f), down = level (0.25f);
    std::printf ("  Auto-pan rhythm input: +12 dB -> %+.2f dB, -12 dB -> %+.2f dB\n", up - at0, down - at0);
    CHECK (std::abs ((up - at0) - 12.0) < 0.5);
    CHECK (std::abs ((down - at0) + 12.0) < 0.5);
}
