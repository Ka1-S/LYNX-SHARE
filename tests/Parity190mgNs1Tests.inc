// Round 190mg (sub-round ns1): Quell's NS1 law (hidden [6]) — an engine with the Waves NS1's own structure for 20 … 100 %
// (REA of the NS1 trial's DSP for the structure, owner's permission of 2026-11-17; every number fitted on our probes and
// renders: /mnt/e/lynx-work/waves3_190mg/ns1/HANDOFF.md, docs/analise/REA_rodada190mg_ns1.md). 0 for new instances and
// every project saved before (bit for bit: ns1/lab/gold.sh); the NS1 translator sets it. Amount 0 runs the NS1 engine's
// own open all-pass pair (round 190mm, owner's decision of 11-19; was the zero law's pair).
#include <cmath>
#include <random>

#include "core/Effects.h"
#include "core/VoiceTools.h"

namespace parity190mgns1
{
inline lynx::EffectSlot quell (double amount, bool ns1)
{
    namespace q = lynx::voicetools::quell;
    auto s = lynx::makeEffectSlot (lynx::EffectKind::quellDenoiser);
    s.params[q::amount] = (float) amount;
    s.params[q::matched] = 1.0f;
    s.params[q::zeroLaw] = 1.0f;
    s.params[q::ns1Law] = ns1 ? 1.0f : 0.0f;
    return s;
}
inline void run (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    for (std::size_t off = 0; off < l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, (int) std::min<std::size_t> (256, l.size() - off), s.params, 120.0);
}
// steady noise (σ = `level`) with a 1 kHz tone of amplitude `tone` from `toneFrom` s on
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr, double level, double tone, double toneFrom)
{
    std::mt19937 rng (1904);
    std::normal_distribution<double> g (0.0, 1.0);
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        const double s = t >= toneFrom ? tone * std::sin (2.0 * M_PI * 1000.0 * t) : 0.0;
        l[i] = (float) (s + level * g (rng));
        r[i] = (float) (s + level * g (rng));
    }
}
inline double rmsDb (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i)
        e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) (to - from) + 1e-30);
}
inline double distanceDb (const std::vector<float>& y, const std::vector<float>& ref)
{
    double e = 0.0, s = 0.0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        e += ((double) y[i] - ref[i]) * ((double) y[i] - ref[i]);
        s += (double) ref[i] * ref[i];
    }
    return 10.0 * std::log10 (e / s + 1e-30);
}
} // namespace parity190mgns1

TEST (parity190mg_ns1_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    namespace q = voicetools::quell;
    CHECK (effectInfo (EffectKind::quellDenoiser).params.size() == 5 && q::ns1Law == 6); // hidden: no control, no string
    auto s = makeEffectSlot (EffectKind::quellDenoiser);
    CHECK (s.params[q::ns1Law] == 0.0f && savedEffectParamCount (s) == 5);
    s.params[q::ns1Law] = 1.0f;
    CHECK (savedEffectParamCount (s) == 7);
}

TEST (parity190mg_ns1_is_translated_with_the_ns1_law)
{
    using namespace lynx;
    namespace q = voicetools::quell;
    int n = 0;
    const auto s = parity190jh::waves ("NS1 Stereo", "NS1", parity190jh::list (253, { { 229, "35" } }), n);
    CHECK (s.kind == EffectKind::quellDenoiser && n == 4 && std::abs (s.params[q::amount] - 0.35f) < 1e-6f);
    CHECK (s.params[q::matched] == 1.0f && s.params[q::zeroLaw] == 1.0f && s.params[q::ns1Law] == 1.0f);
}

TEST (parity190mg_ns1_layout_follows_the_five_saved_states)
{
    namespace q = lynx::voicetools::quell;
    // RealWorld 85 / 86 / 206 = 203 / 226 of NS1's states at 0 / 20 / 35 / 50 / 100 %
    const double at[] { 0.0, 0.2, 0.35, 0.5, 1.0 }, lo[] { 60, 60, 65, 70, 110 }, hi[] { 16000, 14000, 13500, 13000, 8000 };
    const double cut[] { 0, -10, -25, -40, -70 }, pos[] { 0, 0.02, 0.0325, 0.045, 0.8 };
    for (int k = 0; k < 5; ++k)
    {
        const auto l = q::ns1Layout (at[k]);
        CHECK (std::abs (l.minFc - lo[k]) < 1e-9 && std::abs (l.maxFc - hi[k]) < 1e-6);
        CHECK (std::abs (l.cutDb - cut[k]) < 1e-9 && std::abs (l.position - pos[k]) < 1e-12);
    }
    // in between: monotone (the corners close in, the cuts deepen, the position rises)
    auto prev = q::ns1Layout (0.0);
    for (int i = 1; i <= 100; ++i)
    {
        const auto l = q::ns1Layout (i / 100.0);
        CHECK (l.minFc >= prev.minFc && l.maxFc <= prev.maxFc && l.cutDb <= prev.cutDb && l.position >= prev.position);
        prev = l;
    }
}

TEST (parity190mm_ns1_amount_zero_is_the_ns1_engines_open_pair)
{
    using namespace lynx;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> x ((std::size_t) sr), r ((std::size_t) sr);
        parity190mgns1::signal (x, r, sr, 0.01, 0.2, 0.5);
        auto la = x, ra = r, lb = x, rb = r, lc = x, rc = r;
        parity190mgns1::run (parity190mgns1::quell (0.0, false), sr, la, ra); // the zero law alone: round 124's pair
        parity190mgns1::run (parity190mgns1::quell (0.0, true), sr, lb, rb);
        parity190mgns1::run (parity190mgns1::quell (1e-6, true), sr, lc, rc);
        // round 190mm: 0 % is the NS1 engine's own open pair (60 Hz Q 1 · 16 kHz Q 0.8992), continuous with just above
        // it, and close to (not equal to) round 124's 60.07 Hz Q 0.9998 pair: no audible step either way
        CHECK (! (la == lb && ra == rb));
        CHECK (parity190mgns1::distanceDb (lb, lc) < -60.0);
        CHECK (parity190mgns1::distanceDb (lb, la) < -45.0);
    }
}

TEST (parity190mg_ns1_takes_steady_noise_down_and_lets_a_tone_through)
{
    using namespace lynx;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto n = (std::size_t) (8.0 * sr);
        std::vector<float> x (n), r (n);
        parity190mgns1::signal (x, r, sr, 0.003, 0.3, 5.0); // −50 dBFS noise, then a −13 dBFS tone over it from 5 s
        auto l1 = x, r1 = r;
        parity190mgns1::run (parity190mgns1::quell (1.0, true), sr, l1, r1);
        const auto s = (std::size_t) sr;
        const auto noiseIn = parity190mgns1::rmsDb (x, 4 * s, 5 * s), noiseOut = parity190mgns1::rmsDb (l1, 4 * s, 5 * s);
        CHECK (noiseOut < noiseIn - 10.0); // the noise floor taken down at 100 %
        const auto toneIn = parity190mgns1::rmsDb (x, 7 * s, 8 * s), toneOut = parity190mgns1::rmsDb (l1, 7 * s, 8 * s);
        CHECK (toneOut > toneIn - 4.0); // a tone far above it passes
        // Listen plays what is taken away: processed + listened = the open path (the same engine's front end)
        auto sl = parity190mgns1::quell (1.0, true);
        sl.params[voicetools::quell::listen] = 1.0f;
        auto l2 = x, r2 = r;
        parity190mgns1::run (sl, sr, l2, r2);
        CHECK (parity190mgns1::rmsDb (l2, 4 * s, 5 * s) > noiseOut);
    }
}

TEST (parity190mg_ns1_differs_from_round_124_above_zero)
{
    using namespace lynx;
    const double sr = 48000.0;
    std::vector<float> x (48000), r (48000);
    parity190mgns1::signal (x, r, sr, 0.01, 0.2, 0.3);
    auto la = x, ra = r, lb = x, rb = r;
    parity190mgns1::run (parity190mgns1::quell (0.5, false), sr, la, ra);
    parity190mgns1::run (parity190mgns1::quell (0.5, true), sr, lb, rb);
    CHECK (parity190mgns1::distanceDb (lb, la) > -40.0);
}
