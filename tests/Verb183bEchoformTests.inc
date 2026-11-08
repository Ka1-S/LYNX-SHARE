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
