#include "core/FruityDelay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Denormals.h"

// Bit-exactness with FL's plugins needs every float operation in the plugin's order and precision: no contraction
// into fused multiply-adds, and the x87 extended precision of Delay 2's loops kept (long double; 80-bit on every x86
// target LYNX is built for).
#pragma GCC optimize ("fp-contract=off")
#pragma STDC FP_CONTRACT OFF

namespace lynx::fldelay
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

// cvtsd2si: round to nearest, ties to even (the default SSE mode in FL and in LYNX)
int roundEven (double x) noexcept { return (int) std::nearbyint (x); }
} // namespace

// =====================================================================================================================
// Fruity Delay 2 (REA_rodada52 §2)
// =====================================================================================================================

const std::array<int, delay2Controls> delay2Min { -128, 0, 0, 0, 1, -512, 0, 0 };
const std::array<int, delay2Controls> delay2Max { 128, 160, 160, 127, 768, 512, 2, 128 };
const std::array<int, delay2Controls> delay2Default { 0, 128, 128, 64, 144, 0, 0, 128 };

const char* delay2ParamName (int i) noexcept
{
    static const char* names[] { "Input pan", "Input volume", "Dry", "Feedback", "Time", "Stereo offset", "Mode", "Cutoff" };
    return i >= 0 && i < delay2Controls ? names[i] : "";
}

std::optional<Delay2State> parseDelay2 (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 32)
        return std::nullopt;
    Delay2State s;
    for (int i = 0; i < delay2Controls; ++i)
        s.v[(std::size_t) i] = std::clamp (i32At (state, 4 * (std::size_t) i), delay2Min[(std::size_t) i], delay2Max[(std::size_t) i]);
    return s;
}

namespace
{
// FL's volume law: 1 at the unit position, else (11^x − 1)/10 with ln 11 as a float
float flVolume (int raw) noexcept
{
    const auto x = (float) raw * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    constexpr float ln11 = 2.39789534f;
    return (float) ((std::exp ((double) (x * ln11)) - 1.0) * (double) 0.1f);
}

// a per-sample linear ramp toward a target, its slope limited to `maxStep` per sample (start, increment, new value)
struct Ramp
{
    float start, inc;
};

Ramp ramp (float& cur, float target, float maxStep, int n) noexcept
{
    const auto start = cur;
    auto delta = target - start;
    if (delta == 0.0f)
        return { start, 0.0f };
    cur = target;
    delta = delta / (float) n;
    if (std::fabs (delta) > maxStep)
    {
        delta = std::copysign (maxStep, delta);
        cur = start + delta * (float) n;
        if (std::fabs (cur) < 1.17549435e-38f)
        {
            cur = 0.0f;
            delta = -start / (float) n;
        }
    }
    return { start, delta };
}
} // namespace

Delay2Engine::Delay2Engine (const Delay2State& s, double sampleRate, double bpm, int maxBlock, bool settled)
    : rate (sampleRate), block (maxBlock)
{
    tmp.resize ((std::size_t) maxBlock * 2);
    restart (s, bpm, settled);
}

void Delay2Engine::restart (const Delay2State& s, double bpm, bool settled)
{
    fbCur = (float) s.v[3] * 0.0078125f;
    for (auto& g : gCur)
        g = 0.0f;
    stL = stR = 0.0;
    line.clear(); // (its capacity stays)
    set (s, bpm);
    if (settled)
        for (int k = 0; k < 4; ++k)
            gCur[k] = gTarget[k];
}

void Delay2Engine::reserve (double minBpm)
{
    // set()'s longest line: the time knob's maximum at minBpm, the block and the 0.5 s stereo offset
    const auto delay = (double) delay2Max[4] * rate * 0.3125 / std::max (1.0, minBpm);
    const auto most = (std::size_t) (delay + (double) block + rate * 0.5 + 4.0);
    line.reserve (most * 2);
}

void Delay2Engine::set (const Delay2State& s, double bpm)
{
    const auto& p = s.v;
    const auto ratio = 44100.0 / rate;
    step = (float) (0.001 * ratio);
    const auto pan = (float) p[0] * 0.0078125f;
    const auto vol = flVolume (p[1]);
    dry = flVolume (p[2]);
    fbTarget = (float) p[3] * 0.0078125f;
    if (pan >= 0.0f)
    {
        auto g0 = 1.0f - pan;
        const auto g3 = ((1.0f - g0) * 0.5f) * vol;
        g0 = g0 * vol;
        gTarget[0] = g0;
        gTarget[1] = vol - g3;
        gTarget[2] = 0.0f;
        gTarget[3] = g3;
    }
    else
    {
        auto g1 = pan + 1.0f;
        const auto g2 = ((1.0f - g1) * 0.5f) * vol;
        g1 = g1 * vol;
        gTarget[0] = vol - g2;
        gTarget[1] = g1;
        gTarget[2] = g2;
        gTarget[3] = 0.0f;
    }
    mode = p[6];
    const auto c = (float) ((double) (float) (std::exp ((double) ((float) p[7] * 0.0078125f) * std::log (21.0)) - 1.0) * 0.05 * ratio);
    coef = c < 1.0f ? c : 1.0f;
    const auto off = (float) ((double) (float) (std::exp ((double) ((float) std::abs (p[5]) * 0.001953125f) * std::log (1001.0)) - 1.0) * 50.0);
    const auto offSamples = roundEven ((double) ((off * (float) rate) / 100000.0f));
    const auto delay = std::max (1, roundEven ((double) (p[4] * (int) rate) * 0.3125 / (double) (float) bpm));
    const auto sign = p[5] <= 0 ? 0 : 1;
    if (! line.empty() && delay == delaySamples && offSamples == offsetSamples && sign == offsetSign)
        return;
    delaySamples = delay;
    offsetSamples = offSamples;
    offsetSign = sign;
    offL = sign == 0 ? offSamples : 0;
    offR = sign == 0 ? 0 : offSamples;
    frames = delay + block + offSamples;
    line.assign ((std::size_t) frames * 2, 0.0f);
    readPos = 0;
    writePos = delay;
}

void Delay2Engine::process (float* left, float* right, int n) noexcept
{
    if (n <= 0)
        return;
    // input pan/volume matrix, each gain ramping toward its target
    float g[4], gi[4];
    for (int k = 0; k < 4; ++k)
    {
        const auto r = ramp (gCur[k], gTarget[k], step, n);
        g[k] = r.start;
        gi[k] = r.inc;
    }
    const bool ramping = gi[0] != 0.0f || gi[1] != 0.0f || gi[2] != 0.0f || gi[3] != 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto l = left[i], r = right[i];
        tmp[(std::size_t) (2 * i)] = l * g[0] + r * g[2];
        tmp[(std::size_t) (2 * i + 1)] = r * g[1] + l * g[3];
        if (ramping)
            for (int k = 0; k < 4; ++k)
                g[k] += gi[k];
    }
    // into the line, each channel ahead of the write position by its stereo offset
    for (int i = 0; i < n; ++i)
    {
        auto a = (writePos + offL + i) % frames;
        line[(std::size_t) (2 * a)] += tmp[(std::size_t) (2 * i)];
        a = (writePos + offR + i) % frames;
        line[(std::size_t) (2 * a + 1)] += tmp[(std::size_t) (2 * i + 1)];
    }
    // the loop: x87 extended precision, states kept as doubles between blocks
    const auto fr = ramp (fbCur, fbTarget, step, n);
    long double fb = fr.start;
    const long double inc = fr.inc, c = coef, thr = 5.9604644775390625e-08L;
    long double sL = stL, sR = stR;
    const bool toDst = dry == 0.0f;
    for (int i = 0; i < n; ++i)
    {
        auto* rd = &line[(std::size_t) (2 * readPos)];
        auto* wr = &line[(std::size_t) (2 * writePos)];
        const long double lL = rd[0], lR = rd[1];
        long double oL, oR, fL, fR;
        if (mode == 0)
            oL = lL, oR = lR, fL = lL, fR = lR;
        else if (mode == 1)
            oL = lR, oR = lL, fL = lL, fR = lR;
        else
            oL = lR, oR = lL, fL = lR, fR = lL;
        auto y = (fL * fb + (long double) wr[0] - sL) * c + sL;
        sL = y;
        wr[0] = std::fabs (y) > thr ? (float) y : 0.0f;
        y = (fR * fb + (long double) wr[1] - sR) * c + sR;
        sR = y;
        wr[1] = std::fabs (y) > thr ? (float) y : 0.0f;
        fb += inc;
        rd[0] = rd[1] = 0.0f;
        readPos = readPos + 1 == frames ? 0 : readPos + 1;
        writePos = writePos + 1 == frames ? 0 : writePos + 1;
        if (toDst)
        {
            left[i] = (float) oL;
            right[i] = (float) oR;
        }
        else
        {
            const long double dg = dry;
            left[i] = (float) ((long double) left[i] * dg + (long double) (float) oL);
            right[i] = (float) ((long double) right[i] * dg + (long double) (float) oR);
        }
    }
    stL = (double) sL;
    stR = (double) sR;
}

// =====================================================================================================================
// Fruity Delay 3 (REA_rodada52 §3)
// =====================================================================================================================

const std::array<int, delay3Controls> delay3Min { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -3000, 0 };
const std::array<int, delay3Controls> delay3Max { 6000, 3, 1, 1, 768, 6000, 1000, 6000, 6000, 6000, 6000, 6000, 6000, 1, 6000, 3, 6000, 6000, 6000, 6000, 6000, 60000, 6000, 6000, 3000, 6000 };
// FL's "Default" preset
const std::array<int, delay3Controls> delay3Default { 6000, 1, 1, 0, 192, 3000, 500, 6000, 3000, 0, 0, 0, 0, 1, 3000, 0, 6000, 0, 0, 0, 0, 60000, 6000, 6000, 0, 6000 };

const char* delay3ParamName (int i) noexcept
{
    static const char* names[] { "Send", "Delay type", "Tempo sync", "Keep pitch", "Time", "Time R / pan", "Time smoothing",
                                 "Stereo spread", "Mod rate", "Mod amount", "Cutoff mod", "Diffusion", "Diffusion spread",
                                 "Saturation type", "Feedback", "Filter mode", "Cutoff", "Resonance", "Distortion", "Hardness",
                                 "Symmetry", "Rate reduce", "Bit reduce", "Wet", "Colour", "Dry" };
    return i >= 0 && i < delay3Controls ? names[i] : "";
}

std::optional<Delay3State> parseDelay3 (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4 + 4 * (std::size_t) delay3Controls || i32At (state, 0) != 1)
        return std::nullopt;
    Delay3State s;
    for (int i = 0; i < delay3Controls; ++i)
        s.v[(std::size_t) i] = i32At (state, 4 + 4 * (std::size_t) i);
    return s;
}

float delay3EngineValue (int control, int raw) noexcept
{
    if (control == 2 || control == 3 || control == 13)
        return (float) raw;
    const auto lo = delay3Min[(std::size_t) control], hi = delay3Max[(std::size_t) control];
    auto u = (double) (raw - lo) * (1.0 / (double) (hi - lo));
    u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
    return (float) u;
}

namespace
{
constexpr float lfoPeriods[8] { 8.872f, 3.808f, 1.833f, 0.878f, 0.398f, 0.15f, 0.05f, 0.0f };
constexpr float lowTable[5] { 270.0f, 480.0f, 1300.0f, 7000.0f, 22000.0f };
constexpr float highTable[5] { 20.0f, 90.0f, 400.0f, 1300.0f, 4200.0f };

float tableAt (const float* t, float x) noexcept
{
    const auto i = (int) x;
    const auto fr = x - (float) i;
    return (1.0f - fr) * t[i] + fr * t[i + 1];
}

// The engine's float sine, as its runtime library computes it on a CPU with FMA (REA_rodada52 §3.9): the argument in
// double, reduced by multiples of π/2 (two-part constant, fused steps), then the degree-9 sine or degree-10 cosine
// polynomial evaluated with fused multiply-adds, the quadrant's sign applied and the result rounded to float.
float sineF (float x) noexcept
{
    const auto xd = (double) x;
    const auto ax = std::fabs (xd);
    const auto sinPoly = [] (double r) noexcept
    {
        const auto r2 = r * r;
        auto p = std::fma (r2, 2.7557319223985893e-06, -0.0001984126984126984);
        p = std::fma (p, r2, 0.008333333333333333);
        p = std::fma (p, r2, -0.16666666666666666);
        return std::fma (p, r * r2, r);
    };
    if (ax <= 0.78539816339744828) // π/4
    {
        if (ax >= 0.0078125)
            return (float) sinPoly (xd);
        if (ax >= 0.0001220703125)
            return (float) std::fma (-(xd * xd * xd), 0.16666666666666666, xd);
        return x;
    }
    const auto n = (int) std::fma (ax, 0.6366197723675814, 0.5);
    const auto fn = (double) n;
    const auto r = std::fma (-fn, 1.5707963267341256, ax) - fn * 6.077100506506192e-11;
    double y;
    if ((n & 1) != 0)
    {
        const auto r2 = r * r;
        const auto a = std::fma (r2, -0.5, 1.0);
        auto b = std::fma (r2, -2.755731922398589e-07, 2.4801587301587298e-05);
        b = std::fma (b, r2, -0.0013888888888888887);
        b = std::fma (b, r2, 0.041666666666666664);
        y = std::fma (b, r2 * r2, a);
    }
    else
        y = sinPoly (r);
    if ((n & 3) >= 2)
        y = -y;
    if (std::signbit (x))
        y = -y;
    return (float) y;
}
float tanF (float x) noexcept { return (float) std::tan ((double) x); }
float powF (float b, float e) noexcept { return (float) std::pow ((double) b, (double) e); }
float sqrtF (float x) noexcept { return std::sqrt (x); }

// (int) of a non-negative float through 64 bits, its low 32 bits read back unsigned (the delay-index arithmetic)
inline std::uint32_t truncIndex (float x) noexcept { return (std::uint32_t) (std::int64_t) x; }

struct Line
{
    std::vector<float> buf;
    std::uint32_t idx = 0, mask = 0;

    void init (std::uint32_t size) { buf.assign (size, 0.0f); idx = 0; mask = size - 1; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
    // linear interpolation `d` samples behind position `w`
    float read (std::uint32_t w, float d) const noexcept
    {
        const auto i = truncIndex (d);
        const auto fr = d - (float) i;
        const auto a = w - i;
        return (1.0f - fr) * buf[a & mask] + fr * buf[(a - 1) & mask];
    }
    void push (float x) noexcept
    {
        buf[idx] = x;
        idx = (idx + 1) & mask;
    }
};

struct AllPass
{
    float g = 0.0f; // the diffusion amount (scaled by 0.499 when used)
    Line line;
};

struct Svf
{
    float ic1 = 0, ic2 = 0, m0 = 0, m1 = 0, m2 = 1, a1 = 0, a2 = 0, a3 = 0;

    float run (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = ic1 * a1 + a2 * v3;
        const auto v2 = ic1 * a2 + ic2 + a3 * v3;
        ic1 = (v1 + v1) - ic1;
        ic2 = (v2 + v2) - ic2;
        return v1 * m1 + x * m0 + v2 * m2;
    }
};

struct Reducer
{
    float amount = 0, bitsOn = 0, scale = 0, held = 0, ratio = 1, counter = 0;

    float run (float x) noexcept
    {
        if (amount > 0.0f)
        {
            const auto a = amount * 127.0f;
            auto p = a * 0.0002f * a * a - a * 0.0171f * a;
            p = p + a * 0.7435f;
            p = p + 1.0f;
            const auto c = counter + 1.0f;
            counter = c;
            p = p * ratio;
            const auto m = 1.0f > p ? 1.0f : p; // maxss
            if (c < m)
                x = held;
            else if (0.0f > c || 0.0f >= m)
            {
                held = x;
                counter = 0.0f;
            }
            else
            {
                held = x;
                const auto q = c / m;
                counter = (q - (float) (int) q) * c;
            }
        }
        if (bitsOn > 0.0f)
            return (float) (int) (scale * x) / scale;
        return x;
    }
};

struct OnePole
{
    float y1 = 0, x1 = 0, b0 = 1, b1 = 0, a1 = 0, freq = 0, piOverSr = 0;
    int type = 2;
    double rate = 44100.0;

    // FL's set-up at a sample rate (the frequency argument is the previous one, as the plugin computes it)
    void setRate (double sr) noexcept
    {
        rate = sr;
        piOverSr = (float) (1.0 / sr) * 3.14159274f;
        if (type == 0 || type == 1)
        {
            const auto arg = piOverSr * freq;
            const auto lim = (float) sr * 0.49f;
            freq = freq < lim ? freq : lim;
            const auto t = tanF (arg);
            if (type == 0)
            {
                b0 = t / (t + 1.0f);
                b1 = b0;
            }
            else
            {
                b0 = 1.0f / (t + 1.0f);
                b1 = -b0;
            }
            a1 = -(t - 1.0f) / (t + 1.0f);
        }
        else
        {
            type = 2;
            b0 = 1.0f;
            b1 = 0.0f;
            a1 = 0.0f;
        }
    }
    float run (float x) noexcept
    {
        const auto y = b0 * x + b1 * x1 + a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};
} // namespace

struct Delay3Engine::Dsp
{
    double sr = 44100.0, tempo = 120.0;
    // modulation LFO (sine)
    float lfoPhase = 0, lfoRate = 0, lfoOut = 0, lfoInc = 0, lfoNorm = 1;
    bool sync = false, keepPitch = false;
    float distDb = 0, hardness = 1, symmetry = 0;
    int filterMode = 0, coefCounter = 0, satType = 0;
    float halfRate = 22050, cutoffMod = 0, send = 0.5f, dryMul = 1, colour = 0.5f, res = 0, cutoff = 0.5f;
    float modAmt = 0, modDepth = 0, modCoef = 0, feedback = 0.99f, dry = 0.5f;
    float panLL = 0.5f, panLR = 0, panRR = 0, panRL = 0.5f, spreadA = 1, spreadB = 0;
    Line inL, inR, mainL, mainR;
    float srF = 44100, piOverSr = 0;
    Svf svfL, svfR;
    Reducer redL, redR;
    OnePole fbL, fbR, colL, colR;
    float envL = 0, attack = 0, release = 0, envR = 0;
    bool recompute = true;
    int mode = 0;
    float wet = 1;
    AllPass ap[4];
    bool fading = false;
    int fadeFrom = 0, fadeTo = 1, fadeLen = 1000, fadeCount = 0;
    float fadeStep = 0, weight[2] { 1.0f, 0.0f };
    float diffSpread = 0.5f, time = 0, offset = 0.5f;
    int nd[2] { 10, 10 }, dA[2] { 0, 0 }, dB[2] { 0, 0 }, dC[2] { 10, 10 };
    double gnd[2] {}, gA[2] {}, gB[2] {}, gC[2] {};
    bool offLeft = false;
    double smoothK = 1.0, smoothT = 0.1;
    float distPre = 1, distPost = 1;
    float ws[8] {}, wsA = 0, wsB = 0, wsC = 0;

    Dsp()
    {
        for (auto& a : ap)
            a.line.init (0x8000);
        const auto atk = (float) std::pow (0.1, 0.0022675736961451248);
        const auto rel = (float) std::pow (0.1, 0.00022675736961451248);
        attack = 1.0f - atk;
        release = 1.0f - rel;
        fbL.type = fbR.type = 0;
        colL.type = colR.type = 0;
        shaper (-3.0f, 1.0f, 0.0f);
        setDistortion (0.0f);
        prepare (44100.0);
        setColour (0.5f);
        cutoff = 0.5f;
        cutoffMod = 0.0f;
    }

    // --- set-up ------------------------------------------------------------------------------------------------------
    void setLfoRate (float x) noexcept
    {
        lfoRate = x;
        const auto s = x * 5.9994001f;
        const auto i = (int) s;
        const auto fr = s - (float) i;
        const auto period = (1.0f - fr) * lfoPeriods[i] + fr * lfoPeriods[i + 1];
        lfoInc = 1.0f / ((float) sr * period);
        const auto first = lfoPeriods[1] * 0.0f + lfoPeriods[0];
        lfoNorm = (float) std::sqrt ((double) (period / first));
    }

    void shaper (float db, float hard, float sym) noexcept
    {
        wsA = db;
        wsB = hard;
        wsC = sym;
        const auto g = powF (10.0f, db / 20.0f);
        auto hi = (sym + 1.0f) * g;
        auto lo = (1.0f - sym) * g;
        ws[0] = hi * hard;
        hi = hi - ws[0];
        ws[1] = 1.0e-11f > hi ? 1.0e-11f : hi; // maxss
        ws[2] = ws[1] + ws[1];
        ws[3] = 0.25f / ws[1];
        ws[4] = lo * hard;
        lo = lo - ws[4];
        ws[5] = 1.0e-11f > lo ? 1.0e-11f : lo;
        ws[7] = 0.25f / ws[5];
        ws[6] = ws[5] + ws[5];
    }

    void setDistortion (float v) noexcept
    {
        const auto x = v * 0.95f;
        distDb = x * -30.0f;
        shaper (distDb, hardness, symmetry);
        auto p = x * 2.9842999f * x * x * x - x * 7.5177002f * x * x;
        p = p + x * 6.8818998f * x;
        p = p - x * 3.1787f * x;
        p = p + 1.0f;
        distPost = (float) std::pow ((double) (1.0f / p), (double) 0.707f);
        distPre = powF (10.0f, distDb / 20.0f) * distPost;
    }

    void setColour (float v) noexcept
    {
        colour = v;
        if (0.5f > v)
        {
            const auto f = tableAt (lowTable, (v + v) * 3.9999001f);
            const auto t = tanF (f * colL.piOverSr);
            colL.type = 0;
            colL.freq = std::min (f, (float) colL.rate * 0.49f);
            colL.b0 = t / (t + 1.0f);
            colL.b1 = colL.b0;
            colL.a1 = -(t - 1.0f) / (t + 1.0f);
        }
        else if (v == 0.5f)
        {
            colL.type = 2;
            colL.b0 = 1.0f;
            colL.b1 = 0.0f;
            colL.a1 = 0.0f;
        }
        else
        {
            const auto f = tableAt (highTable, ((v - 0.5f) + (v - 0.5f)) * 3.9999001f);
            const auto t = tanF (f * colL.piOverSr);
            colL.type = 1;
            colL.freq = std::min (f, (float) colL.rate * 0.49f);
            colL.b0 = 1.0f / (t + 1.0f);
            colL.b1 = -colL.b0;
            colL.a1 = -(t - 1.0f) / (t + 1.0f);
        }
        colR.b0 = colL.b0;
        colR.b1 = colL.b1;
        colR.a1 = colL.a1;
        colR.freq = colL.freq;
        colR.type = colL.type;
    }

    void prepare (double rate)
    {
        sr = rate;
        halfRate = (float) (rate * 0.5);
        setLfoRate (lfoRate);
        redL.ratio = redR.ratio = (float) (rate / 44100.0);
        // the feedback filters: 10 Hz high-passes computed at 44.1 kHz whatever the rate
        fbL.setRate (44100.0);
        fbR.setRate (44100.0);
        for (auto* f : { &fbL, &fbR })
        {
            f->type = 1;
            f->freq = std::min (10.0f, (float) f->rate * 0.49f);
            const auto t = tanF (f->piOverSr * 10.0f);
            f->b0 = 1.0f / (t + 1.0f);
            f->b1 = -f->b0;
            f->a1 = -(t - 1.0f) / (t + 1.0f);
        }
        colL.setRate (rate);
        colR.setRate (rate);
        srF = (float) rate;
        piOverSr = (float) (3.1415 / rate);
        std::uint32_t size = 0x100000;
        if (rate > 126976.0)
            size = 0x400000;
        else if (rate > 61440.0)
            size = 0x200000;
        inL.init (size);
        inR.init (size);
        mainL.init (size);
        mainR.init (size);
        fadeLen = (int) (rate * 0.1);
        fadeStep = 1.0f / (float) fadeLen;
        clear();
        smoothK = smoothT != 0.0 ? 1.0 - std::pow (0.01, 1.0 / (smoothT * rate)) : 1.0;
        modCoef = (float) (1.0 - std::pow (0.01, 1.0 / (rate * 0.1)));
    }

    void clear() noexcept
    {
        inL.clear();
        inR.clear();
        mainL.clear();
        mainR.clear();
        for (auto& a : ap)
            a.line.clear();
        svfL.ic1 = svfL.ic2 = svfR.ic1 = svfR.ic2 = 0.0f;
        fbL.y1 = fbL.x1 = fbR.y1 = fbR.x1 = 0.0f;
        redL.held = redR.held = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            gnd[k] = nd[k];
            gA[k] = dA[k];
            gB[k] = dB[k];
            gC[k] = dC[k];
        }
        times (0);
        times (1);
    }

    // the delay lengths of set k (REA_rodada52 §3.3)
    void times (int k) noexcept
    {
        double t;
        if (! sync)
        {
            const auto u = (double) time * 0.94 + 0.06;
            t = (u * u * u - 0.000216) * 1.0002160466660799 * 0.999 + 0.001;
        }
        else
        {
            const auto n = (int) (std::int64_t) (767.0 * (double) time + 0.5);
            t = (double) ((float) (n + 1) / 192.0f) * (1.0 / (tempo / 60.0));
        }
        const auto total = (float) (int) (t * sr);
        const auto r = total / ((float) sr * 0.015f);
        const auto m = 1.0f < r ? 1.0f : r; // minss
        const auto s = (float) (sr / 44100.0) * (diffSpread * 590.0f + 65.0f);
        const auto ndv = m * s;
        nd[k] = (int) (1.0f > ndv ? 1.0f : ndv);
        offLeft = 0.5f > offset;
        const auto o = (std::fabs (offset - 0.5f) + std::fabs (offset - 0.5f)) * 0.5f;
        if (mode == 0)
        {
            const auto main = total - (float) nd[k];
            dA[k] = (int) (1.0f > main ? 1.0f : main);
            dB[k] = (int) ((total - (1.0f - o) * total) + (float) dA[k]);
            if (offLeft)
                std::swap (dA[k], dB[k]);
        }
        else if (mode == 1 || mode == 2)
        {
            dA[k] = 1;
            const auto main = total - (float) nd[k];
            const auto pre = o * total * o;
            dC[k] = (int) (1.0f > main ? 1.0f : main);
            dB[k] = (int) (1.0f > pre ? 1.0f : pre);
            if (! offLeft)
            {
                dA[k] = dB[k];
                dB[k] = 1;
            }
        }
    }

    // --- per sample --------------------------------------------------------------------------------------------------
    void lfoTick() noexcept
    {
        const auto p = lfoPhase;
        auto np = p + lfoInc;
        np = np - (float) (int) np;
        lfoOut = (sineF (p * 6.28318548f) + 1.0f) * 0.5f;
        lfoPhase = np;
    }

    float modOffset() noexcept
    {
        auto target = modAmt * halfRate;
        target = target * lfoNorm;
        modDepth = (target - modDepth) * modCoef + modDepth;
        return (lfoOut + 0.5f) * modDepth;
    }

    void coefficients() noexcept
    {
        if (coefCounter == 0 && filterMode <= 2)
        {
            auto x = cutoffMod * lfoOut;
            x = x + cutoff;
            const auto c = 0.0f > x ? 0.0f : (1.0f < x ? 1.0f : x);
            auto r = 0.0f > res ? 0.0f : (1.0f < res ? 1.0f : res);
            r = r * 0.5f;
            r = r * r;
            float k, f;
            if (filterMode == 2)
            {
                k = 1.0f / (r * 4.5999999f + 0.107f);
                f = tableAt (lowTable, c * 3.5999999f);
            }
            else
            {
                k = 1.0f / (r * 4.0f + 0.707f);
                f = tableAt (filterMode == 1 ? highTable : lowTable, c * 3.9990001f);
            }
            const auto lim = srF * 0.49f;
            const auto fc = lim > f ? f : lim;
            const auto g = tanF (fc * piOverSr);
            if (filterMode == 2)
                svfL.m0 = 0.0f, svfL.m1 = k, svfL.m2 = 0.0f;
            else if (filterMode == 1)
                svfL.m0 = 1.0f, svfL.m1 = -k, svfL.m2 = -1.0f;
            else
                svfL.m0 = 0.0f, svfL.m1 = 0.0f, svfL.m2 = 1.0f;
            svfL.a1 = 1.0f / ((g + k) * g + 1.0f);
            svfL.a2 = svfL.a1 * g;
            svfL.a3 = g * svfL.a2;
            svfR.m0 = svfL.m0, svfR.m1 = svfL.m1, svfR.m2 = svfL.m2;
            svfR.a1 = svfL.a1, svfR.a2 = svfL.a2, svfR.a3 = svfL.a3;
        }
        coefCounter = (coefCounter + 1) & 3;
    }

    float filter (Svf& s, float x) const noexcept { return filterMode >= 3 ? x : s.run (x); }

    float saturate (Reducer& red, float& env, float x) noexcept
    {
        x = red.run (x);
        if (satType == 0)
        {
            x = x / distPre;
            const auto a = std::fabs (x);
            env = a < env ? env + (a - env) * release : env + (a - env) * attack;
            if (env > 1.0f)
                x = x / env;
            return distPre * x;
        }
        if (satType != 1)
            return 0.0f;
        // soft knee above the threshold, separately for each polarity (minss: the knee width when it is smaller)
        if (x >= 0.0f)
        {
            if (ws[0] > x)
                return x * distPost;
            const auto over = x - ws[0];
            const auto d = ws[2] < over ? ws[2] : over;
            return ((1.0f - d * ws[3]) * d + ws[0]) * distPost;
        }
        const auto n = -x;
        if (ws[4] > n)
            return x * distPost;
        const auto over = n - ws[4];
        const auto d = ws[6] < over ? ws[6] : over;
        return -((1.0f - d * ws[7]) * d + ws[4]) * distPost;
    }

    float allPass (AllPass& a, double delay, float s, float& out) noexcept
    {
        const auto g = a.g * 0.49900001f;
        const auto d = a.line.read (a.line.idx, (float) delay);
        a.line.push (s - d * g);
        out = (1.0f - g * g) * d + g * s;
        return d;
    }

    float colourRun (OnePole& f, float x) noexcept
    {
        const auto y = x * f.b0 + f.b1 * f.x1 + f.a1 * f.y1;
        f.x1 = x;
        f.y1 = y;
        return y;
    }

    float feedbackRun (OnePole& f, float x) noexcept
    {
        const auto y = x * f.b0 + f.b1 * f.x1 + f.a1 * f.y1;
        f.x1 = x;
        f.y1 = y;
        return y;
    }

    void modeFake (float& L, float& R, float A, float B) noexcept
    {
        const auto in = ((L + R) * 0.5f) * send;
        lfoTick();
        const auto mod = modOffset();
        coefficients();
        const auto w = mainL.idx;
        const auto outL = mainL.read (w, (float) gA[0] + mod) * A + mainL.read (w, (float) gA[1] + mod) * B;
        const auto outR = mainL.read (w, (float) gB[0] + mod) * A + mainL.read (w, (float) gB[1] + mod) * B;
        const auto fb = feedbackRun (fbL, feedback * (offLeft ? outR : outL));
        const auto s = filter (svfL, saturate (redL, envL, fb + in));
        float o1, o2;
        allPass (ap[0], gnd[0], s, o1);
        allPass (ap[1], gnd[1], s, o2);
        mainL.push (o2 * B + o1 * A);
        const auto cL = colourRun (colL, outL);
        const auto cR = colourRun (colR, outR);
        L = (spreadB * cR + spreadA * cL) * wet + (L * dry) * dryMul;
        R = (spreadB * cL + spreadA * cR) * wet + (R * dry) * dryMul;
    }

    void modeStereo (float& L, float& R, float A, float B, bool pingPong) noexcept
    {
        lfoTick();
        const auto mod = (double) modOffset();
        coefficients();
        const auto mL = mainL.read (mainL.idx, (float) (mod + gC[0])) * A + mainL.read (mainL.idx, (float) (mod + gC[1])) * B;
        const auto mR = mainR.read (mainR.idx, (float) (mod + gC[0])) * A + mainR.read (mainR.idx, (float) (mod + gC[1])) * B;
        float xL, xR;
        if (! pingPong)
        {
            inL.push (L);
            xL = inL.read (inL.idx, (float) gA[1]) * B + inL.read (inL.idx, (float) gA[0]) * A;
            inR.push (R);
            xR = inR.read (inR.idx, (float) gB[1]) * B + inR.read (inR.idx, (float) gB[0]) * A;
        }
        else
        {
            xL = R * panRL + L * panLL;
            xR = R * panRR + L * panLR;
        }
        const auto yL = feedbackRun (fbL, mL * feedback);
        auto s = saturate (redL, envL, filter (svfL, xL * send + yL));
        float o1, o2;
        allPass (ap[0], gnd[0], s, o1);
        allPass (ap[1], gnd[1], s, o2);
        (pingPong ? mainR : mainL).push (o1 * A + o2 * B);
        const auto yR = feedbackRun (fbR, mR * feedback);
        s = saturate (redR, envR, filter (svfR, xR * send + yR));
        allPass (ap[2], gnd[0], s, o1);
        allPass (ap[3], gnd[1], s, o2);
        (pingPong ? mainL : mainR).push (o1 * A + o2 * B);
        const auto cL = colourRun (colL, mL);
        const auto cR = colourRun (colR, mR);
        auto oL = spreadA * cL + spreadB * cR;
        auto oR = spreadA * cR + spreadB * cL;
        if (pingPong && satType == 1)
        {
            oL = limit (envL, attack, release, oL);
            oR = limit (envR, attack, release, oR);
        }
        L = (L * dry) * dryMul + oL * wet;
        R = (R * dry) * dryMul + oR * wet;
    }

    static float limit (float& env, float atk, float rel, float x) noexcept
    {
        const auto a = std::fabs (x);
        env = a < env ? (a - env) * rel + env : (a - env) * atk + env;
        return env > 1.0f ? x / env : x;
    }

    void modeOff (float& L, float& R) noexcept
    {
        lfoTick();
        coefficients();
        const auto sL = saturate (redL, envL, filter (svfL, L));
        const auto sR = saturate (redR, envR, filter (svfR, R));
        const auto cL = colourRun (colL, sL);
        const auto cR = colourRun (colR, sR);
        L = (spreadB * cR + spreadA * cL) * wet + dry * L;
        R = (spreadB * cL + spreadA * cR) * wet + dry * R;
    }

    static void glide (double& cur, int target, double k) noexcept
    {
        const auto t = (double) target;
        if (cur == t)
            return;
        const auto diff = t - cur;
        cur = std::fabs (diff) < 0.001 ? t : diff * k + cur;
    }

    void sample (float& L, float& R) noexcept
    {
        if (! keepPitch)
        {
            weight[0] = 1.0f;
            weight[1] = 0.0f;
            fadeFrom = 0;
            fadeTo = 1;
            if (recompute)
            {
                times (0);
                recompute = false;
            }
            glide (gnd[0], nd[0], smoothK);
            glide (gA[0], dA[0], smoothK);
            glide (gB[0], dB[0], smoothK);
            glide (gC[0], dC[0], smoothK);
        }
        else if (! fading && recompute)
        {
            fadeCount = 0;
            recompute = false;
            fading = true;
            times (fadeTo);
            gnd[fadeTo] = nd[fadeTo];
            gA[fadeTo] = dA[fadeTo];
            gB[fadeTo] = dB[fadeTo];
            gC[fadeTo] = dC[fadeTo];
        }
        const auto A = weight[0], B = weight[1];
        if (mode == 0)
            modeFake (L, R, A, B);
        else if (mode == 1)
            modeStereo (L, R, A, B, false);
        else if (mode == 2)
            modeStereo (L, R, A, B, true);
        else if (mode == 3)
            modeOff (L, R);
        if (keepPitch && fading)
        {
            const auto x = (float) fadeCount * fadeStep;
            weight[fadeFrom] = 1.0f - x;
            weight[fadeTo] = x;
            if (++fadeCount == fadeLen)
            {
                std::swap (fadeFrom, fadeTo);
                fading = false;
            }
        }
    }

    // the plugin's parameter law (REA_rodada52 §3.2)
    void set (int i, float f, double v) noexcept
    {
        switch (i)
        {
            case 0: send = f; break;
            case 1:
            {
                const auto m = (int) (std::int64_t) (3.0 * (double) f + 0.5);
                if (m != mode)
                {
                    mode = m;
                    clear();
                    recompute = true;
                }
                break;
            }
            case 2: recompute = true; sync = v >= 0.5; break;
            case 3:
                keepPitch = v >= 0.5;
                if (! keepPitch)
                    recompute = true;
                break;
            case 4: time = f; recompute = true; break;
            case 5:
            {
                auto x = (f + f) - 1.0f;
                if (x >= 0.0f)
                {
                    x = 1.0f - x;
                    panLL = x;
                    panLR = (1.0f - x) * 0.5f;
                    panRR = 1.0f - panLR;
                    panRL = 0.0f;
                }
                else
                {
                    panRR = x + 1.0f;
                    panRL = (1.0f - panRR) * 0.5f;
                    panLL = 1.0f - panRL;
                    panLR = 0.0f;
                }
                offset = f;
                recompute = true;
                break;
            }
            case 6:
                smoothT = (double) f;
                smoothK = smoothT == 0.0 ? 1.0 : 1.0 - std::pow (0.01, 1.0 / (smoothT * sr));
                break;
            case 7:
                spreadA = f * 0.5f + 0.5f;
                spreadB = 1.0f - spreadA;
                break;
            case 8: setLfoRate (f * 0.99000001f); break;
            case 9: modAmt = (float) (v * v) * 0.1f; break;
            case 10: cutoffMod = f * 0.5f; break;
            case 11:
            {
                const auto g = 0.0f > f ? 0.0f : (1.0f < f ? 1.0f : f);
                for (auto& a : ap)
                    a.g = g;
                break;
            }
            case 12: diffSpread = f; recompute = true; break;
            case 13: satType = (int) (std::int64_t) (v + 0.5); break;
            case 14: feedback = f * 1.25f; break;
            case 15: filterMode = (int) (std::int64_t) (3.0 * v + 0.5); break;
            case 16: cutoff = f; break;
            case 17: res = f; break;
            case 18: setDistortion (f); break;
            case 19: hardness = f; shaper (distDb, f, symmetry); break;
            case 20: symmetry = f; shaper (distDb, hardness, f); break;
            case 21:
            {
                const auto x = (float) (1.0 - v);
                const auto r = x * x * x;
                redL.amount = redR.amount = r;
                break;
            }
            case 22:
            {
                auto x = (float) (1.0 - v);
                x = x * x;
                const auto q = sqrtF (sqrtF (x));
                const auto scale = (float) std::pow (2.0, (double) ((1.0f - q) * 14.0f + 2.0f));
                redL.bitsOn = redR.bitsOn = x;
                redL.scale = redR.scale = scale;
                break;
            }
            case 23: wet = f; break;
            case 24: setColour (f); break;
            case 25: dry = f; break;
            default: break;
        }
    }
};

namespace
{
bool smoothed (int i) noexcept
{
    switch (i)
    {
        case 0: case 7: case 10: case 11: case 14: case 16: case 17: case 18: case 19: case 20: case 21: case 22:
        case 23: case 24: case 25:
            return true;
        default:
            return false;
    }
}

// the engine's own defaults (applied when it is created)
constexpr float engineDefaults[delay3Controls] { 1.0f, 0, 0, 0, 0.5f, 0.95f, 0, 1.0f, 0.5f, 0, 0, 0, 0.5f, 0.5f, 0.5f, 0,
                                                 1.0f, 0, 0, 0, 0, 0, 0, 0.5f, 0.5f, 1.0f };
} // namespace

Delay3Engine::Delay3Engine (double sampleRate) : d (std::make_unique<Dsp>())
{
    smooth.reserve (delay3Controls);
    for (int i = 0; i < delay3Controls; ++i)
        applyNow (i, engineDefaults[i]);
    d->clear();
    setSampleRate (sampleRate);
}

Delay3Engine::~Delay3Engine() = default;

void Delay3Engine::applyNow (int index, float value) noexcept
{
    applied[index] = value;
    d->set (index, value, (double) value);
}

void Delay3Engine::setParameter (int index, float value) noexcept
{
    if (index >= 0 && index < delay3Controls)
        hostSet (index, value);
}

void Delay3Engine::setSampleRate (double sampleRate) noexcept
{
    smoothCoef = (float) (1.0 - std::pow (0.01, 1.0 / (sampleRate * 0.005)));
    d->prepare (sampleRate);
}

void Delay3Engine::hostSet (int index, float value) noexcept
{
    const auto v = (double) value;
    const auto f = (float) (v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));
    if (! smoothed (index))
    {
        applyNow (index, f);
        return;
    }
    for (auto& e : smooth)
        if (e.first == index)
        {
            e.second = f;
            return;
        }
    smooth.emplace_back (index, f);
}

void Delay3Engine::settle() noexcept
{
    for (const auto& [i, v] : smooth)
        applyNow (i, v);
    smooth.clear();
    if (d->tempo != hostTempo)
    {
        d->tempo = hostTempo;
        d->recompute = true;
    }
    // reach the time targets and the modulation depth at once
    d->times (0);
    d->times (1);
    d->recompute = false;
    for (int k = 0; k < 2; ++k)
    {
        d->gnd[k] = d->nd[k];
        d->gA[k] = d->dA[k];
        d->gB[k] = d->dB[k];
        d->gC[k] = d->dC[k];
    }
    d->modDepth = (d->modAmt * d->halfRate) * d->lfoNorm;
}

void Delay3Engine::process (float* left, float* right, int n) noexcept
{
    ScopedFlushDenormals ftz;
    if (d->tempo != hostTempo)
    {
        d->tempo = hostTempo;
        d->recompute = true;
    }
    for (int s = 0; s < n; ++s)
    {
        if (++smoothCounter == 8)
        {
            smoothCounter = 0;
            for (std::size_t k = 0; k < smooth.size();)
            {
                const auto [i, target] = smooth[k];
                auto x = applied[i];
                x = x + (target - x) * smoothCoef;
                if (std::fabs (x - target) < 1.0e-4f)
                {
                    applyNow (i, target);
                    smooth.erase (smooth.begin() + (std::ptrdiff_t) k);
                }
                else
                {
                    applyNow (i, x);
                    ++k;
                }
            }
        }
        auto l = left[s], r = right[s];
        if (l > -1.0e-13f && l < 1.0e-13f)
            l = 0.0f;
        if (r > -1.0e-13f && r < 1.0e-13f)
            r = 0.0f;
        d->sample (l, r);
        left[s] = l;
        right[s] = r;
    }
}

// =====================================================================================================================
// The LYNX effects
// =====================================================================================================================

namespace
{
int rawOf (float p, int lo, int hi) noexcept
{
    return std::clamp ((int) std::lround ((double) p * (hi - lo) + lo), lo, hi);
}

class Delay2Effect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        // the engine and its line (room for tempos down to 60 BPM) are made here, off the audio thread
        rate = sampleRate;
        Delay2State s;
        s.v = delay2Default;
        engine = std::make_unique<Delay2Engine> (s, rate, 120.0, block, true);
        engine->reserve (60.0);
        fresh = true;
    }
    void reset() noexcept override { fresh = true; }

    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        Delay2State s;
        for (int i = 0; i < delay2Controls; ++i)
            s.v[(std::size_t) i] = rawOf (params[(std::size_t) i], delay2Min[(std::size_t) i], delay2Max[(std::size_t) i]);
        bpm = bpm > 0.0 ? bpm : 120.0;
        if (engine == nullptr)
            engine = std::make_unique<Delay2Engine> (s, rate, bpm, block, true);
        else if (fresh)
            engine->restart (s, bpm, true);
        else if (s.v != current.v || bpm != tempo)
            engine->set (s, bpm);
        fresh = false;
        current = s;
        tempo = bpm;
        for (int done = 0; done < n;)
        {
            const auto len = std::min (block, n - done);
            engine->process (left + done, right + done, len);
            done += len;
        }
    }

private:
    static constexpr int block = 512;
    double rate = 44100.0, tempo = 0.0;
    Delay2State current;
    std::unique_ptr<Delay2Engine> engine;
    bool fresh = true;
};

class Delay3Effect final : public Effect
{
public:
    // the engine (its lines: 4 × 4 MB at 44.1 kHz, as FL allocates them) is built here, off the audio thread
    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        engine = std::make_unique<Delay3Engine> (rate);
        fresh = true;
    }
    void reset() noexcept override { prepare (rate); }

    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        if (engine == nullptr)
            prepare (rate);
        engine->setTempo (bpm > 0.0 ? bpm : 120.0);
        for (int i = 0; i < delay3Controls; ++i)
        {
            const auto raw = rawOf (params[(std::size_t) i], delay3Min[(std::size_t) i], delay3Max[(std::size_t) i]);
            if (fresh || raw != current[(std::size_t) i])
            {
                current[(std::size_t) i] = raw;
                engine->setParameter (i, delay3EngineValue (i, raw));
            }
        }
        if (fresh)
            engine->settle();
        fresh = false;
        engine->process (left, right, n);
    }

private:
    double rate = 44100.0;
    bool fresh = true;
    std::array<int, delay3Controls> current {};
    std::unique_ptr<Delay3Engine> engine;
};

float normalised (int raw, int lo, int hi) noexcept { return (float) (raw - lo) / (float) (hi - lo); }
} // namespace

std::unique_ptr<Effect> makeDelay2() { return std::make_unique<Delay2Effect>(); }
std::unique_ptr<Effect> makeDelay3() { return std::make_unique<Delay3Effect>(); }

void delay2ToParams (const Delay2State& s, float* params) noexcept
{
    for (int i = 0; i < delay2Controls; ++i)
        params[i] = normalised (s.v[(std::size_t) i], delay2Min[(std::size_t) i], delay2Max[(std::size_t) i]);
}

void delay3ToParams (const Delay3State& s, float* params) noexcept
{
    for (int i = 0; i < delay3Controls; ++i)
        params[i] = normalised (std::clamp (s.v[(std::size_t) i], delay3Min[(std::size_t) i], delay3Max[(std::size_t) i]),
                                delay3Min[(std::size_t) i], delay3Max[(std::size_t) i]);
}

std::string delay2ParamText (int i, float v)
{
    const auto raw = rawOf (v, delay2Min[(std::size_t) std::clamp (i, 0, 7)], delay2Max[(std::size_t) std::clamp (i, 0, 7)]);
    char t[48];
    switch (i)
    {
        case 0: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw / 1.28)); break;
        case 1: case 2: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw / 1.6 * 1.25)); break;
        case 3: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw / 1.27)); break;
        case 4: std::snprintf (t, sizeof (t), "%d:%02d", raw / 48, raw % 48); break;
        case 5: std::snprintf (t, sizeof (t), "%.1f ms", 0.5 * (std::pow (1001.0, std::abs (raw) / 512.0) - 1.0) * (raw < 0 ? -1.0 : 1.0)); break;
        case 6: return raw == 0 ? "Normal" : (raw == 1 ? "Invert" : "Ping pong");
        case 7: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw / 1.28)); break;
        default: return {};
    }
    return t;
}

std::string delay3ParamText (int i, float v, double bpm)
{
    if (i < 0 || i >= delay3Controls)
        return {};
    const auto raw = rawOf (v, delay3Min[(std::size_t) i], delay3Max[(std::size_t) i]);
    const auto u = (double) delay3EngineValue (i, raw);
    char t[48];
    switch (i)
    {
        case 1: return raw == 0 ? "Fake stereo" : (raw == 1 ? "True stereo" : (raw == 2 ? "Ping pong" : "Off"));
        case 2: case 3: return raw != 0 ? "On" : "Off";
        case 4:
        {
            const auto x = u * 0.94 + 0.06;
            const auto ms = ((x * x * x - 0.000216) * 1.0002160466660799 * 0.999 + 0.001) * 1000.0;
            const auto n = (int) (767.0 * u + 0.5) + 1;
            std::snprintf (t, sizeof (t), "%d:%02d (%.0f ms unsynced)", n / 48, n % 48, ms);
            (void) bpm;
            break;
        }
        case 13: return raw != 0 ? "Saturate" : "Limit";
        case 15: return raw == 0 ? "Low pass" : (raw == 1 ? "High pass" : (raw == 2 ? "Band pass" : "Off"));
        case 24: std::snprintf (t, sizeof (t), "%.0f%%", u * 200.0 - 100.0); break;
        default: std::snprintf (t, sizeof (t), "%.0f%%", u * 100.0); break;
    }
    return t;
}
} // namespace lynx::fldelay
