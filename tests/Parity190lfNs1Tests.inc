// Round 190lf (sub-round ns1): REA of the Waves NS1 DSP and its saved states (owner's permission of 2026-11-17;
// /mnt/e/lynx-work/waves2_190lf/ns1/HANDOFF.md, docs/analise/REA_rodada190lf_ns1.md).
// - Quell's matched engine gets a zero law (hidden [5]): at Amount 0 the reference is exactly its all-pass pair (nothing
//   dynamic, no shelf), so the law makes Amount 0 that bare pair and fades the engine in up to 20 %. 0 for new instances
//   and every project saved before (bit for bit: ns1/lab/gold.sh); the NS1 translator sets it.
#include <cmath>
#include <random>

#include "core/Effects.h"
#include "core/VoiceTools.h"

namespace parity190lfns1
{
inline void noisy (std::vector<float>& l, std::vector<float>& r, double sr)
{
    std::mt19937 rng (190);
    std::normal_distribution<double> g (0.0, 1.0);
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        const double tone = 0.3 * std::sin (2.0 * M_PI * 220.0 * t) + 0.05 * std::sin (2.0 * M_PI * 12000.0 * t);
        const double n = (i / 9600) % 2 ? 0.003 : 0.03; // quiet / louder noise every 0.2 s
        l[i] = (float) (tone + n * g (rng));
        r[i] = (float) (0.8 * tone + n * g (rng));
    }
}
inline lynx::EffectSlot quell (double amount, bool zero)
{
    namespace q = lynx::voicetools::quell;
    auto s = lynx::makeEffectSlot (lynx::EffectKind::quellDenoiser);
    s.params[q::amount] = (float) amount;
    s.params[q::matched] = 1.0f;
    s.params[q::zeroLaw] = zero ? 1.0f : 0.0f;
    return s;
}
inline void run (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    for (std::size_t off = 0; off < l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, l.size() - off), s.params, 120.0);
}
// the RBJ all-pass pair the reference is at 0 % (60.07 Hz Q 1.0, 16 kHz Q 0.90), in double
inline std::vector<double> allPassPair (const std::vector<float>& x, double sr)
{
    std::vector<double> y (x.begin(), x.end());
    for (const auto [f0, q] : { std::pair { 60.0658, 0.9998 }, std::pair { 16000.0, 0.8992 } })
    {
        const double w = 2.0 * M_PI * f0 / sr, a = std::sin (w) / (2.0 * q), c = std::cos (w);
        const double b0 = (1 - a) / (1 + a), b1 = -2 * c / (1 + a), b2 = 1.0, a1 = b1, a2 = b0;
        double z1 = 0.0, z2 = 0.0;
        for (auto& v : y)
        {
            const double in = v, out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            v = out;
        }
    }
    return y;
}
inline double distanceDb (const std::vector<float>& y, const std::vector<double>& ref)
{
    double e = 0.0, s = 0.0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        e += (y[i] - ref[i]) * (y[i] - ref[i]);
        s += ref[i] * ref[i];
    }
    return 10.0 * std::log10 (e / s + 1e-30);
}
} // namespace parity190lfns1

TEST (parity190lf_ns1_quell_zero_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    namespace q = voicetools::quell;
    CHECK (effectInfo (EffectKind::quellDenoiser).params.size() == 5 && q::zeroLaw == 5); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::quellDenoiser);
    CHECK (s.params[q::zeroLaw] == 0.0f && savedEffectParamCount (s) == 5);
    s.params[q::zeroLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == 6);
}

TEST (parity190lf_ns1_is_translated_with_the_zero_law)
{
    using namespace lynx;
    namespace q = voicetools::quell;
    int n = 0;
    const auto zero = parity190jh::waves ("NS1 Stereo", "NS1", parity190jh::list (253, { { 85, "60" }, { 86, "16000" } }), n);
    CHECK (zero.kind == EffectKind::quellDenoiser && n == 4 && zero.params[q::amount] == 0.0f);
    CHECK (zero.params[q::matched] == 1.0f && zero.params[q::zeroLaw] == 1.0f);
    const auto half = parity190jh::waves ("NS1 Stereo", "NS1", parity190jh::list (253, { { 229, "50" } }), n);
    CHECK (n == 4 && std::abs (half.params[q::amount] - 0.5f) < 1e-6f && half.params[q::zeroLaw] == 1.0f);
}

TEST (parity190lf_ns1_zero_amount_is_the_bare_all_pass_pair)
{
    using namespace lynx;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> x ((std::size_t) sr), r ((std::size_t) sr);
        parity190lfns1::noisy (x, r, sr);
        const auto ref = parity190lfns1::allPassPair (x, sr);
        auto l0 = x, r0 = r;
        parity190lfns1::run (parity190lfns1::quell (0.0, true), sr, l0, r0);
        CHECK (parity190lfns1::distanceDb (l0, ref) < -120.0); // float output of the same double recursion
        // round 124's engine at 0 % is not (its static HF shelf and the expander on the quiet noise)
        auto lo = x, ro = r;
        parity190lfns1::run (parity190lfns1::quell (0.0, false), sr, lo, ro);
        CHECK (parity190lfns1::distanceDb (lo, ref) > -40.0);
    }
}

TEST (parity190lf_ns1_zero_law_fades_the_engine_in_and_leaves_20_percent_up_alone)
{
    using namespace lynx;
    const double sr = 48000.0;
    std::vector<float> x (48000), r (48000);
    parity190lfns1::noisy (x, r, sr);
    const auto ref = parity190lfns1::allPassPair (x, sr);
    double prev = -1000.0;
    for (const double a : { 0.05, 0.1, 0.15, 0.2 })
    {
        auto l = x, rr = r;
        parity190lfns1::run (parity190lfns1::quell (a, true), sr, l, rr);
        const auto d = parity190lfns1::distanceDb (l, ref);
        CHECK (d > prev); // further from the bare pair as the engine fades in
        prev = d;
    }
    for (const double a : { 0.2, 0.35, 0.5, 1.0 })
    {
        auto l1 = x, r1 = r, l2 = x, r2 = r;
        parity190lfns1::run (parity190lfns1::quell (a, true), sr, l1, r1);
        parity190lfns1::run (parity190lfns1::quell (a, false), sr, l2, r2);
        CHECK (l1 == l2 && r1 == r2); // the engine alone, bit for bit
    }
}
