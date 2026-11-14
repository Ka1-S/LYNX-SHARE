// Round 190fx: dynamics parity — the side chain's frequency weighting of Glow Leveler's Opto classic voicing ("SC weighting")
// measured black-box on three optical references' 100 Hz / 1 kHz / 8 kHz stairs and pink stairs (docs/PARITY.md "Round
// 190fx"). Appended and off by default: Dyn190dmTests.inc keeps proving the sound with it off (its goldens render the
// defaults).
namespace p190fx
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;

// a steady tone: `db` dBFS at `hz` for `seconds`
inline std::vector<float> steady (double hz, double db, double seconds)
{
    std::vector<float> x ((std::size_t) (seconds * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * pi * hz * (double) i / sr));
    return x;
}
// the gain (dB) over the last 0.3 s
inline double settledGain (EffectKind kind, const EffectParams& p, double hz, double db)
{
    const auto x = steady (hz, db, 2.5);
    const auto y = run (kind, p, x);
    const auto a = x.size() - (std::size_t) (0.3 * sr), b = x.size();
    return rmsDb (y, a, b) - rmsDb (x, a, b);
}
} // namespace p190fx

TEST (p190fx_glow_sc_weighting_is_appended_off_and_keeps_the_1k_law)
{
    using namespace p190fx;
    namespace gl = lynx::mixsuite::glow;
    const auto& info = lynx::effectInfo (EffectKind::glowLeveler);
    CHECK ((int) info.params.size() == gl::count && gl::scWeighting == gl::optoMemory + 1 && gl::scWeighting == 14);
    CHECK (info.params[(std::size_t) gl::scWeighting].name == std::string ("SC weighting"));
    CHECK (lynx::makeEffectSlot (EffectKind::glowLeveler).params[(std::size_t) gl::scWeighting] == 0.0f);
    CHECK (lynx::mixsuite::paramText (EffectKind::glowLeveler, gl::scWeighting, 0.0f) == "emphasis");
    CHECK (lynx::mixsuite::paramText (EffectKind::glowLeveler, gl::scWeighting, 1.0f) == "even");

    auto p = lynx::makeEffectSlot (EffectKind::glowLeveler).params;
    p[gl::peakReduction] = 0.6f;
    auto e = p;
    e[gl::scWeighting] = 1.0f;
    // the 1 kHz law fitted before holds: the settled reduction at 1 kHz moves by less than 0.2 dB
    for (const double db : { -18.0, -6.0 })
        CHECK (std::abs (settledGain (EffectKind::glowLeveler, e, 1000.0, db) - settledGain (EffectKind::glowLeveler, p, 1000.0, db)) < 0.2);
    // treble is heard much less than through the voicing's emphasis: 8 kHz keeps more level
    CHECK (settledGain (EffectKind::glowLeveler, e, 8000.0, -6.0) > settledGain (EffectKind::glowLeveler, p, 8000.0, -6.0) + 1.5);
    // nothing changes below the threshold
    CHECK (std::abs (settledGain (EffectKind::glowLeveler, e, 8000.0, -50.0) - settledGain (EffectKind::glowLeveler, p, 8000.0, -50.0)) < 0.01);
    // the measured Limit law keeps its emphasis (its law was fitted on it)
    auto lim = p;
    lim[gl::mode] = 1.0f;
    auto lim1 = lim;
    lim1[gl::scWeighting] = 1.0f;
    const auto x = steady (8000.0, -6.0, 1.0);
    CHECK (run (EffectKind::glowLeveler, lim, x) == run (EffectKind::glowLeveler, lim1, x));
    // Glow's own engine ignores it
    auto own = p;
    own[gl::voicing] = 0.0f;
    auto own1 = own;
    own1[gl::scWeighting] = 1.0f;
    CHECK (run (EffectKind::glowLeveler, own, x) == run (EffectKind::glowLeveler, own1, x));
}
