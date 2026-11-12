// Round 190bz: the 0.1.27 engine law of LYNX's reverbs (core/LynxVerbs.h, law 1): per-band decay with a tone corrector
// after the loop, the diffusion-step onset, rotation scattering and matrix modulation. The classic engine (law 0) must
// render as it did: its goldens below come from the base commit's build (the work folder's gold.cpp linked against the
// objects of d141370db; the same program linked against this round's objects prints the same lines).
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/LynxVerbs.h"

namespace rv190bz
{
using lynx::EffectKind;

struct Stereo
{
    std::vector<float> l, r;
};

inline int lawIndex (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::epochVerb: return lynx::verbs::epoch::engineLaw;
        case EffectKind::atrium: return lynx::verbs::atrium::engineLaw;
        case EffectKind::foilPlate: return lynx::verbs::foil::engineLaw;
        case EffectKind::halo: return lynx::verbs::halo::engineLaw;
        default: return lynx::verbs::horizon::engineLaw;
    }
}

constexpr EffectKind kinds[] = { EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::halo, EffectKind::horizon };

// an impulse through the bare engine
inline Stereo engineImpulse (const lynx::verbs::Settings& s, double seconds, double sr = 48000.0, int block = 512)
{
    lynx::verbs::Engine e (0.3, 0.6);
    e.prepare (sr);
    const auto n = (std::size_t) (seconds * sr);
    Stereo x { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    x.l[0] = x.r[0] = 0.5f;
    for (std::size_t off = 0; off < n; off += (std::size_t) block)
        e.process (x.l.data() + off, x.r.data() + off, (int) std::min<std::size_t> ((std::size_t) block, n - off), s);
    return x;
}

// RT60 of one octave band (Schroeder curve of L and R, fitted between −5 and −25 dB)
inline double bandRt (const Stereo& x, double hz, double sr)
{
    lynx::Biquad hp[2], lp[2];
    for (auto& b : hp)
        b.filter (sr, 1, hz / std::sqrt (2.0), 0.7071);
    for (auto& b : lp)
        b.filter (sr, 0, std::min (hz * std::sqrt (2.0), 0.45 * sr), 0.7071);
    auto l = x.l, r = x.r;
    for (int k = 0; k < 2; ++k)
    {
        hp[k].processBlock (l.data(), r.data(), (int) l.size());
        lp[k].processBlock (l.data(), r.data(), (int) l.size());
    }
    const auto n = l.size();
    std::vector<double> e (n + 1, 0.0);
    for (std::size_t i = n; i-- > 0;)
        e[i] = e[i + 1] + (double) l[i] * l[i] + (double) r[i] * r[i];
    if (e[0] <= 0.0)
        return -1.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, m = 0;
    for (std::size_t i = 0; i < n; i += 16)
    {
        const auto d = 10.0 * std::log10 (std::max (e[i] / e[0], 1e-30));
        if (d > -5.0)
            continue;
        if (d < -25.0)
            break;
        const auto t = (double) i / sr;
        sx += t, sy += d, sxx += t * t, sxy += t * d, m += 1.0;
    }
    if (m < 8.0)
        return -1.0;
    const auto slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : -1.0;
}

inline double energy (const Stereo& x, double from, double to, double sr = 48000.0)
{
    double e = 0.0;
    for (auto i = (std::size_t) (from * sr); i < std::min ((std::size_t) (to * sr), x.l.size()); ++i)
        e += (double) x.l[i] * x.l[i] + (double) x.r[i] * x.r[i];
    return e;
}

inline lynx::verbs::Settings hall()
{
    lynx::verbs::Settings s;
    s.law = 1;
    s.shape = 0;
    s.rt60 = 2.0;
    s.inDiffusion = 0.7;
    s.loopDiffusion = 0.6;
    return s;
}
} // namespace rv190bz

TEST (reverb190bz_new_instances_run_the_law_and_saved_projects_keep_the_classic_engine)
{
    using namespace lynx;
    for (const auto kind : rv190bz::kinds)
    {
        const auto law = (std::size_t) rv190bz::lawIndex (kind);
        CHECK (effectInfo (kind).params.size() == law + 1 && std::string (effectInfo (kind).params[law].name) == "Law");
        CHECK (formatEffectParam (kind, (int) law, 0.0f, 120.0) == "Classic" && formatEffectParam (kind, (int) law, 1.0f, 120.0) == "Measured");
        auto fresh = makeEffectSlot (kind);
        CHECK (fresh.params[law] >= 0.5f);
        auto old = makeEffectSlot (kind);
        upgradeLoadedEffectParams (old, law); // saved by 0.1.26: one parameter short
        CHECK (old.params[law] < 0.5f);
        for (const float v : { 0.0f, 1.0f })
        {
            auto again = makeEffectSlot (kind);
            again.params[law] = v;
            upgradeLoadedEffectParams (again, law + 1);
            CHECK (again.params[law] == v);
            // the way a project stores and reads a slot back (the saved count and values): the choice survives
            const auto count = savedEffectParamCount (again);
            CHECK (count == law + 1);
            auto loaded = makeEffectSlot (kind);
            for (std::size_t i = 0; i < count; ++i)
                loaded.params[i] = std::clamp ((float) savedEffectParamValue (again.params[i]), 0.0f, 1.0f);
            upgradeLoadedEffectParams (loaded, count);
            CHECK (loaded.params[law] == v);
        }
    }
    // Nebula is not one of this round's five: the shared engine's law does not reach it. Its own "Law" is round 190ca's
    // cloud model, appended after "Clear" (rollup 0.1.27: both rounds together; tests/Nebula190caTests.inc)
    {
        const auto& nebula = effectInfo (EffectKind::nebula).params;
        CHECK (nebula.size() == 15 && std::string (nebula[13].name) == "Clear" && std::string (nebula[14].name) == "Law");
    }
}

TEST (reverb190bz_classic_law_renders_as_the_base_commit_did)
{
    using namespace lynx;
    struct Case
    {
        EffectKind kind;
        int idx;
        float val;
        int idx2;
        float val2;
        double sr;
        std::uint64_t hash;
        double energy, a, b;
    };
    // an impulse (0.5 / −0.25) and a noise burst, 1.5 s in blocks of 256: FNV-1a of the float bits, the energy, two samples
    const Case cases[] = {
        { EffectKind::epochVerb, -1, 0.0f, -1, 0.0f, 48000.0, 0xbb24196c2b093df2ull, 25.8472443, -0.000353809417, 0.037696518 },
        { EffectKind::epochVerb, 16, 18.0f / 21.0f, 15, 0.5f, 48000.0, 0xf9072fde8a43e06full, 9.16419441, -0.000176125264, -0.0116846953 },
        { EffectKind::atrium, -1, 0.0f, -1, 0.0f, 48000.0, 0xc8050e03eef55795ull, 66.1522467, 0.00122537941, -0.0534557216 },
        { EffectKind::atrium, 19, 6.0f / 11.0f, 8, 1.0f, 44100.0, 0x6d8c773df74233fcull, 38.5582605, -0.000479800801, 0.00734313345 },
        { EffectKind::foilPlate, -1, 0.0f, -1, 0.0f, 48000.0, 0xe8486cb2d3f1905aull, 35.2886998, -0.00197848585, 0.036238689 },
        { EffectKind::foilPlate, 14, 1.0f, 6, 0.5f, 96000.0, 0x54d239a8d18a991eull, 20.172879, 0.000635098142, 0.00332928193 },
        { EffectKind::halo, -1, 0.0f, -1, 0.0f, 48000.0, 0x5c253b0170d99db7ull, 64.8429635, 0.00013945876, 0.0024064756 },
        { EffectKind::halo, 2, 0.6f, 9, 0.375f, 48000.0, 0x72bb2d9e832b164cull, 66.7699818, -0.000178578062, 0.00680130813 },
        { EffectKind::horizon, -1, 0.0f, -1, 0.0f, 48000.0, 0x6832308cd3110132ull, 76.6312192, -0.000201465475, -0.00282000937 },
        { EffectKind::horizon, 22, 3.0f / 7.0f, 9, 0.5f, 44100.0, 0x3bd625bb69644864ull, 70.908988, -0.000346333807, -0.0044805808 },
    };
    int sameBits = 0;
    for (const auto& c : cases)
    {
        auto slot = makeEffectSlot (c.kind);
        slot.params[(std::size_t) rv190bz::lawIndex (c.kind)] = 0.0f;
        if (c.idx >= 0)
            slot.params[(std::size_t) c.idx] = c.val;
        if (c.idx2 >= 0)
            slot.params[(std::size_t) c.idx2] = c.val2;
        auto fx = makeEffect (slot);
        fx->prepare (c.sr);
        const int n = (int) (1.5 * c.sr), block = 256;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        unsigned s = 777u;
        l[0] = 0.5f;
        r[0] = -0.25f;
        for (int i = n / 3; i < n / 3 + 2000; ++i)
        {
            s = s * 1664525u + 1013904223u;
            l[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
            s = s * 1664525u + 1013904223u;
            r[(std::size_t) i] = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f;
        }
        for (int off = 0; off < n; off += block)
            fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
        std::uint64_t h = 1469598103934665603ull;
        double e = 0.0;
        for (int i = 0; i < n; ++i)
            for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
            {
                std::uint32_t u;
                std::memcpy (&u, &v, 4);
                for (int b = 0; b < 4; ++b)
                {
                    h ^= (u >> (8 * b)) & 0xffu;
                    h *= 1099511628211ull;
                }
                e += (double) v * v;
            }
        // the same sound with any maths library: energy within 0.01 %, two samples of the tail within 2e-6
        CHECK (std::abs (e - c.energy) <= 1e-4 * c.energy);
        CHECK (std::abs ((double) l[(std::size_t) (n / 7)] - c.a) < 2e-6);
        CHECK (std::abs ((double) r[(std::size_t) (n / 2)] - c.b) < 2e-6);
        sameBits += h == c.hash ? 1 : 0;
    }
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64: the local builds).
    // Other libm versions round some transcendental calls differently (GitHub's ubuntu-24.04 has glibc 2.39: 8 of 10),
    // which says nothing about old projects: a project is compared with the same platform's previous build, not across
    // libraries. The sound checks above hold everywhere.
    std::printf ("  classic law: %d of 10 renders hash-identical to the base commit\n", sameBits);
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (sameBits == 10);
   #endif
}

TEST (reverb190bz_band_decay_follows_its_targets)
{
    using namespace rv190bz;
    // synthetic settings: a three-band law with both crossovers and uneven per-band multipliers on top
    struct Row
    {
        double rt, lowMult, lowX, highMult, highX;
        double mult[8];
    };
    const Row rows[] = {
        { 2.0, 1.0, 300.0, 1.0, 8000.0, { 1, 1, 1, 1, 1, 1, 1, 1 } },                         // flat
        { 2.4, 1.4, 250.0, 0.5, 5000.0, { 1, 1, 1, 1, 1, 1, 1, 1 } },                         // the classic three-band shape
        { 1.6, 1.0, 300.0, 1.0, 8000.0, { 1.5, 1.4, 1.2, 1.0, 0.9, 0.75, 0.6, 0.45 } },       // a smooth tilt
        { 3.0, 1.2, 400.0, 0.7, 4000.0, { 0.85, 0.9, 1.0, 1.12, 1.05, 0.92, 0.85, 0.8 } },    // a bump (the shelves follow what is smooth over an octave or more)
    };
    const double bands[8] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000 };
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const auto& row : rows)
        {
            auto s = hall();
            s.rt60 = row.rt;
            s.lowMult = row.lowMult;
            s.lowXover = row.lowX;
            s.highMult = row.highMult;
            s.highXover = row.highX;
            for (int b = 0; b < 8; ++b)
                s.bandRtMult[b] = row.mult[b];
            double want[8];
            lynx::verbs::bandTargets (s, sr, 55.0, want);
            const auto x = engineImpulse (s, 2.2 * row.rt * 1.6, sr);
            double worst = 0.0;
            for (int b = 1; b < 8; ++b) // (63 Hz: too few modes in a 2 s tail for a steady slope)
            {
                const auto got = bandRt (x, bands[b], sr);
                // the interpolated read and the in-loop all-pass take a little off the top octave
                const auto tol = b == 7 ? 0.2 : b == 1 ? 0.18 : 0.12;
                CHECK (got > 0.0 && std::abs (got / want[b] - 1.0) < tol);
                worst = std::max (worst, std::abs (got / want[b] - 1.0));
                if (std::getenv ("LYNX_VERB_VERBOSE") != nullptr)
                    std::printf ("    %.0f Hz at %.0f: %.2f s (target %.2f)\n", bands[b], sr, got, want[b]);
            }
            if (sr == 48000.0)
                std::printf ("  band decay, rt %.1f s: worst band %.1f %% off its target\n", row.rt, 100.0 * worst);
        }
    // the tone corrector moves the level of a band, not its decay
    {
        auto s = hall();
        const auto flat = engineImpulse (s, 5.0);
        s.bandDb[4] = 6.0;
        s.bandDb[5] = 6.0;
        const auto lifted = engineImpulse (s, 5.0);
        const auto rt0 = bandRt (flat, 1000.0, 48000.0), rt1 = bandRt (lifted, 1000.0, 48000.0);
        CHECK (std::abs (rt1 / rt0 - 1.0) < 0.03);
        lynx::Biquad bp;
        auto band = [&] (Stereo v)
        {
            bp.filter (48000.0, 1, 1000.0, 0.7071);
            bp.reset();
            bp.processBlock (v.l.data(), v.r.data(), (int) v.l.size());
            bp.filter (48000.0, 0, 2000.0, 0.7071);
            bp.reset();
            bp.processBlock (v.l.data(), v.r.data(), (int) v.l.size());
            return energy (v, 0.0, 5.0);
        };
        const auto gainDb = 10.0 * std::log10 (band (lifted) / band (flat));
        CHECK (gainDb > 4.5 && gainDb < 7.0);
    }
}

TEST (reverb190bz_onset_density_and_matrix_modulation)
{
    using namespace rv190bz;
    // the onset network alone: a burst of unit energy per unit level, inside its span, different on the two sides
    {
        auto s = hall();
        s.lateLevel = 0.0;
        s.onsetLevel = 1.0;
        s.onsetSec = 0.04;
        const auto x = engineImpulse (s, 0.5);
        const auto all = energy (x, 0.0, 0.5), in = energy (x, 0.0, 0.045);
        CHECK (std::abs (10.0 * std::log10 (all / 0.5)) < 0.5); // input energy 2 × 0.25
        CHECK (in > 0.999 * all);
        int taps = 0;
        double lr = 0.0;
        for (std::size_t i = 0; i < (std::size_t) (0.045 * 48000.0); ++i)
        {
            taps += std::abs (x.l[i]) > 1e-6f ? 1 : 0;
            lr += (double) x.l[i] * x.r[i];
        }
        CHECK (taps >= 40);                    // 4 × 4 × 4 paths, some coincide
        CHECK (std::abs (lr) < 0.5 * 0.25);    // the sides are not copies of each other
    }
    // heard beside the tank it raises the share of the first 80 ms
    {
        auto s = hall();
        const auto plain = engineImpulse (s, 4.0);
        s.onsetLevel = 0.6;
        const auto with = engineImpulse (s, 4.0);
        const auto c80 = [] (const Stereo& x) { return 10.0 * std::log10 (energy (x, 0.0, 0.08) / energy (x, 0.08, 4.0)); };
        CHECK (c80 (with) > c80 (plain) + 1.0);
    }
    // rotation scattering: less mixing, a slower build-up of the echo density (fewer arrivals early on), the same decay
    {
        auto s = hall();
        s.inDiffusion = 0.0;
        s.loopDiffusion = 0.0;
        const auto count = [] (const Stereo& x)
        {
            int k = 0;
            for (std::size_t i = 0; i < (std::size_t) (0.25 * 48000.0); ++i)
                k += std::abs (x.l[i]) > 1e-5f ? 1 : 0;
            return k;
        };
        const auto full = engineImpulse (s, 5.0);
        s.mixAngle = 0.3;
        const auto less = engineImpulse (s, 5.0);
        CHECK (count (less) > 0 && count (full) > 0);
        const auto rtFull = bandRt (full, 1000.0, 48000.0), rtLess = bandRt (less, 1000.0, 48000.0);
        CHECK (std::abs (rtLess / rtFull - 1.0) < 0.1);
        CHECK (std::abs (10.0 * std::log10 (energy (less, 0.0, 5.0) / energy (full, 0.0, 5.0))) < 1.5);
    }
    // matrix modulation: the tail differs from the still one, its energy and decay stay
    {
        auto s = hall();
        const auto still = engineImpulse (s, 5.0);
        s.matrixModDepth = 0.4;
        s.matrixModRateHz = 1.3;
        const auto moving = engineImpulse (s, 5.0);
        double diff = 0.0;
        for (std::size_t i = 0; i < still.l.size(); ++i)
            diff += std::pow ((double) still.l[i] - moving.l[i], 2.0) + std::pow ((double) still.r[i] - moving.r[i], 2.0);
        CHECK (diff > 0.2 * energy (still, 0.0, 5.0));
        CHECK (std::abs (10.0 * std::log10 (energy (moving, 0.0, 5.0) / energy (still, 0.0, 5.0))) < 1.0);
        CHECK (std::abs (bandRt (moving, 1000.0, 48000.0) / bandRt (still, 1000.0, 48000.0) - 1.0) < 0.1);
    }
}

TEST (reverb190bz_stable_at_extreme_settings_rates_and_in_silence)
{
    using namespace lynx;
    using namespace rv190bz;
    // the five effects on law 1: every control at 0, at 1 and at seeded random values, three rates
    unsigned seed = 4242u;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; };
    for (const auto kind : kinds)
        for (const double sr : { 44100.0, 48000.0, 96000.0 })
            for (int variant = 0; variant < 5; ++variant)
            {
                auto slot = makeEffectSlot (kind);
                const auto count = effectInfo (kind).params.size();
                for (std::size_t p = 0; p < count; ++p)
                    slot.params[p] = variant == 0 ? 0.0f : variant == 1 ? 1.0f : rnd();
                slot.params[0] = 1.0f;
                slot.params[(std::size_t) lawIndex (kind)] = 1.0f;
                auto fx = makeEffect (slot);
                fx->prepare (sr);
                const int n = (int) (3.0 * sr);
                std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
                for (int i = 0; i < (int) (0.3 * sr); ++i)
                    l[(std::size_t) i] = 0.5f * (rnd() - 0.5f), r[(std::size_t) i] = 0.5f * (rnd() - 0.5f);
                for (int off = 0; off < n; off += 480)
                    fx->process (l.data() + off, r.data() + off, std::min (480, n - off), slot.params, 120.0);
                bool finite = true;
                float peak = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                    peak = std::max ({ peak, std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i]) });
                }
                CHECK (finite);
                CHECK (peak < 24.0f);
            }
    // the bare engine held "for ever" with the per-band multipliers at their ends: the tail may not grow
    for (int variant = 0; variant < 3; ++variant)
    {
        auto s = hall();
        s.rt60 = 1000.0;
        s.lowMult = variant == 1 ? 4.0 : 1.0;
        s.highMult = variant == 2 ? 4.0 : 0.3;
        for (int b = 0; b < 8; ++b)
            s.bandRtMult[b] = variant == 0 ? 50.0 : (b & 1) != 0 ? 4.0 : 0.25;
        s.modDepthMs = 1.5;
        s.modRateHz = 2.0;
        s.matrixModDepth = 0.7;
        s.onsetLevel = 1.0;
        s.onsetFeed = 1.0;
        const auto x = engineImpulse (s, 20.0);
        CHECK (energy (x, 18.0, 20.0) <= 1.02 * energy (x, 2.0, 4.0));
        bool finite = true;
        for (const auto v : x.l)
            finite = finite && std::isfinite (v);
        CHECK (finite);
    }
    // silence after a short tail: the output dies away without entering the denormal range
    {
        auto s = hall();
        s.rt60 = 0.3;
        for (int b = 0; b < 8; ++b)
            s.bandDb[b] = (b & 1) != 0 ? 4.0 : -4.0;
        s.onsetLevel = 0.5;
        const auto x = engineImpulse (s, 12.0);
        int subnormal = 0;
        float late = 0.0f;
        for (std::size_t i = 0; i < x.l.size(); ++i)
        {
            subnormal += std::fpclassify (x.l[i]) == FP_SUBNORMAL || std::fpclassify (x.r[i]) == FP_SUBNORMAL ? 1 : 0;
            if (i > x.l.size() - 48000)
                late = std::max ({ late, std::abs (x.l[i]), std::abs (x.r[i]) });
        }
        CHECK (subnormal == 0);
        CHECK (late < 1e-9f);
    }
    // any block size gives the same samples (the modulators and filters keep their state per sample)
    {
        auto s = hall();
        s.modDepthMs = 1.0;
        s.matrixModDepth = 0.3;
        s.onsetLevel = 0.4;
        s.bandRtMult[6] = 0.6;
        const auto a = engineImpulse (s, 1.0, 48000.0, 512), b = engineImpulse (s, 1.0, 48000.0, 37);
        double worst = 0.0;
        for (std::size_t i = 0; i < a.l.size(); ++i)
            worst = std::max (worst, (double) std::abs (a.l[i] - b.l[i]));
        CHECK (worst == 0.0);
    }
}

TEST (reverb190bz_unfitted_modes_stay_close_to_the_classic_law)
{
    using namespace lynx;
    using namespace rv190bz;
    // a mode without fitted values runs law 1 with neutral numbers: the decay per band and the level of the classic law
    auto slot = makeEffectSlot (EffectKind::epochVerb);
    slot.params[16] = 7.0f / 21.0f; // Bright Hall
    slot.params[12] = 0.0f;         // no modulation
    Stereo out[2];
    for (int law = 0; law < 2; ++law)
    {
        slot.params[(std::size_t) verbs::epoch::engineLaw] = (float) law;
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        const auto n = (std::size_t) (10.0 * 48000.0);
        out[law] = { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
        out[law].l[0] = out[law].r[0] = 0.5f;
        for (std::size_t off = 0; off < n; off += 512)
            fx->process (out[law].l.data() + off, out[law].r.data() + off, (int) std::min<std::size_t> (512, n - off), slot.params, 120.0);
    }
    for (const double hz : { 250.0, 1000.0, 4000.0 })
        CHECK (std::abs (bandRt (out[1], hz, 48000.0) / bandRt (out[0], hz, 48000.0) - 1.0) < 0.15);
    CHECK (std::abs (10.0 * std::log10 (energy (out[1], 0.0, 10.0) / energy (out[0], 0.0, 10.0))) < 1.5);
}

TEST (reverb190bz_fitted_voicings_wet_level_and_the_plate_decay_blend)
{
    using namespace lynx;
    using namespace rv190bz;
    const auto impulse = [] (EffectSlot slot, double seconds)
    {
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        const auto n = (std::size_t) (seconds * 48000.0);
        Stereo x { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
        x.l[0] = x.r[0] = 0.5f;
        for (std::size_t off = 0; off < n; off += 512)
            fx->process (x.l.data() + off, x.r.data() + off, (int) std::min<std::size_t> (512, n - off), slot.params, 120.0);
        return x;
    };
    // the wet-level laws: the default hall and the shimmer came out 1.9 dB and 3.0 dB under their references on the
    // classic law (BASELINE-TAIL of round 190bp); the fitted voicings carry the correction
    for (const auto kind : { EffectKind::epochVerb, EffectKind::halo })
    {
        auto slot = makeEffectSlot (kind);
        slot.params[0] = 1.0f;
        const auto law = (std::size_t) lawIndex (kind);
        const auto measured = impulse (slot, 8.0);
        slot.params[law] = 0.0f;
        const auto classic = impulse (slot, 8.0);
        const auto up = 10.0 * std::log10 (energy (measured, 0.0, 8.0) / energy (classic, 0.0, 8.0));
        std::printf ("  %s: wet level of the measured law %+.2f dB against the classic one\n", effectInfo (kind).name, up);
        CHECK (up > 0.8 && up < 5.0);
    }
    // the measured plate voicing is fitted at 1, 3 and 10 s and blended between: no step in level or decay across a knot
    {
        auto slot = makeEffectSlot (EffectKind::foilPlate);
        slot.params[0] = 1.0f;
        slot.params[14] = 1.0f;
        double lastDb = 0.0, lastRt = 0.0;
        int k = 0;
        for (const float decay : { 0.090f, 0.097f, 0.099f, 0.101f, 0.110f, 0.480f, 0.499f, 0.501f, 0.520f })
        {
            slot.params[2] = decay;
            const auto x = impulse (slot, 9.0);
            const auto db = 10.0 * std::log10 (energy (x, 0.0, 9.0)), rt = bandRt (x, 1000.0, 48000.0);
            if (k != 0 && k != 5)
            {
                CHECK (std::abs (db - lastDb) < 0.6);
                CHECK (std::abs (rt / lastRt - 1.0) < 0.12);
            }
            lastDb = db;
            lastRt = rt;
            ++k;
        }
    }
}
