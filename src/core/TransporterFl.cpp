#include "core/TransporterFl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Denormals.h"
#include "core/IldspMath.h"
#include "core/Song.h"

// Clean-room rebuild of FL's Transporter engine (docs/analise/REA_rodada167f-transporter.md). Float32 like the
// library; the order of every sum and product follows the library's build, the comparisons keep its NaN behaviour
// where the SSE instruction decides it (maxss / minss / unordered compares).

#pragma clang fp contract(off)

namespace lynx::transporter
{
namespace
{
using namespace lynx::ildsp;

constexpr float twoSqrt2 = 2.828427f, sqrtHalf = 0.70710677f, eps = 1.1920929e-07f, rnd = 2.3283064e-10f;
constexpr int ringMask = 1023;
constexpr std::size_t loopSize = 3840000; // 20 s at the 192 kHz cap

// the note values of the tempo lengths / triggers (whole, half, … 1/32; dotted; triplets; quintuplets; none)
constexpr float noteValue[22] { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 1.5f, 0.75f, 0.375f, 0.1875f, 0.09375f,
                                0.33333334f, 0.16666667f, 0.083333336f, 0.041666664f, 0.020833332f, 0.2f, 0.1f, 0.05f, 0.025f, 0.0125f, 0.0f };
const char* const noteName[22] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/1 dotted", "1/2 dotted", "1/4 dotted", "1/8 dotted",
                                 "1/16 dotted", "1/1 triplet", "1/2 triplet", "1/4 triplet", "1/8 triplet", "1/16 triplet",
                                 "1/1 quint.", "1/2 quint.", "1/4 quint.", "1/8 quint.", "1/16 quint.", "Off" };

inline float maxss (float a, float b) noexcept { return a > b ? a : b; } // SSE maxss / minss (NaN gives b)
inline float minss (float a, float b) noexcept { return a < b ? a : b; }
inline bool below (float a, float b) noexcept { return ! (a >= b); }     // comiss + setb: true when unordered
inline bool differ (float a, float b) noexcept { return a < b || a > b; } // ucomiss + setne: false when unordered
inline int cvtt (float v) noexcept { return _mm_cvttss_si32 (_mm_set_ss (v)); }
inline float fmodf_ (float x, float y) noexcept { return std::fmod (x, y); }
inline float wrap (float x, float len) noexcept { return fmodf_ (fmodf_ (x, len) + len, len); }
// x − floor(x) the library's way (cvttss2si, one less below zero; large or integral values give 0)
inline float frac (float x) noexcept
{
    const int i = cvtt (x);
    float f = x;
    if (i != INT32_MIN && (float) i != x)
        f = (float) (i - (int) (std::signbit (x) ? 1 : 0));
    return x - f;
}

// the program's sine table: 65536 steps of one turn, through the library's sinf
const float* sineTable()
{
    static const auto table = [] {
        std::vector<float> t (65536);
        for (int k = 0; k < 65536; ++k)
            t[(std::size_t) k] = sinf_ ((float) k * 9.58738019e-05f);
        return t;
    }();
    return table.data();
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

Engine::Coef coef (float c, float hz)
{
    Engine::Coef k;
    k.t = tanf_ (c * hz);
    k.gk = k.t + 1.4142135f;
    const float den = k.gk * k.t + 1.0f;
    k.g = k.t / den;
    k.g2 = k.g + k.g;
    k.t2 = k.t + k.t;
    return k;
}

struct Step
{
    float bp, lp;
};
// one state-variable-filter step: v is the input minus the feedback (computed by the caller in the library's order)
inline Step step (Engine::Svf& s, const Engine::Coef& k, float v) noexcept
{
    const float s1 = s.s1, s2 = s.s2;
    s.s1 = k.g2 * v + s1;
    const float bp = k.g * v + s1;
    s.s2 = k.t2 * bp + s2;
    return { bp, k.t * bp + s2 };
}
} // namespace

// --- controls -------------------------------------------------------------------------------------------------------

float engineValue (int k, int raw) noexcept
{
    const int lo = rawMin[(std::size_t) std::clamp (k, 0, numParams - 1)], hi = rawMax[(std::size_t) std::clamp (k, 0, numParams - 1)];
    const auto norm = (float) std::clamp (((double) raw - lo) * (1.0 / (double) (hi - lo)), 0.0, 1.0);
    switch (k)
    {
        case 0: case 1: case 2: // FL's level law: 0 dB at the top, −∞ at 0
        {
            if (norm == 1.0f)
                return 0.0f;
            const auto c = (float) ((std::exp ((double) (norm * 2.3978953f)) - 1.0) * (double) 0.1f);
            return c <= 0.0f ? -INFINITY : (float) (std::log10 ((double) c) * 20.0);
        }
        case 3: case 4: case 5: case 9: case 21: return norm;
        case 6: case 10: return (float) (((double) norm - 0.5) * 2.0);
        case 7: return (float) ((double) norm * 1.98 + 0.02);
        case 8: return norm * 9.0f + 1.0f;
        default: return (float) raw;
    }
}

float beatOf (double ticks, int ppq) noexcept
{
    const auto beats = (double) ((float) (int) ticks * (1.0f / (float) ppq)) / 4.0;
    return (float) ((beats - std::trunc (beats)) * 4.0);
}

// --- engine ---------------------------------------------------------------------------------------------------------

Engine::Engine()
{
    dryL.assign (ringMask + 1, 0.0f);
    dryR.assign (ringMask + 1, 0.0f);
    for (auto& l : loop)
        l.buffer.assign (loopSize, 0.0f);
    for (auto& b : in)
        b.assign (0x4000, 0.0f);
    for (auto& b : out)
        b.assign (0x4000, 0.0f);
}

void Engine::prepare (float sampleRate)
{
    // the program's constants
    const int sr = (int) sampleRate;
    fs = std::clamp ((float) sr, 1.0f, 192000.0f);
    if (! ((float) sr > 1.0f))
        fs = 1.0f;
    const float c = 1.0f / fs;
    const float w = c * 31.415926f;
    float t = tanf_ (w);
    kBypass = t / (t + 1.0f);
    kBypass2 = kBypass + kBypass;
    look = fs * 0.005f;
    t = tanf_ (c * 62.831852f);
    kSmooth = t / (t + 1.0f);
    kSmooth2 = kSmooth + kSmooth;
    loopLen = fs * 20.0f;
    kTrig = 1.0f / (c * 1.8055984f + 1.0f);
    kTrig1 = 1.0f - kTrig;
    holdLen = fs * 0.02f - 1.0f;
    c149 = coef (c, 149.44017f);
    c355 = coef (c, 355.43063f);
    c2010 = coef (c, 2010.6193f);
    kRelease = 1.0f / (c * 288.89572f + 1.0f);
    kRatio = c * 14.444787f;
    kAttack = 1.0f / (c * 14444.787f + 1.0f);
    c845 = coef (c, 845.36127f);
    c4782 = coef (c, 4782.0854f);
    c11373 = coef (c, 11373.78f);
    c27051 = coef (c, 27051.56f);
    minGap = fs * 0.01f;
    decay = expf_ (-w);
    panStep = c * 20.0f;
    twoFs = fs + fs;
    for (auto& l : loop)
        std::fill (l.buffer.begin(), l.buffer.end(), 0.0f);
    clear();
    // the wrapper
    rate = sampleRate;
    fadeCoef = expf_ ((float) ((double) (1.0f / fmaxf_ (1.0f, sampleRate)) * -94.24777960769379));
    setParam (23, 1.0f);
    setParam (24, 2.0f);
    setParam (25, 1.0f);
}

void Engine::clear() noexcept
{
    one = 0;
    bypassS = 0;
    iota = 0;
    std::fill (dryL.begin(), dryL.end(), 0.0f);
    std::fill (dryR.begin(), dryR.end(), 0.0f);
    gainS = 0;
    index = 0;
    for (auto& s : sv)
        s = {};
    for (auto& b : band)
        b = {};
    thrS = 0;
    hold = peakCount = peakMax = 0;
    meter = 0;
    std::fill (std::begin (trigPrev), std::end (trigPrev), 0.0f);
    std::fill (std::begin (trigS), std::end (trigS), 0.0f);
    beatPrev = 0;
    seed = 0;
    panPhase = panS = 0;
    for (auto& l : loop)
    {
        auto buffer = std::move (l.buffer);
        l = {};
        l.buffer = std::move (buffer);
    }
}

int Engine::latency() const noexcept
{
    if (lookahead == 0.0f)
        return 0;
    return (int) (rate * 0.005f);
}

void Engine::setParam (int k, float v) noexcept
{
    const auto minLen = [this] { minLenA = mode == 3 ? 240.0f / tempo * noteValue[std::clamp (lenIndex, 0LL, 21LL)] : rawMinLen; };
    switch (k)
    {
        case 0: inputDb = v; break;
        case 1: loopADb = v; break;
        case 2: loopBDb = v; break;
        case 3: reverseP = v; break;
        case 4: octaveP = v; break;
        case 5: downUpP = v; break;
        case 6: spread = v; break;
        case 7: rawMinLen = v; if (mode != 3) minLenA = v; break;
        case 8: ratio = v; break;
        case 9: sensitivity = v; break;
        case 10: rawBounce = v; if (mode != 3) bounceRate = v; break;
        case 11: freeze = v; break;
        case 12: if (! flushRequest) flushRequest = v > 0.5f; break;
        case 13:
            mode = (long long) std::round (v);
            loopMode = mode == 3 ? 1.0f : (float) mode;
            bounceRate = mode == 3 ? 0.0f : rawBounce;
            minLen();
            break;
        case 14: bypassFlag = v > 0.5f; bypass = v; break;
        case 15: trigA = v; break;
        case 16: trigB = v; break;
        case 17: trigAB = v; break;
        case 18: deterministic = v; break;
        case 19: lenIndex = (long long) std::round (v); if (mode == 3) minLen(); break;
        case 20:
        {
            trigIndex = (long long) std::round (v);
            const float nv = noteValue[std::clamp (trigIndex, 0LL, 21LL)];
            tempoMul = nv == 0.0f ? 0.0f : 0.25f / nv;
            break;
        }
        case 21: ignore = v; break;
        case 22: tempo = v; if (mode == 3) minLen(); break;
        case 23: lookahead = v; break;
        case 24: source = v; break;
        case 25: stereo = v; break;
        default: break;
    }
}

// a band's transient: its energy through a slow and an adaptive four-stage follower; true when the level (in
// nepers) rose by more than 0.5 since the last sample
bool Engine::onset (Band& b, float x, float threshold) const noexcept
{
    const float e = x * x;
    float y = e;
    for (auto& s : b.slow)
    {
        const float k = y <= s ? kRelease : kAttack;
        y = (1.0f - k) * y + s * k;
        s = y;
    }
    float m = b.mag;
    if (m <= eps)
        m = eps;
    float r = std::sqrt (y) * (1.0f / m);
    if (r <= 0.001f)
        r = 0.001f;
    const float a = 1.0f / ((threshold < logf_ (r) ? 8388608.0f : 20.0f) * kRatio + 1.0f);
    y = e;
    for (auto& s : b.fast)
    {
        const float k = y <= s ? kRelease : a;
        y = (1.0f - k) * y + s * k;
        s = y;
    }
    b.mag = std::sqrt (y);
    float mm = b.mag;
    if (mm <= 0.001f)
        mm = 0.001f;
    const float level = logf_ (mm);
    const bool up = 0.5f < level - b.level;
    b.level = level;
    return up;
}

void Engine::process (const float* inL, const float* inR, const float* detL, const float* detR, const float* beat, float* outL, float* outR,
                      float* sendL, float* sendR, int n) noexcept
{
    ScopedFlushDenormals ftz;
    for (int done = 0; done < n;)
    {
        const int m = std::min (0x4000, n - done);
        std::memcpy (in[0].data(), inL + done, sizeof (float) * (std::size_t) m);
        std::memcpy (in[1].data(), inR + done, sizeof (float) * (std::size_t) m);
        std::memcpy (in[2].data(), detL + done, sizeof (float) * (std::size_t) m);
        std::memcpy (in[3].data(), detR + done, sizeof (float) * (std::size_t) m);
        std::memcpy (in[4].data(), beat + done, sizeof (float) * (std::size_t) m);
        // a flush fades the output out, then clears the loop buffers and restarts the write position
        if (! flushRequest)
            flushZone = 0.0f;
        else if (fade[0] < 1.0e-7f)
        {
            for (auto& l : loop)
                std::fill (l.buffer.begin(), l.buffer.end(), 0.0f);
            flushZone = 1.0f;
            flushRequest = false;
        }
        compute (m);
        for (int c = 0; c < 2; ++c)
        {
            float* o = c == 0 ? outL + done : outR + done;
            for (int i = 0; i < m; ++i)
            {
                const auto target = (float) (! flushRequest || bypassFlag);
                fade[c] = (fade[c] - target) * fadeCoef + target;
                o[i] = fade[c] * fade[c] * out[c][(std::size_t) i];
            }
        }
        if (sendL != nullptr)
            std::memcpy (sendL + done, out[2].data(), sizeof (float) * (std::size_t) m);
        if (sendR != nullptr)
            std::memcpy (sendR + done, out[3].data(), sizeof (float) * (std::size_t) m);
        done += m;
    }
}

void Engine::compute (int n) noexcept
{
    const float delayF = cvtt (lookahead) != 0 ? look : eps;
    const int delay = (int) delayF;
    const float inGain = powf_ (10.0f, inputDb * 0.05f);
    const float kEnv = 1.0f / (14.444787f / delayF + 1.0f), kEnv1 = 1.0f - kEnv;
    const int modulus = cvtt (0.5f >= flushZone ? loopLen : 1.0f);
    const float sens1 = 1.0f - sensitivity;
    const float threshold = sens1 * sens1 * 6.5f;
    const bool adaptive = loopMode == 2.0f, bouncing = loopMode == 1.0f;
    const int det = cvtt (deterministic);
    const float keep2Fs = (1.0f - ignore) * twoFs;
    const float shrink = expf_ (bounceRate * -0.5f);
    const float fsLenA = fs * minLenA, rev2Fs = twoFs * reverseP, oct2Fs = twoFs * octaveP, du2Fs = twoFs * downUpP;
    const float gainA = powf_ (10.0f, loopADb * 0.05f);
    const float depth = below (spread, 0.0f) ? 1.0f : spread;
    const int mono = cvtt (stereo);
    const float panInc = spread * panStep;
    const float invRatio = 1.0f / ratio;
    const float lenB = invRatio * minLenA, fsLenB = fs * lenB;
    const float gainB = powf_ (10.0f, loopBDb * 0.05f);
    const bool autoPan = below (spread, 0.0f);
    const bool srcBoth = ! (source < 2.0f), srcRight = ! srcBoth && ! (source < 1.0f);

    for (int i = 0; i < n; ++i)
    {
        // bypass cross-fade (5 Hz), dry delay (the look-ahead), dry gain (10 Hz)
        const float bv = (1.0f - bypassS) - bypass;
        const float bypassNew = bv * kBypass2 + bypassS;
        float gb = bv * kBypass + bypassS;
        gb = gb * gb;
        const float xL = in[0][(std::size_t) i];
        dryL[(std::size_t) (iota & ringMask)] = xL;
        const float dL = dryL[(std::size_t) ((iota - delay) & ringMask)];
        const int pos = (one + index) % modulus;
        const float gainNew = kSmooth2 * (inGain - gainS) + gainS;
        const float dryG = kSmooth * (inGain - gainS) + gainS;
        gainS = gainNew;
        index = pos;
        const auto posF = (float) pos;

        // the detector's input and its eight bands
        float x = in[3][(std::size_t) i];
        if (srcBoth)
            x = (x + in[2][(std::size_t) i]) * sqrtHalf;
        else if (! srcRight)
            x = in[2][(std::size_t) i];
        auto sA = step (sv[0], c2010, x - (c2010.gk * sv[0].s1 + sv[0].s2));
        const float hiA = x - sA.bp * twoSqrt2;
        auto sB = step (sv[1], c2010, sA.lp - (sv[1].s1 * c2010.gk + sv[1].s2));
        const float lowA = sB.lp;
        auto sC = step (sv[2], c355, lowA - (c355.gk * sv[2].s1 + sv[2].s2));
        const float hiC = lowA - sC.bp * twoSqrt2;
        auto sD = step (sv[3], c355, sC.lp - (sv[3].s1 * c355.gk + sv[3].s2));
        const float lowC = sD.lp;
        auto sE = step (sv[4], c149, lowC - (c149.gk * sv[4].s1 + sv[4].s2));
        auto sF = step (sv[5], c149, sE.lp - (sv[5].s1 * c149.gk + sv[5].s2));
        const float b0 = sF.lp;
        const float b1 = (lowC - sE.bp * twoSqrt2) - b0;

        const float thrV = (1.5f - thrS) + threshold;
        const float thr = kSmooth * thrV + thrS;
        thrS = kSmooth2 * thrV + thrS;
        int count = (int) onset (band[0], b0, thr);
        count += (int) onset (band[1], b1, thr);

        auto sG = step (sv[6], c845, hiC - ((c845.gk * sv[6].s1 + lowC) + sv[6].s2));
        auto sH = step (sv[7], c845, sG.lp - (sv[7].s1 * c845.gk + sv[7].s2));
        const float b2 = sH.lp;
        const float b3 = (hiC - (sG.bp * twoSqrt2 + lowC)) - b2;
        count += (int) onset (band[2], b2, thr);
        count += (int) onset (band[3], b3, thr);

        auto sI = step (sv[8], c11373, hiA - ((c11373.gk * sv[8].s1 + lowA) + sv[8].s2));
        const float hiI = hiA - (sI.bp * twoSqrt2 + lowA);
        auto sJ = step (sv[9], c11373, sI.lp - (sv[9].s1 * c11373.gk + sv[9].s2));
        const float lowI = sJ.lp;
        auto sK = step (sv[10], c4782, lowI - (c4782.gk * sv[10].s1 + sv[10].s2));
        auto sL = step (sv[11], c4782, sK.lp - (sv[11].s1 * c4782.gk + sv[11].s2));
        const float b4 = sL.lp;
        const float b5 = (lowI - sK.bp * twoSqrt2) - b4;
        count += (int) onset (band[4], b4, thr);
        count += (int) onset (band[5], b5, thr);

        auto sM = step (sv[12], c27051, hiI - ((c27051.gk * sv[12].s1 + lowI) + sv[12].s2));
        const float hiM = sM.bp * twoSqrt2;
        auto sN = step (sv[13], c27051, sM.lp - (sv[13].s1 * c27051.gk + sv[13].s2));
        const float b6 = sN.lp;
        const float b7 = (hiI - (hiM + lowI)) - b6;
        count += (int) onset (band[6], b6, thr);
        count += (int) onset (band[7], b7, thr);

        // the trigger: an odd count of rising bands, at least 20 ms after the last one (the program's bitwise and)
        const int trig = count & (int) ((float) hold >= holdLen);
        hold = trig == 0 ? hold + 1 : 0;
        const bool keepPeak = (float) peakCount < minGap && trig < peakMax;
        peakCount = keepPeak ? peakCount + 1 : 0;
        peakMax = keepPeak ? peakMax : trig;
        meter = minss (decay * meter + (float) peakMax, 1.0f);

        // the update buttons (rising edges), and the trigger density (four one-poles) for the Adaptive length
        const float dA = trigA - trigPrev[0], dAB = trigAB - trigPrev[1], dB = trigB - trigPrev[2];
        trigPrev[0] = trigA;
        trigPrev[1] = trigAB;
        trigPrev[2] = trigB;
        const int eAB = cvtt ((float) (int) (dAB > 0.0f) * dAB);
        const int eA = cvtt ((float) (int) (dA > 0.0f) * dA) | eAB;
        const int eB = cvtt ((float) (int) (dB > 0.0f) * dB) | eAB;
        trigS[0] = (float) (eB | eA | trig) * kTrig1 + kTrig * trigS[0];
        trigS[1] = kTrig * trigS[1] + trigS[0] * kTrig1;
        trigS[2] = kTrig1 * trigS[1] + kTrig * trigS[2];
        trigS[3] = kTrig1 * trigS[2] + kTrig * trigS[3];
        const float adaptLenA = minss (std::fabs (minLenA - 1.0f / maxss (fs * trigS[3], eps)), 2.0f);

        // the song's beat grid (Tempo mode)
        const float bt = tempoMul * in[4][(std::size_t) i];
        const float beatPos = frac (bt);
        const bool wrapped = beatPos - beatPrev < 0.0f && bouncing;
        beatPrev = beatPos;

        // eight draws of the generator per sample
        unsigned r[8];
        unsigned s = seed;
        for (auto& v : r)
            v = s = s * 0x41c64e6du + 0xd3dbe645u;
        seed = s;
        const auto draw = [&] (int k) { return (float) (int) r[k] * rnd + 0.5f; };
        const float dpos = fmodf_ (posF, twoFs);

        const float lenSamples[2] { fs * (adaptive ? adaptLenA : minLenA), fs * (adaptive ? adaptLenA * invRatio : lenB) };
        const float fsBase[2] { fsLenA, fsLenB };
        const float gains[2] { gainA, gainB };
        const int edge[2] { eA, eB };
        constexpr int drawIgnore[2] { 1, 0 }, drawRev[2] { 7, 6 }, drawOct[2] { 5, 4 }, drawDu[2] { 3, 2 };
        const float xR = in[1][(std::size_t) i];
        dryR[(std::size_t) (iota & ringMask)] = xR;
        const float mid = (xR + xL) * sqrtHalf;
        float loopOut[2], gLoop[2];
        for (int a = 0; a < 2; ++a)
        {
            auto& L = loop[a];
            const float len1 = lenSamples[a] - 1.0f;
            int reached = len1 <= (float) L.countPrev ? trig : 0;
            L.count = reached == 0 ? L.countPrev + 1 : 0;
            L.freeze = reached == 0 ? L.freezePrev : freeze;
            const int beatTrig = (int) (len1 <= (float) L.wrapCountPrev) & (int) wrapped;
            L.wrapCount = beatTrig == 0 ? L.wrapCountPrev + 1 : 0;
            int pass = (int) (dpos < keep2Fs);
            if (det == 0)
                pass = (int) (ignore < draw (drawIgnore[a]));
            if (cvtt (L.freeze) != 0)
                reached = 0;
            const int retrig = (reached | beatTrig | edge[a]) & pass;
            L.lastStart = retrig == 0 ? L.lastStartPrev : L.startPrev;
            L.start = retrig == 0 ? L.startPrev : posF;
            const int bc = L.bounceFlagPrev == 0 ? L.bounceCountPrev + 1 : 0;
            float bl = fsBase[a];
            if (retrig == 0)
            {
                bl = L.bounceLenPrev;
                if (L.bounceFlagPrev != 0)
                    bl = bl * shrink;
            }
            if (! (bl < loopLen))
                bl = loopLen;
            L.bounceFlag = (int) (bl <= (float) bc);
            L.bounceCount = bc;
            L.bounceLen = bl;
            float length = bl;
            if (! bouncing)
                length = wrap (L.start - L.lastStart, loopLen);
            length = maxss (length, 48.0f);
            L.length = length;
            L.phase = fmodf_ ((1.0f - (float) retrig) * L.phasePrev + 1.0f, length);
            if (retrig == 0)
            {
                L.reverse = L.reversePrev;
                L.rate = L.ratePrev;
            }
            else
            {
                int rv = (int) (dpos < rev2Fs);
                if (det == 0)
                    rv = (int) (draw (drawRev[a]) < reverseP);
                L.reverse = rv;
                bool oct = dpos < oct2Fs, up = dpos < du2Fs;
                if (det == 0)
                    oct = draw (drawOct[a]) < octaveP;
                float rt = 1.0f;
                if (oct)
                {
                    if (det == 0)
                        up = draw (drawDu[a]) < downUpP;
                    rt = up ? 2.0f : 0.5f;
                }
                L.rate = rt;
            }
            const float t = L.rate * L.phase;
            const float read = L.reverse != 0 ? wrap (L.start - t, loopLen) : fmodf_ (t + L.lastStart, loopLen);
            // de-click: an envelope that closes for the look-ahead time after every jump of the read position
            const int jump = (int) below (std::fabs (posF - read), 2.0f) | (int) below (L.phase - L.phasePrev, 0.0f)
                             | (int) differ (length, L.lengthPrev);
            L.sinceJump = (1.0f - (float) jump) * L.sinceJumpPrev + 1.0f;
            const int closed = (int) (L.sinceJump <= delayF);
            L.env[0] = (1.0f - (float) closed) * kEnv1 + kEnv * L.env[0];
            L.env[1] = kEnv * L.env[1] + L.env[0] * kEnv1;
            L.env[2] = kEnv * L.env[2] + L.env[1] * kEnv1;
            L.env[3] = kEnv * L.env[3] + L.env[2] * kEnv1;
            const float gs = L.gain;
            L.gain = (gains[a] - gs) * kSmooth2 + gs;
            gLoop[a] = ((gains[a] - gs) * kSmooth + gs) * L.env[3];
            L.buffer[(std::size_t) pos] = mono == 0 ? mid : (a == 0 ? xL : xR);
            L.ring[iota & ringMask] = L.buffer[(std::size_t) cvtt (read)];
            loopOut[a] = L.ring[(iota - delay) & ringMask];
            // this sample's values become the previous ones
            L.countPrev = L.count;
            L.freezePrev = L.freeze;
            L.wrapCountPrev = L.wrapCount;
            L.lastStartPrev = L.lastStart;
            L.startPrev = L.start;
            L.bounceFlagPrev = L.bounceFlag;
            L.bounceCountPrev = L.bounceCount;
            L.bounceLenPrev = L.bounceLen;
            L.lengthPrev = L.length;
            L.phasePrev = L.phase;
            L.reversePrev = L.reverse;
            L.ratePrev = L.rate;
            L.sinceJumpPrev = L.sinceJump;
        }

        // the pan position: auto-pan (spread < 0) turns a sine at up to 20 Hz, else the spread itself
        panPhase = frac (one == 1 ? panPhase - panInc : 0.0f);
        const float lfo = autoPan ? sineTable()[(std::size_t) cvtt (panPhase * 65536.0f)] : 1.0f;
        const float ps = panS, pv = lfo - ps;
        panS = pv * kSmooth2 + ps;
        const float pan = ((pv * kSmooth + ps) * depth + 1.0f) * 0.5f;

        // the mix: loops panned against each other plus the dry signal, cross-faded with the delayed dry by bypass
        const float dR = dryR[(std::size_t) ((iota - delay) & ringMask)];
        const float pan1 = 1.0f - pan, gb1 = 1.0f - gb;
        const auto o = (std::size_t) i;
        out[0][o] = ((pan * gLoop[0] * loopOut[0] + dryG * dL) + pan1 * gLoop[1] * loopOut[1]) * gb + dL * gb1;
        out[1][o] = ((loopOut[0] * gLoop[0] * pan1 + dR * dryG) + gLoop[1] * pan * loopOut[1]) * gb + dR * gb1;
        out[2][o] = gb * loop[0].env[3] * loopOut[0];
        out[3][o] = gb * loop[1].env[3] * loopOut[1];
        ++iota;
        one = 1;
        bypassS = bypassNew;
    }
}

// --- state, texts, effect -------------------------------------------------------------------------------------------

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;
    const auto v = i32At (d, 0);
    if (v < 1 || v > 8)
        return std::nullopt;
    const std::size_t controls = v >= 8 ? 22 : 20;
    if (d.size() < 4 + 4 * controls)
        return std::nullopt;
    State s;
    for (std::size_t k = 0; k < controls; ++k)
        s.raw[k] = std::clamp (i32At (d, 4 + 4 * k), rawMin[k], rawMax[k]);
    if (controls < 22)
        s.raw[21] = 0; // older states: the ignore probability is reset, the tempo trigger keeps its default
    std::size_t o = 4 + 4 * controls;
    if (o < d.size())
        s.flushWhenDisabled = d[o] != 0;
    o += 2; // the second byte (look-ahead) is read but not used
    const auto field = [&] (int& f) {
        if (o + 4 <= d.size())
            f = i32At (d, o);
        o += 4;
    };
    field (s.source);
    field (s.stereo);
    field (s.sendTrack);
    field (s.inputTrack);
    s.source = std::clamp (s.source, 0, 2);
    s.stereo = std::clamp (s.stereo, 0, 1);
    return s;
}

void toParams (const State& s, float* params) noexcept
{
    const auto put = [params] (int k, int raw) { params[k] = (float) ((double) (raw - rawMin[(std::size_t) k]) / (rawMax[(std::size_t) k] - rawMin[(std::size_t) k])); };
    for (int k = 0; k < numControls; ++k)
        put (k, s.raw[(std::size_t) k]);
    put (sourceParam, s.source);
    put (stereoParam, s.stereo);
    put (sendParam, std::clamp (s.sendTrack, 0, rawMax[sendParam]));
}

int rawOf (int param, float v01) noexcept
{
    const auto k = (std::size_t) std::clamp (param, 0, numParams - 1);
    return rawMin[k] + (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * (float) (rawMax[k] - rawMin[k]));
}

std::string paramText (int param, float v01)
{
    const int raw = rawOf (param, v01);
    char b[48];
    switch (param)
    {
        case 0: case 1: case 2:
        {
            const float db = engineValue (param, raw);
            if (std::isinf (db))
                return "-inf";
            std::snprintf (b, sizeof b, "%.1f dB", (double) db);
            return b;
        }
        case 3: case 4: case 5: case 9: case 21: std::snprintf (b, sizeof b, "%.0f%%", (double) engineValue (param, raw) * 100.0); return b;
        case 6:
        {
            const float v = engineValue (param, raw);
            std::snprintf (b, sizeof b, v < 0.0f ? "Auto-pan %.0f%%" : "%.0f%%", (double) std::fabs (v) * 100.0);
            return b;
        }
        case 7: std::snprintf (b, sizeof b, "%.0f ms", (double) engineValue (param, raw) * 1000.0); return b;
        case 8: std::snprintf (b, sizeof b, "%.2f", (double) engineValue (param, raw)); return b;
        case 10: std::snprintf (b, sizeof b, "%.2f", (double) engineValue (param, raw)); return b;
        case 13:
        {
            static const char* const modes[] { "Auto", "Bouncing", "Adaptive", "Tempo" };
            return modes[std::clamp (raw, 0, 3)];
        }
        case 18: return raw != 0 ? "Deterministic" : "Probabilistic";
        case 19: case 20: return noteName[std::clamp (raw, 0, 21)];
        case sourceParam:
        {
            static const char* const sources[] { "Left", "Right", "Left + right" };
            return sources[std::clamp (raw, 0, 2)];
        }
        case stereoParam: return raw != 0 ? "Stereo" : "Mono";
        case sendParam:
            if (raw <= 0)
                return "None";
            std::snprintf (b, sizeof b, "Insert %d", raw);
            return b;
        default: return raw != 0 ? "On" : "Off";
    }
}

namespace
{
class TransporterEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (float) sampleRate;
        ready = false;
    }
    // FL's flush: the engine's state is cleared and the loop buffers are flushed (after a fade-out)
    void reset() noexcept override
    {
        if (! ready)
            return;
        engine->clear();
        engine->setParam (12, 1.0f);
    }
    // the look-ahead the plug-in reports to FL's PDC: trunc(rate · 0.005)
    int latencySamples() const noexcept override { return (int) (rate * 0.005f); }
    void setSongPosition (double t) noexcept override { ticks = t; }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
        sidechained = true;
    }
    void setSendOutput (float* l, float* r) noexcept override
    {
        sendL = l;
        sendR = r;
    }
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        std::array<int, numParams> raw {};
        for (int k = 0; k < numParams; ++k)
            raw[(std::size_t) k] = rawOf (k, params[(std::size_t) k]);
        if (! ready)
        {
            if (! engine)
                engine = std::make_unique<Engine>();
            engine->prepare (rate);
            engine->setParam (24, (float) raw[sourceParam]);
            engine->setParam (25, (float) raw[stereoParam]);
            for (int k = 0; k < numControls; ++k)
                if (k != 12)
                    engine->setParam (k, engineValue (k, raw[(std::size_t) k]));
            current = raw;
            held = {};
            queued = {};
            tempo = -1.0;
            ready = true;
        }
        else
        {
            // FL's Flush and Update buttons are momentary (pressed = 1, released = 0; the engine reacts to the press).
            // LYNX's push buttons flip their value at each click: any change is a press, given to the engine for one
            // block and released on the next; a click during that block is pressed after the release (round 167n)
            for (int b = 0; b < numButtons; ++b)
            {
                const int k = buttons[(std::size_t) b];
                const bool released = held[(std::size_t) b];
                if (released)
                {
                    engine->setParam (k, 0.0f);
                    held[(std::size_t) b] = false;
                }
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    current[(std::size_t) k] = raw[(std::size_t) k];
                    queued[(std::size_t) b] = std::min (queued[(std::size_t) b] + 1, 2);
                }
                if (queued[(std::size_t) b] > 0 && ! released)
                {
                    engine->setParam (k, 1.0f);
                    held[(std::size_t) b] = true;
                    --queued[(std::size_t) b];
                }
            }
            for (int k = 0; k < numParams; ++k)
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    if (k < numControls)
                        engine->setParam (k, engineValue (k, raw[(std::size_t) k]));
                    else if (k == sourceParam)
                        engine->setParam (24, (float) raw[(std::size_t) k]);
                    else if (k == stereoParam)
                        engine->setParam (25, (float) raw[(std::size_t) k]);
                    current[(std::size_t) k] = raw[(std::size_t) k];
                }
        }
        if (bpm > 0.0 && bpm != tempo) // FL's tempo event
        {
            engine->setParam (22, (float) bpm);
            tempo = bpm;
        }
        for (int done = 0; done < n;)
        {
            const int m = std::min (blockSize, n - done);
            // FL's beat input: the bar position at the start of the block (the host's integer tick), for every sample
            const double t = ticks + (double) done * (bpm > 0.0 ? bpm : 120.0) * (double) lynx::ppq / (60.0 * (double) rate);
            std::fill (beat.begin(), beat.begin() + m, beatOf (t, lynx::ppq));
            const float* dl = left + done;
            const float* dr = right + done;
            if (sidechained)
            {
                if (scL != nullptr && scR != nullptr)
                {
                    std::memcpy (detL.data(), scL + done, sizeof (float) * (std::size_t) m);
                    std::memcpy (detR.data(), scR + done, sizeof (float) * (std::size_t) m);
                }
                else
                {
                    std::fill (detL.begin(), detL.begin() + m, 0.0f);
                    std::fill (detR.begin(), detR.begin() + m, 0.0f);
                }
                dl = detL.data();
                dr = detR.data();
            }
            engine->process (left + done, right + done, dl, dr, beat.data(), left + done, right + done, sendA.data(), sendB.data(), m);
            if (sendL != nullptr && sendR != nullptr) // the loops go to the send's insert (added: left = A, right = B)
                for (int i = 0; i < m; ++i)
                {
                    sendL[done + i] += sendA[(std::size_t) i];
                    sendR[done + i] += sendB[(std::size_t) i];
                }
            done += m;
        }
        sidechained = false;
        scL = scR = nullptr;
        sendL = sendR = nullptr;
    }

private:
    static constexpr int numButtons = 4;
    static constexpr std::array<int, numButtons> buttons { 12, 15, 16, 17 }; // Flush, Update A / B / A+B
    static constexpr int blockSize = 512;
    std::array<bool, numButtons> held {}; // pressed in the previous block (released in this one)
    std::array<int, numButtons> queued {}; // clicks not given to the engine yet
    std::unique_ptr<Engine> engine;
    std::array<int, numParams> current {};
    std::vector<float> beat = std::vector<float> (blockSize), detL = std::vector<float> (blockSize), detR = std::vector<float> (blockSize),
                       sendA = std::vector<float> (blockSize), sendB = std::vector<float> (blockSize);
    const float *scL = nullptr, *scR = nullptr;
    float *sendL = nullptr, *sendR = nullptr;
    double ticks = 0.0, tempo = -1.0;
    float rate = 44100.0f;
    bool ready = false, sidechained = false;
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<TransporterEffect>(); }
} // namespace lynx::transporter
