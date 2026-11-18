// Round 190lf, sub-round "dyn": REA of the Waves CLA-2A (owner's permission of 2026-11-17; docs/analise/
// REA_rodada190lf_dyn.md, /mnt/e/lynx-work/waves2_190lf/dyn/HANDOFF.md).
// - Glow Leveler's Opto classic voicing gets a cell law (hidden cellLaw): LYNX's compact opto behaviour model (feedback
//   side chain through a low and a high peaking filter, a fast and a slow conductance sharing the light, the high band
//   reduced less), fitted on our demo-mode probes. Off for new instances and every project saved before (bit for bit:
//   lab/gold.sh). The CLA-2A translator sets it.
#include <cmath>

#include "core/Effects.h"
#include "core/MixSuite.h"

namespace parity190lf
{
namespace g = lynx::mixsuite::glow;
inline lynx::EffectSlot glow (double pr, double gain, bool limit, double hf, bool cell)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::glowLeveler);
    s.params[(std::size_t) g::voicing] = 1.0f;
    s.params[(std::size_t) g::optoLaw] = 1.0f;
    s.params[(std::size_t) g::releaseLaw] = 1.0f;
    s.params[(std::size_t) g::peakReduction] = (float) (pr / 100.0);
    s.params[(std::size_t) g::gain] = (float) (gain / 100.0);
    s.params[(std::size_t) g::mode] = limit ? 1.0f : 0.0f;
    s.params[(std::size_t) g::emphasis] = (float) (hf / 100.0);
    s.params[(std::size_t) g::cellLaw] = cell ? 1.0f : 0.0f;
    return s;
}
// the gain (dB) of the last 100 ms of a 1 kHz tone at `db` dBFS held `sec` s
inline double steadyDb (const lynx::EffectSlot& s, double db, double sec = 2.0)
{
    const double sr = 48000.0;
    const auto n = (std::size_t) (sec * sr);
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
        l[i] = r[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * M_PI * 1000.0 * (double) i / sr));
    const auto x = l;
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    for (std::size_t off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, n - off), s.params, 120.0);
    double ey = 0.0, ex = 0.0;
    for (auto i = n - (std::size_t) (0.1 * sr); i < n; ++i)
        ey += (double) l[i] * l[i], ex += (double) x[i] * x[i];
    return 10.0 * std::log10 (ey / ex);
}
} // namespace parity190lf

TEST (parity190lf_glow_cell_law_is_hidden_and_off_for_new_and_old_glows)
{
    using namespace lynx;
    namespace g = mixsuite::glow;
    const auto& info = effectInfo (EffectKind::glowLeveler);
    CHECK (info.params.size() == (std::size_t) g::count && g::cellLaw == g::count); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::glowLeveler);
    CHECK (s.params[(std::size_t) g::cellLaw] == 0.0f && savedEffectParamCount (s) <= (std::size_t) g::count);
    s.params[(std::size_t) g::cellLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) g::cellLaw + 1);
}

TEST (parity190lf_glow_cell_law_levels_like_the_measured_reference)
{
    using parity190lf::glow;
    using parity190lf::steadyDb;
    // below the threshold: the gain dial's measured level (Gain 40: +8.48 dB at 1 kHz)
    CHECK (std::abs (steadyDb (glow (46, 40, false, 50, true), -40.0) - 8.48) < 0.2);
    // more peak reduction = more reduction at a loud tone, Limit more than Compress at a high dial
    const auto c46 = steadyDb (glow (46, 40, false, 50, true), -6.0), c70 = steadyDb (glow (70, 40, false, 50, true), -6.0);
    const auto c90 = steadyDb (glow (90, 40, false, 50, true), -6.0);
    CHECK (c46 > c70 && c70 > c90 && c46 < 8.0);
    // the output keeps rising with the input (a leveler, not a brickwall) in Compress
    CHECK (steadyDb (glow (70, 40, false, 50, true), 0.0) + 0.0 > steadyDb (glow (70, 40, false, 50, true), -12.0) - 12.0);
    // Hi Freq turned down = less side-chain sensitivity at 1 kHz (the measured shift)
    CHECK (steadyDb (glow (70, 40, false, 0, true), -10.0) > steadyDb (glow (70, 40, false, 100, true), -10.0));
    // the switch changes the sound; off keeps the round-165w law
    CHECK (std::abs (steadyDb (glow (70, 40, false, 50, true), -10.0) - steadyDb (glow (70, 40, false, 50, false), -10.0)) > 1e-3);
}

TEST (parity190lf_waves_cla2a_translates_onto_the_cell_law)
{
    using namespace lynx;
    namespace g = mixsuite::glow;
    std::string values;
    for (int i = 0; i < 208; ++i)
        values += i == 38 ? "40 " : i == 74 ? "70 " : i == 153 ? "50 " : i == 204 ? "100 " : i == 140 ? "2 " : "0 ";
    int n = 0;
    const auto s = vm123::wavesSlot ("CLA-2A", values.c_str(), n);
    CHECK (s.kind == EffectKind::glowLeveler && n == 8 && s.params[(std::size_t) g::cellLaw] == 1.0f);
    CHECK (std::abs (s.params[(std::size_t) g::peakReduction] - 0.70f) < 1e-6f && s.params[(std::size_t) g::voicing] == 1.0f);
}
