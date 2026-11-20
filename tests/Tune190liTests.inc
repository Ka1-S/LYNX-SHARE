// Round 190li: the vocal pitch toolkit's CPU pass (core/VocalPitch.cpp: the YIN difference function, the full-rate
// refinement and the pitch marks' correlation summed lag by lag over linear scratch copies; LYNX Tune's per-sample
// coefficients once per block). The results are the same to the bit (checked offline with tools/lynxtunelab on the
// vocal lab's phrases); these tests guard what the change could break: scratch buffers carrying state from one call,
// one instance or one sample rate into the next, and the detector's tracking of a moving pitch.

#include <cmath>
#include <vector>

#include "core/Effects.h"
#include "core/LynxTune.h"
#include "core/VocalPitch.h"

namespace t190li
{
constexpr double pi = 3.14159265358979323846;

// a vowel-like tone (harmonics 1/k^1.3 through two resonances) on the pitch curve f0(t)
template <typename F>
std::vector<float> vowel (double fs, int n, F&& f0)
{
    std::vector<float> y ((std::size_t) n);
    double ph = 0.0, z[2][2] {};
    const double fr[2] { 700.0, 1220.0 }, bw[2] { 80.0, 90.0 };
    for (int i = 0; i < n; ++i)
    {
        const auto f = f0 (i / fs);
        ph += f / fs;
        ph -= std::floor (ph);
        double x = 0.0;
        for (int k = 1; k <= std::min (60, (int) (fs * 0.45 / f)); ++k)
            x += std::sin (2.0 * pi * k * ph) / std::pow (k, 1.3);
        double o = 0.0;
        for (int r = 0; r < 2; ++r)
        {
            const auto rr = std::exp (-pi * bw[r] / fs), a1 = -2.0 * rr * std::cos (2.0 * pi * fr[r] / fs), a2 = rr * rr;
            const auto v = (1.0 - rr) * x - a1 * z[r][0] - a2 * z[r][1];
            z[r][1] = z[r][0];
            z[r][0] = v;
            o += v;
        }
        y[(std::size_t) i] = (float) (0.3 * o);
    }
    return y;
}

struct Est
{
    double period;
    float clarity;
    bool voiced;
    bool operator== (const Est& o) const { return period == o.period && clarity == o.clarity && voiced == o.voiced; }
};

std::vector<Est> track (lynx::vocal::PitchDetector& det, const std::vector<float>& x)
{
    std::vector<Est> out;
    for (const auto v : x)
        if (det.push (v))
            out.push_back ({ det.estimate().period, det.estimate().clarity, det.estimate().voiced });
    return out;
}

lynx::EffectParams tuneParams()
{
    lynx::EffectParams p {};
    const auto& info = lynx::lynxtune::info();
    for (std::size_t i = 0; i < info.params.size(); ++i)
        p[i] = info.params[i].defaultValue;
    p[lynx::lynxtune::scale] = 0.0f; // chromatic
    return p;
}

std::vector<float> runTune (lynx::Effect& fx, const std::vector<float>& x, const lynx::EffectParams& p)
{
    std::vector<float> l = x;
    for (int i = 0; i < (int) x.size(); i += 256)
        fx.process (l.data() + i, nullptr, std::min (256, (int) x.size() - i), p, 120.0);
    return l;
}
} // namespace t190li

TEST (vocal_pitch_scratch_buffers_keep_no_state_round190li)
{
    // a gliding vowel with vibrato (the marks' correlation and both difference functions all run on it)
    const double fs = 48000.0;
    const auto x = t190li::vowel (fs, (int) (0.9 * fs), [] (double t) {
        return 190.0 * std::exp2 ((t * 500.0 + 30.0 * std::sin (2.0 * t190li::pi * 5.5 * t)) / 1200.0);
    });
    const auto other = t190li::vowel (fs, (int) (0.5 * fs), [] (double) { return 523.0; });
    // the detector: fresh, after other material and a reset, after a prepare at another rate
    lynx::vocal::PitchDetector a, b, c;
    a.prepare (fs, 80.0, 1000.0, 139);
    b.prepare (fs, 80.0, 1000.0, 139);
    c.prepare (96000.0, 55.0, 700.0, 278);
    t190li::track (b, other);
    t190li::track (c, other);
    b.reset();
    c.prepare (fs, 80.0, 1000.0, 139);
    const auto ea = t190li::track (a, x), eb = t190li::track (b, x), ec = t190li::track (c, x);
    CHECK (! ea.empty());
    CHECK (ea == eb);
    CHECK (ea == ec);
    // LYNX Tune (detector + marks + grains): fresh, and after other material and a reset — the same output to the bit
    const auto p = t190li::tuneParams();
    auto f1 = lynx::lynxtune::make (&p), f2 = lynx::lynxtune::make (&p);
    f1->prepare (fs);
    f2->prepare (fs);
    t190li::runTune (*f2, other, p);
    f2->reset();
    const auto y1 = t190li::runTune (*f1, x, p), y2 = t190li::runTune (*f2, x, p);
    CHECK (y1 == y2);
    double e = 0.0;
    for (const auto v : y1)
        e += (double) v * v;
    CHECK (e > 1.0); // (it did process)
}

TEST (vocal_pitch_detector_follows_a_glide_round190li)
{
    // a vowel gliding 200 -> 300 Hz in 0.5 s (1.4 c / ms): each voiced estimate reads the pitch at its window centre
    // within 3 c on average and 12 c at worst, and every estimate after the first 40 ms is voiced
    for (const double fs : { 44100.0, 48000.0 })
    {
        auto f0 = [] (double t) { return 200.0 * std::pow (1.5, std::clamp ((t - 0.1) / 0.5, 0.0, 1.0)); };
        const auto x = t190li::vowel (fs, (int) (0.8 * fs), f0);
        lynx::vocal::PitchDetector det;
        const auto hop = (int) std::lround (128.0 * fs / 44100.0);
        det.prepare (fs, 80.0, 1000.0, hop);
        det.setHarmonicGuard (true);
        double sum = 0.0, worst = 0.0;
        int n = 0, unvoiced = 0;
        for (int i = 0; i < (int) x.size(); ++i)
            if (det.push (x[(std::size_t) i]))
            {
                const auto t = (double) (i - det.windowCentre()) / fs;
                if (t < 0.04)
                    continue;
                if (! det.estimate().voiced)
                {
                    ++unvoiced;
                    continue;
                }
                const auto c = std::abs (1200.0 * std::log2 (det.estimate().hz / f0 (t)));
                sum += c;
                worst = std::max (worst, c);
                ++n;
            }
        CHECK (n > 100);
        CHECK (unvoiced == 0);
        CHECK (sum / std::max (1, n) < 3.0);
        CHECK (worst < 12.0);
    }
}
