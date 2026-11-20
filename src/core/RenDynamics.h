#pragma once

// Round 190mm (sub-round dyn): LYNX's models of two more members of the Renaissance dynamics family — the one-knob vocal
// compressor (Renaissance Vox role) and the split / wide-band de-esser (Renaissance DeEsser role). Clean-room: the
// structure was read from the references' DSP (docs/analise/REA_rodada190mm_dyn.md §1, §2), written here in our own
// words; every constant below was fitted on our own demo-mode probes of the references (§1.2, §2.2). Header-only and
// self-contained (the lab's fitting driver compiles it on its own).
//
// Shared by both: a linked peak detector, the gain computer in linear gain, its minimum over a look-ahead window (a power
// of two ≥ 1 ms), a slow follower s of that gain, the output gain y = the deeper of gain and follower, released with a
// coefficient read from two tables (the release shape by 2s − y, a speed-up by s), two boxcars (27 / 38 samples at the
// 64-sample window) over y in fixed point, the audio delayed by the window.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lynx::rendyn
{
// ---- constants (fitted, REA_rodada190mm_dyn.md §1.2)
constexpr double voxAllPass[3] { 0.6303, 0.3583, 0.1607 }; // the detector's three first-order all-pass coefficients
constexpr double voxCeiling = 0.99;                         // the make-up: a full-scale peak comes out at this level
constexpr double voxFollowFallMs = 249.545, voxFollowRiseMs = 501.479; // the slow follower s
constexpr double voxReleaseMs = 443.998;                    // the release shape's longest time (2s − y = 0)
constexpr double voxShapePow = 4.0;                         // shape: time · (1 − u)^pow (kept as read)
constexpr double voxFastMs[3] { 0.5, 31.864, 152.273 };     // the speed-up table's corner times (s at √½, ½, ¼)
// the speed-up corners as read (the DeEsser's probes don't move them, §2.2)
constexpr double renTableFastMs[3] { 0.5, 30.0, 150.0 };
constexpr double voxBoxShare = 0.422;                       // the first boxcar's share of the window

inline double coefMs (double ms, double sr) noexcept { return ms <= 0.0 ? 0.0 : std::exp (-1000.0 / (ms * sr)); }
inline int windowFor (double sr) noexcept
{
    int w = 1;
    const int target = (int) (sr / 1000.0);
    while (w < target)
        w *= 2;
    return std::max (w, 2);
}

// the two 256-entry release tables (index = the value · 255, rounded)
struct ReleaseTables
{
    static constexpr int size = 256;
    std::array<float, size> shape {}, speed {};
    void build (double sr, double releaseMs, double shapePow, const double fastMs[3]) noexcept
    {
        const double floorV = std::log (10.0) / sr;
        std::array<double, size> sh {}, sp {};
        for (int i = 0; i < size; ++i)
        {
            const double v = std::max (floorV, std::pow (1.0 - (double) i / size, shapePow));
            sh[(std::size_t) i] = coefMs (v * releaseMs, sr);
        }
        const int iHi = (int) std::floor (std::sqrt (0.5) * (size - 1) + 0.5), iMid = (int) std::floor ((size - 1) * 0.5 + 0.5),
                  iLo = (int) std::floor ((size - 1) * 0.25 + 0.5);
        const double k = 1.0 / std::log (10.0);
        const double cMid = coefMs (k * fastMs[1], sr) / sh[(std::size_t) iMid], cLo = coefMs (k * fastMs[2], sr) / sh[(std::size_t) iLo];
        const double cTop = coefMs (k * fastMs[0], sr) / sh[(std::size_t) iHi];
        for (int i = size - 1; i >= iHi; --i)
            sp[(std::size_t) i] = cMid * std::pow (cTop / cMid, (double) (i - iHi + 1) / (size - iHi));
        for (int i = iHi - 1; i >= iMid; --i)
            sp[(std::size_t) i] = cLo * std::pow (cMid / cLo, (double) (i - iMid + 1) / (iHi - iMid));
        for (int i = iMid - 1; i >= iLo; --i)
            sp[(std::size_t) i] = std::pow (cLo, (double) (i - iLo + 1) / (iMid - iLo));
        for (int i = iLo - 1; i >= 0; --i)
            sp[(std::size_t) i] = 1.0;
        for (int i = 0; i < size; ++i)
        {
            shape[(std::size_t) i] = (float) sh[(std::size_t) i];
            speed[(std::size_t) i] = (float) sp[(std::size_t) i];
        }
    }
    static int at (double u) noexcept { return std::clamp ((int) (u * (size - 1) + 0.5), 0, size - 1); }
    double coef (double s, double y) const noexcept { return (double) speed[(std::size_t) at (s)] * shape[(std::size_t) at (2.0 * s - y)]; }
};

// The smoothing chain after the gain computer: window minimum, follower, release, boxcars. Feed one gain per sample,
// get the smoothed gain (the audio is delayed by window() samples by the caller).
class GainChain
{
public:
    void prepare (double sr, double releaseMs = voxReleaseMs, double shapePow = voxShapePow, const double* fastMs = voxFastMs,
                  double fallMs = voxFollowFallMs, double riseMs = voxFollowRiseMs) noexcept
    {
        win = windowFor (sr);
        boxA = (int) std::floor (win * voxBoxShare + 0.5);
        boxB = win - boxA + 1;
        tables.build (sr, releaseMs, shapePow, fastMs);
        cFall = coefMs (fallMs, sr);
        cRise = coefMs (riseMs, sr);
        reset();
    }
    void reset() noexcept
    {
        qh = qt = 0;
        n0 = 0;
        s = y = 1.0;
        k = 0.0;
        const auto one = (std::int64_t) fix;
        b1.fill (one);
        b2.fill (one * boxA);
        sum1 = one * boxA;
        sum2 = one * boxA * boxB;
        p1 = p2 = 0;
    }
    int window() const noexcept { return win; }
    // g: the curve gain this sample (≤ 1); post: a factor applied after the release (the gate)
    double next (double g, double post = 1.0) noexcept
    {
        // the minimum over the last `win` gains (monotonic queue)
        while (qt != qh && qv[(std::size_t) ((qt - 1) & (cap - 1))] >= g)
            --qt;
        qv[(std::size_t) (qt & (cap - 1))] = g;
        qi[(std::size_t) (qt & (cap - 1))] = n0;
        ++qt;
        if (qi[(std::size_t) (qh & (cap - 1))] <= n0 - win)
            ++qh;
        if (qh >= cap)
        {
            qh -= cap;
            qt -= cap;
        }
        if (++n0 >= (1 << 30))
        {
            for (int j = qh; j != qt; ++j)
                qi[(std::size_t) (j & (cap - 1))] -= (1 << 29);
            n0 -= (1 << 29);
        }
        double m = qv[(std::size_t) (qh & (cap - 1))];
        s = (s - m) * (m < s ? cFall : cRise) + m;
        m = std::min (m, s);
        if (y <= m)
            m += (y - m) * k;
        y = m;
        k = tables.coef (s, y);
        // two boxcars (exact running sums in fixed point)
        const auto gi = (std::int64_t) std::llround (std::clamp (post * y, 0.0, 4.0) * fix);
        sum1 += gi - b1[(std::size_t) p1];
        b1[(std::size_t) p1] = gi;
        if (++p1 == boxA) p1 = 0;
        sum2 += sum1 - b2[(std::size_t) p2];
        b2[(std::size_t) p2] = sum1;
        if (++p2 == boxB) p2 = 0;
        return (double) sum2 / ((double) boxA * boxB * fix);
    }
    double gain() const noexcept { return y; }

private:
    static constexpr int cap = 512;
    static constexpr double fix = 1073741824.0; // 2^30
    int win = 64, boxA = 27, boxB = 38;
    ReleaseTables tables;
    double cFall = 0.0, cRise = 0.0, s = 1.0, y = 1.0, k = 0.0;
    std::array<double, cap> qv {};
    std::array<int, cap> qi {};
    int qh = 0, qt = 0, n0 = 0;
    std::array<std::int64_t, cap> b1 {}, b2 {};
    std::int64_t sum1 = 0, sum2 = 0;
    int p1 = 0, p2 = 0;
};

// ---- the one-knob vocal compressor (Renaissance Vox role)
struct VoxSettings
{
    double compressionDb = 0.0; // −36 … 0: the threshold; the make-up follows it
    double gateDb = -80.0;      // −80 = off … 0
    double gateReleaseMs = 130.0;
    double gainDb = 0.0;        // −36 … 0
};

// the fitted constants as one set (the lab's fitting driver varies them)
struct VoxFit
{
    double allPass[3] { voxAllPass[0], voxAllPass[1], voxAllPass[2] };
    double ceiling = voxCeiling, releaseMs = voxReleaseMs, shapePow = voxShapePow;
    double fastMs[3] { voxFastMs[0], voxFastMs[1], voxFastMs[2] };
    double fallMs = voxFollowFallMs, riseMs = voxFollowRiseMs;
};

class Vox
{
public:
    void prepare (double sampleRate, const VoxFit& f = {}) noexcept
    {
        sr = sampleRate;
        fit = f;
        chain.prepare (sr, fit.releaseMs, fit.shapePow, fit.fastMs, fit.fallMs, fit.riseMs);
        win = chain.window();
        reset();
    }
    void reset() noexcept
    {
        chain.reset();
        std::memset (apx, 0, sizeof (apx));
        std::memset (apy, 0, sizeof (apy));
        env = 0.0;
        dl.fill (0.0f);
        dr.fill (0.0f);
        dp = 0;
        last = 0.0f;
    }
    int latency() const noexcept { return win; }
    float reductionDb() const noexcept { return last; }

    void process (float* l, float* r, int n, const VoxSettings& st) noexcept
    {
        const double t = std::pow (10.0, std::clamp (st.compressionDb, -36.0, 0.0) / 20.0);
        const bool gate = st.gateDb > -79.95;
        const double gateK = gate ? std::pow (10.0, -std::clamp (st.gateDb, -80.0, 0.0) / 20.0) : 0.0;
        const double gC = coefMs (std::clamp (st.gateReleaseMs, 10.0, 250.0), sr);
        const double makeup = fit.ceiling * std::pow (10.0, std::clamp (st.gainDb, -36.0, 0.0) / 20.0) / t;
        double minG = 1.0;
        for (int i = 0; i < n; ++i)
        {
            double p = 0.0;
            const float in[2] { l[i], r[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                const double v = in[ch];
                double m = std::abs (v);
                for (int k = 0; k < 3; ++k)
                {
                    const double yv = (v - apy[ch][k]) * fit.allPass[k] + apx[ch][k];
                    apx[ch][k] = v;
                    apy[ch][k] = yv;
                    m = std::max (m, std::abs (yv));
                }
                p = std::max (p, m);
            }
            env = env < p ? p : p + (env - p) * gC;
            const double g = t / (p * (1.0 - t) + t);
            const double post = gate ? std::min (1.0, env * gateK) : 1.0;
            const double G = chain.next (g, post);
            minG = std::min (minG, G);
            const float xl = dl[(std::size_t) dp], xr = dr[(std::size_t) dp];
            dl[(std::size_t) dp] = l[i];
            dr[(std::size_t) dp] = r[i];
            if (++dp == win) dp = 0;
            l[i] = (float) (xl * G * makeup);
            r[i] = (float) (xr * G * makeup);
        }
        last = (float) (minG < 1.0 ? -20.0 * std::log10 (std::max (minG, 1e-9)) : 0.0);
    }

private:
    static constexpr int cap = 512;
    double sr = 48000.0;
    int win = 64;
    VoxFit fit;
    GainChain chain;
    double apx[2][3] {}, apy[2][3] {};
    double env = 0.0;
    std::array<float, cap> dl {}, dr {};
    int dp = 0;
    float last = 0.0f;
};

// ---- the split / wide-band de-esser (Renaissance DeEsser role)
constexpr double essFollowFallMs = 15.045, essFollowRiseMs = 30.027; // its slow follower (fitted, fit/ess1.log)
constexpr double essReleaseMs = 480.486;                            // its release shape's longest time (fitted)
constexpr double essDetectorShare = 0.6001;                         // the sibilance detector's high-pass: this share of Freq
struct EssSettings
{
    double thresholdDb = -40.0, rangeDb = -16.0, freqHz = 5506.0, q = 0.42;
    bool bandPass = false, wideBand = false;
    // the reference's hidden values (its state carries them): the absolute / relative corner, the curve, the two
    // envelopes' times
    double relativeDb = -12.0, curve = 0.0, lowAttackMs = 24.9, lowReleaseMs = 248.7, hiAttackMs = 0.5, hiReleaseMs = 1.0;
};
struct EssFit
{
    double fallMs = essFollowFallMs, riseMs = essFollowRiseMs, releaseMs = essReleaseMs, shapePow = voxShapePow;
    double fastMs[3] { renTableFastMs[0], renTableFastMs[1], renTableFastMs[2] };
    double detectorShare = essDetectorShare;
};
struct Biquad2
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double run (double x, double* z) const noexcept // z: x1, x2, y1, y2
    {
        const double y = b0 * x + b1 * z[0] + b2 * z[1] - a1 * z[2] - a2 * z[3];
        z[1] = z[0];
        z[0] = x;
        z[3] = z[2];
        z[2] = y;
        return y;
    }
};

class DeEsser
{
public:
    void prepare (double sampleRate, const EssFit& f = {}) noexcept
    {
        sr = sampleRate;
        fit = f;
        chain.prepare (sr, fit.releaseMs, fit.shapePow, fit.fastMs, fit.fallMs, fit.riseMs);
        win = chain.window();
        designed = false;
        reset();
    }
    void reset() noexcept
    {
        chain.reset();
        std::memset (z, 0, sizeof (z));
        e1 = e2 = 0.0;
        dLo.fill (0.0);
        dHi.fill (0.0);
        dp = 0;
        last = 0.0f;
    }
    int latency() const noexcept { return win; }
    float reductionDb() const noexcept { return last; }

    void process (float* l, float* r, int n, const EssSettings& st) noexcept
    {
        if (! designed || st.freqHz != dFreq || st.q != dQ || st.bandPass != dBand)
            design (st);
        const double u = std::max (st.thresholdDb, st.relativeDb);
        const double cAbs = std::pow (10.0, u / 20.0), cRel = std::pow (10.0, (st.thresholdDb - u) / 20.0);
        const double floorG = std::pow (10.0, std::min (0.0, st.rangeDb) / 20.0), k = std::trunc (st.curve);
        const double a1u = coefMs (st.lowAttackMs, sr), a1d = coefMs (st.lowReleaseMs, sr);
        const double a2u = coefMs (st.hiAttackMs, sr), a2d = coefMs (st.hiReleaseMs, sr);
        double minG = 1.0;
        for (int i = 0; i < n; ++i)
        {
            const double x[2] { l[i], r[i] };
            double lo[2], hi[2], vLo = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                // the crossover: low = the section squared, high = its all-pass minus the low (in phase)
                const double s1 = sec.run (x[ch], z[ch][0]);
                lo[ch] = sec.run (s1, z[ch][1]);
                hi[ch] = ap.run (x[ch], z[ch][2]) - lo[ch];
                vLo = std::max (vLo, std::abs (lo[ch]));
            }
            // the sibilance detector on the mid of the high bands
            const double vHi = std::abs (det.run (det.run (0.5 * (hi[0] + hi[1]), z[0][3]), z[0][4]));
            e1 = (e1 - vLo) * (e1 < vLo ? a1u : a1d) + vLo;
            e2 = (e2 - vHi) * (e2 < vHi ? a2u : a2d) + vHi;
            const double a = std::min (e1 + cAbs, 2.0) * cRel;
            const double b = std::max (a, e2 * (1.0 - a * k) + a * k);
            const double g = std::max (a / b, floorG);
            const double G = chain.next (g);
            minG = std::min (minG, G);
            for (int ch = 0; ch < 2; ++ch)
            {
                const double oLo = dLo[(std::size_t) (dp * 2 + ch)], oHi = dHi[(std::size_t) (dp * 2 + ch)];
                dLo[(std::size_t) (dp * 2 + ch)] = st.wideBand ? 0.0 : lo[ch];
                dHi[(std::size_t) (dp * 2 + ch)] = st.wideBand ? x[ch] : hi[ch];
                (ch ? r : l)[i] = (float) (oLo + G * oHi);
            }
            if (++dp == win) dp = 0;
        }
        last = (float) (minG < 1.0 ? -20.0 * std::log10 (std::max (minG, 1e-9)) : 0.0);
    }

private:
    void design (const EssSettings& st) noexcept
    {
        designed = true;
        dFreq = st.freqHz;
        dQ = st.q;
        dBand = st.bandPass;
        const double f = std::clamp (st.freqHz, 20.0, 0.45 * sr), q = std::max (0.05, st.q);
        double t, n0, n1;
        if (st.bandPass) // a notch section (its square is the band's complement)
        {
            t = 2.0 * std::tan (3.14159265358979323846 * f / sr);
            const double w = (2.0 / t) * (2.0 / t);
            n0 = w + 1.0;
            n1 = 2.0 - 2.0 * w;
        }
        else // a low-pass section
        {
            t = std::tan (3.14159265358979323846 * f / sr) * std::sqrt (2.0);
            n0 = 1.0;
            n1 = 2.0;
        }
        const double d19 = 2.0 / (q * t), d18 = 4.0 / (t * t), d17 = 1.0 / (d19 + 1.0 + d18);
        sec.b0 = sec.b2 = n0 * d17;
        sec.b1 = n1 * d17;
        sec.a1 = (2.0 - 8.0 / (t * t)) * d17;
        sec.a2 = (1.0 - d19 + d18) * d17;
        ap.b0 = sec.a2;
        ap.b1 = sec.a1;
        ap.b2 = 1.0;
        ap.a1 = sec.a1;
        ap.a2 = sec.a2;
        // the detector: a second-order Butterworth high-pass at a share of Freq, run twice
        const double fd = std::clamp (st.freqHz * fit.detectorShare, 20.0, 0.45 * sr);
        const double K = std::tan (3.14159265358979323846 * fd / sr), Q = std::sqrt (0.5), nrm = 1.0 / (1.0 + K / Q + K * K);
        det.b0 = nrm;
        det.b1 = -2.0 * nrm;
        det.b2 = nrm;
        det.a1 = 2.0 * (K * K - 1.0) * nrm;
        det.a2 = (1.0 - K / Q + K * K) * nrm;
    }
    static constexpr int cap = 512;
    double sr = 48000.0;
    int win = 64;
    EssFit fit;
    GainChain chain;
    Biquad2 sec, ap, det;
    bool designed = false, dBand = false;
    double dFreq = 0.0, dQ = 0.0;
    double z[2][5][4] {};
    double e1 = 0.0, e2 = 0.0;
    std::array<double, cap * 2> dLo {}, dHi {};
    int dp = 0;
    float last = 0.0f;
};
} // namespace lynx::rendyn
