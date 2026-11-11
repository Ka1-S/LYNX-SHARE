// Round 190am: the true-peak filter of core/Loudness.h read without a wrap test.

#include "core/Loudness.h"

// The true-peak filter was rewritten to read its history without a wrap test (a third of the time). The numbers must be
// the same to the last bit: the limiter and the export use it too.
TEST (extras190am_true_peak_is_bit_identical_to_the_plain_form)
{
    using lynx::loudness::TruePeak;
    constexpr int phases = TruePeak::phases, taps = TruePeak::tapsPerPhase;
    // the plain form: the coefficients as TruePeak builds them, the history in a ring read with a modulo
    double h[phases][taps];
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int n = phases * taps;
        for (int k = 0; k < n; ++k)
        {
            const double t = (k - (n - 1) / 2.0) / phases;
            const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (k + 0.5) / n);
            h[k % phases][k / phases] = sinc * w;
        }
        for (auto& ph : h)
        {
            double s = 0.0;
            for (const auto c : ph) s += c;
            for (auto& c : ph) c /= s;
        }
    }
    double hist[taps] = {};
    int pos = 0;
    TruePeak fast;
    unsigned seed = 190;
    int different = 0;
    for (int i = 0; i < 200000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double x = ((seed >> 8) / 8388608.0 - 1.0) * (i % 5000 < 100 ? 1e-25 : 0.9);
        hist[pos] = x;
        pos = (pos + 1) % taps;
        double m = 0.0;
        for (int ph = 0; ph < phases; ++ph)
        {
            double s = 0.0;
            for (int k = 0; k < taps; ++k)
                s += h[ph][k] * hist[(pos - 1 - k + taps * 2) % taps];
            m = std::max (m, std::abs (s));
        }
        const double got = fast.push (x);
        different += std::memcmp (&got, &m, sizeof (double)) != 0;
    }
    CHECK (different == 0);
    fast.clear();
    CHECK (fast.push (0.0) == 0.0);
}
