// Round 190dw: FL Studio Mobile's Equalizer, Graphic EQ and Filter as exact Pocket engines (core/PocketFxE.cpp).
// Each engine's impulse response is checked against an independent double-precision model written here from the laws
// (design, 2× hold-and-keep path, the band pipelines' delays), plus the laws themselves, the channel modes, the silence
// gate and extremes. The renders against FL Studio Mobile inside FL are in the round's notes
// (/mnt/e/lynx-work/flm190dw/fx, 34 sets at the 16-bit floor).

namespace eq190dw
{
using C5 = std::array<double, 5>; // b0, b1, b2, a1, a2 (a0 = 1)

// FLM's band design in double: the cookbook forms with A = 10^(dB/20) and the bandwidth in octaves
inline C5 band (int kind, double hz, double db, double oct, double rate)
{
    const double A = std::pow (10.0, db / 20.0);
    const double w = std::clamp (2.0 * 3.141592653589793 * hz / rate, 0.0, 3.0);
    const double s = std::sin (w), c = std::cos (w);
    const double alpha = s * std::sinh (std::log (2.0) / 2.0 * oct * w / s);
    double b0, b1, b2, a0, a1, a2;
    if (kind == 1)
        b0 = 1 + alpha * A, b1 = -2 * c, b2 = 1 - alpha * A, a0 = 1 + alpha / A, a1 = -2 * c, a2 = 1 - alpha / A;
    else
    {
        const double r = 2 * std::sqrt (A) * alpha, sg = kind == 0 ? 1.0 : -1.0;
        b0 = A * ((A + 1) - sg * (A - 1) * c + r);
        b1 = sg * 2 * A * ((A - 1) - sg * (A + 1) * c);
        b2 = A * ((A + 1) - sg * (A - 1) * c - r);
        a0 = (A + 1) + sg * (A - 1) * c + r;
        a1 = -sg * 2 * ((A - 1) + sg * (A + 1) * c);
        a2 = (A + 1) + sg * (A - 1) * c - r;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

struct Bq
{
    C5 c {};
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double operator() (double x)
    {
        const double y = c[0] * x + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};

inline std::vector<double> cascade (std::vector<Bq> f, const std::vector<double>& x)
{
    std::vector<double> y (x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        for (auto& b : f)
            v = b (v);
        y[i] = v;
    }
    return y;
}

// the left output of an engine for a unit impulse at sample 0 (both sides); `right` gets the right side
inline std::vector<float> ir (lynx::EffectKind kind, const std::vector<float>& values, int n, double fs = 44100.0,
                              std::vector<float>* right = nullptr)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (fs);
    auto p = makeEffectSlot (kind).params;
    for (std::size_t k = 0; k < values.size(); ++k)
        p[k] = values[k];
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[0] = r[0] = 1.0f;
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    if (right != nullptr)
        *right = r;
    return l;
}

inline double maxDiff (const std::vector<float>& a, const std::vector<double>& b)
{
    double m = 0;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        m = std::max (m, std::abs ((double) a[i] - b[i]));
    return m;
}

// the Equalizer's reference: hold each sample twice, run the four bands (band 4 first) at 2·fs, keep one output in
// two; FLM's pipeline adds three half-sample steps, so output n is the 2× sequence's sample 2n − 2
inline std::vector<double> eqReference (const std::vector<float>& v, int n, double fs, int channel)
{
    std::vector<Bq> f;
    for (int b = 3; b >= 0; --b)
    {
        const int kind = std::clamp ((int) (v[(std::size_t) (4 * b + 3)] * 2.0f), 0, 2);
        const double hz = std::pow (2.0, (double) v[(std::size_t) (4 * b + 1)] * 9.25 + 5.0);
        const double oct = std::max ((double) v[(std::size_t) (4 * b + 2)] * 7.0 + 0.25, 0.0001);
        double db = ((double) v[(std::size_t) (4 * b)] - 0.5) * 24.0;
        const int mode = v.size() > (std::size_t) (16 + b) ? (int) (v[(std::size_t) (16 + b)] * 4.0f) : 0;
        if ((mode == 1 && channel == 1) || (mode == 2 && channel == 0))
            db = 0.0;
        if ((mode == 3 && channel == 1) || (mode == 4 && channel == 0))
            db = -db;
        Bq q;
        q.c = band (kind, hz, db, oct, 2.0 * fs);
        f.push_back (q);
    }
    std::vector<double> up ((std::size_t) (2 * n), 0.0);
    up[0] = up[1] = 1.0;
    const auto y2 = cascade (f, up);
    std::vector<double> y ((std::size_t) n, 0.0);
    for (int i = 1; i < n; ++i)
        y[(std::size_t) i] = y2[(std::size_t) (2 * i - 2)];
    return y;
}
} // namespace eq190dw

TEST (pocketeq190dw_laws)
{
    using namespace lynx;
    CHECK (std::abs (pocket::eqHz (0.0f) - 32.0) < 1e-9);
    CHECK (std::abs (pocket::eqHz (1.0f) - std::pow (2.0, 14.25)) < 1e-6);
    CHECK (std::abs (pocket::eqHz (0.07f) - 50.0) < 1.0); // FLM's "Low Cut 50Hz" preset value
    CHECK (std::abs (pocket::eqOctaves (0.5f) - 3.75) < 1e-6 && std::abs (pocket::eqOctaves (0.0f) - 0.25) < 1e-9);
    CHECK (pocket::eqKind (0.0f) == 0 && pocket::eqKind (0.51f) == 1 && pocket::eqKind (1.0f) == 2);
    CHECK (pocket::eqChannelMode (0.0f) == 0 && pocket::eqChannelMode (0.3f) == 1 && pocket::eqChannelMode (0.5f) == 2
           && pocket::eqChannelMode (0.75f) == 3 && pocket::eqChannelMode (1.0f) == 4);
    CHECK (std::abs (pocket::filterHzOf (0.0f) - 16.0) < 1e-9 && std::abs (pocket::filterHzOf (1.0f) - std::pow (2.0, 14.3)) < 1e-6);
    CHECK (std::abs (pocket::filterQOf (0.0f) - 0.8) < 1e-6 && std::abs (pocket::filterQOf (1.0f) - 2.8) < 1e-6);
    CHECK (pocket::filterType (0.0f) == 0 && pocket::filterType (0.5f) == 1 && pocket::filterType (1.0f) == 2);
    CHECK (pocket::filterPairs (0.0f) == 1 && pocket::filterPairs (0.5f) == 2 && pocket::filterPairs (1.0f) == 4);
    // the doubled amplitude law: a +6 dB (shown) peak lifts its centre by 12 dB
    const auto c = pocket::eqBandCoefficients (1, 1000.0, 6.0, 1.0, 88200.0);
    const double w = 2.0 * 3.141592653589793 * 1000.0 / 88200.0;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const double db = 20.0 * std::log10 (std::abs ((c[0] + c[1] * z1 + c[2] * z2) / (1.0 + c[3] * z1 + c[4] * z2)));
    CHECK (std::abs (db - 12.0) < 0.001);
    const auto r = eq190dw::band (1, 1000.0, 6.0, 1.0, 88200.0);
    for (int k = 0; k < 5; ++k)
        CHECK (std::abs (c[(std::size_t) k] - r[(std::size_t) k]) < 2e-6);
    // the texts FLM shows
    CHECK (pocket::eqParamText (0, 0.75f) == "6.0 dB" && pocket::eqParamText (1, 0.0f) == "32.0 Hz");
    CHECK (pocket::eqParamText (3, 0.0f) == "Low shelf" && pocket::eqParamText (16, 0.3f) == "Left");
    CHECK (pocket::graphicEqParamText (0, 1.0f) == "12.0 dB");
    CHECK (pocket::filterParamText (2, 1.0f) == "HP" && pocket::filterParamText (3, 0.5f) == "48 dB/O");
    // parameter lists: the module's values after its Enable, FLM's defaults
    CHECK ((int) effectInfo (EffectKind::pocketEq).params.size() == pocket::eqParams);
    CHECK ((int) effectInfo (EffectKind::pocketGraphicEq).params.size() == pocket::graphicEqParams);
    CHECK ((int) effectInfo (EffectKind::pocketFilter).params.size() == pocket::filterParams);
    CHECK (std::abs (effectInfo (EffectKind::pocketFilter).params[0].defaultValue - 0.7f) < 1e-6f);
}

TEST (pocketeq190dw_equalizer_matches_its_model)
{
    using namespace lynx;
    const std::vector<std::vector<float>> sets {
        { 0.5595f, 0.2065f, 0.1862f, 0.0f, 0.4403f, 0.3207f, 0.6021f, 0.51f, 0.5f, 1.0f, 0.5f, 0.51f, 0.6318f, 0.5696f, 0.208f, 1.0f, 0, 0, 0, 0 },
        { 0.0f, 0.006f, 0.3691f, 0.0f, 0.5f, 0.4f, 0.5f, 0.51f, 0.5f, 0.6f, 0.5f, 0.51f, 0.5337f, 0.6317f, 0.5f, 1.0f, 0, 0, 0, 0 },
        { 0.5f, 0.2f, 0.5f, 0.51f, 0.25f, 0.75f, 0.1f, 0.51f, 0.6875f, 0.95f, 0.05f, 0.51f, 0.75f, 0.84f, 0.3f, 1.0f, 0, 0, 0, 0 },
        { 0.75f, 0.55f, 0.2f, 0.51f, 0.3f, 0.4f, 0.5f, 0.51f, 0.6f, 0.6f, 0.5f, 0.0f, 0.4f, 0.8f, 0.5f, 0.51f, 0.3f, 0.5f, 0.75f, 1.0f },
    };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (const auto& v : sets)
        {
            std::vector<float> right;
            const auto l = eq190dw::ir (EffectKind::pocketEq, v, 4096, fs, &right);
            const double eL = eq190dw::maxDiff (l, eq190dw::eqReference (v, 4096, fs, 0));
            const double eR = eq190dw::maxDiff (right, eq190dw::eqReference (v, 4096, fs, 1));
            CHECK (eL < 2e-4 && eR < 2e-4);
            if (! (eL < 2e-4 && eR < 2e-4))
                std::printf ("  fs %.0f: |error| L %.2e R %.2e\n", fs, eL, eR);
        }
    // flat: the input comes out one sample late (the pipeline's three half-sample steps and the hold)
    const auto flat = eq190dw::ir (EffectKind::pocketEq, {}, 64);
    CHECK (std::abs (flat[0]) < 1e-6f && std::abs (flat[1] - 1.0f) < 1e-6f && std::abs (flat[2]) < 1e-6f);
}

TEST (pocketeq190dw_graphic_eq_matches_its_model)
{
    using namespace lynx;
    static constexpr double centres[8] { 80, 160, 375, 750, 1500, 3000, 6000, 12000 };
    const std::vector<std::vector<float>> sets { { 0.0f, 0.0691f, 0.1406f, 0.3586f, 0.2175f, 0.1276f, 0.0862f, 0.0f },
                                                 { 0.249f, 0.3314f, 0.351f, 0.5f, 0.5f, 0.4255f, 0.6098f, 0.649f },
                                                 { 1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 1.0f } };
    for (double fs : { 44100.0, 96000.0 })
        for (const auto& v : sets)
        {
            std::vector<eq190dw::Bq> f;
            for (int b = 0; b < 8; ++b)
            {
                eq190dw::Bq q;
                q.c = eq190dw::band (b == 0 ? 0 : b == 7 ? 2 : 1, centres[b], (double) v[(std::size_t) b] * 24.0 - 12.0, 1.0, fs);
                f.push_back (q);
            }
            std::vector<double> x (4096, 0.0);
            x[0] = 1.0;
            const auto y = eq190dw::cascade (f, x);
            std::vector<double> late (4096, 0.0); // two samples inside each of the four pairs
            for (int i = 8; i < 4096; ++i)
                late[(std::size_t) i] = y[(std::size_t) (i - 8)];
            const double e = eq190dw::maxDiff (eq190dw::ir (EffectKind::pocketGraphicEq, v, 4096, fs), late);
            CHECK (e < 5e-4);
            if (! (e < 5e-4))
                std::printf ("  graphic fs %.0f: |error| %.2e\n", fs, e);
        }
}

TEST (pocketeq190dw_filter_matches_its_model)
{
    using namespace lynx;
    const std::vector<std::vector<float>> sets { { 1.0f, 0.0f, 0.0f, 0.5f }, { 0.7f, 0.0f, 0.0f, 0.5f }, { 0.5788f, 0.5037f, 1.0f, 0.0f },
                                                 { 0.6075f, 1.0f, 0.0f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f }, { 0.6f, 0.3f, 0.0f, 1.0f },
                                                 { 0.3f, 0.8f, 0.5f, 1.0f } };
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (const auto& v : sets)
        {
            const double hz = pocket::filterHzOf (v[0]), q = pocket::filterQOf (v[1]);
            const int type = pocket::filterType (v[2]), pairs = pocket::filterPairs (v[3]);
            const double w = 2.0 * 3.141592653589793 * hz / fs, s = std::sin (w), c = std::cos (w);
            eq190dw::Bq bq;
            if (type == 1)
            {
                const double a0 = 1.0 + s / (2.0 * q);
                bq.c = { s / 2.0 / a0, 0.0, -s / 2.0 / a0, -2.0 * c / a0, (1.0 - s / (2.0 * q)) / a0 };
            }
            else
            {
                const double a0 = 1.0 + s / q, g = (type == 0 ? 1.0 - c : 1.0 + c) / (2.0 + s / q), z = type == 0 ? 2.0 : -2.0;
                bq.c = { g, z * g, g, -2.0 * c / a0, (1.0 - s / q) / a0 };
            }
            for (auto& k : bq.c)
                k = (double) (float) k; // FLM's coefficients are single precision: compare the arithmetic, not the rounding
            std::vector<eq190dw::Bq> f ((std::size_t) (2 * pairs), bq);
            std::vector<double> x (4096, 0.0);
            x[0] = 1.0;
            const auto y = eq190dw::cascade (f, x);
            const double level = type == 0 ? std::pow (0.8, pairs) : 1.0;
            std::vector<double> late (4096, 0.0); // one sample inside each pair
            for (int i = pairs; i < 4096; ++i)
                late[(std::size_t) i] = level * y[(std::size_t) (i - pairs)];
            double peak = 0;
            for (double t : late)
                peak = std::max (peak, std::abs (t));
            const double e = eq190dw::maxDiff (eq190dw::ir (EffectKind::pocketFilter, v, 4096, fs), late);
            CHECK (e < 2e-3 * std::max (1.0, peak)); // (8 stages of a narrow low band-pass: float rounding in the state)
            if (! (e < 2e-3 * std::max (1.0, peak)))
                std::printf ("  filter fs %.0f type %d pairs %d: |error| %.2e\n", fs, type, pairs, e);
        }
}

TEST (pocketeq190dw_channel_modes_and_silence)
{
    using namespace lynx;
    // band 1 boosted on the left only: the right side stays flat (one sample late)
    std::vector<float> v (20, 0.5f);
    for (int b = 0; b < 4; ++b)
        v[(std::size_t) (4 * b + 3)] = 0.51f, v[(std::size_t) (4 * b + 1)] = 0.2f + 0.2f * (float) b;
    for (int b = 0; b < 4; ++b)
        v[(std::size_t) (16 + b)] = 0.0f;
    v[0] = 0.8f;
    v[16] = 0.3f;
    std::vector<float> right;
    const auto left = eq190dw::ir (EffectKind::pocketEq, v, 256, 44100.0, &right);
    CHECK (std::abs (right[1] - 1.0f) < 1e-6f && std::abs (right[2]) < 1e-6f);
    CHECK (std::abs (left[2]) > 1e-3f);
    // opposite gains: left + / right − mirror each other's response (a boost and the same cut)
    v[16] = 0.75f;
    const auto l3 = eq190dw::ir (EffectKind::pocketEq, v, 256, 44100.0, &right);
    CHECK (eq190dw::maxDiff (l3, eq190dw::eqReference (v, 256, 44100.0, 0)) < 2e-4);
    CHECK (eq190dw::maxDiff (right, eq190dw::eqReference (v, 256, 44100.0, 1)) < 2e-4);

    // silence in → silence out, exactly; after the tail the module stops and starts afresh on new input
    for (auto kind : { EffectKind::pocketEq, EffectKind::pocketGraphicEq, EffectKind::pocketFilter })
    {
        auto fx = makeEffect (kind);
        fx->prepare (44100.0);
        const auto p = makeEffectSlot (kind).params;
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        bool silent = true;
        for (int k = 0; k < 20; ++k)
        {
            fx->process (l.data(), r.data(), 512, p, 120.0);
            for (int i = 0; i < 512; ++i)
                silent = silent && l[(std::size_t) i] == 0.0f && r[(std::size_t) i] == 0.0f;
        }
        CHECK (silent);
        // an impulse, then 11 s of silence (past every tail), then the same impulse: the same response
        auto first = std::vector<float> (512, 0.0f), again = first;
        first[0] = 1.0f;
        auto rr = first;
        fx->process (first.data(), rr.data(), 512, p, 120.0);
        for (int k = 0; k < (int) (11.0 * 44100.0 / 512.0) + 2; ++k)
        {
            std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
            fx->process (l.data(), r.data(), 512, p, 120.0);
        }
        again[0] = 1.0f;
        rr = again;
        fx->process (again.data(), rr.data(), 512, p, 120.0);
        CHECK (first == again);
    }
}

TEST (pocketeq190dw_extremes_stay_finite)
{
    using namespace lynx;
    std::mt19937 rng (190);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (auto kind : { EffectKind::pocketEq, EffectKind::pocketGraphicEq, EffectKind::pocketFilter })
            for (float x : { 0.0f, 1.0f })
            {
                auto fx = makeEffect (kind);
                fx->prepare (fs);
                auto p = makeEffectSlot (kind).params;
                for (std::size_t k = 0; k < effectInfo (kind).params.size(); ++k)
                    p[k] = x;
                std::vector<float> l (4096), r (4096);
                for (auto& s : l)
                    s = u (rng);
                for (auto& s : r)
                    s = u (rng);
                for (int at = 0; at < 4096; at += 512)
                    fx->process (l.data() + at, r.data() + at, 512, p, 120.0);
                bool finite = true;
                for (int i = 0; i < 4096; ++i)
                    finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]) && std::abs (l[(std::size_t) i]) < 1e7f;
                CHECK (finite);
            }
}
