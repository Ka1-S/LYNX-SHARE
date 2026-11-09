// Round 183b slice 3: Halo (the ValhallaShimmer role), measured black-box on the reference (tp172d's bank and 13 new
// captures with the impulse at −20 dB: Mod depth, Mod rate, Size, Reverb mode, Feedback, Diffusion). Its Mono mode
// (mono in) gives a partly correlated tail, more so with Size; its stereo modes stay uncorrelated; its first 80 ms
// carry less of the energy than this tank's did.
namespace verb183bhalo
{
inline double corr (const verbs100::Stereo& s, std::size_t from, std::size_t to)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = from; i < std::min (to, s.l.size()); ++i)
        lr += (double) s.l[i] * s.r[i], ll += (double) s.l[i] * s.l[i], rr += (double) s.r[i] * s.r[i];
    return lr / std::sqrt (ll * rr + 1e-30);
}
} // namespace verb183bhalo

TEST (halo_mono_mode_stereo_and_onset_verb183b)
{
    using namespace verbs100;
    using lynx::EffectKind;
    auto p = defaults (EffectKind::halo);
    p[0] = 1.0f;
    p[9] = 0.125f; // the reference's Mono mode (its default)
    const std::size_t from = 2400, to = 48000;
    p[4] = 0.3f;
    const auto small = verb183bhalo::corr (impulse (EffectKind::halo, p, 1.0), from, to);
    p[4] = 0.5f;
    const auto mid = impulse (EffectKind::halo, p, 3.0);
    p[4] = 0.8f;
    const auto big = verb183bhalo::corr (impulse (EffectKind::halo, p, 1.0), from, to);
    // the reference: 0.25 / 0.31 / 0.43 at Size 30 / 50 / 80 %
    CHECK (std::abs (verb183bhalo::corr (mid, from, to) - 0.31) < 0.15);
    CHECK (big > small + 0.05);
    // its stereo modes: ~0
    p[4] = 0.5f;
    p[9] = 0.625f;
    const auto wide = impulse (EffectKind::halo, p, 3.0);
    CHECK (std::abs (verb183bhalo::corr (wide, from, to)) < 0.15);
    // Mono keeps the level of the full width (within 3 dB of the stereo mode at the same settings)
    CHECK (std::abs (energyDb (mid, 0, mid.l.size()) - energyDb (wide, 0, wide.l.size())) < 3.0);
    // its first 80 ms carry little of the energy: under −14 dB of the first 3 s in Mono
    CHECK (energyDb (mid, 0, 3840) - energyDb (mid, 0, mid.l.size()) < -14.0);
}
