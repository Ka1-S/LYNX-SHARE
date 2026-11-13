// --- Round 190v: Crest's Two-way and Vari-mu laws and Sentry's Vocal noise reduction, fitted black-box to round
// trial190k's reference renders (LYNX's own test signals through the public parameter API; docs/PARITY.md "Round
// 190v"). The numbers below are those renders' 1 kHz stair gains (dB, the last 140 ms of each 0.4 s step). ---------------

#include "core/DynamicsSuite.h"

namespace p190v
{
using lynx::EffectKind;
using lynx::EffectParams;
using namespace dyn108;

// threshold (dB), ratio, knee (dB), attack (ms) and the gain at −48, −36, −24, −12, −6 and 0 dBFS (release 200 ms)
struct Row
{
    double thr, ratio, knee, att;
    double gain[6];
};

// the worst difference (dB) between Crest's stair gains and the rows
inline double worstError (lynx::dyn::crest::Style style, const Row* rows, std::size_t count)
{
    namespace c = lynx::dyn::crest;
    const std::vector<double> levels { -48, -36, -24, -12, -6, 0 };
    std::size_t per = 0;
    const auto x = stair (levels, 0.5, 1000.0, per);
    double worst = 0.0;
    for (std::size_t k = 0; k < count; ++k)
    {
        const auto& row = rows[k];
        auto p = defaults (EffectKind::crestCompressor);
        p[c::style] = c::styleValue (style);
        p[c::threshold] = (float) ((row.thr + 60.0) / 60.0);
        p[c::ratio] = c::ratioValue (row.ratio);
        p[c::knee] = (float) (row.knee / 72.0);
        p[c::attack] = c::attackValue (row.att);
        p[c::release] = c::releaseValue (200.0);
        p[c::autoGain] = 0.0f;
        const auto g = stairGains (x, run (EffectKind::crestCompressor, p, x), per, levels.size());
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            const auto err = std::abs (g[i] - row.gain[i]);
            if (err > worst)
                worst = err;
            if (err > 0.8)
                std::printf ("  style %d threshold %g ratio %g knee %g attack %g at %g dBFS: %.2f dB (measured %.2f)\n", (int) style,
                             row.thr, row.ratio, row.knee, row.att, levels[i], g[i], row.gain[i]);
        }
    }
    return worst;
}

inline std::vector<float> noise (double dbfs, double seconds, unsigned seed)
{
    std::vector<float> x ((std::size_t) (seconds * sr));
    std::uint32_t s = seed;
    double ms = 0.0;
    for (auto& v : x)
    {
        s = s * 1664525u + 1013904223u;
        v = (float) ((double) (s >> 8) / 8388608.0 - 1.0);
        ms += (double) v * v;
    }
    const auto k = std::pow (10.0, dbfs / 20.0) / std::sqrt (ms / (double) x.size());
    for (auto& v : x)
        v = (float) (v * k);
    return x;
}
} // namespace p190v

TEST (p190v_crest_two_way_follows_the_reference_grid)
{
    namespace c = lynx::dyn::crest;
    // upward compression under threshold − 6 dB, a fixed gain that falls as the threshold rises and as the attack slows,
    // downward compression over threshold + 3 dB; round 108's −18 dB stairs are the first row
    const p190v::Row rows[] {
        { -18, 4, 0, 10, { 4.30, 0.03, -4.17, -5.76, -9.40, -13.01 } },
        { -24, 4, 0, 0.25, { 5.01, 0.75, -1.31, -6.68, -10.30, -13.89 } },
        { -24, 4, 0, 100, { 2.82, -1.44, -4.19, -8.35, -11.97, -15.57 } },
        { -36, 2, 18, 10, { 1.77, 0.10, -1.87, -4.85, -6.35, -7.87 } },
        { -12, 10, 0, 10, { 8.00, 2.15, -3.71, -6.88, -8.84, -13.72 } },
        { -24, 4, 72, 10, { 4.23, -0.08, -4.87, -10.20, -13.07, -16.08 } },
        { -48, 4, 0, 10, { 0.40, -4.66, -11.87, -19.00, -22.84, -26.68 } },
        { -30, 10, 18, 100, { 4.78, -0.38, -4.26, -12.49, -17.21, -21.82 } },
    };
    const auto worst = p190v::worstError (c::Style::twoWay, rows, std::size (rows));
    std::printf ("  Two-way against the grid: worst error %.2f dB over %d points\n", worst, (int) std::size (rows) * 6);
    CHECK (worst <= 0.8);
}

TEST (p190v_crest_vari_mu_follows_the_reference_grid)
{
    namespace c = lynx::dyn::crest;
    // the reduction depends on the level more than on the overshoot (threshold −48 against −18: 4.5 dB more at 0 dBFS,
    // not 22) and falls as the knee grows (−14.6 / −13.0 / −10.8 dB at knee 0 / 18 / 72); round 108's stairs are row 1
    const p190v::Row rows[] {
        { -18, 4, 0, 10, { 0.00, 0.00, 0.00, -2.53, -6.82, -11.55 } },
        { -24, 4, 0, 0.25, { 0.00, 0.00, 0.00, -5.58, -9.93, -14.60 } },
        { -24, 4, 18, 0.25, { 0.00, 0.00, -0.17, -4.55, -8.54, -12.97 } },
        { -24, 4, 72, 0.25, { 0.00, -0.02, -0.41, -3.71, -6.95, -10.80 } },
        { -48, 4, 0, 10, { 0.00, -0.17, -2.12, -7.99, -11.89, -16.09 } },
        { -36, 2, 18, 10, { 0.00, 0.00, -0.15, -1.82, -4.11, -7.23 } },
        { -12, 10, 72, 10, { 0.00, -0.01, -0.73, -6.00, -10.05, -14.47 } },
        { -30, 10, 0, 0.25, { 0.00, 0.00, -2.58, -11.47, -16.37, -21.44 } },
        { -24, 10, 12, 0.25, { 0.00, 0.00, -0.50, -8.07, -12.97, -18.11 } },
    };
    const auto worst = p190v::worstError (c::Style::variMu, rows, std::size (rows));
    std::printf ("  Vari-mu against the grid: worst error %.2f dB over %d points\n", worst, (int) std::size (rows) * 6);
    CHECK (worst <= 0.8);
    // the editor's curve says the same: a larger knee reduces less, a lower threshold barely more
    auto p = p190v::defaults (lynx::EffectKind::crestCompressor);
    p[c::style] = c::styleValue (c::Style::variMu);
    p[c::threshold] = 0.6f;
    p[c::ratio] = c::ratioValue (4.0);
    p[c::knee] = 0.0f;
    const auto k0 = c::staticGainDb (p, 0.0);
    p[c::knee] = 1.0f;
    const auto k72 = c::staticGainDb (p, 0.0);
    CHECK (k72 > k0 + 2.0);
    p[c::knee] = 0.0f;
    p[c::threshold] = 0.2f;
    const auto t48 = c::staticGainDb (p, 0.0);
    p[c::threshold] = 0.0f;
    const auto t60 = c::staticGainDb (p, 0.0);
    CHECK (std::abs (t48 - t60) < 0.01 && t48 < k0 && t48 > k0 - 4.0);
    // every ratio and threshold gives a finite, monotonic curve; ratio 1 does nothing
    for (float r = 0.0f; r <= 1.0f; r += 0.125f)
        for (float t = 0.0f; t <= 1.0f; t += 0.125f)
            for (float k = 0.0f; k <= 1.0f; k += 0.25f)
            {
                p[c::ratio] = r;
                p[c::threshold] = t;
                p[c::knee] = k;
                double last = 0.0;
                bool ok = true;
                for (double x = -90.0; x <= 0.0; x += 1.5)
                {
                    const auto g = c::staticGainDb (p, x);
                    ok = ok && std::isfinite (g) && g <= 1e-9 && g <= last + 1e-9 && (r > 0.0f || g > -1e-6);
                    last = g;
                }
                CHECK (ok);
            }
}

TEST (p190v_crest_two_way_release_and_safety)
{
    namespace c = lynx::dyn::crest;
    using namespace dyn108;
    // a −40 → −6 → −40 dBFS step at threshold −24, ratio 4, attack 10 ms: the gain is half-way back after about a quarter
    // of the release knob (50 ms at 200, 240 ms at 1000 on the reference); it used to be back within 3 ms whatever the knob
    std::vector<float> x ((std::size_t) (5 * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (std::pow (10.0, (i >= (std::size_t) sr && i < (std::size_t) (2 * sr) ? -6.0 : -40.0) / 20.0) * std::sin (2.0 * pi * 1000.0 * (double) i / sr));
    for (const auto& [rel, want] : { std::pair { 200.0, 50.0 }, std::pair { 1000.0, 240.0 } })
    {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::style] = c::styleValue (c::Style::twoWay);
        p[c::threshold] = 0.6f;
        p[c::ratio] = c::ratioValue (4.0);
        p[c::knee] = 0.0f;
        p[c::attack] = c::attackValue (10.0);
        p[c::release] = c::releaseValue (rel);
        p[c::autoGain] = 0.0f;
        const auto y = run (EffectKind::crestCompressor, p, x);
        // the gain at ±0.9 s around the step back down, and the time to half-way between them
        // (10 ms windows from 10 ms after the step: round 190al's band splits ring for a few ms on the step itself, and
        // the quiet low and high bands are lifted, which a 2 ms window read as the gain being back)
        const auto gainAt = [&] (std::size_t i) { return rmsDb (y, i - 240, i + 240) - rmsDb (x, i - 240, i + 240); };
        const auto from = gainAt ((std::size_t) (1.9 * sr)), to = gainAt ((std::size_t) (4.9 * sr));
        double half = 1e9;
        for (std::size_t i = (std::size_t) (2 * sr) + 720; i < (std::size_t) (4.9 * sr); i += 48)
            if (gainAt (i) >= 0.5 * (from + to))
            {
                half = (double) (i - (std::size_t) (2 * sr)) / sr * 1000.0;
                break;
            }
        std::printf ("  Two-way release %g ms: %.1f dB -> %.1f dB, half-way after %.0f ms (reference %.0f)\n", rel, from, to, half, want);
        CHECK (std::abs (from + 11.4) < 0.8 && std::abs (to - 0.9) < 0.8);
        CHECK (half > want * 0.6 && half < want * 1.6);
    }
    // silence stays silence and nothing blows up at the extremes (the lift of quiet parts is bounded as before)
    auto p = defaults (EffectKind::crestCompressor);
    p[c::style] = c::styleValue (c::Style::twoWay);
    p[c::threshold] = 1.0f;
    p[c::ratio] = 1.0f;
    p[c::range] = 1.0f;
    p[c::autoGain] = 0.0f;
    std::vector<float> quiet ((std::size_t) sr, 0.0f);
    for (std::size_t i = quiet.size() / 2; i < quiet.size(); ++i)
        quiet[i] = (float) (1e-5 * std::sin (2.0 * pi * 440.0 * (double) i / sr)); // −100 dBFS
    const auto y = run (EffectKind::crestCompressor, p, quiet);
    float peak = 0.0f;
    bool finite = true;
    for (auto v : y)
    {
        finite = finite && std::isfinite (v);
        peak = std::max (peak, std::abs (v));
    }
    CHECK (finite && peak < 4e-5f);
}

TEST (p190v_sentry_vocal_turns_noise_down_after_a_pause)
{
    namespace s = lynx::dyn::sentry;
    using namespace dyn108;
    // 0.5 s of silence, then white noise at −20 dBFS for 2.5 s; threshold −60 (the gate itself stays open on it)
    auto x = p190v::noise (-20.0, 3.0, 190u);
    std::fill (x.begin(), x.begin() + (std::ptrdiff_t) (0.5 * sr), 0.0f);
    const auto gainOver = [&] (const std::vector<float>& y, double from, double to) {
        return rmsDb (y, (std::size_t) (from * sr), (std::size_t) (to * sr)) - rmsDb (x, (std::size_t) (from * sr), (std::size_t) (to * sr));
    };
    auto p = defaults (EffectKind::sentryGate);
    p[s::threshold] = 0.0f;
    p[s::lookaheadOn] = 0.0f;
    p[s::style] = s::styleValue (s::Style::vocal);
    CHECK (s::styleOf (p[s::style]) == s::Style::vocal);
    const auto vocal = run (EffectKind::sentryGate, p, x);
    const auto early = gainOver (vocal, 0.6, 0.7), mid = gainOver (vocal, 1.05, 1.15), late = gainOver (vocal, 2.35, 2.45);
    std::printf ("  Sentry Vocal on -20 dBFS noise after a pause: %.1f dB at 0.15 s, %.1f at 0.6 s, %.1f at 1.9 s (reference -13.6 / -8.0 / -2.8)\n",
                 early, mid, late);
    CHECK (early < -10.0 && early > -17.0);
    CHECK (mid < -5.0 && mid > -11.0 && mid > early);
    CHECK (late > -6.5 && late > mid);
    // the range limits it
    auto pr = p;
    pr[s::range] = 0.1f; // 10 dB
    const auto ranged = gainOver (run (EffectKind::sentryGate, pr, x), 0.6, 0.7);
    CHECK (ranged > early + 3.0 && ranged > -10.0 && ranged < -3.0);
    // a 1 kHz sine after the same pause passes untouched
    auto t = tone (1000.0, -20.0, 3.0);
    std::fill (t.begin(), t.begin() + (std::ptrdiff_t) (0.5 * sr), 0.0f);
    const auto ty = run (EffectKind::sentryGate, p, t);
    CHECK (std::abs (rmsDb (ty, (std::size_t) (0.6 * sr), (std::size_t) (0.7 * sr)) - rmsDb (t, (std::size_t) (0.6 * sr), (std::size_t) (0.7 * sr))) < 0.3);
    // the other styles have none of it: the noise comes through at 0 dB once the gate is open
    for (auto st : { s::Style::classic, s::Style::clean, s::Style::guitar })
    {
        auto q = p;
        q[s::style] = s::styleValue (st);
        const auto g = gainOver (run (EffectKind::sentryGate, q, x), 0.6, 0.7);
        CHECK (std::abs (g) < 0.1);
    }
}

TEST (p190v_projects_saved_before_keep_the_classic_laws)
{
    using namespace lynx;
    namespace c = dyn::crest;
    namespace s = dyn::sentry;
    using namespace dyn108;
    // new instances run the measured laws; a state saved before the parameter existed loads with it off, a state saved
    // with it keeps what it had (both values survive a load)
    for (const auto kind : { EffectKind::crestCompressor, EffectKind::sentryGate })
    {
        const auto law = (std::size_t) (kind == EffectKind::crestCompressor ? (int) c::law : (int) s::law);
        // (round 190dm appended Sentry's trigger controls after it)
        CHECK (effectInfo (kind).params.size() == (kind == EffectKind::sentryGate ? (std::size_t) s::count : law + 1)
               && std::string (effectInfo (kind).params[law].name) == "Law");
        auto fresh = makeEffectSlot (kind);
        CHECK (fresh.params[law] >= 0.5f);
        auto old = makeEffectSlot (kind);
        upgradeLoadedEffectParams (old, law); // saved by 0.1.25: one parameter short
        CHECK (old.params[law] < 0.5f);
        for (const float v : { 0.0f, 1.0f })
        {
            auto again = makeEffectSlot (kind);
            again.params[law] = v;
            upgradeLoadedEffectParams (again, law + 1);
            CHECK (again.params[law] == v);
        }
    }
    // the classic laws are round 108's, as 0.1.25 rendered them (1 kHz stairs at threshold −36, ratio 4, knee 0, attack
    // 10 ms, release 200 ms: the gain at −48, −36, −24, −12, −6 and 0 dBFS, taken from the build whose classic renders
    // hash-match 0.1.25's, /mnt/e/lynx-work/parity190v/hash/hash.md)
    const std::vector<double> levels { -48, -36, -24, -12, -6, 0 };
    std::size_t per = 0;
    const auto x = stair (levels, 0.5, 1000.0, per);
    const struct { c::Style style; double gain[6]; } classic[] {
        { c::Style::twoWay, { -2.184, -4.312, -9.160, -16.432, -20.069, -23.704 } },
        { c::Style::variMu, { 0.000, 0.000, -6.304, -15.916, -20.587, -25.258 } },
    };
    for (const auto& row : classic)
    {
        auto p = defaults (EffectKind::crestCompressor);
        p[c::style] = c::styleValue (row.style);
        p[c::threshold] = 0.4f;
        p[c::ratio] = c::ratioValue (4.0);
        p[c::knee] = 0.0f;
        p[c::attack] = c::attackValue (10.0);
        p[c::release] = c::releaseValue (200.0);
        p[c::autoGain] = 0.0f;
        p[c::law] = 0.0f;
        const auto old = stairGains (x, run (EffectKind::crestCompressor, p, x), per, levels.size());
        p[c::law] = 1.0f;
        const auto now = stairGains (x, run (EffectKind::crestCompressor, p, x), per, levels.size());
        std::printf ("  classic style %d: %.3f %.3f %.3f %.3f %.3f %.3f\n", (int) row.style, old[0], old[1], old[2], old[3], old[4], old[5]);
        double apart = 0.0;
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            CHECK (std::abs (old[i] - row.gain[i]) < 0.02);
            apart = std::max (apart, std::abs (old[i] - now[i]));
        }
        CHECK (apart > 1.0); // and the measured law is another one
        // another style does not hear the switch
        p[c::style] = c::styleValue (c::Style::clean);
        const auto a = run (EffectKind::crestCompressor, p, x);
        p[c::law] = 0.0f;
        CHECK (a == run (EffectKind::crestCompressor, p, x));
    }
    // Sentry: with the law off the Vocal style has no noise reduction (noise after a pause comes through at 0 dB)
    auto n = p190v::noise (-20.0, 3.0, 190u);
    std::fill (n.begin(), n.begin() + (std::ptrdiff_t) (0.5 * sr), 0.0f);
    auto q = defaults (EffectKind::sentryGate);
    q[s::threshold] = 0.0f;
    q[s::lookaheadOn] = 0.0f;
    q[s::style] = s::styleValue (s::Style::vocal);
    q[s::law] = 0.0f;
    const auto y = run (EffectKind::sentryGate, q, n);
    const auto a = (std::size_t) (0.6 * sr), b = (std::size_t) (0.7 * sr);
    CHECK (std::abs (rmsDb (y, a, b) - rmsDb (n, a, b)) < 0.1);
    q[s::style] = s::styleValue (s::Style::classic);
    const auto off = run (EffectKind::sentryGate, q, n);
    q[s::law] = 1.0f;
    CHECK (off == run (EffectKind::sentryGate, q, n));
}
