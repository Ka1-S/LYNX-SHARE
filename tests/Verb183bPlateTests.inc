// Round 183b slice 4: Foil Plate with its voicing off (the ValhallaPlate role), measured black-box on the reference
// (tp172d's settings and 14 new captures with the impulse at −20 dB: Decay, Size, Mod depth, Low / High EQ gain,
// Width, Pre-delay). Width keeps the level; Low EQ gain spans ±12 dB with its half-gain point at ~0.47 × the
// frequency law; High EQ boosts +12 dB but cuts only −4.5 dB.
namespace verb183bplate
{
inline double toneDb (const lynx::EffectParams& p, double hz)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::foilPlate);
    fx->prepare (48000.0);
    const int n = 96000;
    std::vector<float> a ((std::size_t) n), b;
    for (int i = 0; i < n; ++i)
        a[(std::size_t) i] = (float) (0.05 * std::sin (2.0 * 3.14159265358979 * hz * i / 48000.0));
    b = a;
    for (int i = 0; i < n; i += 256)
        fx->process (a.data() + i, b.data() + i, std::min (256, n - i), p, 120.0);
    double e = 0.0;
    for (int i = n / 2; i < n; ++i)
        e += (double) a[(std::size_t) i] * a[(std::size_t) i] + (double) b[(std::size_t) i] * b[(std::size_t) i];
    return 10.0 * std::log10 (e + 1.0e-30);
}
} // namespace verb183bplate

TEST (foil_valhallaplate_laws_verb183b)
{
    using namespace verbs100;
    using lynx::EffectKind;
    auto p = defaults (EffectKind::foilPlate);
    p[0] = 1.0f;
    // Width 0 / 100 / 200 % keeps the level (the reference: −27.4 / −27.5 / −27.2 dB)
    p[4] = 0.0f;
    const auto mono = impulse (EffectKind::foilPlate, p, 3.0);
    p[4] = 1.0f;
    const auto wide = impulse (EffectKind::foilPlate, p, 3.0);
    CHECK (std::abs (energyDb (mono, 0, mono.l.size()) - energyDb (wide, 0, wide.l.size())) < 1.0);
    // Low EQ gain 0 % cuts ~12 dB at 60 Hz (the reference: −11.5; the old ±9 dB law: −8.9)
    p = defaults (EffectKind::foilPlate);
    p[0] = 1.0f;
    const auto flat = verb183bplate::toneDb (p, 60.0);
    p[8] = 0.0f;
    const auto cut = verb183bplate::toneDb (p, 60.0) - flat;
    CHECK (cut < -10.0 && cut > -14.0);
    // High EQ gain 0 % takes only ~3 … 5 dB at 10 kHz, 100 % adds ~+9 … +12 (the reference: −4.1 / +10.3 at 11.4 kHz)
    p[8] = 0.5f;
    const auto top = verb183bplate::toneDb (p, 10000.0);
    p[10] = 0.0f;
    const auto down = verb183bplate::toneDb (p, 10000.0) - top;
    p[10] = 1.0f;
    const auto up = verb183bplate::toneDb (p, 10000.0) - top;
    CHECK (down < -2.0 && down > -6.0);
    CHECK (up > 7.0 && up < 13.0);
}
