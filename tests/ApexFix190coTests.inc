// Round 190co: Apex Limiter's true-peak ceiling and the true-peak meter (core/Loudness.h TruePeak).
//
// Round 190cn measured the 0.1.26 limiter with True peak on and the ceiling at −1 dB: up to +1.07 dBTP with
// oversampling off and −0.76 dBTP at 2× … 8× on a 16× reference meter, while LYNX's own meter read the same files up to
// 1.35 dB low (its four points sat between the samples and its 12-tap filter was short). These tests carry that
// reference meter (16×, 128 taps per phase, Kaiser β = 10, the sample peak folded in) and hold both to it.

#include "core/ApexLimiter.h"
#include "core/Loudness.h"
#include "core/Mastering.h"

namespace apexfix190co
{
constexpr double pi = 3.14159265358979323846;

// the reference: the highest |value| of x[from … to) oversampled 16× (dB), and never under the sample peak
inline double reference16x (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    constexpr int L = 16, T = 128, n = L * T;
    static const std::vector<double> h = [] {
        std::vector<double> c ((std::size_t) n);
        std::vector<double> sum ((std::size_t) L, 0.0);
        for (int k = 0; k < n; ++k)
        {
            const double t = (k - (n - 1) / 2.0) / L;
            c[(std::size_t) k] = (std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t)) * lynx::loudness::kaiser ((k - (n - 1) / 2.0) / ((n - 1) / 2.0), 10.0);
            sum[(std::size_t) (k % L)] += c[(std::size_t) k];
        }
        for (int k = 0; k < n; ++k) c[(std::size_t) k] /= sum[(std::size_t) (k % L)]; // every phase passes DC at unity
        return c;
    }();
    double peak = 0.0;
    for (auto i = from; i < to; ++i) peak = std::max (peak, (double) std::abs (x[i]));
    for (auto i = std::max (from, (std::size_t) T); i < to; ++i)
        for (int ph = 0; ph < L; ++ph)
        {
            double s = 0.0;
            for (int j = 0; j < T; ++j) s += h[(std::size_t) (j * L + ph)] * x[i - (std::size_t) j];
            peak = std::max (peak, std::abs (s));
        }
    return 20.0 * std::log10 (std::max (peak, 1e-12));
}

struct Noise
{
    unsigned seed;
    double next() noexcept { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 8388608.0 - 1.0; }
};

// round 190cn's three signals (n samples; `rate` only places the tones)
// a dense mix: low sines, kick-like thumps, noise bursts and two-sample clicks, peak 0.708 (−3 dBFS)
inline std::vector<float> mix (int n, double rate, unsigned seed)
{
    std::vector<double> x ((std::size_t) n, 0.0);
    Noise rnd { seed };
    const double f[] = { 55.0, 110.0, 220.0, 660.0, 1760.0, 5300.0 }, a[] = { 0.5, 0.3, 0.2, 0.15, 0.1, 0.05 };
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 6; ++k) x[(std::size_t) i] += a[k] * std::sin (2.0 * pi * f[k] * i / rate);
    for (int start = 0; start < n; start += (int) (0.25 * rate)) // thumps
        for (int i = 0; i < (int) (0.2 * rate) && start + i < n; ++i)
        {
            const double t = i / rate;
            x[(std::size_t) (start + i)] += 1.2 * std::sin (2.0 * pi * (50.0 + 120.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 14.0);
        }
    for (int start = (int) (0.0625 * rate); start < n; start += (int) (0.125 * rate)) // noise bursts
    {
        const int m = (int) (0.03 * rate);
        for (int i = 0; i < m && start + i < n; ++i) x[(std::size_t) (start + i)] += 0.5 * 1.7 * rnd.next() * std::exp (-i / (m / 4.0));
    }
    for (int k = 0; k < 12; ++k) // clicks
    {
        const auto at = (std::size_t) ((rnd.next() * 0.5 + 0.5) * (n - 10));
        const double s = rnd.next() < 0.0 ? -1.0 : 1.0;
        x[at] += s; x[at + 1] += s;
    }
    double peak = 0.0;
    for (const auto v : x) peak = std::max (peak, std::abs (v));
    std::vector<float> y ((std::size_t) n);
    for (int i = 0; i < n; ++i) y[(std::size_t) i] = (float) (x[(std::size_t) i] / peak * 0.7079);
    return y;
}
// a sine a quarter of the rate, 45° off the samples: samples ±0.707, true peak 1
inline std::vector<float> quarterSine (int n)
{
    std::vector<float> y ((std::size_t) n);
    for (int i = 0; i < n; ++i) y[(std::size_t) i] = (float) std::sin (0.5 * pi * i + 0.25 * pi);
    return y;
}
// a sweep from 500 Hz to 0.45 of the rate, 3 dB over full scale and clipped there
inline std::vector<float> clippedSweep (int n, double rate)
{
    std::vector<float> y ((std::size_t) n);
    const double secs = n / rate;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        y[(std::size_t) i] = (float) std::clamp (1.41 * std::sin (2.0 * pi * (500.0 * t + 0.5 * (rate * 0.45 - 500.0) / secs * t * t)), -1.0, 1.0);
    }
    return y;
}
// 20 ms in and out, so neither meter sees a cut edge
inline void fade (std::vector<float>& x, double rate)
{
    const auto m = (std::size_t) (0.02 * rate);
    for (std::size_t i = 0; i < m && i < x.size(); ++i)
    {
        const auto w = (float) (0.5 - 0.5 * std::cos (pi * (double) i / (double) m));
        x[i] *= w;
        x[x.size() - 1 - i] *= w;
    }
}

inline lynx::EffectParams defaults()
{
    lynx::EffectParams p {};
    const auto& inf = lynx::apex::info();
    for (std::size_t i = 0; i < inf.params.size(); ++i) p[i] = inf.params[i].defaultValue;
    return p;
}
inline void process (lynx::Effect& fx, std::vector<float>& l, std::vector<float>& r, const lynx::EffectParams& p, int block = 512)
{
    for (std::size_t i = 0; i < l.size(); i += (std::size_t) block)
        fx.process (l.data() + i, r.data() + i, (int) std::min ((std::size_t) block, l.size() - i), p, 120.0);
}
inline double meterDb (std::vector<float> x, double rate)
{
    lynx::loudness::Meter m;
    m.prepare (rate);
    auto r = x;
    for (std::size_t i = 0; i < x.size(); i += 512) m.process (x.data() + i, r.data() + i, (int) std::min ((std::size_t) 512, x.size() - i));
    m.flushTruePeak();
    return m.truePeakDb();
}
} // namespace apexfix190co

// The estimator against its plain form: the 2× sequence (the samples and the 128-tap midpoints), the 4× sequence (the
// 12-tap midpoints of that), the parabola through every local maximum; push (x[n]) answers for the four 4× values
// around x[n − delay].
TEST (apexfix190co_true_peak_estimator_is_the_plain_cascade)
{
    using lynx::loudness::TruePeak;
    constexpr int n = 6000, T1 = TruePeak::taps, T2 = TruePeak::taps2;
    CHECK (TruePeak::delay == 68 && TruePeak::phases == 4);
    std::vector<double> x ((std::size_t) n, 0.0);
    apexfix190co::Noise rnd { 190 };
    for (int i = 0; i < n; ++i)
        x[(std::size_t) i] = (double) (float) (i < 200 || (i > 2000 && i < 2600) ? 0.0 : rnd.next() * (i % 1500 < 300 ? 1e-3 : 0.9)); // (silent stretches: the gate)
    auto window = [] (int count, double beta) {
        std::vector<double> c ((std::size_t) count);
        double sum = 0.0;
        for (int j = 0; j < count; ++j)
        {
            const double t = j - (count - 1) / 2.0;
            c[(std::size_t) j] = std::sin (apexfix190co::pi * t) / (apexfix190co::pi * t) * lynx::loudness::kaiser (t / ((count - 1) / 2.0), beta);
            sum += c[(std::size_t) j];
        }
        for (auto& v : c) v /= sum;
        return c;
    };
    const auto c1 = window (T1, 10.0), c2 = window (T2, 8.0);
    auto at = [] (const std::vector<double>& v, long long i) { return i >= 0 && i < (long long) v.size() ? v[(std::size_t) i] : 0.0; };
    std::vector<double> s2 ((std::size_t) n * 2), s4 ((std::size_t) n * 4);
    for (long long k = 0; k < n; ++k)
    {
        s2[(std::size_t) (2 * k)] = x[(std::size_t) k];
        double m = 0.0;
        for (int j = 0; j < T1; ++j) m += c1[(std::size_t) j] * at (x, k + T1 / 2 - j);
        s2[(std::size_t) (2 * k + 1)] = m;
    }
    for (long long i = 0; i < 2 * n; ++i)
    {
        s4[(std::size_t) (2 * i)] = s2[(std::size_t) i];
        double m = 0.0;
        for (int j = 0; j < T2; ++j) m += c2[(std::size_t) j] * at (s2, i + T2 / 2 - j);
        s4[(std::size_t) (2 * i + 1)] = m;
    }
    TruePeak tp;
    double p0 = 0.0, p1 = 0.0, worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double got = tp.push (x[(std::size_t) i]);
        double want = 0.0;
        for (long long q = 4LL * (i - TruePeak::delay) - 2; q < 4LL * (i - TruePeak::delay) + 2; ++q)
        {
            const double a = std::abs (at (s4, q));
            if (p1 >= p0 && p1 >= a && 2.0 * p1 - p0 - a > 0.0) want = std::max (want, p1 + 0.125 * (p0 - a) * (p0 - a) / (2.0 * p1 - p0 - a));
            want = std::max (want, a);
            p0 = p1; p1 = a;
        }
        worst = std::max (worst, std::abs (got - want));
    }
    std::printf ("  true-peak estimator vs its plain form: worst difference %.2g\n", worst);
    CHECK (worst < 2e-5);
    // cleared: silent again
    tp.clear();
    CHECK (tp.push (0.0) < 1e-6 && tp.quiet());
}

// The meter against the 16× reference: within 0.1 dB on round 190cn's material, which is what a limiter (the 0.1.26
// one, modelled by a clip of the samples) leaves behind. The 0.1.26 meter read these 0.2 … 1.4 dB low.
TEST (apexfix190co_true_peak_meter_matches_the_16x_reference)
{
    using namespace apexfix190co;
    const double rate = 48000.0;
    const int n = 36000;
    struct Case { const char* name; std::vector<float> x; };
    std::vector<Case> cases;
    cases.push_back ({ "quarter-rate sine at 45 degrees", quarterSine (n) });
    cases.push_back ({ "clipped sweep", clippedSweep (n, rate) });
    {
        auto x = mix (n, rate, 7);
        for (auto& v : x) v = std::clamp (v * 3.98f, -0.891f, 0.891f); // + 12 dB, the samples clipped at −1 dB
        cases.push_back ({ "mix + 12 dB clipped at -1 dB", x });
    }
    {
        auto x = mix (n, rate, 11);
        for (auto& v : x) v *= 1.3f;
        cases.push_back ({ "mix, not limited", x });
    }
    {
        std::vector<float> x ((std::size_t) n);
        Noise rnd { 3 };
        for (auto& v : x) v = (float) std::clamp (rnd.next() * 1.6, -0.891, 0.891);
        cases.push_back ({ "white noise clipped at -1 dB", x });
    }
    for (auto& c : cases)
    {
        fade (c.x, rate);
        const double ref = reference16x (c.x, 0, c.x.size()), got = meterDb (c.x, rate);
        std::printf ("  true-peak meter, %-32s reference %7.3f dBTP, LYNX %7.3f (%+.3f)\n", c.name, ref, got, got - ref);
        CHECK (std::abs (got - ref) < 0.1);
    }
}

// A peak that lies on a sample is a true peak too, also when it is the last thing in a file.
TEST (apexfix190co_true_peak_is_never_under_the_sample_peak)
{
    for (const int at : { 100, 4000, 4799 })
    {
        std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
        l[(std::size_t) at] = 0.9f;
        double lufs = 0.0, tp = 0.0;
        lynx::mastering::measure (l, r, 48000.0, lufs, tp);
        CHECK (std::abs (tp - 20.0 * std::log10 (0.9)) < 0.01);
        lynx::loudness::Meter m;
        m.prepare (48000.0);
        m.process (l.data(), r.data(), 4800);
        CHECK (m.truePeakDb() >= m.samplePeakDb() && m.takeBlockTruePeak() >= 0.9 - 1e-6);
    }
    // a flat top of clipped samples: the 0.1.26 meter read −1.6 dB here (its points sat between the samples)
    std::vector<float> l (4800, 0.0f), r;
    for (int i = 1000; i < 1004; ++i) l[(std::size_t) i] = (i & 1) != 0 ? 0.8f : -0.8f;
    l[2000] = 0.8f;
    r = l;
    CHECK (apexfix190co::meterDb (l, 48000.0) >= 20.0 * std::log10 (0.8) - 1e-6);
}

// The point of the round: with True peak on, what leaves the limiter stays at the ceiling on the 16× reference meter
// (+ 0.1 dB at most), at every oversampling setting and rate. The 0.1.26 limiter gave + 1.3 … + 2.1 dB with oversampling
// off and + 0.1 … + 0.25 dB with it on.
TEST (apexfix190co_limiter_holds_the_true_peak_ceiling)
{
    using namespace apexfix190co;
    using namespace lynx::apex;
    struct Setting { double rate; int os; };
    const Setting settings[] = { { 44100.0, 0 }, { 44100.0, 2 }, { 48000.0, 0 }, { 48000.0, 1 }, { 48000.0, 2 }, { 48000.0, 3 }, { 96000.0, 0 }, { 96000.0, 1 } };
    double worst = -100.0;
    for (const auto& s : settings)
    {
        const int n = (int) (s.rate == 96000.0 ? 0.6 * s.rate : 1.0 * s.rate);
        for (int sig = 0; sig < 3; ++sig)
        {
            auto l = sig == 0 ? mix (n, s.rate, 7) : sig == 1 ? clippedSweep (n, s.rate) : quarterSine (n);
            auto r = l;
            if (sig == 0) std::reverse (r.begin(), r.end()); // (two different channels)
            auto p = defaults();
            p[pGain] = (sig == 0 ? 12.0f : 6.0f) / 30.0f;
            p[pCeiling] = 1.0f - 1.0f / 30.0f; // −1 dB
            p[pTruePeak] = 1.0f;
            p[pOversampling] = (float) s.os / 3.0f;
            auto fx = make (&p);
            fx->prepare (s.rate);
            process (*fx, l, r, p);
            const auto from = (std::size_t) (0.3 * s.rate);
            l.erase (l.begin(), l.begin() + (long) from);
            r.erase (r.begin(), r.begin() + (long) from);
            fade (l, s.rate);
            fade (r, s.rate);
            const double tp = std::max (reference16x (l, 0, l.size()), sig == 0 ? reference16x (r, 0, r.size()) : -100.0);
            float sample = 0.0f;
            for (const auto v : l) sample = std::max (sample, std::abs (v));
            worst = std::max (worst, tp);
            if (tp > -1.0 + 0.1 || tp < -1.5)
                std::printf ("  apex true peak: %.0f Hz, oversampling %dx, signal %d: %.3f dBTP (sample peak %.3f dB)\n", s.rate, 1 << s.os, sig, tp, 20.0 * std::log10 (sample));
            CHECK (tp <= -1.0 + 0.1); // the ceiling holds
            CHECK (tp > -1.5);        // and nothing is given away: the peaks sit at it
            CHECK (sample <= (float) std::pow (10.0, -1.0 / 20.0) * 1.00001f);
            // the plugin's own read-out (the highest output true peak) agrees
            CHECK (fx->controlOutput (7) <= -1.0f + 0.1f);
        }
    }
    std::printf ("  apex true peak, ceiling -1 dB: the highest output over 24 renders %.3f dBTP (16x reference)\n", worst);
}

// Harder to hold: no look-ahead (the core's gain is a clipper), the loudest style into a 0 dB ceiling, Unity gain.
TEST (apexfix190co_limiter_holds_the_ceiling_at_extreme_settings)
{
    using namespace apexfix190co;
    using namespace lynx::apex;
    const double rate = 48000.0;
    const int n = 48000;
    struct Case { const char* name; float look, style, ceiling, unity, gainDb; double limit; };
    const Case cases[] = { { "look-ahead 0", 0.0f, 0.0f, 1.0f - 1.0f / 30.0f, 0.0f, 12.0f, -1.0 },
                           { "Loud, ceiling 0 dB, + 18 dB", 0.2f, 5.0f / 7.0f, 1.0f, 0.0f, 18.0f, 0.0 },
                           { "Unity gain", 0.2f, 0.0f, 1.0f - 1.0f / 30.0f, 1.0f, 12.0f, -13.0 } };
    for (const auto& c : cases)
        for (const int os : { 0, 2 })
        {
            auto l = mix (n, rate, 7);
            auto r = l;
            auto p = defaults();
            p[pGain] = c.gainDb / 30.0f; p[pCeiling] = c.ceiling; p[pLookahead] = c.look; p[pStyle] = c.style; p[pUnity] = c.unity;
            p[pOversampling] = (float) os / 3.0f;
            auto fx = make (&p);
            fx->prepare (rate);
            process (*fx, l, r, p);
            l.erase (l.begin(), l.begin() + 14400);
            fade (l, rate);
            const double tp = reference16x (l, 0, l.size());
            std::printf ("  apex true peak, %s, oversampling %dx: %.3f dBTP (limit %.1f)\n", c.name, 1 << os, tp, c.limit);
            CHECK (tp <= c.limit + 0.1 && tp > c.limit - 0.5);
        }
}

// Treble only: bursts of noise differenced once, so nearly all of it sits in the top octave and the peaks between
// the samples stand 3 … 4 dB over them, each the sum of many samples either side. A dip of the gain around such a
// peak that is only as deep as ceiling / peak leaves it about 0.1 dB over (the far samples keep their size): the
// guard dips 6 % deeper.
TEST (apexfix190co_limiter_holds_the_ceiling_on_treble_only_peaks)
{
    using namespace apexfix190co;
    using namespace lynx::apex;
    const double rate = 48000.0;
    const int n = 96000;
    for (const float gainDb : { 12.0f, 18.0f })
    {
        Noise rnd { 5 };
        std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
        double last = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = rnd.next();
            l[(std::size_t) i] = (float) (0.25 * (v - last) * (i % 12000 < 3000 ? 1.0 : 0.25));
            last = v;
        }
        for (int i = 0; i < n; ++i) r[(std::size_t) i] = l[(std::size_t) (n - 1 - i)];
        auto p = defaults();
        p[pGain] = gainDb / 30.0f;
        p[pCeiling] = 1.0f; // 0 dB
        p[pTruePeak] = 1.0f;
        auto fx = make (&p);
        fx->prepare (rate);
        process (*fx, l, r, p);
        l.erase (l.begin(), l.begin() + 14400);
        r.erase (r.begin(), r.begin() + 14400);
        fade (l, rate);
        fade (r, rate);
        const double tp = std::max (reference16x (l, 0, l.size()), reference16x (r, 0, r.size()));
        std::printf ("  apex true peak, treble-only bursts, + %.0f dB into 0 dB: %.3f dBTP\n", (double) gainDb, tp);
        CHECK (tp <= 0.06 && tp > -0.5);
    }
}

// Latency: with True peak on the guard's delay is reported (and a click leaves after exactly that many samples); with it
// off the latency is the one of 0.1.26. Under the ceiling the guard is a delay and nothing else, bit for bit.
TEST (apexfix190co_latency_is_reported_and_exact)
{
    using namespace lynx::apex;
    CHECK (guardLatency (48000.0) == 85 && guardLatency (44100.0) == 85 && guardLatency (96000.0) == 97);
    CHECK (latencyAt (48000.0, false) == 244 && latencyAt (44100.0, false) == 225 && latencyAt (96000.0, false) == 484);
    CHECK (latencyAt (48000.0) == 244 + 85 && latencyFor (48000.0, 4, false) - latencyFor (48000.0, 1, false) == latencyFor (48000.0, 4) - latencyFor (48000.0, 1));
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int os : { 0, 1, 3 })
            for (const bool truePeak : { false, true })
            {
                auto p = apexfix190co::defaults();
                p[pGain] = 0.0f;
                p[pTruePeak] = truePeak ? 1.0f : 0.0f;
                p[pOversampling] = (float) os / 3.0f;
                auto fx = make (&p);
                const int want = latencyFor (rate, 1 << os, truePeak);
                fx->prepare (rate);
                CHECK (fx->latencySamples() == want); // known before the first block (the mixer's delay compensation)
                CHECK (want == latencyFor (rate, 1 << os, false) + (truePeak ? guardLatency (rate) : 0));
                std::vector<float> a (4096, 0.0f), b (4096, 0.0f);
                a[100] = b[100] = 0.25f;
                fx->process (a.data(), b.data(), 4096, p, 120.0);
                CHECK (fx->latencySamples() == want);
                int at = 0;
                for (int i = 1; i < 4096; ++i) if (std::abs (a[(std::size_t) i]) > std::abs (a[(std::size_t) at])) at = i;
                CHECK (at == 100 + want);
                // the switch moves the report with the next block
                auto q = p;
                q[pTruePeak] = truePeak ? 0.0f : 1.0f;
                fx->process (a.data(), b.data(), 64, q, 120.0);
                CHECK (fx->latencySamples() == latencyFor (rate, 1 << os, ! truePeak));
            }
    // under the ceiling, oversampling off: the input, delayed, to the bit (the guard's gain is exactly 1)
    auto p = apexfix190co::defaults();
    p[pGain] = 0.0f;
    auto fx = make (&p);
    fx->prepare (48000.0);
    std::vector<float> l (9600), r (9600);
    apexfix190co::Noise rnd { 5 };
    for (std::size_t i = 0; i < l.size(); ++i) { l[i] = (float) (0.3 * rnd.next()); r[i] = (float) (0.3 * rnd.next()); }
    const auto inL = l, inR = r;
    apexfix190co::process (*fx, l, r, p, 300);
    const auto lat = (std::size_t) fx->latencySamples();
    int different = 0;
    for (auto i = lat; i < l.size(); ++i) different += (l[i] != inL[i - lat]) + (r[i] != inR[i - lat]);
    CHECK (different == 0);
    // and with a block of any size: the same output as with small blocks (the guard works in chunks of 1024)
    auto fx2 = make (&p);
    fx2->prepare (48000.0);
    auto l2 = inL, r2 = inR;
    p[pGain] = 0.4f;
    fx->reset();
    l = inL; r = inR;
    apexfix190co::process (*fx, l, r, p, 64);
    apexfix190co::process (*fx2, l2, r2, p, 9600);
    different = 0;
    for (std::size_t i = 0; i < l.size(); ++i) different += l[i] != l2[i];
    CHECK (different == 0);
}

// Audition and Unity gain with the guard in the chain: output + what Audition plays is what it was without the guard
// (the input × gain, delayed), only later by the guard's latency: Audition plays what the core and the guard remove
// together. Unity gain is the same output ÷ gain.
TEST (apexfix190co_audition_and_unity_include_the_guard)
{
    using namespace lynx::apex;
    const double rate = 48000.0;
    const int n = 24000;
    for (const int os : { 0, 2 })
    {
        const auto in = apexfix190co::mix (n, rate, 7);
        auto p = apexfix190co::defaults();
        p[pGain] = 12.0f / 30.0f;
        p[pOversampling] = (float) os / 3.0f;
        auto render = [&] (lynx::EffectParams q, bool truePeak, int param) {
            q[pTruePeak] = truePeak ? 1.0f : 0.0f;
            if (param >= 0) q[(std::size_t) param] = 1.0f;
            auto fx = make (&q);
            fx->prepare (rate);
            auto l = in, r = in;
            apexfix190co::process (*fx, l, r, q);
            return l;
        };
        const auto out = render (p, true, -1), removed = render (p, true, pAudition), unity = render (p, true, pUnity);
        const auto outOff = render (p, false, -1), removedOff = render (p, false, pAudition);
        const auto lat = (std::size_t) latencyFor (rate, 1 << os, true), guard = (std::size_t) guardLatency (rate);
        const double g = std::pow (10.0, 12.0 / 20.0);
        double errInput = 0.0, errSame = 0.0, errUnity = 0.0, most = 0.0, more = 0.0;
        for (auto i = lat + 2000; i < (std::size_t) n; ++i)
        {
            if (os == 0) errInput = std::max (errInput, std::abs ((double) out[i] + removed[i] - g * in[i - lat]));
            errSame = std::max (errSame, std::abs ((double) out[i] + removed[i] - ((double) outOff[i - guard] + removedOff[i - guard])));
            errUnity = std::max (errUnity, std::abs ((double) unity[i] * g - out[i]));
            most = std::max (most, (double) std::abs (removed[i]));
            more = std::max (more, std::abs ((double) removed[i] - removedOff[i - guard]));
        }
        std::printf ("  apex with the guard, oversampling %dx: output + audition vs input x gain %.2g, vs the same without the guard %.2g, unity x gain vs output %.2g\n",
                     1 << os, errInput, errSame, errUnity);
        // (oversampled, the 0.1.26 chain clips its output's samples once more at the base rate, which its Audition
        // never played: the sums differ by what that clip took, a few thousandths)
        CHECK (errInput < 1e-4 && errSame < (os == 0 ? 1e-5 : 0.01) && errUnity < 1e-5 && most > 0.5);
        CHECK (more > 1e-3); // (Audition does play what the guard takes off)
    }
}

// True peak off is the limiter of 0.1.26: the same latency and the same samples (values taken from a 0.1.26 build).
TEST (apexfix190co_true_peak_off_is_the_0126_limiter)
{
    using namespace lynx::apex;
    const double rate = 48000.0;
    const int n = 24000;
    const int at[] = { 300, 1234, 5000, 9999, 12345, 17000, 23999 };
    // (seven samples of each channel, then Σ|left| and Σ|right|; oversampling off, then 4×)
    const double golden[2][16] = {
        { 0.850093186, 0.714264035, -0.106273673, 0.323803037, 0.241509572, 0.384221703, 0.188192934, -0.089988783, 0.0685212761, 0.155899942, -0.160913751, -0.0923467278, 0.119298324, -0.278179497, 7587.26846, 7513.85391 },
        { 0.109058872, 0.746363103, -0.0584203713, 0.330869615, 0.217091814, 0.373394012, 0.194639221, -0.134828448, -0.110012978, 0.389718205, -0.370645285, -0.187773183, 0.0212126411, -0.0439765677, 7139.08137, 7082.94125 },
    };
    for (int os = 0; os < 2; ++os)
    {
        auto l = apexfix190co::mix (n, rate, 7);
        auto r = l;
        std::reverse (r.begin(), r.end());
        auto p = apexfix190co::defaults();
        p[pGain] = 12.0f / 30.0f;
        p[pTruePeak] = 0.0f;
        p[pOversampling] = os == 0 ? 0.0f : 2.0f / 3.0f;
        auto fx = make (&p);
        fx->prepare (rate);
        CHECK (fx->latencySamples() == (os == 0 ? 244 : 299));
        apexfix190co::process (*fx, l, r, p);
        double sumL = 0.0, sumR = 0.0, worst = 0.0;
        for (int i = 0; i < n; ++i) { sumL += std::abs (l[(std::size_t) i]); sumR += std::abs (r[(std::size_t) i]); }
        for (int k = 0; k < 7; ++k)
        {
            worst = std::max (worst, std::abs ((double) l[(std::size_t) at[k]] - golden[os][k]));
            worst = std::max (worst, std::abs ((double) r[(std::size_t) at[k]] - golden[os][7 + k]));
        }
        std::printf ("  apex, true peak off, oversampling %s: worst sample difference to 0.1.26 %.2g, sums %.2g / %.2g\n", os == 0 ? "off" : "4x", worst,
                     std::abs (sumL - golden[os][14]), std::abs (sumR - golden[os][15]));
        CHECK (worst < 2e-6);
        CHECK (std::abs (sumL - golden[os][14]) < 2e-3 && std::abs (sumR - golden[os][15]) < 2e-3);
    }
}

// Silence and quiet programme cost next to nothing in the guard and the meter (their filters sleep), and waking up
// loses nothing: a loud burst after a long silence is held like any other.
TEST (apexfix190co_guard_sleeps_and_wakes)
{
    using namespace lynx::apex;
    const double rate = 48000.0;
    auto p = apexfix190co::defaults();
    p[pGain] = 12.0f / 30.0f;
    p[pCeiling] = 1.0f - 1.0f / 30.0f;
    auto fx = make (&p);
    fx->prepare (rate);
    std::vector<float> l (48000, 0.0f);
    const auto burst = apexfix190co::clippedSweep (9600, rate);
    for (int i = 0; i < 9600; ++i) l[(std::size_t) (30000 + i)] = burst[(std::size_t) i];
    auto r = l;
    apexfix190co::process (*fx, l, r, p);
    bool silent = true;
    for (int i = 0; i < 29000; ++i) silent = silent && l[(std::size_t) i] == 0.0f;
    CHECK (silent);
    const double tp = apexfix190co::reference16x (l, 29000, 48000);
    std::printf ("  apex true peak, a burst after silence: %.3f dBTP\n", tp);
    CHECK (tp <= -1.0 + 0.1 && tp > -1.6);
}
