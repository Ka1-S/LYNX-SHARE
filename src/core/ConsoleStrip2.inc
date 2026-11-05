// round tp172c: the modern console channel strip (core/VoiceMatch.h, namespace strip2) — included by VoiceMatch.cpp.
// Every number here comes from the trial's renders (tp172c ana/: bandfit, shelf4, filtfit, dynfit, the ramp fits).
namespace strip2
{
namespace
{
#include "core/ConsoleStrip2Tables.inc"

constexpr double kPi = 3.14159265358979323846;
inline double dbGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double coef (double ms, double sr) noexcept { return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-1000.0 / (ms * sr)); }
inline double soft (double o, double k) noexcept { return o > 30.0 * k ? o : k * std::log1p (std::exp (o / k)); }

// position of x in an ascending grid: index i and fraction t in [0, 1] (clamped at the ends)
void locate (double x, const double* grid, int n, int& i, double& t) noexcept
{
    if (x <= grid[0]) { i = 0; t = 0.0; return; }
    if (x >= grid[n - 1]) { i = n - 2; t = 1.0; return; }
    i = 0;
    while (i < n - 2 && x > grid[i + 1])
        ++i;
    t = (x - grid[i]) / (grid[i + 1] - grid[i]);
}

// a band's gain dial on its grid: below the smallest measured step (±3 dB) the shape of ±3 is kept, its gain scaled down
struct GainPos { int i = 0; double t = 0.0, scale = 1.0; };
GainPos gainPos (double g, const double* grid, int n) noexcept
{
    GainPos p;
    int small = 0; // index of the smallest positive entry
    while (small < n && grid[small] < 0.0)
        ++small;
    if (std::abs (g) < grid[small])
    {
        p.scale = std::abs (g) / grid[small];
        p.i = g >= 0.0 ? small : small - 1;
        p.t = 0.0;
        if (p.i >= n - 1) { p.i = n - 2; p.t = 1.0; }
        return p;
    }
    locate (g, grid, n, p.i, p.t);
    return p;
}

// bilinear look-up of value k of a [nf][ng][stride] table
double lookup2 (const float* tab, int ng, int stride, int fi, double ft, const GainPos& gp, int k) noexcept
{
    auto at = [&] (int f, int g) { return (double) tab[(f * ng + g) * stride + k]; };
    const double a = at (fi, gp.i) + gp.t * (at (fi, gp.i + 1) - at (fi, gp.i));
    const double b = at (fi + 1, gp.i) + gp.t * (at (fi + 1, gp.i + 1) - at (fi + 1, gp.i));
    return a + ft * (b - a);
}

const double kLfF[5] { 40, 100, 182, 400, 600 }, kHfF[5] { 1500, 3000, 7500, 12000, 22000 };
const double kLfG[6] { -16.5, -8, -3, 3, 8, 16.5 }, kHfG[6] { -20, -8, -3, 3, 8, 20 };
const double kLmfF[5] { 200, 500, 800, 1200, 2000 }, kHmfF[5] { 600, 1500, 2450, 4000, 7000 };
const double kMidQ[5] { 0.5, 1.0, 1.5, 2.0, 2.5 }, kMidQ3[3] { 0.5, 1.5, 2.5 };
const double kMidG[8] { -20, -12, -6, -3, 3, 6, 12, 20 };
// the filters: the dial's value -> the measured corner
const double kHpDial[7] { 25, 40, 60, 100, 200, 350, 500 }, kHpHz[7] { 18.1, 38.1, 74.0, 108.2, 224.8, 379.0, 478.0 };
const double kLpDial[5] { 3, 5, 8, 12, 20 }, kLpHz[5] { 3418.8, 4308.2, 8676.2, 13313.6, 23900.0 };

double tableInterp (double x, const double* xs, const double* ys, int n, bool logY) noexcept
{
    double lx[8], ly[8];
    for (int k = 0; k < n; ++k) { lx[k] = std::log (xs[k]); ly[k] = logY ? std::log (ys[k]) : ys[k]; }
    int i; double t;
    const double v = std::log (std::max (x, 1e-9));
    if (v < lx[0]) // extrapolate the first ratio
        return ys[0] / xs[0] * x;
    locate (v, lx, n, i, t);
    const double y = ly[i] + t * (ly[i + 1] - ly[i]);
    return logY ? std::exp (y) : y;
}

void setBq (Biquad& q, double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}

void peaking (Biquad& q, double sr, double f0, double Q, double g) noexcept
{
    f0 = std::min (f0, 0.47 * sr);
    const double A = std::pow (10.0, g / 40.0), w = 2.0 * kPi * f0 / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * Q);
    setBq (q, 1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
}

// first-order shelf (bilinear, prewarped at fc): boosts keep their corner at fc, cuts mirror them (E type)
void shelf1 (Biquad& q, double sr, double fc, double g, bool high) noexcept
{
    const double G = dbGain (g), K = std::tan (kPi * std::min (fc, 0.47 * sr) / sr);
    double b0, b1, a0, a1;
    if (! high)
    {
        if (G >= 1.0) { b0 = K * G + 1; b1 = K * G - 1; a0 = K + 1; a1 = K - 1; }
        else { b0 = K + 1; b1 = K - 1; a0 = K / G + 1; a1 = K / G - 1; }
    }
    else
    {
        if (G >= 1.0) { b0 = G + K; b1 = K - G; a0 = 1 + K; a1 = K - 1; }
        else { b0 = 1 + K; b1 = K - 1; a0 = 1 / G + K; a1 = K - 1 / G; }
    }
    setBq (q, b0, b1, 0.0, a0, a1, 0.0);
}

// second-order shelf with independent zero / pole Q (G type): poles at fp, zeros at fp·√G (low) or fp / √G (high)
void shelf2 (Biquad& q, double sr, double fp, double qz, double qp, double g, bool high) noexcept
{
    const double G = dbGain (g), wp = std::tan (kPi * std::min (fp, 0.47 * sr) / sr);
    const double wz = high ? wp / std::sqrt (G) : wp * std::sqrt (G);
    const double k = high ? (wp * wp) / (wz * wz) : 1.0;
    setBq (q, k * (1 + wz / qz + wz * wz), k * (2 * wz * wz - 2), k * (1 - wz / qz + wz * wz),
           1 + wp / qp + wp * wp, 2 * wp * wp - 2, 1 - wp / qp + wp * wp);
}

bool shelfOrBell (Biquad& q, double sr, const Settings& s, bool high) noexcept
{
    const double g = high ? s.hfGain : s.lfGain;
    if (std::abs (g) < 0.05)
        return false;
    const double fd = high ? s.hfKHz * 1000.0 : s.lfHz;
    const double* fs = high ? kHfF : kLfF;
    const double* gs = high ? kHfG : kLfG;
    double lf[5];
    for (int k = 0; k < 5; ++k) lf[k] = std::log (fs[k]);
    int fi; double ft;
    locate (std::log (fd), lf, 5, fi, ft);
    const auto gp = gainPos (g, gs, 6);
    const bool bell = high ? s.hfBell : s.lfBell;
    if (bell)
    {
        const float* t = s.typeG ? (high ? kGHFBell : kGLFBell) : (high ? kEHFBell : kELFBell);
        peaking (q, sr, fd * lookup2 (t, 6, 3, fi, ft, gp, 0), lookup2 (t, 6, 3, fi, ft, gp, 1), gp.scale * lookup2 (t, 6, 3, fi, ft, gp, 2));
    }
    else if (! s.typeG)
    {
        const float* t = high ? kEHFShelf : kELFShelf;
        shelf1 (q, sr, fd * lookup2 (t, 6, 2, fi, ft, gp, 0), gp.scale * lookup2 (t, 6, 2, fi, ft, gp, 1), high);
    }
    else
    {
        const float* t = high ? kGHFShelf : kGLFShelf;
        shelf2 (q, sr, fd * lookup2 (t, 6, 4, fi, ft, gp, 0), lookup2 (t, 6, 4, fi, ft, gp, 1), lookup2 (t, 6, 4, fi, ft, gp, 2),
                gp.scale * lookup2 (t, 6, 4, fi, ft, gp, 3), high);
    }
    return true;
}

// the mid bells: centre and width from the grid at +12 (frequency x Q), their change with the gain from the gain grid
// (Q x gain at the band's middle frequency), the gain from the gain grid
bool midBell (Biquad& q, double sr, const Settings& s, bool high) noexcept
{
    const double g = high ? s.hmfGain : s.lmfGain;
    if (std::abs (g) < 0.05)
        return false;
    const double fd = (high ? s.hmfKHz : s.lmfKHz) * 1000.0, qd = std::clamp (high ? s.hmfQ : s.lmfQ, 0.5, 2.5);
    const double* fs = high ? kHmfF : kLmfF;
    const float* grid = s.typeG ? (high ? kGHMFGrid : kGLMFGrid) : (high ? kEHMFGrid : kELMFGrid);
    const float* gain = s.typeG ? (high ? kGHMFGain : kGLMFGain) : (high ? kEHMFGain : kELMFGain);
    double lf[5];
    for (int k = 0; k < 5; ++k) lf[k] = std::log (fs[k]);
    int fi, qi, q3; double ft, qt, q3t;
    locate (std::log (fd), lf, 5, fi, ft);
    locate (qd, kMidQ, 5, qi, qt);
    locate (qd, kMidQ3, 3, q3, q3t);
    GainPos qp; qp.i = qi; qp.t = qt;
    const double f12 = lookup2 (grid, 5, 2, fi, ft, qp, 0), Q12 = lookup2 (grid, 5, 2, fi, ft, qp, 1);
    const auto gp = gainPos (g, kMidG, 8);
    auto gt = [&] (int k, const GainPos& p) { // gain grid [q3][g][3], q interpolated
        auto at = [&] (int qq, int gg) { return (double) gain[(qq * 8 + gg) * 3 + k]; };
        const double a = at (q3, p.i) + p.t * (at (q3, p.i + 1) - at (q3, p.i));
        const double b = at (q3 + 1, p.i) + p.t * (at (q3 + 1, p.i + 1) - at (q3 + 1, p.i));
        return a + q3t * (b - a);
    };
    GainPos at12; at12.i = 6; at12.t = 0.0; // the +12 column
    const double fr = f12 * gt (0, gp) / gt (0, at12), Q = Q12 * gt (1, gp) / gt (1, at12);
    peaking (q, sr, fd * fr, Q, gp.scale * gt (2, gp));
    return true;
}

double magOf (const Biquad& q, double sr, double hz) noexcept
{
    const double w = 2.0 * kPi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return std::abs ((q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2));
}

struct Design
{
    Biquad hp1, hp2, lp, bands[4];
    bool hpOn = false, lpOn = false, bandOn[4] {};
    void make (const Settings& s, double sr) noexcept
    {
        hpOn = s.hpHz >= 20.5;
        if (hpOn) // 3rd order: a first-order section and a Q = 1 biquad at the measured corner
        {
            const double fc = tableInterp (s.hpHz, kHpDial, kHpHz, 7, true);
            const double K = std::tan (kPi * fc / sr);
            setBq (hp1, 1, -1, 0, 1 + K, K - 1, 0);
            const double w = 2.0 * kPi * fc * 0.996 / sr, cw = std::cos (w), al = std::sin (w) / 2.0;
            setBq (hp2, (1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
        }
        const double lpHz = s.lpKHz > 0.0 && s.lpKHz < 27.6 ? tableInterp (s.lpKHz, kLpDial, kLpHz, 5, true) : 0.0;
        lpOn = lpHz > 0.0 && lpHz < 0.45 * sr;
        if (lpOn)
        {
            const double w = 2.0 * kPi * lpHz / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.706);
            setBq (lp, (1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
        }
        bandOn[0] = s.eqOn && shelfOrBell (bands[0], sr, s, false);
        bandOn[1] = s.eqOn && midBell (bands[1], sr, s, false);
        bandOn[2] = s.eqOn && midBell (bands[2], sr, s, true);
        bandOn[3] = s.eqOn && shelfOrBell (bands[3], sr, s, true);
    }
};

// the compressor's static law ([EXEC] the trial's ramps; the slope per 1 / ratio, the RMS detector's ~1.15x of it)
double peakSlope (double ratio) noexcept
{
    static const double inv[5] { 0.0, 0.1, 0.25, 0.5, 1.0 }, sl[5] { 1.0, 0.88, 0.753, 0.593, 0.0 };
    const double x = ratio >= 1e6 ? 0.0 : 1.0 / std::max (1.0, ratio);
    int i; double t;
    locate (x, inv, 5, i, t);
    return sl[i] + t * (sl[i + 1] - sl[i]);
}
} // namespace

double makeupDb (const Settings& s) noexcept
{
    if (! s.autoMakeup || s.compRatio <= 1.0005)
        return 0.0;
    // [EXEC] 0.6 dB per dB of threshold under the dial's top (+10), times the ratio's slope (ratios 2 and 10 at -15 dB:
    // +8.9 / +13.2 dB; none at the default +10), then the offset
    return 0.6 * peakSlope (s.compRatio) * std::max (0.0, 10.0 - s.compThreshold) + s.makeupOffsetDb;
}

double compStaticDb (const Settings& s, double inputDb) noexcept
{
    if (s.compRatio <= 1.0005)
        return inputDb;
    const double u = std::clamp (-s.compThreshold / 15.0, -0.7, 1.4);
    const double slope = peakSlope (s.compRatio) * (s.compPeak ? 1.0 : 1.135 + 0.03 * u);
    const double thr = s.compThreshold + (s.compPeak ? -17.9 : -13.5 + 0.5 * u);
    const double knee = s.compPeak ? 0.06 : std::max (0.1, 0.68 + 0.82 * u);
    return inputDb - slope * soft (inputDb - thr, knee) + makeupDb (s);
}

double responseDb (const Settings& s, double sampleRate, double hz)
{
    Design d;
    d.make (s, sampleRate);
    double m = 1.0;
    if (d.hpOn) m *= magOf (d.hp1, sampleRate, hz) * magOf (d.hp2, sampleRate, hz);
    if (d.lpOn) m *= magOf (d.lp, sampleRate, hz);
    for (int k = 0; k < 4; ++k)
        if (d.bandOn[k]) m *= magOf (d.bands[k], sampleRate, hz);
    return gainDb (m);
}

void Strip::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    designedOnce = false;
    reset();
}

void Strip::reset() noexcept
{
    hp1.reset(); hp2.reset(); lp.reset();
    for (auto& b : bands) b.reset();
    env = gr = genv = ggr = holdLeft = 0.0;
    gateOpen = false;
}

void Strip::design (const Settings& s) noexcept
{
    if (designedOnce && std::memcmp (&s, &designed, sizeof (Settings)) == 0)
        return;
    designed = s;
    designedOnce = true;
    Design d;
    d.make (s, sr);
    const auto copy = [] (Biquad& to, const Biquad& from) { to.b0 = from.b0; to.b1 = from.b1; to.b2 = from.b2; to.a1 = from.a1; to.a2 = from.a2; };
    copy (hp1, d.hp1); copy (hp2, d.hp2); copy (lp, d.lp);
    for (int k = 0; k < 4; ++k) copy (bands[k], d.bands[k]);
    hpOn = d.hpOn; lpOn = d.lpOn;
    for (int k = 0; k < 4; ++k) bandOn[k] = d.bandOn[k];
}

void Strip::process (float* l, float* r, int n, const Settings& s) noexcept
{
    design (s);
    const double inG = dbGain (s.inputDb), outG = dbGain (s.outputDb) * (s.phase ? -1.0 : 1.0);
    // compressor ([EXEC] dynfit on the stairs and bursts: attack and the RMS law move a little with the threshold)
    const bool compOn = s.dynOn && s.compRatio > 1.0005;
    const double u = std::clamp (-s.compThreshold / 15.0, -0.7, 1.4);
    const double slope = peakSlope (s.compRatio) * (s.compPeak ? 1.0 : 1.135 + 0.03 * u);
    const double thr = s.compThreshold + (s.compPeak ? -17.9 : -13.5 + 0.5 * u);
    const double knee = s.compPeak ? 0.06 : std::max (0.1, 0.68 + 0.82 * u);
    const double atkMs = s.compFast ? std::max (2.0, 3.0 + 1.5 * u) : std::max (20.0, 35.0 + 18.0 * u);
    const double relMs = s.compPeak ? std::max (5.0, 680.0 * s.compRelease - 63.0) : 680.0 * s.compRelease;
    const double cA = coef (atkMs, sr), cR = coef (relMs, sr);
    const double dA = coef (s.compPeak ? (s.compFast ? 0.04 : 0.001) : 0.05, sr);
    const double dR = coef (s.compPeak ? 16.7 * peakSlope (s.compRatio) : 1.0, sr);
    const double mix = std::clamp (s.compMix, 0.0, 1.0);
    const double makeup = compOn ? dbGain (makeupDb (s)) : 1.0;
    // gate / expander ([EXEC] the ramps: the expander's threshold 17.7 dB under the dial, 0.86 dB per dB below it; the
    // gate opens 3.2 dB over that and closes 1.3 dB under it after its hold; range 10 / 40 dB on the dial = 7.9 / 47.8;
    // the release a slew of 8.4 dB/s per second of the dial)
    const bool gateOn = s.dynOn && s.gateRange > 0.05;
    const double gRange = 0.3976 * std::pow (std::max (s.gateRange, 0.0), 1.298);
    const double gT = s.gateThreshold - 17.7;
    const double gA = coef (s.gateFast ? 0.1 : 1.0, sr), gD = coef (10.0, sr);
    const double gSlew = 8.4 / std::max (0.05, s.gateRelease) / sr; // dB per sample
    float maxComp = 0.0f, maxGate = 0.0f;

    auto eq = [this] (double& a, double& b) {
        if (hpOn) { a = hp2.process (hp1.process ((float) a, 0), 0); b = hp2.process (hp1.process ((float) b, 1), 1); }
        if (lpOn) { a = lp.process ((float) a, 0); b = lp.process ((float) b, 1); }
        for (int k = 0; k < 4; ++k)
            if (bandOn[k]) { a = bands[k].process ((float) a, 0); b = bands[k].process ((float) b, 1); }
    };

    for (int i = 0; i < n; ++i)
    {
        double a = l[i] * inG, b = r[i] * inG;
        if (! s.dynamicsPreEq)
            eq (a, b);
        if (gateOn)
        {
            const double lev = std::max (std::abs (a), std::abs (b));
            genv = lev > genv ? lev : genv + gD * (lev - genv);
            const double levDb = gainDb (genv);
            double target;
            if (! s.expander)
            {
                if (levDb >= gT + 3.2) { gateOpen = true; holdLeft = s.gateHold * sr; }
                else if (levDb < gT - 1.3 && gateOpen && (holdLeft -= 1.0) <= 0.0) gateOpen = false;
                target = gateOpen ? 0.0 : gRange;
            }
            else
                target = std::min (gRange, 0.86 * soft (gT - levDb, 0.3));
            if (target < ggr) ggr += gA * (target - ggr);      // opening
            else ggr = std::min (target, ggr + gSlew);         // closing: a dB slew
            const double g = dbGain (-ggr);
            a *= g; b *= g;
            maxGate = std::max (maxGate, (float) ggr);
        }
        if (compOn)
        {
            double levDb;
            if (s.compPeak)
            {
                const double lev = std::max (std::abs (a), std::abs (b));
                env += (lev > env ? dA : dR) * (lev - env);
                levDb = gainDb (env);
            }
            else
            {
                const double ms = 0.5 * (a * a + b * b);
                env += (ms > env ? dA : dR) * (ms - env);
                levDb = 10.0 * std::log10 (std::max (env, 1e-24)) + 3.0103;
            }
            const double target = slope * soft (levDb - thr, knee);
            gr += (target > gr ? cA : cR) * (target - gr);
            const double g = dbGain (-gr) * makeup;
            a = a * (1.0 - mix) + a * g * mix;
            b = b * (1.0 - mix) + b * g * mix;
            maxComp = std::max (maxComp, (float) gr);
        }
        if (s.dynamicsPreEq)
            eq (a, b);
        l[i] = (float) (a * outG);
        r[i] = (float) (b * outG);
    }
    lastComp = maxComp;
    lastGate = maxGate;
}
} // namespace strip2
