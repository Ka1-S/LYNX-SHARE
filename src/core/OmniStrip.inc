// OmniStrip.inc (included by MixSuite.cpp inside namespace lynx::mixsuite::ridge, anonymous namespace) — round 190mm
// (scheps): Ridge Strip's hidden "omni" law. A five-module channel strip — Pre (saturation, thump, high- / low-pass),
// Gate / expander, DS2 (two dynamic bands), EQ (four bands) and a compressor with four detector types, auto make-up,
// mix and output — run in a stored order between an input and an output stage. LYNX's own model of the structure the
// reference's manual and our demo-mode probes show (docs/analise/REA_rodada190mm_scheps.md): every constant below was
// fitted on our own probe renders (the lab scripts and fit/*.json of round 190mm "scheps"); no vendor code or table.

namespace omnistrip
{
constexpr double pi = 3.14159265358979323846;

// ---- filter designs (b0 b1 b2 / a0 a1 a2 into lynx::Biquad) ----
void flat (Biquad& f) noexcept { f.set (1, 0, 0, 1, 0, 0); }
// RBJ forms (bilinear, pre-warped at f0)
void rbjHp2 (Biquad& f, double sr, double hz, double q) noexcept
{
    const auto w = 2 * pi * std::min (hz, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
    f.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
void rbjLp2 (Biquad& f, double sr, double hz, double q) noexcept
{
    const auto w = 2 * pi * std::min (hz, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
    f.set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
void hp1 (Biquad& f, double sr, double hz) noexcept
{
    const auto k = std::tan (pi * std::min (hz, 0.49 * sr) / sr);
    f.set (1, -1, 0, 1 + k, k - 1, 0);
}
void lp1 (Biquad& f, double sr, double hz) noexcept
{
    const auto k = std::tan (pi * std::min (hz, 0.49 * sr) / sr);
    f.set (k, k, 0, 1 + k, k - 1, 0);
}
void rbjPeak (Biquad& f, double sr, double hz, double q, double db) noexcept
{
    const auto a = std::pow (10.0, db / 40), w = 2 * pi * std::min (hz, 0.49 * sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
    f.set (1 + al * a, -2 * c, 1 - al * a, 1 + al / a, -2 * c, 1 - al / a);
}
void rbjShelf (Biquad& f, double sr, double hz, double q, double db, bool high) noexcept
{
    const auto a = std::pow (10.0, db / 40), w = 2 * pi * std::min (hz, 0.49 * sr) / sr, c = std::cos (w);
    const auto s = 2 * std::sqrt (a) * std::sin (w) / (2 * q);
    if (high)
        f.set (a * ((a + 1) + (a - 1) * c + s), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - s),
               (a + 1) - (a - 1) * c + s, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - s);
    else
        f.set (a * ((a + 1) - (a - 1) * c + s), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - s),
               (a + 1) + (a - 1) * c + s, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - s);
}
// the measured bells: an analog bell (pole Q = q·A, zero Q = q / A, A = gain^½) with bilinear (pre-warped) poles and
// a numerator matched to the analog magnitude at DC, f0 and Nyquist — the form that nulls our probes to -60 … -100 dB
void matchedBell (Biquad& f, double sr, double hz, double q, double db) noexcept
{
    hz = std::min (hz, 0.45 * sr);
    const auto g = std::pow (10.0, db / 20), A = std::sqrt (g), qp = q * A, qz = q / A, w0 = 2 * pi * hz / sr;
    const auto K = std::tan (w0 / 2), d = 1 + K / qp + K * K;
    const auto a1 = 2 * (K * K - 1) / d, a2 = (1 - K / qp + K * K) / d;
    auto ana2 = [&] (double w) { // |H(jw)|² of the analog bell, w in rad/sample mapped to analog by w / w0 ratio
        const auto x = w / w0, re = 1 - x * x;
        return (re * re + x * x / (qz * qz)) / (re * re + x * x / (qp * qp));
    };
    const auto A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
    const auto s2 = std::sin (w0 / 2) * std::sin (w0 / 2), p0 = 1 - s2, p1 = s2, p2 = 4 * p0 * p1;
    const auto B0 = A0, B1 = A1 * ana2 (pi);
    const auto B2 = (g * g * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;
    const auto W = 0.5 * (std::sqrt (B0) + std::sqrt (std::max (B1, 0.0)));
    const auto b0 = 0.5 * (W + std::sqrt (std::max (W * W + B2, 0.0))), b1 = 0.5 * (std::sqrt (B0) - std::sqrt (std::max (B1, 0.0)));
    const auto b2 = -B2 / (4 * b0);
    f.set (b0, b1, b2, 1, a1, a2);
}
// piecewise-linear interpolation over a table (x ascending)
template <std::size_t N> double interp (const double (&x)[N], const double (&y)[N], double v) noexcept
{
    if (v <= x[0]) return y[0];
    for (std::size_t i = 1; i < N; ++i)
        if (v <= x[i]) return y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]);
    return y[N - 1];
}

// ---- EQ laws (probe batches eq1 / eq2, lab/fiteq2.py) ----
// bells: the analog Q (A-symmetric) per type against |gain| (Wide / Narrow differ between Tone and Mid); Parametric = Q / √2
constexpr double bellG[] = { 1, 3, 6, 12, 18 };
constexpr double toneWideQ[] = { 0.2351, 0.2556, 0.2880, 0.3676, 0.4705 };
constexpr double toneNarrowQ[] = { 0.4711, 0.5110, 0.5764, 0.7330, 0.9367 };
constexpr double midWideQ[] = { 0.0451, 0.0619, 0.0879, 0.1693, 0.3261 };
constexpr double midNarrowQ[] = { 0.1879, 0.2340, 0.3222, 0.6120, 1.1675 };
// shelves: RBJ shelves at f × ratio(|gain|) with Q(gain); the low shelf pre-warped, the high shelf unwarped (K = π f r / sr)
constexpr double shelfG[] = { 3, 6, 12, 18 };
constexpr double lfRatioLog[] = { 2.75321, 2.05458, 1.30887, 0.84476 }; // ln ratio
constexpr double lfQBoost[] = { 0.4899, 0.4835, 0.4710, 0.4578 };
constexpr double lfQCut[] = { 0.5027, 0.5100, 0.5220, 0.5349 };
constexpr double hfQ[] = { 0.4982, 0.4927, 0.4720, 0.4397 };

void eqBand (Biquad& f, double sr, int band, int type, double hz, double db, double qSet) noexcept
{
    if (std::abs (db) < 1e-6) { flat (f); return; }
    const auto g = std::abs (db);
    const bool shelfBand = band == 0 || band == 3;
    if (shelfBand && type == 2) { matchedBell (f, sr, hz, qSet / std::sqrt (2.0), db); return; }   // Parametric
    if (! shelfBand)
    {
        double q = qSet / std::sqrt (2.0);
        if (type == 0) q = interp (bellG, band == 1 ? toneWideQ : midWideQ, g);
        else if (type == 1) q = interp (bellG, band == 1 ? toneNarrowQ : midNarrowQ, g);
        matchedBell (f, sr, hz, q, db);
        return;
    }
    if (band == 0)
    {
        // ln ratio: the table, and ~1 / gain below 3 dB (the measured trend)
        const auto lr = g < 3 ? lfRatioLog[0] + std::log (3.0 / g) : interp (shelfG, lfRatioLog, g);
        auto q = interp (shelfG, db > 0 ? lfQBoost : lfQCut, g);
        auto f0 = hz * std::exp (lr);
        if (type == 0) { f0 = hz * 2.2; q = 0.8; }   // ResShelf: a shelf with a resonant bump (approximation, REA doc §3)
        rbjShelf (f, sr, std::min (f0, 0.45 * sr), q, db, false);
        return;
    }
    const auto r = std::exp2 (0.04155 * (g - 12.0));
    auto fa = hz * r;
    auto q = interp (shelfG, hfQ, g);
    if (type == 0) { fa = hz * 0.6; q = 0.9; }      // ResShelf (approximation)
    const auto f0 = std::atan (pi * fa / sr) * sr / pi;
    rbjShelf (f, sr, std::min (f0, 0.45 * sr), q, db, true);
}

// ---- compressor (probe batches comp1 / mk1 / mk2, lab/fitcomp.py, lab/fittau.py, lab/gentab.py) ----
#include "core/OmniStripTables.inc"
struct CompType
{
    double dT, knee, rScale;   // threshold offset dB, knee width dB, ratio scale (1 + (r - 1) s)
    double a0, aK;             // attack time constant = a0 + aK · attack (s)
    double rK, rExp;           // fast release = rK · (release / 50 ms)^rExp · 50 ms
    double slowK, slowW0, slowW1; // slow release stage = slowK · release, weight slowW0 + slowW1 · log10 (release / 200 ms)
};
constexpr CompType compTypes[4] = {
    { 2.90, 14.7, 1.000, 0.00022, 0.047, 0.58, 0.40, 3.5, 0.24, 0.51 },   // VCA
    { 3.76, 18.1, 1.519, 0.00010, 0.450, 1.10, 1.17, 3.0, 0.00, 0.00 },   // FET
    { 2.29, 16.0, 0.652, 0.00000, 0.0367, 0.385, 1.00, 3.0, 0.00, 0.00 }, // Opt (attack: see setup; programme probes prog1)
    { -0.54, 34.1, 0.673, 0.00015, 0.042, 0.49, 0.43, 2.0, 0.32, 0.34 },  // Soft
};
double makeupDb (int type, double thr, double ratio, double attMs, double relMs) noexcept
{
    type = std::clamp (type, 0, 3);
    constexpr double th[] = { -50, -40, -30, -20, -10, -5, 0 };
    constexpr double rs[] = { 1.6, 2.3, 3.3, 5.0, 7.5, 16.0, 50.0, 1e9 };
    auto rIndex = [&] (double r) { // position in the ratio table on the (1 - 1 / r) scale
        const auto u = 1 - 1 / std::max (r, 1.0);
        if (u <= 1 - 1 / rs[0]) return u / (1 - 1 / rs[0]) - 1.0;   // below 1.6 : 1 → towards 0 at 1 : 1
        for (int i = 1; i < 8; ++i) { const auto hi = 1 - 1 / rs[i], lo = 1 - 1 / rs[i - 1]; if (u <= hi) return (i - 1) + (u - lo) / (hi - lo); }
        return 7.0;
    };
    auto tIndex = [&] (double t) {
        if (t <= th[0]) return 0.0;
        for (int i = 1; i < 7; ++i) if (t <= th[i]) return (i - 1) + (t - th[i - 1]) / (th[i] - th[i - 1]);
        return 6.0;
    };
    const auto ri = rIndex (ratio), ti = tIndex (thr);
    auto at = [&] (int t, double r) {
        if (r < 0) return makeupThrRatio[type][t][0] * (r + 1.0);
        const auto i = std::min ((int) r, 6); const auto fr = r - i;
        return makeupThrRatio[type][t][i] * (1 - fr) + makeupThrRatio[type][t][i + 1] * fr;
    };
    const auto t0 = std::min ((int) ti, 5); const auto ft = ti - t0;
    const auto base = at (t0, ri) * (1 - ft) + at (t0 + 1, ri) * ft;
    // the time factor: bilinear on log attack / log release
    constexpr double atk[] = { 0.4, 1.76, 7.75, 34.09, 150.0 }, rel[] = { 50, 158.1, 500, 1581.1, 5000 };
    auto logIndex = [] (const double (&x)[5], double v) {
        v = std::clamp (v, x[0], x[4]);
        for (int i = 1; i < 5; ++i) if (v <= x[i]) return (i - 1) + std::log (v / x[i - 1]) / std::log (x[i] / x[i - 1]);
        return 4.0;
    };
    const auto ai = logIndex (atk, attMs), li = logIndex (rel, relMs);
    const auto a0 = std::min ((int) ai, 3), l0 = std::min ((int) li, 3);
    const auto fa = ai - a0, fl = li - l0;
    const auto& T = makeupTime[type];
    const auto tf = (T[a0][l0] * (1 - fl) + T[a0][l0 + 1] * fl) * (1 - fa) + (T[a0 + 1][l0] * (1 - fl) + T[a0 + 1][l0 + 1] * fl) * fa;
    return base * tf / T[0][0];
}

// ---- saturation (probe batch pre1, lab/anasat.py / lab/curve.py) ----
// Odd / Even: a level-independent gain, a first-order all-pass (~8 Hz, the phase our probes show) and a soft clipper
// whose ceiling falls with the amount; Heavy: a static soft clipper (knee n = 5); Crush: Heavy's curve driven harder
double satGainDb (int type, double amount) noexcept
{
    amount = std::clamp (amount, 0.0, 100.0);
    switch (type)
    {
        case 0: return 0.5 + 0.00201 * std::pow (amount, 1.807);   // Odd: +1.39 dB at 30, +8.69 at 100
        case 1: return 0.5 + 0.05 * amount;                         // Even: +2.0 at 30, +5.5 at 100
        case 2: return 0.0153 * std::pow (amount, 1.398);           // Heavy: +1.76 at 30, +9.47 at 100
        default: return 3.2 + 0.104 * amount;                       // Crush: +6.3 at 30, +13.6 at 100
    }
}
double satCeiling (int type, double amount) noexcept
{
    amount = std::clamp (amount, 0.0, 100.0);
    switch (type)
    {
        case 0: return std::exp (std::log (2.5) + amount / 100.0 * std::log (0.43 / 2.5));   // Odd: ≈ 0.43 at 100
        case 1: return std::exp (std::log (2.5) + amount / 100.0 * std::log (0.55 / 2.5));   // Even
        case 2: return std::exp (std::log (1.53) + amount / 100.0 * std::log (0.2154 / 1.53)); // Heavy: 0.85 at 30, 0.2154 at 100
        default: return std::exp (std::log (0.6) + amount / 100.0 * std::log (0.06 / 0.6));  // Crush
    }
}
inline double softClip (double v, double c, double n) noexcept
{
    const auto u = std::abs (v) / c;
    return v / std::pow (1 + std::pow (u, n), 1 / n);
}

struct Engine
{
    void prepare (double rate) noexcept { sr = rate; reset(); }
    void reset() noexcept
    {
        for (auto* f : { &hpA, &hpB, &lpA, &lpB, &thumpA, &thumpB, &eq[0], &eq[1], &eq[2], &eq[3], &scHp, &scBell, &scShelf,
                         &gHp, &gLp, &ds[0].sc, &ds[1].sc, &ds[0].cut, &ds[1].cut })
            f->reset();
        apL = apR = apXL = apXR = 0;
        cg = cg2 = 0; gateG = 1; gateOpen = false; gateEnv = 0; limG = 1;
        ds[0].env = ds[1].env = 0; ds[0].gr = ds[1].gr = -1;
        lastKey = -1;
    }
    float compGrDb() const noexcept { return (float) cgShown; }
    float gateGrDb() const noexcept { return (float) (-20 * std::log10 (std::max (gateG, 1e-6))); }

    void process (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        namespace o = omni;
        setup (p);
        const auto gin = std::pow (10.0, o::db (p[omniIn]) / 20) , gout = std::pow (10.0, o::db (p[omniOut]) / 20);
        const auto il = on (p[omniInvL]) ? -gin : gin, ir = on (p[omniInvR]) ? -gin : gin;
        for (int i = 0; i < n; ++i) { l[i] = (float) (l[i] * il); r[i] = (float) (r[i] * ir); }
        int order[5];
        o::orderOf (p[omniOrder], order);
        for (int m : order)
            switch (m)
            {
                case o::pre: if (on (p[oPreOn])) processPre (l, r, n, p); break;
                case o::gate: if (on (p[oGateOn])) processGate (l, r, n, p); break;
                case o::ds2: if (on (p[oDsOn])) processDs (l, r, n, p); break;
                case o::eq: if (on (p[oEqOn])) for (auto& f : eq) f.processBlock (l, r, n); break;
                case o::comp: if (on (p[oCompOn])) processComp (l, r, n, p); break;
                default: break;
            }
        if (gout != 1.0)
            for (int i = 0; i < n; ++i) { l[i] = (float) (l[i] * gout); r[i] = (float) (r[i] * gout); }
        if (on (p[omniLimOn])) // the output brickwall limiter (instant attack, 50 ms release; not measured)
        {
            const auto ceil = std::pow (10.0, o::db (p[omniLimThr]) / 20), rel = std::exp (-1 / (0.05 * sr));
            for (int i = 0; i < n; ++i)
            {
                const auto pk = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
                const auto want = pk * limG > ceil ? ceil / pk : 1.0;
                limG = want < limG ? want : rel * limG + (1 - rel) * want;
                l[i] = (float) (l[i] * limG); r[i] = (float) (r[i] * limG);
            }
        }
    }

private:
    static bool on (float v) noexcept { return v > 0.5f; }

    // coefficients from the parameters (only when they change: a key over every value)
    void setup (const EffectParams& p) noexcept
    {
        namespace o = omni;
        double key = 0;
        for (int i = omniLaw; i < omniEnd; ++i) key = key * 1.000003 + p[(std::size_t) i] * (i + 1);
        if (key == lastKey) return;
        lastKey = key;
        // Pre filters
        const auto hpHz = o::hz (p[oHpHz]), lpHz = o::hz (p[oLpHz]);
        const auto qs = o::q (p[oHpQ]), ql = o::q (p[oLpQ]);
        auto q12 = [] (double q) { return q >= 1 ? 0.70711 * q : 0.70711 * std::pow (q, 0.301); };
        hpSlope = std::clamp (o::index (p[oHpSlope]), 0, 3); lpSlope = std::clamp (o::index (p[oLpSlope]), 0, 3);
        flat (hpA); flat (hpB); flat (lpA); flat (lpB);
        if (on (p[oHpOn]))
            switch (hpSlope)
            {
                case 0: hp1 (hpA, sr, hpHz); break;
                case 1: rbjHp2 (hpA, sr, hpHz, q12 (qs)); break;
                case 2: rbjHp2 (hpA, sr, hpHz, 1.41421 * q12 (qs)); hp1 (hpB, sr, hpHz); break;
                default: rbjHp2 (hpA, sr, hpHz * 0.630957, 4.0163 * std::sqrt (qs)); rbjHp2 (hpB, sr, hpHz * 1.584893, 2.2592 * std::sqrt (qs)); break;
            }
        if (on (p[oLpOn]))
            switch (lpSlope)
            {
                case 0: lp1 (lpA, sr, lpHz); break;
                case 1: rbjLp2 (lpA, sr, lpHz, q12 (ql)); break;
                case 2: rbjLp2 (lpA, sr, lpHz, 1.41421 * q12 (ql)); lp1 (lpB, sr, lpHz); break;
                default: rbjLp2 (lpA, sr, lpHz * 1.584893, 4.0163 * std::sqrt (ql)); rbjLp2 (lpB, sr, lpHz * 0.630957, 2.2592 * std::sqrt (ql)); break;
            }
        // thump (+2 / +4 dB: a very wide low bell and a faint high shelf, probe batch pre1)
        flat (thumpA); flat (thumpB);
        if (p[oThump] > 0.25f)
        {
            const bool four = p[oThump] > 0.75f;
            rbjPeak (thumpA, sr, four ? 122.0 : 65.0, four ? 0.098 : 0.107, four ? 3.99 : 2.0);
            rbjShelf (thumpB, sr, 5800.0, 0.63, 0.27, true);
        }
        // saturation
        satType = std::clamp (o::index (p[oSatType]), 0, 3);
        const auto amount = 100.0 * std::clamp ((double) p[oSatAmount], 0.0, 1.0);
        satGain = std::pow (10.0, satGainDb (satType, amount) / 20);
        satC = satCeiling (satType, amount);
        apC = (1 - std::tan (pi * 8.0 / sr)) / (1 + std::tan (pi * 8.0 / sr));
        // EQ
        for (int b = 0; b < 4; ++b)
        {
            const auto base = oEqBand0 + 5 * b;
            if (! on (p[(std::size_t) base])) { flat (eq[b]); continue; }
            eqBand (eq[b], sr, b, std::clamp (o::index (p[(std::size_t) base + 1]), 0, 2), o::hz (p[(std::size_t) base + 2]),
                    o::db (p[(std::size_t) base + 3]), o::q (p[(std::size_t) base + 4]));
        }
        // compressor
        cType = std::clamp (o::index (p[oCompType]), 0, 3);
        const auto& T = compTypes[cType];
        const auto ratio = o::ratio (p[oCompRatio]), attMs = o::ms (p[oCompAtt]), relMs = o::ms (p[oCompRel]);
        cThr = o::db (p[oCompThr]) + T.dT;
        cSlope = 1 - 1 / (1 + (std::min (ratio, 20.0) - 1) * T.rScale);
        cKnee = T.knee;
        auto tauA = T.a0 + T.aK * attMs * 1e-3;
        if (cType == 2) // Opt: the cell's attack slows with the release setting (probe batches comp1 / prog1)
            tauA = T.aK * std::max (attMs * 1e-3, 0.05 * std::clamp (std::log2 (relMs / 50.0) / 2.0, 0.0, 1.0));
        cAtt = std::exp (-1 / (std::max (tauA, 1e-5) * sr));
        const auto fastRel = T.rK * std::pow (relMs / 50.0, T.rExp) * 0.05;
        cRel = std::exp (-1 / (fastRel * sr));
        cW2 = std::clamp (T.slowW0 + T.slowW1 * std::log10 (relMs / 200.0), 0.0, 0.7);
        cRel2 = std::exp (-1 / (T.slowK * relMs * 1e-3 * sr));
        cMakeup = std::pow (10.0, makeupDb (cType, o::db (p[oCompThr]), ratio, attMs, relMs) / 20);
        cMix = std::clamp ((double) p[oCompMix], 0.0, 1.0);
        cOut = std::pow (10.0, o::db (p[oCompOut]) / 20);
        flat (scHp); flat (scBell); flat (scShelf);
        if (on (p[oScHpOn])) rbjHp2 (scHp, sr, o::hz (p[oScHpHz]), 0.70711);
        if (on (p[oScBellOn])) rbjPeak (scBell, sr, o::hz (p[oScBellHz]), 0.709, o::db (p[oScBellDb]));
        if (on (p[oScShelfOn])) rbjShelf (scShelf, sr, o::hz (p[oScShelfHz]), 0.709, o::db (p[oScShelfDb]), true);
        scFilters = on (p[oScHpOn]) || on (p[oScBellOn]) || on (p[oScShelfOn]);
        // gate
        flat (gHp); flat (gLp);
        if (on (p[oGateHpOn])) rbjHp2 (gHp, sr, o::hz (p[oGateHpHz]), 0.70711);
        if (on (p[oGateLpOn])) rbjLp2 (gLp, sr, o::hz (p[oGateLpHz]), 0.70711);
        // DS2 bands
        for (int b = 0; b < 2; ++b)
        {
            auto& d = ds[b];
            const auto base = b == 0 ? oDs1On : oDs2On;
            d.on = on (p[(std::size_t) base]);
            d.thr = o::db (p[(std::size_t) base + 1]);
            d.type = std::clamp (o::index (p[(std::size_t) base + 2]), 0, 3);
            d.hz = o::hz (p[(std::size_t) base + 3]);
            d.range = std::max (3.0, -o::db (p[(std::size_t) base + 4]));
            switch (d.type)
            {
                case 0: rbjLp2 (d.sc, sr, d.hz, 0.70711); break;
                case 1: rbjPeak (d.sc, sr, d.hz, 1.0, 12.0); break;
                case 2: rbjPeak (d.sc, sr, d.hz, 4.0, 18.0); break;
                default: rbjHp2 (d.sc, sr, d.hz, 0.70711); break;
            }
            d.gr = -1; // re-design the cut
        }
        dsOut = std::pow (10.0, o::db (p[oDsOut]) / 20);
        dsAtt = std::exp (-1 / (std::max (o::ms (p[oDsAtt]), 0.1) * 1e-3 * sr));
        dsRel = std::exp (-1 / (std::max (o::ms (p[oDsRel]), 1.0) * 1e-3 * sr));
    }

    void processPre (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        if (on (p[oSatOn]))
        {
            const auto knee = satType == 2 ? 5.0 : satType == 3 ? 3.0 : 4.0;
            const auto bias = satType == 1 ? 0.35 * satC : 0.0;
            const auto off = bias != 0 ? softClip (bias, satC, knee) : 0.0;
            // the small-signal slope at the bias point, so that the measured level-independent gain holds
            const auto eps = 1e-4 * satC;
            const auto norm = bias != 0 ? 2 * eps / (softClip (bias + eps, satC, knee) - softClip (bias - eps, satC, knee)) : 1.0;
            for (int i = 0; i < n; ++i)
            {
                double x[2] = { l[i] * satGain, r[i] * satGain };
                for (int c = 0; c < 2; ++c)
                {
                    auto y = (softClip (x[c] + bias, satC, knee) - off) * norm;
                    if (satType <= 1)
                    {   // the first-order all-pass the probes show (in phase at the top, inverted at DC)
                        auto& xs = c == 0 ? apXL : apXR; auto& ys = c == 0 ? apL : apR;
                        const auto a = apC * y - xs + apC * ys;
                        xs = y; ys = a; y = a;
                    }
                    x[c] = y;
                }
                l[i] = (float) x[0]; r[i] = (float) x[1];
            }
        }
        if (p[oThump] > 0.25f) { thumpA.processBlock (l, r, n); thumpB.processBlock (l, r, n); }
        if (on (p[oHpOn])) { hpA.processBlock (l, r, n); if (hpSlope >= 2) hpB.processBlock (l, r, n); }
        if (on (p[oLpOn])) { lpA.processBlock (l, r, n); if (lpSlope >= 2) lpB.processBlock (l, r, n); }
    }

    void processComp (float* l, float* r, int n, const EffectParams&) noexcept
    {
        const auto& T = compTypes[cType];
        (void) T;
        double peak = 0;
        for (int i = 0; i < n; ++i)
        {
            double s = 0.5 * ((double) l[i] + (double) r[i]);
            if (scFilters) { s = scHp.process ((float) s, 0); s = scBell.process ((float) s, 0); s = scShelf.process ((float) s, 0); }
            const auto L = 20 * std::log10 (std::abs (s) + 1e-15), d = L - cThr;
            double t = 0;
            if (2 * std::abs (d) <= cKnee) t = cSlope * (d + cKnee / 2) * (d + cKnee / 2) / (2 * cKnee);
            else if (d > 0) t = cSlope * d;
            const auto c = t > cg ? cAtt : cRel;
            cg = c * cg + (1 - c) * t;
            const auto c2 = cg > cg2 ? cAtt : cRel2;
            cg2 = c2 * cg2 + (1 - c2) * cg;
            const auto G = (1 - cW2) * cg + cW2 * cg2;
            peak = std::max (peak, G);
            const auto wet = std::pow (10.0, -G / 20) * cMakeup;
            const auto g = (wet * cMix + (1 - cMix)) * cOut;
            l[i] = (float) (l[i] * g); r[i] = (float) (r[i] * g);
        }
        cgShown = peak;
    }

    void processGate (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        namespace o = omni;
        const auto thrV = p[oGateThr];
        const auto thr = thrV <= 0.0f ? -1e9 : o::db (thrV);
        const auto closeDb = o::db (p[oGateClose]);
        const auto close = closeDb < -0.01 ? thr + closeDb : thr;   // Close: a second, lower closing threshold
        const auto floor = std::pow (10.0, std::min (0.0, o::db (p[oGateFloor])) / 20);
        const bool exp = on (p[oGateExp]);
        const auto att = std::exp (-1 / (std::max (o::ms (p[oGateAtt]), 0.05) * 1e-3 * sr));
        const auto rel = std::exp (-1 / (std::max (o::ms (p[oGateRel]), 0.05) * 1e-3 * sr));
        const auto envC = std::exp (-1 / (0.001 * sr));
        const bool hp = on (p[oGateHpOn]), lp = on (p[oGateLpOn]);
        for (int i = 0; i < n; ++i)
        {
            double s = 0.5 * ((double) l[i] + (double) r[i]);
            if (hp) s = gHp.process ((float) s, 0);
            if (lp) s = gLp.process ((float) s, 0);
            gateEnv = std::max (std::abs (s), envC * gateEnv);
            const auto L = 20 * std::log10 (gateEnv + 1e-15);
            if (gateOpen ? L < close : L >= thr) gateOpen = ! gateOpen;
            double want = 1;
            if (exp) want = L >= thr ? 1.0 : std::max (floor, std::pow (10.0, (L - thr) / 20));   // 2 : 1 downward expander
            else want = gateOpen ? 1.0 : floor;
            const auto c = want > gateG ? att : rel;
            gateG = c * gateG + (1 - c) * want;
            l[i] = (float) (l[i] * gateG); r[i] = (float) (r[i] * gateG);
        }
    }

    void processDs (float* l, float* r, int n, const EffectParams&) noexcept
    {
        // each band: its side-chain shape detects; above the threshold the same region is cut (up to the range)
        constexpr int sub = 16;
        for (int i0 = 0; i0 < n; i0 += sub)
        {
            const auto m = std::min (sub, n - i0);
            for (auto& d : ds)
            {
                if (! d.on) continue;
                double e = 0;
                for (int i = i0; i < i0 + m; ++i)
                {
                    const auto s = d.sc.process ((float) (0.5 * ((double) l[i] + (double) r[i])), 0);
                    e = std::max (e, std::abs ((double) s));
                }
                const auto c = e > d.env ? dsAtt : dsRel;
                d.env = c * d.env + (1 - c) * e;
                const auto L = 20 * std::log10 (d.env + 1e-15);
                const auto want = std::clamp ((L - d.thr) * 0.75, 0.0, d.range);
                if (std::abs (want - d.gr) > 0.05)
                {
                    d.gr = want;
                    switch (d.type)
                    {
                        // the shelves' cut reaches below / above the detection corner (probe batch prog1: 2 … 5 kHz cut
                        // with a 6 kHz high shelf)
                        case 0: rbjShelf (d.cut, sr, d.hz * 1.8, 0.6, -want, false); break;
                        case 1: rbjPeak (d.cut, sr, d.hz, 1.0, -want); break;
                        case 2: rbjPeak (d.cut, sr, d.hz, 4.0, -want); break;
                        default: rbjShelf (d.cut, sr, d.hz * 0.55, 0.6, -want, true); break;
                    }
                }
                d.cut.processBlock (l + i0, r + i0, m);
            }
        }
        if (dsOut != 1.0)
            for (int i = 0; i < n; ++i) { l[i] = (float) (l[i] * dsOut); r[i] = (float) (r[i] * dsOut); }
    }

    double sr = 48000;
    double lastKey = -1;
    Biquad hpA, hpB, lpA, lpB, thumpA, thumpB, eq[4], scHp, scBell, scShelf, gHp, gLp;
    int hpSlope = 1, lpSlope = 1, satType = 0, cType = 0;
    double satGain = 1, satC = 1, apC = 0, apL = 0, apR = 0, apXL = 0, apXR = 0;
    double cThr = 0, cSlope = 0, cKnee = 0, cAtt = 0, cRel = 0, cRel2 = 0, cW2 = 0, cMakeup = 1, cMix = 1, cOut = 1, cg = 0, cg2 = 0, cgShown = 0;
    bool scFilters = false;
    double gateG = 1, gateEnv = 0, limG = 1;
    bool gateOpen = false;
    struct DsBand { Biquad sc, cut; bool on = false; int type = 3; double thr = 0, hz = 6000, range = 12, env = 0, gr = -1; } ds[2];
    double dsOut = 1, dsAtt = 0, dsRel = 0;
};
} // namespace omnistrip
