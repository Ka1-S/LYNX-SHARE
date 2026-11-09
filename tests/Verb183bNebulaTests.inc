// Round 183b slice 2: Nebula (the Valhalla Supermassive role), measured black-box on the reference (tp172d's 41
// probe renders and 19 new captures: Mod depth 0 / 10 / 25 / 50 / 100 %, Mod rate, Width, Density, Mix 100 %).
// The reference's tail is decorrelated whenever it modulates (L/R correlation 0.34 / 0.13 / 0 at Mod depth 10 / 25 /
// 50 %) and mono without modulation; its eight voices fall apart as they modulate, so the unmodulated tail is ~9 dB
// louder; Width 50 % is mono and 0 % swaps the sides. Its mono modes stay mono.
namespace verb183bneb
{
using lynx::EffectKind;

inline double tailCorr (const verbs100::Stereo& s, std::size_t from, std::size_t to)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = from; i < std::min (to, s.l.size()); ++i)
        lr += (double) s.l[i] * s.r[i], ll += (double) s.l[i] * s.l[i], rr += (double) s.r[i] * s.r[i];
    return lr / std::sqrt (ll * rr + 1e-30);
}
} // namespace verb183bneb

TEST (nebula_stereo_follows_modulation_verb183b)
{
    using namespace verbs100;
    auto p = defaults (EffectKind::nebula); // 300 ms, Feedback 50 %, Density 0, Width 100 %, Mod depth 50 %
    p[0] = 1.0f;
    const auto from = (std::size_t) (0.25 * 48000), to = (std::size_t) (3.0 * 48000);
    const auto moving = impulse (EffectKind::nebula, p, 3.0);
    CHECK (std::abs (verb183bneb::tailCorr (moving, from, to)) < 0.3); // the reference: −0.01
    p[11] = 0.0f;
    const auto still = impulse (EffectKind::nebula, p, 3.0);
    CHECK (verb183bneb::tailCorr (still, from, to) > 0.95); // the reference: 1.00
    // its unmodulated voices add up: ~9 dB louder (the reference: +8.9 dB)
    const auto d = energyDb (still, from, to) - energyDb (moving, from, to);
    CHECK (d > 6.0 && d < 12.0);
    // Width 50 % is mono
    p = defaults (EffectKind::nebula);
    p[0] = 1.0f;
    p[7] = 0.5f;
    CHECK (verb183bneb::tailCorr (impulse (EffectKind::nebula, p, 3.0), from, to) > 0.99);
    // a mono mode of the reference (its ninth: a single repeat pattern, L = R) stays mono
    p = defaults (EffectKind::nebula);
    p[0] = 1.0f;
    p[12] = 8.0f / 21.0f;
    CHECK (verb183bneb::tailCorr (impulse (EffectKind::nebula, p, 6.0), from, (std::size_t) (6.0 * 48000)) > 0.95);
}
