// Round 183b slice 7: Echoform as the ValhallaDelay role, measured black-box on its demo (12 new captures at Mix
// 100 %): in Tape mode at 300 ms and Feedback 70 % each repeat loses 4.3 … 5.6 dB (ours lost 6.5 … 7.9: the tape path
// took 1.9 dB too much on every pass) and the first repeat sits 2.6 dB higher.
TEST (echoform_tape_repeats_verb183b)
{
    using namespace es101;
    namespace ef = es::echoform;
    auto p = echoform (ef::Mode::tape, 300.0);
    p[ef::feedback] = 0.35f; // 70 %
    std::vector<float> in (48000 * 2, 0.0f);
    in[0] = 0.5f;
    const auto y = run (EffectKind::echoform, p, in);
    auto echoDb = [&] (int k) {
        double e = 0.0;
        const auto at = (std::size_t) (k * 0.3 * 48000.0);
        for (std::size_t i = at - 2400; i < at + 7200; ++i)
            e += (double) y.l[i] * y.l[i];
        return 10.0 * std::log10 (e + 1e-30);
    };
    const auto d12 = echoDb (2) - echoDb (1), d23 = echoDb (3) - echoDb (2);
    CHECK (d12 < -3.5 && d12 > -7.0);
    CHECK (d23 < -3.0 && d23 > -6.5);
}

// Round 183b slice 7b: in the Single style one tape transport moves both sides (the ValhallaDelay role's repeats stay
// mono with wow and flutter on: L/R correlation 0.998; two independent swings gave 0.18)
TEST (echoform_single_tape_wow_moves_both_sides_verb183b)
{
    using namespace es101;
    namespace ef = es::echoform;
    auto p = echoform (ef::Mode::tape, 300.0);
    p[ef::style] = ef::styleValue (ef::Style::single);
    p[ef::feedback] = 0.35f;
    p[ef::wow] = p[ef::flutter] = 0.5f;
    std::vector<float> in (48000 * 2, 0.0f);
    in[0] = 0.5f;
    const auto y = run (EffectKind::echoform, p, in);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = 4800; i < y.l.size(); ++i)
        lr += (double) y.l[i] * y.r[i], ll += (double) y.l[i] * y.l[i], rr += (double) y.r[i] * y.r[i];
    CHECK (ll > 1e-6);
    CHECK (lr / std::sqrt (ll * rr + 1e-30) > 0.99);
}
