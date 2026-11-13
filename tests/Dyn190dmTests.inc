// Round 190dm: dynamics parity (docs/PARITY.md "Round 190dm") — Sentry Gate's transient trigger, Glow Leveler's cell memory
// (Opto classic voicing) and Hush De-esser's Air / Mix. Every new control is appended and off by default; the first test
// proves that the sound with them off is the sound of the base commit.
//
// The helper part only uses parameters the base commit already had, so the same code built against the base objects
// printed the goldens below (define D190DM_HELPERS_ONLY to include the helpers alone).
namespace d190dm
{
using lynx::EffectKind;
using lynx::EffectParams;

struct Case
{
    EffectKind kind;
    std::vector<std::pair<int, float>> set; // parameter index -> value (all of them existed before the round)
};

struct Render
{
    std::uint64_t hash = 1469598103934665603ull;
    double energy = 0.0, a = 0.0, b = 0.0;
};

// 1.5 s at 48 kHz: a 1 kHz tone at −6 dBFS (0.2 … 0.5 s), noise bursts at −14 dBFS (0.6 … 0.65 s and 0.9 … 1.2 s), a
// 7 kHz burst at −20 dBFS (1.25 … 1.3 s), the right channel a little different; blocks of 256 samples
inline Render render (const Case& c)
{
    constexpr double sr = 48000.0, pi = 3.141592653589793;
    auto slot = lynx::makeEffectSlot (c.kind);
    for (const auto& [k, v] : c.set)
        slot.params[(std::size_t) k] = v;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    const int n = (int) (1.5 * sr), block = 256;
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    unsigned s = 190u;
    const auto noise = [&s] {
        s = s * 1664525u + 1013904223u;
        return (double) (s >> 8) / 8388608.0 - 1.0;
    };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        double x = 0.0, y = 0.0;
        if (t >= 0.2 && t < 0.5)
        {
            x = 0.5 * std::sin (2.0 * pi * 1000.0 * t);
            y = 0.45 * std::sin (2.0 * pi * 1000.0 * t + 0.3);
        }
        if ((t >= 0.6 && t < 0.65) || (t >= 0.9 && t < 1.2))
        {
            x += 0.35 * noise();
            y += 0.3 * noise();
        }
        if (t >= 1.25 && t < 1.3)
        {
            x += 0.1 * std::sin (2.0 * pi * 7000.0 * t);
            y += 0.1 * std::sin (2.0 * pi * 7000.0 * t);
        }
        l[(std::size_t) i] = (float) x;
        r[(std::size_t) i] = (float) y;
    }
    for (int off = 0; off < n; off += block)
        fx->process (l.data() + off, r.data() + off, std::min (block, n - off), slot.params, 120.0);
    Render out;
    for (int i = 0; i < n; ++i)
        for (const float v : { l[(std::size_t) i], r[(std::size_t) i] })
        {
            std::uint32_t u;
            std::memcpy (&u, &v, 4);
            for (int k = 0; k < 4; ++k)
            {
                out.hash ^= (u >> (8 * k)) & 0xffu;
                out.hash *= 1099511628211ull;
            }
            out.energy += (double) v * v;
        }
    out.a = l[(std::size_t) (n / 3 - 1001)];
    out.b = r[(std::size_t) (n * 2 / 3)];
    return out;
}

// the settings proven unchanged: defaults and the other voicings / styles of the three effects (indices of the base commit)
inline std::vector<Case> cases()
{
    return {
        { EffectKind::sentryGate, {} },                                              // Sentry defaults
        { EffectKind::sentryGate, { { 0, 0.5f }, { 1, 0.4f }, { 3, 0.4f } } },       // Vocal style (its noise reduction on)
        { EffectKind::sentryGate, { { 0, 0.75f }, { 6, 0.5f }, { 9, 1.0f } } },      // Guitar, hold, look-ahead
        { EffectKind::sentryGate, { { 1, 0.7f }, { 27, 0.34f }, { 25, 1.0f } } },    // 2x oversampling, ducking
        { EffectKind::hushDeEsser, {} },                                             // Hush defaults
        { EffectKind::hushDeEsser, { { 9, 1.0f }, { 2, 0.7f } } },                   // classic split voicing
        { EffectKind::hushDeEsser, { { 19, 1.0f }, { 1, 0.3f } } },                  // vocal voicing
        { EffectKind::glowLeveler, {} },                                             // Glow defaults (Opto classic, measured)
        { EffectKind::glowLeveler, { { 7, 0.0f }, { 0, 0.7f } } },                   // Glow's own engine
        { EffectKind::glowLeveler, { { 10, 0.0f }, { 8, 0.0f }, { 0, 0.75f } } },    // round 123's release and law
        { EffectKind::glowLeveler, { { 2, 1.0f }, { 0, 0.8f } } },                   // Limit
    };
}
} // namespace d190dm

#ifndef D190DM_HELPERS_ONLY
namespace d190dm
{
struct Golden
{
    std::uint64_t hash;
    double energy, a, b;
};
// printed by the helpers above built against the base commit (77026c3a2, g++ 15, glibc 2.43, x86-64)
const Golden goldens[] = {
#include "Dyn190dmGoldens.inc"
};

// a tone burst through the gate: `hz` at `db` dBFS from `t0` s for `len` s, on a quiet bed of `bedDb` (none: −200)
inline std::vector<float> toneAt (double hz, double db, double t0, double len, double total, double bedDb = -200.0)
{
    std::vector<float> x ((std::size_t) (total * 48000.0));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        const double lv = t >= t0 && t < t0 + len ? db : bedDb;
        x[i] = (float) (std::pow (10.0, lv / 20.0) * std::sin (2.0 * 3.141592653589793 * hz * t));
    }
    return x;
}
// output against input over 1 ms around `t` (dB)
inline double gainAt (const std::vector<float>& x, const std::vector<float>& y, double t)
{
    const auto a = (std::size_t) (t * 48000.0), b = a + 48;
    double ex = 0.0, ey = 0.0;
    for (auto i = a; i < b && i < x.size(); ++i)
    {
        ex += (double) x[i] * x[i];
        ey += (double) y[i] * y[i];
    }
    return 10.0 * std::log10 ((ey + 1e-30) / (ex + 1e-30));
}
inline EffectParams transientGate (float shape = 0.5f, double releaseMs = 500.0, double rangeDb = 100.0)
{
    namespace s = lynx::dyn::sentry;
    auto p = lynx::makeEffectSlot (EffectKind::sentryGate).params;
    p[s::trigger] = 1.0f;
    p[s::threshold] = 0.0f; // −60 dB
    p[s::range] = (float) (rangeDb / 100.0);
    p[s::release] = (float) std::cbrt (releaseMs / 5000.0);
    p[s::hold] = (float) std::sqrt ((18.75 + 0.056 * releaseMs) / 250.0);
    p[s::attack] = 0.1316f; // 0.3 ms
    p[s::releaseShape] = shape;
    return p;
}
} // namespace d190dm

TEST (dyn190dm_new_controls_off_sound_like_the_base_commit)
{
    using namespace d190dm;
    const auto cs = cases();
    CHECK (cs.size() == std::size (goldens));
    int same = 0;
    for (std::size_t k = 0; k < cs.size() && k < std::size (goldens); ++k)
    {
        const auto r = render (cs[k]);
        const auto& g = goldens[k];
        // the same sound with any maths library: energy within 0.01 %, two samples within 2e-6
        CHECK (std::abs (r.energy - g.energy) <= 1e-4 * g.energy + 1e-12);
        CHECK (std::abs (r.a - g.a) < 2e-6);
        CHECK (std::abs (r.b - g.b) < 2e-6);
        same += r.hash == g.hash ? 1 : 0;
        if (std::getenv ("LYNX_D190DM_VERBOSE") != nullptr)
            std::printf ("    case %d: hash %016llx (golden %016llx)\n", (int) k, (unsigned long long) r.hash, (unsigned long long) g.hash);
    }
    std::printf ("  off: %d of %d renders hash-identical to the base commit\n", same, (int) cs.size());
    // bit for bit where the maths library is the one the goldens were made with (glibc 2.43 on x86-64: the local builds);
    // other libm versions may round transcendental calls differently, which says nothing about old projects
   #if defined (__linux__) && defined (__x86_64__) && defined (__GLIBC__) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 43) \
       && ! defined (LYNX_VERB_HASH_OFF)
    CHECK (same == (int) cs.size());
   #endif
    // the new controls are appended, with the defaults that keep that sound
    namespace s = lynx::dyn::sentry;
    const auto sentry = lynx::makeEffectSlot (EffectKind::sentryGate).params;
    CHECK (sentry[s::trigger] == 0.0f && ! s::transientTrigger (sentry));
    const auto hush = lynx::makeEffectSlot (EffectKind::hushDeEsser).params;
    CHECK (hush[lynx::mixsuite::hush::air] == 0.0f && hush[lynx::mixsuite::hush::mix] == 1.0f);
    const auto glow = lynx::makeEffectSlot (EffectKind::glowLeveler).params;
    CHECK (glow[lynx::mixsuite::glow::optoMemory] == 0.0f);
}

TEST (dyn190dm_transient_gate_opens_on_onsets_and_follows_its_curve)
{
    using namespace d190dm;
    namespace s = lynx::dyn::sentry;
    // a 1 kHz tone at −40 dBFS that jumps to −6 dBFS at 1.5 s: open at once, hold, then the Mid curve (measured on the
    // reference within 0.2 dB rms) whatever the level does
    std::vector<float> x ((std::size_t) (3.0 * 48000.0));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        x[i] = (float) (std::pow (10.0, (t >= 1.5 && t < 1.7 ? -6.0 : -40.0) / 20.0) * std::sin (2.0 * 3.141592653589793 * 1000.0 * t));
    }
    const auto y = dyn108::run (EffectKind::sentryGate, transientGate(), x);
    CHECK (std::abs (gainAt (x, y, 1.505)) < 0.3);                 // open within milliseconds
    const double hold = (18.75 + 0.056 * 500.0) * 0.001;
    for (const double tau : { 0.05, 0.15, 0.3, 0.5, 0.65 })
    {
        const auto want = s::transientCurveDb (s::Shape::mid, tau, 0.5);
        CHECK (std::abs (gainAt (x, y, 1.5 + hold + tau) - want) < 0.6);
    }
    CHECK (gainAt (x, y, 2.5) < -90.0);                            // closed down to the range, though the tone goes on
    // a stair of +6 dB steps is no onset: closed after the first, it stays closed
    std::vector<float> st ((std::size_t) (4.0 * 48000.0));
    for (std::size_t i = 0; i < st.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        st[i] = (float) (std::pow (10.0, (-30.0 + 6.0 * std::floor (t)) / 20.0) * std::sin (2.0 * 3.141592653589793 * 1000.0 * t));
    }
    auto ramp = st;
    for (std::size_t i = 0; i < 48; ++i)
        ramp[i] *= (float) i / 48.0f;
    const auto ys = dyn108::run (EffectKind::sentryGate, transientGate(), ramp);
    for (const double t : { 1.5, 2.5, 3.5 })
        CHECK (gainAt (ramp, ys, t) < -90.0);
    // the detector weights the highs (a first difference): 100 Hz at −48 dBFS from silence does not open it, 8 kHz does
    const auto lo = toneAt (100.0, -48.0, 0.3, 0.5, 1.0), hi = toneAt (8000.0, -48.0, 0.3, 0.5, 1.0);
    CHECK (gainAt (lo, dyn108::run (EffectKind::sentryGate, transientGate(), lo), 0.35) < -90.0);
    CHECK (gainAt (hi, dyn108::run (EffectKind::sentryGate, transientGate(), hi), 0.35) > -0.5);
    // with the side chain low-passed (a kick's), the first difference is unity at the low-pass: a 100 Hz burst opens it
    auto kick = transientGate();
    kick[s::lowPass] = s::filterValue (150.0);
    kick[s::threshold] = (float) ((-25.0 + 60.0) / 60.0);
    const auto k60 = toneAt (100.0, -6.0, 0.3, 0.3, 1.0);
    CHECK (gainAt (k60, dyn108::run (EffectKind::sentryGate, kick, k60), 0.36) > -0.5);
    // under the threshold nothing opens it
    auto quiet = transientGate();
    quiet[s::threshold] = 0.5f; // −30 dB
    const auto q = toneAt (8000.0, -40.0, 0.3, 0.5, 1.0);
    CHECK (gainAt (q, dyn108::run (EffectKind::sentryGate, quiet, q), 0.35) < -90.0);
}

TEST (dyn190dm_transient_gate_shapes_range_and_release)
{
    using namespace d190dm;
    namespace s = lynx::dyn::sentry;
    // the curves: Smooth a straight line in dB, Sharp closes first, Mid in between; the time scales with the release
    CHECK (std::abs (s::transientCurveDb (s::Shape::smooth, 0.5, 0.5) + 16.1) < 1e-9);
    CHECK (s::transientCurveDb (s::Shape::sharp, 0.4, 0.5) < s::transientCurveDb (s::Shape::mid, 0.4, 0.5));
    CHECK (s::transientCurveDb (s::Shape::mid, 0.4, 0.5) < s::transientCurveDb (s::Shape::smooth, 0.4, 0.5) + 0.5);
    CHECK (std::abs (s::transientCurveDb (s::Shape::mid, 0.1, 0.1) - s::transientCurveDb (s::Shape::mid, 2.0, 2.0)) < 1e-9);
    CHECK (s::transientCurveDb (s::Shape::mid, 0.75, 0.5) <= -100.0);
    // the range stops the curve: range 40 runs the same curve until −40
    const auto x = toneAt (1000.0, -6.0, 0.2, 1.8, 2.0, -40.0);
    const auto y100 = dyn108::run (EffectKind::sentryGate, transientGate (0.0f), x);
    const auto y40 = dyn108::run (EffectKind::sentryGate, transientGate (0.0f, 500.0, 40.0), x);
    const double hold = (18.75 + 0.056 * 500.0) * 0.001;
    CHECK (std::abs (gainAt (x, y100, 0.2 + hold + 0.8) - gainAt (x, y40, 0.2 + hold + 0.8)) < 0.3);
    CHECK (std::abs (gainAt (x, y40, 1.9) + 40.0) < 0.3);
    // a short release closes sooner than a long one
    const auto fast = dyn108::run (EffectKind::sentryGate, transientGate (0.5f, 60.0), x);
    CHECK (gainAt (x, fast, 0.4) < -90.0 && gainAt (x, y100, 0.4) > -12.0);
    // every value is finite, the other settings of the gate (ducking, oversampling) run with it
    auto p = transientGate();
    p[s::ducking] = 1.0f;
    p[s::oversampling] = 0.34f;
    for (const auto v : dyn108::run (EffectKind::sentryGate, p, x))
        CHECK (std::isfinite (v));
    CHECK (lynx::dyn::paramText (EffectKind::sentryGate, s::trigger, 1.0f) == "transient");
    CHECK (lynx::dyn::paramText (EffectKind::sentryGate, s::releaseShape, 1.0f) == "sharp");
    CHECK (std::abs (s::sensitivityOf (s::sensitivityValue (5.0)) - 5.0) < 1e-6);
}

TEST (dyn190dm_hush_air_and_mix)
{
    using namespace d190dm;
    namespace h = lynx::mixsuite::hush;
    // Air: the measured shelf (8.15 kHz, S 0.5) on what Hush puts out; a quiet tone is not de-essed, so it shows the shelf
    auto p = lynx::makeEffectSlot (EffectKind::hushDeEsser).params;
    p[h::voicing] = 1.0f; // classic split voicing (no latency)
    const auto lowTone = toneAt (500.0, -40.0, 0.0, 1.0, 1.0), highTone = toneAt (16000.0, -40.0, 0.0, 1.0, 1.0);
    const auto g = [&] (const EffectParams& q, const std::vector<float>& x) { return gainAt (x, dyn108::run (EffectKind::hushDeEsser, q, x), 0.8); };
    const auto lowOff = g (p, lowTone), highOff = g (p, highTone);
    auto a = p;
    a[h::air] = (float) (8.5 / 12.0);
    CHECK (std::abs (g (a, lowTone) - lowOff) < 0.1);
    lynx::Biquad sh;
    sh.shelf (48000.0, h::airHz, 8.5, true, h::airSlope);
    const double w = 2.0 * 3.141592653589793 * 16000.0 / 48000.0;
    // |H(e^jw)| of the shelf: numerator and denominator at z^-1 = e^-jw
    const auto cplx = [w] (double c0, double c1, double c2) {
        const double re = c0 + c1 * std::cos (w) + c2 * std::cos (2.0 * w), im = -c1 * std::sin (w) - c2 * std::sin (2.0 * w);
        return std::sqrt (re * re + im * im);
    };
    const auto mag = cplx (sh.b0, sh.b1, sh.b2) / cplx (1.0, sh.a1, sh.a2);
    CHECK (std::abs ((g (a, highTone) - highOff) - 20.0 * std::log10 (mag)) < 0.2);
    CHECK (20.0 * std::log10 (mag) > 7.0);
    // Mix 0: the dry signal, aligned with the latency (the modern voicing has 2 ms of look-ahead)
    auto m = lynx::makeEffectSlot (EffectKind::hushDeEsser).params;
    m[h::mix] = 0.0f;
    m[h::range] = 1.0f;
    const auto x = toneAt (7000.0, -6.0, 0.0, 1.0, 1.0);
    const auto y = dyn108::run (EffectKind::hushDeEsser, m, x);
    double e = 0.0, ex = 0.0;
    for (std::size_t i = 4800; i < 40000; ++i)
    {
        e += ((double) y[i] - x[i]) * ((double) y[i] - x[i]);
        ex += (double) x[i] * x[i];
    }
    CHECK (10.0 * std::log10 (e / ex + 1e-30) < -100.0);
    // Mix 50 %: half the processed signal plus half the dry one, sample for sample (the dry aligned with the latency)
    auto half = lynx::makeEffectSlot (EffectKind::hushDeEsser).params;
    half[h::range] = 1.0f;
    const auto wet = dyn108::run (EffectKind::hushDeEsser, half, x);
    CHECK (gainAt (x, wet, 0.8) < -3.0);
    half[h::mix] = 0.5f;
    const auto mixed = dyn108::run (EffectKind::hushDeEsser, half, x);
    double worst = 0.0;
    for (std::size_t i = 0; i + 4800 < x.size(); ++i)
        worst = std::max (worst, std::abs ((double) mixed[i] - (0.5 * wet[i] + 0.5 * x[i])));
    CHECK (worst < 1e-6);
    CHECK (lynx::mixsuite::paramText (EffectKind::hushDeEsser, h::air, 0.0f) == "off");
}

TEST (dyn190dm_glow_memory_recovers_fast_after_short_bursts_slowly_after_long_ones)
{
    using namespace d190dm;
    namespace gl = lynx::mixsuite::glow;
    auto p = lynx::makeEffectSlot (EffectKind::glowLeveler).params;
    p[gl::peakReduction] = 0.6f;
    p[gl::optoMemory] = 1.0f;
    // the reduction left 0.5 s after a 20 ms and after a 2 s burst at −6 dBFS (a −40 dBFS 1 kHz probe shows it)
    const auto left = [&] (const EffectParams& q, double len) {
        const auto x = toneAt (1000.0, -6.0, 0.3, len, 0.3 + len + 1.2, -40.0);
        const auto y = dyn108::run (EffectKind::glowLeveler, q, x);
        return gainAt (x, y, 0.3 + len + 1.1) - gainAt (x, y, 0.3 + len + 0.5);
    };
    const auto shortB = left (p, 0.02), longB = left (p, 2.0);
    CHECK (shortB < longB - 0.5); // longer exposure, slower recovery: more of it still to come
    // off: the release before (the first test proves it bit for bit at the defaults)
    auto off = p;
    off[gl::optoMemory] = 0.0f;
    const auto x = toneAt (1000.0, -6.0, 0.3, 0.5, 2.0, -40.0);
    CHECK (dyn108::run (EffectKind::glowLeveler, off, x) != dyn108::run (EffectKind::glowLeveler, p, x));
    for (const auto v : dyn108::run (EffectKind::glowLeveler, p, x))
        CHECK (std::isfinite (v));
    // Limit ignores it
    auto lim = p;
    lim[gl::mode] = 1.0f;
    auto lim0 = lim;
    lim0[gl::optoMemory] = 0.0f;
    CHECK (dyn108::run (EffectKind::glowLeveler, lim, x) == dyn108::run (EffectKind::glowLeveler, lim0, x));
}
#endif
