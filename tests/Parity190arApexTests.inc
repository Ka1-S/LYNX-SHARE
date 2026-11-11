// Round 190ar part 2: Apex Limiter's levelling stage (parameters "Level threshold" and "Level attack"; off at 0 dB).
// Measured black-box on a limiter with a threshold under its ceiling (docs/PARITY.md "Round 190ar, part 2"): the steady
// level is held at the threshold with a slow attack and the release time, the ceiling only catches what gets through.
#include "core/ApexLimiter.h"

namespace p190arapex
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;
namespace ap = lynx::apex;

// a 1 kHz tone: −40 dBFS for 0.2 s, `loud` dBFS for `burst` s, −40 dBFS for `tail` s
inline std::vector<float> quietLoudQuiet (double loud, double burst, double tail)
{
    std::vector<float> x ((std::size_t) ((0.2 + burst + tail) * sr));
    const auto a = (std::size_t) (0.2 * sr), b = (std::size_t) ((0.2 + burst) * sr);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= a && i < b ? loud : -40.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    return x;
}
inline double levelAt (const std::vector<float>& y, double seconds, double window = 0.005)
{
    return rmsDb (y, (std::size_t) ((seconds - window) * sr), (std::size_t) ((seconds + window) * sr));
}
inline double peakDb (const std::vector<float>& y, double from, double to)
{
    float m = 0.0f;
    for (auto i = (std::size_t) (from * sr); i < (std::size_t) (to * sr); ++i)
        m = std::max (m, std::abs (y[i]));
    return 20.0 * std::log10 ((double) m + 1e-12);
}
inline EffectParams settings (double thresholdDb, double attackMs = 9.0)
{
    auto p = defaults (EffectKind::apexLimiter);
    p[ap::pLevelThreshold] = (float) (-thresholdDb / 36.0);
    p[ap::pLevelAttack] = (float) (std::log (attackMs / 0.1) / std::log (1000.0));
    p[ap::pRelease] = ap::releaseValue (300.0);
    return p;
}
} // namespace p190arapex

TEST (p190ar_apex_levelling_is_off_by_default)
{
    using namespace p190arapex;
    const auto& info = ap::info();
    CHECK (ap::paramCount == 24 && info.params.size() == 24);
    CHECK (std::string (info.params[ap::pLevelThreshold].name) == "Level threshold" && info.params[ap::pLevelThreshold].defaultValue == 0.0f);
    CHECK (ap::paramText (ap::pLevelThreshold, 0.0f) == "Off" && ap::paramText (ap::pLevelThreshold, 1.0f / 3.0f) == "-12.0 dB");
    CHECK (ap::paramText (ap::pLevelAttack, info.params[ap::pLevelAttack].defaultValue) == "9.00 ms");
    CHECK (std::abs (ap::levelAttackMs (0.0f) - 0.1) < 1e-9 && std::abs (ap::levelAttackMs (1.0f) - 100.0) < 1e-6);
    // off: the attack control does nothing, and the latency is the same with the stage on
    const auto x = quietLoudQuiet (0.0, 0.3, 0.3);
    auto p = defaults (EffectKind::apexLimiter);
    auto q = p;
    q[ap::pLevelAttack] = 0.1f;
    int la = 0, lb = 0;
    CHECK (run (EffectKind::apexLimiter, p, x, &la) == run (EffectKind::apexLimiter, q, x));
    const auto on = run (EffectKind::apexLimiter, settings (-12.0), x, &lb);
    CHECK (la == lb);
    CHECK (on != run (EffectKind::apexLimiter, p, x));
}

TEST (p190ar_apex_levelling_holds_the_threshold_and_lets_the_onset_through)
{
    using namespace p190arapex;
    // 0 dBFS for 0.5 s with the threshold at −12 dB: the steady level sits at the threshold; the first milliseconds are
    // louder (9 ms attack) but never over the ceiling (−0.1 dB)
    const auto x = quietLoudQuiet (0.0, 0.5, 1.5);
    const auto y = run (EffectKind::apexLimiter, settings (-12.0), x);
    const auto steady = levelAt (y, 0.6);
    CHECK (steady > -13.5 && steady < -10.0);
    const auto onset = peakDb (y, 0.2, 0.203);
    CHECK (onset > -6.0 && onset <= -0.09);
    CHECK (peakDb (y, 0.0, 2.1) <= -0.09);
    // about half of the reduction (in dB) is reached after one attack time
    const auto mid = levelAt (y, 0.2 + 0.009, 0.001);
    CHECK (mid < onset - 2.0 && mid > steady + 2.0);
    // a faster attack catches the onset earlier
    const auto fast = run (EffectKind::apexLimiter, settings (-12.0, 0.5), x);
    CHECK (levelAt (fast, 0.2 + 0.004, 0.001) < levelAt (y, 0.2 + 0.004, 0.001) - 3.0);
    // the release: 300 ms after the burst about 1 / e of the reduction (12 dB) is left on the −40 dBFS tone
    const auto left = -40.0 - levelAt (y, 0.7 + 0.3);
    CHECK (left > 3.0 && left < 6.0);
    CHECK (std::abs (-40.0 - levelAt (y, 2.1)) < 0.2);
    // under the knee nothing happens: −20 dBFS against a −12 dB threshold
    const auto q = quietLoudQuiet (-20.0, 0.3, 0.2);
    CHECK (std::abs (levelAt (run (EffectKind::apexLimiter, settings (-12.0), q), 0.4) + 20.0) < 0.05);
    for (const auto v : y)
        CHECK (std::isfinite (v));
}
