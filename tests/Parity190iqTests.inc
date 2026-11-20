// Round 190iq: Hush De-esser's measured single-vocal / Allround engine (vmatch::vocalds) gets timing revision 1, fitted
// black-box on refcap030's Pro-DS captures (the `ess` probe: a 500 Hz body under sibilant bursts, 35 settings; the gain
// on the body at 0.25 ms steps; /mnt/e/lynx-work/parity190iq/HANDOFF.md). The revision is a hidden parameter: a new Hush
// and a translated Pro-DS state run it, a project or preset saved before keeps round 148 / tp172b's timing bit for bit.
// Checked: the parameter and its load rules, the translation, revision 0 against the base commit's renders (fingerprints
// from its objects, /mnt/e/lynx-work/parity190iq/gold), and revision 1's own timing.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/Effects.h"
#include "core/MixSuite.h"

namespace parity190iq
{
namespace hu = lynx::mixsuite::hush;
// the gold input: a 500 Hz body (-14 dBFS) with resonant 7 kHz hiss bursts of rising level (as gold/gold.cpp)
inline void signal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    unsigned s = 4242u;
    double y1 = 0.0, y2 = 0.0;
    const double w = 2.0 * M_PI * 7000.0 / sr, rr = 0.93, a1 = 2.0 * rr * std::cos (w), a2 = -rr * rr;
    const int n = (int) l.size();
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        s = s * 1664525u + 1013904223u;
        const double nz = (double) (s >> 8) / 8388608.0 - 1.0;
        const double y = nz + a1 * y1 + a2 * y2; y2 = y1; y1 = y;
        const int k = (int) (t / 0.25);
        const double ph = t - 0.25 * k, lvl = std::pow (10.0, (-36.0 + 6.0 * (k % 6)) / 20.0);
        const double burst = ph > 0.05 && ph < 0.2 ? lvl * 0.05 * y : 0.0;
        const double v = 0.2 * std::sin (2.0 * M_PI * 500.0 * t) + burst;
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (0.9 * v);
    }
}
inline lynx::EffectSlot slot (bool vocal)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::hushDeEsser);
    s.params[hu::rangeLaw] = 1.0f;
    s.params[hu::vocalVoicing] = vocal ? 1.0f : 0.0f;
    s.params[hu::allround] = vocal ? 0.0f : 1.0f;
    s.params[hu::lookaheadTime] = 10.0f / 15.0f;
    return s;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size(), block = 256;
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), s.params, 120.0);
}
// the gain (dB) on a 500 Hz body: the output's 500 Hz component over the input's, 2 ms windows, latency removed
inline std::vector<double> bodyGainDb (const std::vector<float>& in, const std::vector<float>& out, int latency, double sr)
{
    const int win = (int) (0.002 * sr), n = (int) in.size() - latency;
    std::vector<double> g;
    for (int a = 0; a + win <= n; a += win / 4)
    {
        double ri = 0, ii = 0, ro = 0, io = 0;
        for (int i = a; i < a + win; ++i)
        {
            const double c = std::cos (2.0 * M_PI * 500.0 * i / sr), sn = std::sin (2.0 * M_PI * 500.0 * i / sr);
            ri += in[(std::size_t) i] * c; ii += in[(std::size_t) i] * sn;
            ro += out[(std::size_t) (i + latency)] * c; io += out[(std::size_t) (i + latency)] * sn;
        }
        g.push_back (10.0 * std::log10 ((ro * ro + io * io + 1e-30) / (ri * ri + ii * ii + 1e-30)));
    }
    return g;
}
} // namespace parity190iq

TEST (parity190iq_hush_timing_is_hidden_new_instances_run_it_old_projects_do_not)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    const auto& info = effectInfo (EffectKind::hushDeEsser);
    CHECK (info.params.size() == (std::size_t) hu::timing); // hidden: not in the info, so no new control or string
    auto s = makeEffectSlot (EffectKind::hushDeEsser);
    CHECK (s.params[(std::size_t) hu::timing] == 1.0f);
    CHECK (savedEffectParamCount (s) == (std::size_t) hu::vocalRevision + 1); // saved, since it is not zero (round 190js: and the
                                                                                // Single Vocal revision after it)
    auto now = s;
    upgradeLoadedEffectParams (now, (std::size_t) hu::timing + 1);
    CHECK (now.params[(std::size_t) hu::timing] == 1.0f);
    // saved before this round (the named values only): round 148 / tp172b's timing, and saved again it stays that way
    auto old = s;
    upgradeLoadedEffectParams (old, (std::size_t) hu::timing);
    CHECK (old.params[(std::size_t) hu::timing] == 0.0f);
    CHECK (old.params[(std::size_t) hu::vocalRevision] == 0.0f && savedEffectParamCount (old) == (std::size_t) hu::timing);
}

TEST (parity190iq_pro_ds_translation_runs_the_measured_timing)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    // her first state's values (round 165x's test state)
    std::vector<float> st { 0.0f, 0.2715f, 6.0f, 0.0f, 0.75f, 1.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                            0.0f, 0.0f, 0.0f, 0.2f, 0.0f, -1.0f, 0.0f, 0.0f };
    int n = 0;
    const auto s = m124x::substitute ("FabFilter Pro-DS", p165w::fabf (st), n);
    CHECK (s.kind == EffectKind::hushDeEsser && s.params[(std::size_t) hu::timing] == 1.0f);
    st[0] = 1.0f; // Allround
    const auto a = m124x::substitute ("FabFilter Pro-DS", p165w::fabf (st), n);
    CHECK (a.params[(std::size_t) hu::allround] == 1.0f && a.params[(std::size_t) hu::timing] == 1.0f);
}

TEST (parity190iq_hush_timing0_renders_as_the_base_commit_did)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    struct Case
    {
        int voc, idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // 1.6 s of the gold input in blocks of 256: FNV-1a of the float bits, the energy, two samples. From the base commit's
    // objects (/mnt/e/lynx-work/parity190iq/gold: gold_base.txt; the round's objects with the timing forced to 0 print the
    // same lines, gold_new.txt)
    const Case cases[] = {
        { 1, -1, 0.00000000f, -1, 0.00000000f, 48000.0, 0xcb2091111d13551cull, 2776.64223, 0.173205078, 0 },
        { 1, 1, 0.300000012f, 12, 0.00000000f, 44100.0, 0x33d11631fae75275ull, 2549.68013, 0.169534072, 0.00641005859 },
        { 0, -1, 0.00000000f, -1, 0.00000000f, 48000.0, 0x1fadfc958d6d0b0dull, 2764.61469, 0.173205078, 0 },
        { 0, 1, 0.400000006f, 3, 1.00000000f, 96000.0, 0x34a2320b385f30c8ull, 4356.9863, 0.173205078, -1.14727216e-14 },
        { 0, 6, 0.00000000f, 2, 0.250000000f, 48000.0, 0x1182c1b6f882c147ull, 2768.71729, 0.173205078, 0 },
    };
    for (const auto& c : cases)
    {
        auto s = parity190iq::slot (c.voc != 0);
        s.params[(std::size_t) hu::timing] = 0.0f;
        s.params[(std::size_t) hu::vocalRevision] = 0.0f; // round 190js: a project saved before has Single Vocal's revision off too
        if (c.idx >= 0) s.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0) s.params[(std::size_t) c.idx2] = c.val2;
        const int n = (int) (1.6 * c.sr);
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        parity190iq::signal (l, r, c.sr);
        parity190iq::render (s, c.sr, l, r);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b) { h ^= (u >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
                e += (double) v * v;
            }
       #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 43))
        CHECK (h == c.hash); // the libm the fingerprints were taken with (sin / pow / log10 / exp may differ elsewhere)
       #endif
        CHECK (std::abs (e - c.energy) < 1e-6 * c.energy + 1e-9);
        CHECK (std::abs ((double) l[(std::size_t) (n / 3)] - c.a) < 1e-6 && std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 1e-6);
    }
}

TEST (parity190iq_hush_timing1_moves_the_gain_later_and_releases_slower)
{
    using namespace lynx;
    namespace hu = mixsuite::hush;
    // Allround, wide band, look-ahead 10 ms, at 48 kHz: a 500 Hz body (-14 dBFS) with a -6 dBFS 7 kHz burst at 0.5 … 0.7 s;
    // the gain on the body in 2 ms windows, 0.5 ms steps
    const double sr = 48000.0;
    const int n = (int) sr;
    std::vector<float> in ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        in[(std::size_t) i] = (float) (0.2 * std::sin (2.0 * M_PI * 500.0 * t) + (t >= 0.5 && t < 0.7 ? 0.5 * std::sin (2.0 * M_PI * 7000.0 * t) : 0.0));
    }
    const auto gain = [&] (float timing, float lookahead) {
        auto s = parity190iq::slot (false);
        s.params[(std::size_t) hu::mode] = 1.0f; // wide band (the translated Pro-DS default)
        s.params[(std::size_t) hu::timing] = timing;
        s.params[(std::size_t) hu::lookahead] = lookahead;
        auto l = in, r = in;
        auto fx = makeEffect (s);
        fx->prepare (sr);
        const int lat = fx->latencySamples();
        for (int off = 0; off < n; off += 256)
            fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
        return parity190iq::bodyGainDb (in, l, lat, sr);
    };
    const auto at = [] (double ms) { return (std::size_t) std::lround (ms / 0.5); };
    const auto first = [&] (const std::vector<double>& g, double from, double to, bool below, double db) {
        for (auto i = at (from); i < at (to) && i < g.size(); ++i)
            if (below ? g[i] < db : g[i] > db) return (double) i * 0.5;
        return 1e9;
    };
    const auto g0 = gain (0.0f, 1.0f), g1 = gain (1.0f, 1.0f);
    const double on0 = first (g0, 470, 520, true, -1.0), on1 = first (g1, 470, 520, true, -1.0);
    const double off0 = first (g0, 690, 760, false, -1.0), off1 = first (g1, 690, 760, false, -1.0);
    std::printf ("  hush timing: onset %.1f -> %.1f ms, recovery %.1f -> %.1f ms (revision 0 -> 1)\n", on0, on1, off0, off1);
    CHECK (on1 - on0 > 2.5 && on1 - on0 < 6.0);  // the measured look-ahead: ~4 ms less at 10 ms
    CHECK (off1 - off0 > 5.0 && off1 - off0 < 20.0); // the slower release (~3.3 times)
    // the level law is the same: the reduction in the middle of the burst within 0.1 dB
    double m0 = 0, m1 = 0;
    for (auto i = at (590); i < at (600); ++i) { m0 += g0[i]; m1 += g1[i]; }
    const double k = (double) (at (600) - at (590));
    CHECK (m0 / k < -10.0 && std::abs (m0 / k - m1 / k) < 0.1);
    // without look-ahead the measured ahead is clamped at 0: the gain does not move before the burst
    const auto gz = gain (1.0f, 0.0f);
    CHECK (first (gz, 470, 497, true, -0.3) > 1e8);
}
