#include "core/PitchShifterFl.h"

#include "core/Effects.h"
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

// Exact port of FL's Pitch Shifter (docs/analise/REA_rodada61.md). The engines compute in single precision in a fixed
// operation order; every expression below keeps that order and its float/double types, so contraction into FMA stays
// off. Names and structure are our own.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::psfl
{
namespace
{
constexpr int kGrains = 20;
constexpr int kClipSize = 1001;
constexpr int kWinSize = 1024;

std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t b = 0;
    std::memcpy (&b, &f, 4);
    return b;
}
float floatOf (std::uint32_t b) noexcept
{
    float f = 0;
    std::memcpy (&f, &b, 4);
    return f;
}

// the run-time's fminf / fmaxf as the engines call them (NaN never reaches them)
float fmin_ (float a, float b) noexcept { return b < a ? b : a; }
float fmax_ (float a, float b) noexcept { return b > a ? b : a; }

// ---------------------------------------------------------------------------------------------- CRT float functions
// tanhf: |x| < 0.9 and 0.9 ≤ |x| ≤ 1 use two rational forms x + x³·P(x²)/Q(x²); 1 < |x| ≤ 10 goes through e^{2|x|} by a
// 32-entry 2^(j/32) table (head with 9 significant bits + float tail) and a cubic; above 10 the result is ±1.
const float kExpHead[32] = { 1.0f, 1.021484375f, 1.04296875f, 1.06640625f, 1.08984375f, 1.11328125f, 1.138671875f,
    1.162109375f, 1.1875f, 1.21484375f, 1.240234375f, 1.267578125f, 1.294921875f, 1.32421875f, 1.353515625f, 1.3828125f,
    1.4140625f, 1.443359375f, 1.4765625f, 1.5078125f, 1.541015625f, 1.57421875f, 1.609375f, 1.64453125f, 1.681640625f,
    1.716796875f, 1.755859375f, 1.79296875f, 1.833984375f, 1.873046875f, 1.9140625f, 1.95703125f };
const std::uint32_t kExpTail[32] = {
    0x00000000u, 0x39d86988u, 0x3aab0d9fu, 0x3a407404u, 0x3a2e0f1eu, 0x3a90e62du,
    0x38f4dce0u, 0x3ad3bea3u, 0x3adfc146u, 0x39d39b9cu, 0x3ad4c982u, 0x3ac10c0cu,
    0x3afb5aa6u, 0x3a856ad3u, 0x3a41f752u, 0x3a8fd607u, 0x391e6678u, 0x3aeebd1du,
    0x398a39f4u, 0x3ab13329u, 0x3a9ca845u, 0x3ae6f619u, 0x3a923054u, 0x3aa07647u,
    0x391f9958u, 0x3aeede5fu, 0x39cdeec0u, 0x3ae41b9du, 0x37c6e7c0u, 0x3a92e66fu,
    0x3a95f454u, 0x38ecb6d0u };
} // namespace

float tanhfCrt (float x) noexcept
{
    const auto bx = bitsOf (x);
    const auto ax = bx & 0x7fffffffu;
    if (ax < 0x39000000u)
        return x;
    if (ax > 0x7f800000u)
        return x + x;
    const float sign = 1.0f - (float) (ax != bx ? 1 : 0) * 2.0f;
    const float a = sign * x;
    if (a > 10.0f)
        return 1.0f * sign;
    if (! (1.0f < a))
    {
        const float g = a * a;
        const float ga = g * a;
        float p, q;
        if (0.9f > a)
        {
            p = (g * floatOf (0x384d2b7du) - floatOf (0x3abfbc9eu)) * g - floatOf (0x3e9058e0u);
            q = g * floatOf (0x3eaf769cu) + floatOf (0x3f588550u);
        }
        else
        {
            p = (g * floatOf (0x382089d8u) - floatOf (0x3aa18e00u)) * g - floatOf (0x3e7679b1u);
            q = g * floatOf (0x3e95c65fu) + floatOf (0x3f38db60u);
        }
        return ((p * ga) / q + a) * sign;
    }
    float x2 = a + a;
    float t = x2 * floatOf (0x4238aa3bu); // 32/ln2
    t = t > 0.0f ? t + 0.5f : t - 0.5f;
    const int k = (int) t;
    const int j = k & 31;
    const float head = kExpHead[j], tail = floatOf (kExpTail[j]);
    x2 = x2 - (float) k * floatOf (0x3cb17000u); // ln2/32, high part
    const int m = (k & ~31) / 32;
    const float lo = (float) (-k) * floatOf (0x3585fdf4u); // ln2/32, low part
    const float r = lo + x2;
    const float r2 = r * r;
    float e = (r * floatOf (0x3e2aaaabu) + 0.5f) * r2;
    e = (e + lo) + x2;
    e = e * (tail + head);
    e = (e + tail) + head;
    const int m1 = m / 2, m2 = m - m1;
    e = e * floatOf ((std::uint32_t) (m1 + 127) << 23);
    e = e * floatOf ((std::uint32_t) (m2 + 127) << 23);
    return (1.0f - 2.0f / (e + 1.0f)) * sign;
}

namespace
{
// expf: exp(t) as e^{k·ln2/64}·(1 + r + r²/2 + r³/6) in double, rounded to float (the run-time's SSE2 path)
float expfCrt (float x) noexcept
{
    static const auto table = [] {
        std::array<double, 64> t {};
        for (int i = 0; i < 64; ++i)
            t[(std::size_t) i] = std::exp2 (i / 64.0);
        return t;
    }();
    const auto xd = (double) x;
    const auto t = xd * 92.33248261689366;
    if (! std::isfinite (xd) || t >= 8192.0 || t < -9600.0)
        return (float) std::exp (xd);
    const auto k = (long long) std::nearbyint (t);
    const auto r = xd - (double) k * 0.010830424696249145;
    const auto q = (r * r) * (r * 0.16666666666666666 + 0.5) + r;
    const auto e = table[(std::size_t) (k & 63)];
    return (float) ((q * e + e) * std::ldexp (1.0, (int) ((k - (k & 63)) / 64)));
}


// soft-clip table shared by both engines: tanh on 1001 points of [−5, 5]
std::vector<float> clipTable()
{
    std::vector<float> t ((std::size_t) kClipSize);
    const float step = 10.0f / (float) (kClipSize - 1);
    for (int i = 0; i < kClipSize; ++i)
        t[(std::size_t) i] = tanhfCrt ((float) i * step - 5.0f);
    return t;
}

// squared-sine window of n points, computed in double and rounded
std::vector<float> sineSquared (int n)
{
    std::vector<float> w ((std::size_t) std::max (n, 0));
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin ((3.141592653589793 / ((double) n - 1.0)) * (double) i);
        w[(std::size_t) i] = (float) (s * s);
    }
    return w;
}

// 3-stage high-pass of the delay feedback path (transposed direct form II), coefficients per sample rate:
// {b0 b1 b2 a1 a2} × 3 stages; 22.05, 48, 88.2, 96 and 192 kHz have their own sets, every other rate uses the last one
struct FeedbackCoefs
{
    float c[15];
};
FeedbackCoefs feedbackCoefs (float rate)
{
    struct Row
    {
        int rate;
        std::uint32_t s1[5], s2b1, s2b2, s2a1, s2a2, s3b1, s3a1;
    };
    static const Row rows[] = {
        { 22050, { 0x3f7476deu, 0xbff44ac0u, 0x3f741eaeu, 0xbffa216du, 0x3f7476deu }, 0xc00008d5u, 0x3f8011b1u, 0xbffda9aau, 0x3f7b8813u, 0xbf801c88u, 0xbf78cea6u },
        { 48000, { 0x3f7aa288u, 0xbffa8facu, 0x3f7a7cdfu, 0xbffd4bb6u, 0x3f7aa289u }, 0xc000193eu, 0x3f803284u, 0xbffef264u, 0x3f7deff6u, 0xbf7fc18eu, 0xbf7cabb2u },
        { 88200, { 0x3f7d10efu, 0xbffcfde4u, 0x3f7ceae8u, 0xbffe86d5u, 0x3f7d10f8u }, 0xc000193eu, 0x3f803284u, 0xbfff6e65u, 0x3f7ee01bu, 0xbf7fc18eu, 0xbf7e2ebfu },
        { 96000, { 0x3f7d4da2u, 0xbffd3a93u, 0x3f7d2793u, 0xbffea56bu, 0x3f7d4da2u }, 0xc000193eu, 0x3f803284u, 0xbfff7a52u, 0x3f7ef771u, 0xbf7fc18eu, 0xbf7e5478u },
        { 192000, { 0x3f7ea5e7u, 0xbffe92beu, 0x3f7e7fa4u, 0xbfff504au, 0x3f7ea148u }, 0xc000193eu, 0x3f803284u, 0xbfffbd17u, 0x3f7f7ae3u, 0xbf7fc18eu, 0xbf7f2f37u },
        { 0, { 0x3f7a2a76u, 0xbffa17a3u, 0x3f7a04dfu, 0xbffd0ea7u, 0x3f7a2a75u }, 0xc000193eu, 0x3f803284u, 0xbffeda1du, 0x3f7dc179u, 0xbf7fc18eu, 0xbf7c60dcu },
    };
    const int r = (int) std::round ((double) rate);
    const Row* row = &rows[5];
    for (int k = 0; k < 5; ++k)
        if (rows[k].rate == r)
            row = &rows[k];
    FeedbackCoefs f {};
    for (int i = 0; i < 5; ++i)
        f.c[i] = floatOf (row->s1[i]);
    f.c[5] = 1.0f;
    f.c[6] = floatOf (row->s2b1);
    f.c[7] = floatOf (row->s2b2);
    f.c[8] = floatOf (row->s2a1);
    f.c[9] = floatOf (row->s2a2);
    f.c[10] = 1.0f;
    f.c[11] = floatOf (row->s3b1);
    f.c[12] = 0.0f;
    f.c[13] = floatOf (row->s3a1);
    f.c[14] = 0.0f;
    return f;
}

// one channel of the feedback high-pass: three transposed-DF2 biquads
struct FeedbackFilter
{
    float s1[3] {}, s2[3] {};
    float run (const FeedbackCoefs& k, float x) noexcept
    {
        float in = x;
        for (int s = 0; s < 3; ++s)
        {
            const float* c = k.c + 5 * s;
            const float y = in * c[0] + s1[s];
            s1[s] = (in * c[1] + s2[s]) - y * c[3];
            s2[s] = in * c[2] - y * c[4];
            in = y;
        }
        return in;
    }
};

float clipLookup (const std::vector<float>& tab, float index, float gain) noexcept
{
    const float v = fmax_ (0.0f, fmin_ (999.0f, index));
    const int i = (int) v;
    const float fr = v - (float) i;
    return ((1.0f - fr) * tab[(std::size_t) i] + fr * tab[(std::size_t) i + 1]) * gain;
}

// The granular engine's random source (MSVC rand() recurrence). In FL one state is shared by every instance in the
// process; LYNX gives each instance its own, starting where a freshly loaded engine starts.
struct Lcg
{
    std::uint32_t s = 233994u;
    int next() noexcept
    {
        s = s * 0x343fdu + 0x269ec3u;
        return (int) ((s >> 16) & 0x7fffu);
    }
};

// radix-2 complex FFT in double (forward, scaled by 1/n)
void fftForwardScaled (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto half = len / 2;
        for (std::size_t k = 0; k < half; ++k)
        {
            const double ang = -2.0 * 3.141592653589793238462643383 * (double) k / (double) len;
            const std::complex<double> w (std::cos (ang), std::sin (ang));
            for (std::size_t i = 0; i < n; i += len)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + half] * w;
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
        }
    }
    const double inv = 1.0 / (double) n;
    for (auto& c : a)
        c *= inv;
}
} // namespace

// ================================================================================================== granular engine
struct Engine::Granular
{
    float fbL = 0, fbR = 0;
    float rate = 44100.0f;
    std::vector<float> bufL, bufR;
    int bufLen = 0, wpos = 0;
    float semis = 0, density = 2.5f, jitter = 0, randAmt = 0, mix = 0.5f;
    int grainLen = 0, spacing = 0, countdown = 0;
    float ratio = 1.0f, jitterScale = 0;
    int active[kGrains] {}, length[kGrains] {}, remain[kGrains] {};
    float pos[kGrains] {}, step[kGrains] {};
    std::vector<float> window;
    float prevL = 0, prevR = 0;
    float dCur = 0, dCoef = 0, dTarget = 0, dMax = 0, dMin = 0, feedback = 0.5f, tempo = 120.0f;
    int dLen = 0, dWrite = 0, mode = 1;
    std::vector<float> dL, dR;
    std::vector<float> clip;
    float clipGain = 1.0f, clipDrive = 0.2f;
    bool clipOutput = true;
    FeedbackCoefs fc {};
    FeedbackFilter ffL, ffR;
    Lcg rng;

    void setRate (float r)
    {
        rate = r;
        bufLen = (int) (rate * 0.2f);
        bufL.assign ((std::size_t) std::max (bufLen, 1), 0.0f);
        bufR.assign ((std::size_t) std::max (bufLen, 1), 0.0f);
        wpos = 0;
        semis = 0;
        jitterScale = rate * floatOf (0x3d39c277u); // rate·2000/44100
        prevL = prevR = 0;
        density = 2.5f;
        jitter = 0;
        randAmt = 0;
        mix = 0.5f;
        ratio = 1.0f;
        grainLen = (int) (rate * floatOf (0xc3d2dcfeu)); // a negative start value: the first grain starts at once
        spacing = (int) std::round ((double) ((float) grainLen * 0.4f));
        countdown = spacing;
        dLen = (int) (rate * 4.01f);
        dMax = (float) (dLen - 1);
        dL.assign ((std::size_t) dLen, 0.0f);
        dR.assign ((std::size_t) dLen, 0.0f);
        dMin = rate * 0.001f;
        dWrite = 0;
        dTarget = dCur = rate * 0.1f;
        dCoef = 4.41f / rate;
        feedback = 0.5f;
        mode = 1;
        tempo = 120.0f;
        clip = clipTable();
        clipGain = 1.0f;
        clipDrive = 0.2f;
        clipOutput = true;
        ffL = {};
        ffR = {};
        fc = feedbackCoefs (rate);
        fbL = fbR = 0;
        std::fill (std::begin (active), std::end (active), 0);
        window = sineSquared (kWinSize);
    }

    void clear()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        std::fill (dL.begin(), dL.end(), 0.0f);
        std::fill (dR.begin(), dR.end(), 0.0f);
        ffL = {};
        ffR = {};
        fbL = fbR = 0;
        std::fill (std::begin (active), std::end (active), 0);
    }

    void setParam (int id, float v)
    {
        switch (id)
        {
            case 0:
                semis = v;
                ratio = (float) std::pow (2.0, (double) (v * 0.083333336f));
                break;
            case 1: grainLen = (int) ((v * 0.001f) * rate); break;
            case 2: density = v; break;
            case 3: jitter = v; break;
            case 4: randAmt = v; break;
            case 5: mix = v; break;
            case 6:
                dTarget = (v * rate) * 0.001f;
                dTarget = fmax_ (dMin, fmin_ (dTarget, dMax));
                break;
            case 7: feedback = v; break;
            case 8: mode = (int) std::round ((double) v); break;
            case 9: tempo = v; break;
            case 10:
                dTarget = ((v * rate) * 15.0f) / tempo;
                dTarget = fmin_ (dTarget, dMax);
                break;
            default: break; // 11…14: not used by the plug-in
        }
    }

    float interp (const std::vector<float>& b, int ip, float fr, float fr2) const noexcept
    {
        const int im1 = ip - 1 >= 0 ? ip - 1 : bufLen + ip - 1;
        int ip1 = ip + 1;
        if (ip1 >= bufLen)
            ip1 -= bufLen;
        const int ip2 = ip + 2 < bufLen ? ip + 2 : ip + 2 - bufLen;
        const float c3 = floatOf (0x3eaaaa9fu), c6 = floatOf (0x3e2aaac1u);
        const float y0 = b[(std::size_t) ip], ym = b[(std::size_t) im1], y1 = b[(std::size_t) ip1], y2 = b[(std::size_t) ip2];
        const float a = (y1 - (ym * c3 + y0 * 0.5f)) - y2 * c6;
        const float bq = (y2 * c6 - ym * c6) + (y0 - y1) * 0.5f;
        const float cq = (y1 * 0.5f + ym * 0.5f) - y0;
        return ((a + bq * fr2) * fr + y0) + cq * fr2;
    }

    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x0 = in[2 * i], x1 = in[2 * i + 1];
            float dl = 0, dr = 0, a = x0, b = x1;
            if (mode != 1)
            {
                dCur = (dTarget - dCur) * dCoef + dCur;
                const int id = (int) dCur;
                int r1 = dWrite - id;
                const float fr = dCur - (float) id;
                while (r1 < 0)
                    r1 += dLen;
                int r2 = r1 - 1;
                while (r2 < 0)
                    r2 += dLen;
                const float g = 1.0f - fr;
                dl = g * dL[(std::size_t) r1] + fr * dL[(std::size_t) r2];
                dr = g * dR[(std::size_t) r1] + fr * dR[(std::size_t) r2];
                if (mode == 2)
                {
                    a = x0 + fbL;
                    b = x1 + fbR;
                }
                else if (mode == 3)
                {
                    a = dl;
                    b = dr;
                }
            }
            // anti-alias one-pole for upward shifts
            const float c = (float) std::fmin ((double) (2.0f - ratio), 1.0);
            const float pl = ((c + 1.0f) * a + (1.0f - c) * prevL) * 0.5f;
            prevL = a;
            const float pr = ((c + 1.0f) * b + (1.0f - c) * prevR) * 0.5f;
            prevR = b;
            bufL[(std::size_t) wpos] = pl;
            bufR[(std::size_t) wpos] = pr;
            if (++wpos >= bufLen)
                wpos = 0;
            if (--countdown < 1)
            {
                const int r = rng.next();
                spacing = (int) ((float) grainLen / density + 0.5f);
                countdown = spacing - (int) (((float) r * floatOf (0xb8000100u)) * (jitter * jitterScale));
                int k = 0;
                while (k < kGrains && active[k] != 0)
                    ++k;
                if (k < kGrains)
                {
                    active[k] = 1;
                    int off = (int) ((double) (ratio - 1.0f) * (double) grainLen);
                    if (! (ratio > 1.0f))
                        off = 0;
                    const int r2 = rng.next();
                    float p = (float) (wpos - off - 5) - ((float) r2 * floatOf (0x38000100u)) * (randAmt * jitterScale);
                    while (0.0f > p)
                        p += (float) bufLen;
                    while (! ((float) bufLen > p))
                        p -= (float) bufLen;
                    pos[k] = p;
                    step[k] = ratio;
                    length[k] = grainLen;
                    remain[k] = grainLen;
                }
            }
            float sl = 0, sr = 0;
            for (int k = 0; k < kGrains; ++k)
            {
                if (active[k] == 0)
                    continue;
                float wi = (1.0f - (float) remain[k] / (float) length[k]) * ((float) kWinSize - 1.0f);
                const int wix = (int) wi;
                wi = wi - (float) wix;
                float w = (1.0f - wi) * window[(std::size_t) wix];
                if (wix != kWinSize - 1)
                    w = wi * window[(std::size_t) wix + 1] + w;
                const float p = pos[k];
                const int ip = (int) p;
                const float fr = p - (float) ip;
                const float fr2 = fr * fr;
                sl = sl + interp (bufL, ip, fr, fr2) * w;
                sr = sr + interp (bufR, ip, fr, fr2) * w;
                float np = p + step[k];
                if (! (np < (float) bufLen))
                    np -= (float) bufLen;
                pos[k] = np;
                if (--remain[k] < 1)
                    active[k] = 0;
            }
            if (clipOutput)
            {
                sl = clipLookup (clip, (sl * clipDrive) * 501.0f + 500.0f, clipGain);
                sr = clipLookup (clip, (sr * clipDrive) * 501.0f + 500.0f, clipGain);
            }
            if (mode != 1)
            {
                float fl = 0, fr = 0;
                if (mode == 2)
                    fl = dl, fr = dr;
                else if (mode == 3)
                    fl = sl, fr = sr;
                else if (mode == 4)
                    fl = dl, fr = dr;
                fbL = ffL.run (fc, fl) * feedback;
                fbR = ffR.run (fc, fr) * feedback;
                if (! clipOutput)
                {
                    fbL = clipLookup (clip, (fbL * clipDrive) * 501.0f + 500.0f, clipGain);
                    fbR = clipLookup (clip, (clipDrive * fbR) * 501.0f + 500.0f, clipGain);
                }
                float wl = 0, wr = 0;
                if (mode == 2)
                    wl = sl, wr = sr;
                else if (mode == 3)
                    wl = x0 + fbL, wr = x1 + fbR;
                else if (mode == 4)
                    wl = sl + fbL, wr = sr + fbR;
                dL[(std::size_t) dWrite] = wl;
                dR[(std::size_t) dWrite] = wr;
                if (++dWrite > dLen - 1)
                    dWrite = 0;
            }
            const float wetL = mode == 4 ? sl + fbL : sl;
            const float wetR = mode == 4 ? sr + fbR : sr;
            out[2 * i] = mix * wetL + (1.0f - mix) * x0;
            out[2 * i + 1] = mix * wetR + (1.0f - mix) * x1;
        }
    }
};

// ================================================================================== PSOLA engine with pitch detector
struct Engine::Psola
{
    float rate = 44100.0f;
    float hp[5] {}, hs1 = 0, hs2 = 0;
    int channel = 1;
    float zcCoef = 0, zcThreshold = 0.6f, fadeStep = 0, fade = 1.0f, zc = 0, sgn = 0;
    std::vector<float> ringL, ringR;
    int ringLen = 0, ringPos = 0;
    // delay lines aligning the dry and unvoiced signals with the analysis
    std::vector<float> vL, uL, vR, uR, dryL, dryR;
    int lag = 0, lagAlloc = 0, lagPos = 0;
    int anaCount = 500, mark = 0, period = 500, synCount = 600;
    int pitchMode = 1;
    float ratio = 1.0f, semis = 0, formRatio = 1.0f, formSemis = 0;
    int active[kGrains] {}, length[kGrains] {}, remain[kGrains] {};
    float pos[kGrains] {}, step[kGrains] {};
    std::vector<float> window;
    // detector
    int fftN = 4096, frameLen = 2048, frameAlloc = 8916, framePos = 0, binLo = 0, binHi = 0;
    std::vector<float> frame, frameWin, mag;
    std::vector<std::complex<double>> spec;
    float magExp = 0.2f, harmWidth = 2.0f, detected = 60.0f, f0 = 60.0f, smoothA = 0, smoothB = 0;
    float peakPos[100] {}, peakAmp[100] {};
    // delay / feedback
    float mix = 1.0f, dCur = 0, dCoef = 0, dTarget = 0, dMax = 0, dMin = 0, feedback = 0.5f, tempo = 120.0f;
    int dLen = 0, dWrite = 0, mode = 1;
    std::vector<float> dL, dR;
    std::vector<float> clip;
    float clipGain = 1.0f;
    bool clipOutput = true;
    FeedbackCoefs fc {};
    FeedbackFilter ffL, ffR;
    float fbL = 0, fbR = 0;

    void setRate (float r)
    {
        rate = r;
        semis = 0;
        formSemis = 0;
        magExp = 0.2f;
        harmWidth = 2.0f;
        channel = 1;
        mix = 1.0f;
        const int ri = (int) std::round ((double) rate);
        if (ri == 22050)
        {
            const float v[] = { 0.989976f, -1.979952f, 0.989976f, -1.9798515f, 0.98005253f };
            std::copy (v, v + 5, hp);
        }
        else if (ri == 48000)
        {
            const float v[] = { 0.99538267f, -1.9907653f, 0.99538267f, -1.9907441f, 0.9907867f };
            std::copy (v, v + 5, hp);
        }
        else if (ri == 88200)
        {
            const float v[] = { 0.9974845f, -1.994969f, 0.9974845f, -1.9949627f, 0.9949754f };
            std::copy (v, v + 5, hp);
        }
        else if (ri == 96000)
        {
            const float v[] = { 0.99768865f, -1.9953773f, 0.99768865f, -1.995372f, 0.99538267f };
            std::copy (v, v + 5, hp);
        }
        else if (ri == 192000)
        {
            const float v[] = { 0.99884367f, -1.9976873f, 0.99884367f, -1.997686f, 0.99768865f };
            std::copy (v, v + 5, hp);
        }
        else
        {
            const float v[] = { 0.9949754f, -1.9899508f, 0.9949754f, -1.9899255f, 0.989976f };
            std::copy (v, v + 5, hp);
        }
        hs1 = hs2 = 0;
        sgn = 0;
        zc = 0;
        zcThreshold = 0.6f;
        fade = 1.0f;
        zcCoef = 42115.5f / rate;
        fadeStep = floatOf (0x42855556u) / rate; // 66.66667
        ringLen = (int) (rate * floatOf (0x3db9c277u));
        ringL.assign ((std::size_t) std::max (ringLen, 1), 0.0f);
        ringR.assign ((std::size_t) std::max (ringLen, 1), 0.0f);
        ringPos = 0;
        frameLen = (int) (rate * floatOf (0x3d3e37c6u));
        frameAlloc = 8916;
        frame.assign ((std::size_t) frameAlloc, 0.0f);
        framePos = 0;
        lag = (int) ((float) frameLen * 1.2f);
        lagAlloc = (int) ((float) frameAlloc * 1.2f);
        for (auto* v : { &vL, &uL, &vR, &uR, &dryL, &dryR })
            v->assign ((std::size_t) lagAlloc, 0.0f);
        lagPos = 0;
        fftN = (int) rate > 89999 ? 8192 : 4096;
        mag.assign ((std::size_t) fftN, 0.0f);
        spec.assign ((std::size_t) fftN, {});
        f0 = detected = 60.0f;
        pitchMode = 1;
        smoothA = rate * floatOf (0x33f37a97u);
        smoothB = 1.0f - smoothA;
        const float invRate = 1.0f / rate;
        binLo = (int) (((float) fftN * 60.0f) * invRate);
        binHi = (int) (((float) fftN * 1500.0f) * invRate);
        dLen = (int) ((double) rate * 4.01);
        dMin = rate * 0.001f;
        dMax = (float) (dLen - 1);
        dL.assign ((std::size_t) dLen, 0.0f);
        dR.assign ((std::size_t) dLen, 0.0f);
        dWrite = 0;
        feedback = 0.5f;
        mode = 1;
        dTarget = dCur = rate * 0.1f;
        dCoef = 4.41f / rate;
        tempo = 120.0f;
        clip = clipTable();
        clipGain = 1.0f;
        clipOutput = true;
        ffL = {};
        ffR = {};
        fbL = fbR = 0;
        fc = feedbackCoefs (rate);
        frameWin = sineSquared (frameLen);
        ratio = (float) std::pow (2.0, (double) (semis * 0.083333336f));
        formRatio = (float) std::pow (2.0, (double) (formSemis * 0.083333336f));
        synCount = 600;
        anaCount = 500;
        mark = 0;
        period = 500;
        std::fill (std::begin (active), std::end (active), 0);
        window = sineSquared (kWinSize);
    }

    void clear()
    {
        for (auto* v : { &ringL, &ringR, &vL, &uL, &vR, &uR, &dryL, &dryR, &dL, &dR, &frame })
            std::fill (v->begin(), v->end(), 0.0f);
        ffL = {};
        ffR = {};
        fbL = fbR = 0;
        hs1 = hs2 = 0;
        std::fill (std::begin (active), std::end (active), 0);
    }

    void setParam (int id, float v)
    {
        switch (id)
        {
            case 0:
                semis = v;
                ratio = (float) std::pow (2.0, (double) (v * 0.083333336f));
                break;
            case 1:
                formSemis = v;
                formRatio = (float) std::pow (2.0, (double) (v * 0.083333336f));
                break;
            case 2: channel = (int) std::round ((double) v); break;
            case 3: mix = v; break;
            case 4:
                dTarget = (v * rate) * 0.001f;
                dTarget = fmax_ (dMin, fmin_ (dTarget, dMax));
                break;
            case 5: feedback = v; break;
            case 6: mode = (int) std::round ((double) v); break;
            case 7: tempo = v; break;
            case 8:
                dTarget = (15.0f / tempo) * (v * rate);
                dTarget = fmax_ (fmin_ (dTarget, dMax), 1.0f);
                break;
            case 9:
            {
                frameLen = (int) ((v * rate) * floatOf (0x37be37c6u)); // 1/44100
                if (frameLen > frameAlloc - 1)
                    frameLen = frameAlloc - 1;
                std::fill (frame.begin(), frame.end(), 0.0f);
                framePos = 0;
                lag = (int) ((float) frameLen * 1.2f);
                for (auto* d : { &vL, &uL, &vR, &uR, &dryL, &dryR })
                    std::fill (d->begin(), d->end(), 0.0f);
                lagPos = 0;
                frameWin = sineSquared (frameLen);
                break;
            }
            case 10: pitchMode = (int) std::round ((double) v); break;
            default: break; // 11, 12: soft-clip options the plug-in never sends
        }
    }

    void detect()
    {
        // a frame longer than the FFT (192 kHz with the accurate detector) overruns the plug-in's buffer: only the
        // first fftN samples are transformed here (the plug-in's result in that case is not confirmed)
        const int used = std::min (frameLen, fftN);
        for (int i = 0; i < used; ++i)
            spec[(std::size_t) i] = { (double) frame[(std::size_t) i], 0.0 };
        for (int i = used; i < fftN; ++i)
            spec[(std::size_t) i] = {};
        fftForwardScaled (spec);
        const double ex = (double) (magExp * 0.5f);
        for (int i = 0; i < fftN; ++i)
        {
            const auto& c = spec[(std::size_t) i];
            const double p = c.real() * c.real() + c.imag() * c.imag();
            mag[(std::size_t) i] = (float) std::pow (p, ex);
        }
        int count = 0;
        // (the peak search reads a bin on each side: at device rates far above 192 kHz the 60 Hz bin is bin 0 and the
        // search read before the spectrum — issue #134's audit; unchanged at every rate FL runs at)
        for (int k = std::max (binLo, 1); k < std::min (binHi, fftN - 1) && count != 100; ++k)
        {
            const float m0 = mag[(std::size_t) k], mm = mag[(std::size_t) k - 1], mp = mag[(std::size_t) k + 1];
            if (! (m0 > mm) || ! (m0 > mp))
                continue;
            const float d = mp - mm;
            const float half = d * 0.5f;
            const float curv = ((mp + mm) - (m0 + m0)) * 0.5f;
            peakAmp[count] = m0 - (d * d) * (0.0625f / curv);
            peakPos[count] = (float) k - (0.5f / curv) * half;
            ++count;
        }
        int best = 0;
        for (int i = 1; i < count; ++i)
            if (! (peakAmp[best] > peakAmp[i]))
                best = i;
        const int candidates = std::min (best * 2, count);
        float bestPos = peakPos[0];
        float bestScore = floatOf (0xb07763dfu);
        const int quarter = fftN / 4;
        for (int c = 0; c < candidates; ++c)
        {
            const float f = peakPos[c];
            float score = 0;
            int j = (int) (f * 0.25f);
            if (j < quarter)
            {
                const float sigma = f * harmWidth;
                const double inv = 1.0 / (double) f;
                const float g = -1.0f / (sigma * sigma);
                for (; j < quarter; ++j)
                {
                    const float jf = (float) j;
                    const float co = (float) std::cos (((double) jf * 6.283185307179586) * inv);
                    const float dj = jf - f;
                    const float e = expfCrt ((dj * dj) * g);
                    score = (co * mag[(std::size_t) j]) * e + score;
                }
            }
            if (score >= bestScore)
            {
                bestScore = score;
                bestPos = f;
            }
        }
        detected = (bestPos * rate) / (float) fftN;
    }

    float interp (const std::vector<float>& b, int ip, float fr, float fr2) const noexcept
    {
        int im1 = ip - 1;
        if (im1 < 0)
            im1 += ringLen;
        int ip1 = ip + 1;
        if (ip1 > ringLen - 1)
            ip1 -= ringLen;
        int ip2 = ip + 2;
        if (ip2 > ringLen - 1)
            ip2 -= ringLen;
        const float c3 = floatOf (0x3eaaaa9fu), c6 = floatOf (0x3e2aaac1u);
        const float y0 = b[(std::size_t) ip], ym = b[(std::size_t) im1], y1 = b[(std::size_t) ip1], y2 = b[(std::size_t) ip2];
        const float a = (y1 - (y0 * 0.5f + ym * c3)) - y2 * c6;
        const float cq = (ym + y1) * 0.5f - y0;
        const float bq = (y2 * c6 - ym * c6) + (y0 - y1) * 0.5f;
        return ((bq * fr2 + a) * fr + y0) + cq * fr2;
    }

    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float xl = in[2 * i], xr = in[2 * i + 1];
            float dl = 0, dr = 0;
            if (mode != 1)
            {
                if (mode == 4)
                    dCur = (dTarget - dCur) * dCoef + dCur;
                else
                {
                    const float t = fmax_ (dTarget - (float) lag, 1.0f);
                    dCur = (t - dCur) * dCoef + dCur;
                }
                const int id = (int) dCur;
                int r1 = dWrite - id;
                const float fr = dCur - (float) id;
                while (r1 < 0)
                    r1 += dLen;
                int r2 = r1 - 1;
                while (r2 < 0)
                    r2 += dLen;
                const float g = 1.0f - fr;
                dl = g * dL[(std::size_t) r1] + fr * dL[(std::size_t) r2];
                dr = g * dR[(std::size_t) r1] + fr * dR[(std::size_t) r2];
            }
            float l = xl, r = xr;
            if (mode == 2)
                l = fbL + xl, r = fbR + xr;
            else if (mode == 3)
                l = dl, r = dr;
            // analysis channel, DC high-pass, zero-crossing voicing fade
            float a = l;
            if (channel != 1)
            {
                a = r;
                if (channel != 2)
                    a = (r + l) * 0.5f;
            }
            const float y = a * hp[0] + hs1;
            hs1 = (a * hp[1] + hs2) - y * hp[3];
            hs2 = a * hp[2] - y * hp[4];
            const float s = y >= 0.0f ? 1.0f : -1.0f;
            const float prevS = sgn;
            sgn = s;
            zc = std::fabs (s - prevS) * 0.05f + zc * zcCoef;
            if (zc > zcThreshold)
            {
                fade = fade - fadeStep;
                if (! (fade >= 0.0f))
                    fade = 0.0f;
            }
            else
            {
                fade = fadeStep + fade;
                if (fade > 1.0f)
                    fade = 1.0f;
            }
            const float voicedL = fade * l, unvoicedL = (1.0f - fade) * l;
            const float unvoicedR = (1.0f - fade) * r, voicedR = r * fade;
            vL[(std::size_t) lagPos] = voicedL;
            uL[(std::size_t) lagPos] = unvoicedL;
            vR[(std::size_t) lagPos] = voicedR;
            uR[(std::size_t) lagPos] = unvoicedR;
            dryL[(std::size_t) lagPos] = xl;
            dryR[(std::size_t) lagPos] = xr;
            if (++lagPos > lag - 1)
                lagPos = 0;
            const float lvL = vL[(std::size_t) lagPos], luL = uL[(std::size_t) lagPos];
            const float lvR = vR[(std::size_t) lagPos], luR = uR[(std::size_t) lagPos];
            const float ldL = dryL[(std::size_t) lagPos], ldR = dryR[(std::size_t) lagPos];
            ringL[(std::size_t) ringPos] = lvL;
            ringR[(std::size_t) ringPos] = lvR;
            if (++ringPos > ringLen - 1)
                ringPos = 0;
            // detector frame
            float c = voicedL;
            if (channel != 1)
            {
                c = voicedR;
                if (channel != 2)
                    c = (voicedR + voicedL) * 0.5f;
            }
            frame[(std::size_t) framePos] = c * frameWin[(std::size_t) framePos];
            if (++framePos > frameLen - 1)
            {
                framePos = 0;
                detect();
            }
            f0 = detected * smoothA + smoothB * f0;
            f0 = fmin_ (fmax_ (f0, 60.0f), 1500.0f);
            // analysis marks one period apart
            if (--anaCount < 1)
            {
                mark = ringPos - period;
                if (mark < 0)
                    mark += ringLen;
                period = (int) (rate / f0);
                anaCount = period;
            }
            // synthesis grains
            if (--synCount < 1)
            {
                float sp;
                if (pitchMode == 2)
                    sp = (floatOf (0x3bfa7eb7u) / ratio) * rate;
                else if (pitchMode == 3)
                    sp = (floatOf (0x3b7a7eb7u) / ratio) * rate;
                else if (pitchMode == 4)
                    sp = (floatOf (0x3afa7eb7u) / ratio) * rate;
                else
                    sp = (float) period / ratio;
                synCount = (int) sp;
                int k = 0;
                while (k < kGrains && active[k] == 1)
                    ++k;
                if (k < kGrains)
                {
                    active[k] = 1;
                    pos[k] = (float) mark;
                    step[k] = formRatio;
                    length[k] = (int) (((float) period + (float) period) / step[k]);
                    remain[k] = (int) (((float) period + (float) period) / step[k]);
                }
            }
            float sl = 0, sr = 0;
            for (int k = 0; k < kGrains; ++k)
            {
                if (active[k] != 1)
                    continue;
                float wi = (1.0f - (float) remain[k] / (float) length[k]) * ((float) kWinSize - 1.0f);
                const int wix = (int) wi;
                wi = wi - (float) wix;
                float w = (1.0f - wi) * window[(std::size_t) wix];
                if (wix != kWinSize - 1)
                    w = wi * window[(std::size_t) wix + 1] + w;
                const int ip = (int) pos[k];
                const float fr = pos[k] - (float) ip;
                const float fr2 = fr * fr;
                sl = w * interp (ringL, ip, fr, fr2) + sl;
                sr = w * interp (ringR, ip, fr, fr2) + sr;
                float np = step[k] + pos[k];
                if (np > (float) (ringLen - 1))
                    np -= (float) ringLen;
                pos[k] = np;
                if (--remain[k] < 1)
                    active[k] = 0;
            }
            sl = sl + luL;
            sr = luR + sr;
            if (clipOutput)
            {
                sl = clipLookup (clip, (floatOf (0x42c86667u) / clipGain) * sl + 500.0f, clipGain);
                sr = clipLookup (clip, (floatOf (0x42c86667u) / clipGain) * sr + 500.0f, clipGain);
            }
            if (mode != 1)
            {
                float fl = 0, fr = 0;
                if (mode == 2 || mode == 4)
                    fl = dl, fr = dr;
                else if (mode == 3)
                    fl = sl, fr = sr;
                fbL = feedback * ffL.run (fc, fl);
                fbR = ffR.run (fc, fr) * feedback;
                if (! clipOutput)
                {
                    fbL = clipLookup (clip, (floatOf (0x42c86667u) / clipGain) * fbL + 500.0f, clipGain);
                    fbR = clipLookup (clip, (floatOf (0x42c86667u) / clipGain) * fbR + 500.0f, clipGain);
                }
                float wl = 0, wr = 0;
                if (mode == 2)
                    wl = sl, wr = sr;
                else if (mode == 3)
                    wl = fbL + xl, wr = fbR + xr;
                else if (mode == 4)
                    wl = fbL + sl, wr = fbR + sr;
                dL[(std::size_t) dWrite] = wl;
                dR[(std::size_t) dWrite] = wr;
                if (++dWrite > dLen - 1)
                    dWrite = 0;
            }
            const float wetL = mode == 4 ? sl + fbL : sl;
            const float wetR = mode == 4 ? sr + fbR : sr;
            out[2 * i] = wetL * mix + (1.0f - mix) * ldL;
            out[2 * i + 1] = wetR * mix + (1.0f - mix) * ldR;
        }
    }
};

// ===================================================================================================== plug-in level
namespace
{
const int kMin[13] = { -1200, -1200, 0, 0, 2000, 100, 0, 0, 1, 0, 0, 0, 0 };
const int kMax[13] = { 1200, 1200, 2, 2400, 15000, 500, 100, 100, 768, 100, 3, 65536, 3 };
} // namespace

int rawMin (int i) noexcept { return (i >= 0 && i < 13) ? kMin[i] : 0; }
int rawMax (int i) noexcept { return (i >= 0 && i < 13) ? kMax[i] : 1; }

float normalised (int i, int raw) noexcept
{
    if (i < 0 || i >= 13)
        return 0.0f;
    const double scale = 1.0 / (double) (kMax[i] - kMin[i]);
    double v = (double) (raw - kMin[i]) * scale;
    v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    return (float) v;
}

bool parsePitchShifter (const std::uint8_t* d, std::size_t n, State& out)
{
    auto i32 = [&] (std::size_t o) {
        std::int32_t v = 0;
        std::memcpy (&v, d + o, 4);
        return (int) v;
    };
    // version, 13 parameters, formant-correction knob copy, tempo-sync byte, engine, detector; version 7 adds the
    // spectrum speed and two display bytes (69 / 75 bytes)
    if (d == nullptr || n < 69)
        return false;
    State s;
    s.version = i32 (0);
    if (s.version != 6 && s.version != 7)
        return false;
    for (int k = 0; k < 13; ++k)
        s.params[(std::size_t) k] = i32 (4 + 4 * (std::size_t) k);
    s.tempoSync = d[60] != 0;
    s.engine = i32 (61) != 0 ? 1 : 0;
    s.detector = i32 (65) != 0 ? 1 : 0;
    out = s;
    return true;
}

Engine::Engine() : psola (std::make_unique<Psola>()), gran (std::make_unique<Granular>())
{
    prepare (44100.0);
}
Engine::~Engine() = default;

void Engine::sendParam (int i)
{
    const int raw = st.params[(std::size_t) i];
    const float x = normalised (i, raw);
    auto combinedFormant = [&] {
        const float fo = (float) (((double) normalised (1, st.params[1]) - 0.5) * 24.0);
        const float pi = (float) (((double) normalised (0, st.params[0]) - 0.5) * 24.0);
        const float keep = 1.0f - normalised (11, st.params[11]);
        const float v = fo + pi * keep;
        return v < -12.0f ? -12.0f : (v > 12.0f ? 12.0f : v);
    };
    const bool correction = normalised (11, st.params[11]) < 1.0f;
    switch (i)
    {
        case 0:
        {
            const float semis = (float) (((double) x - 0.5) * 24.0);
            psola->setParam (0, semis);
            if (correction)
                psola->setParam (1, combinedFormant());
            gran->setParam (0, semis);
            break;
        }
        case 1:
            psola->setParam (1, correction ? combinedFormant() : (float) (((double) x - 0.5) * 24.0));
            break;
        case 2: psola->setParam (2, (float) (raw + 1)); break;
        case 3:
            psola->setParam (3, x);
            gran->setParam (5, x);
            break;
        case 4: gran->setParam (1, (float) ((double) raw / 100.0)); break;
        case 5: gran->setParam (2, (float) ((double) raw / 100.0)); break;
        case 6: gran->setParam (3, x); break;
        case 7: gran->setParam (4, x); break;
        case 8:
            if (! st.tempoSync)
            {
                const float ms = x * 999.0f + 1.0f;
                psola->setParam (4, ms);
                gran->setParam (6, ms);
            }
            else
            {
                psola->setParam (8, x * 16.0f);
                gran->setParam (10, x * 16.0f);
            }
            break;
        case 9:
            psola->setParam (5, x);
            gran->setParam (7, x);
            break;
        case 10:
            psola->setParam (6, (float) (raw + 1));
            gran->setParam (8, (float) (raw + 1));
            break;
        case 11: psola->setParam (1, combinedFormant()); break;
        case 12: psola->setParam (10, (float) (raw + 1)); break;
        default: break;
    }
}

void Engine::setState (const State& s)
{
    st = s;
    for (int i = 0; i < 13; ++i)
        sendParam (i);
}

void Engine::setRaw (int index, int raw)
{
    if (index < 0 || index >= 13)
        return;
    st.params[(std::size_t) index] = std::clamp (raw, kMin[index], kMax[index]);
    sendParam (index);
}

void Engine::setTempo (double bpm)
{
    tempo = bpm;
    psola->setParam (7, (float) bpm);
    gran->setParam (9, (float) bpm);
}

void Engine::prepare (double sampleRate)
{
    rate = sampleRate;
    psola->setRate ((float) sampleRate);
    gran->setRate ((float) sampleRate);
    for (int i = 0; i < 13; ++i)
        sendParam (i);
    psola->setParam (9, st.detector != 0 ? 2048.0f : 1024.0f);
    if (tempo != 120.0)
        setTempo (tempo);
}

void Engine::reset()
{
    psola->clear();
    gran->clear();
}

int Engine::latencyFrames() const noexcept
{
    return st.engine == 0 ? psola->lag : 0;
}

void Engine::process (float* left, float* right, int numFrames)
{
    for (int off = 0; off < numFrames;)
    {
        const int n = std::min (512, numFrames - off);
        io.resize ((std::size_t) n * 4);
        float* src = io.data();
        float* dst = io.data() + 2 * n;
        for (int i = 0; i < n; ++i)
        {
            src[2 * i] = left[off + i];
            src[2 * i + 1] = right[off + i];
        }
        if (st.engine == 0)
            psola->process (src, dst, n);
        else
            gran->process (src, dst, n);
        for (int i = 0; i < n; ++i)
        {
            left[off + i] = dst[2 * i];
            right[off + i] = dst[2 * i + 1];
        }
        off += n;
    }
}

// --- the LYNX mixer effect ------------------------------------------------------------------------------------------

void toParams (const State& s, EffectParams& p) noexcept
{
    for (int i = 0; i < 13; ++i)
        p[(std::size_t) i] = normalised (i, s.params[(std::size_t) i]);
    p[13] = s.engine == 1 ? 1.0f : 0.0f;
    p[14] = s.detector == 1 ? 1.0f : 0.0f;
    p[15] = s.tempoSync ? 1.0f : 0.0f;
}

State fromParams (const EffectParams& p) noexcept
{
    State s;
    for (int i = 0; i < 13; ++i)
    {
        const float v = std::clamp (p[(std::size_t) i], 0.0f, 1.0f);
        s.params[(std::size_t) i] = rawMin (i) + (int) std::lround ((double) v * (double) (rawMax (i) - rawMin (i)));
    }
    s.engine = p[13] >= 0.5f ? 1 : 0;
    s.detector = p[14] >= 0.5f ? 1 : 0;
    s.tempoSync = p[15] >= 0.5f;
    return s;
}

std::string paramText (int param, float v)
{
    char t[48];
    if (param >= 13 && param < numParams)
    {
        static const char* const names[3][2] = { { "FFT", "granular" }, { "fast", "accurate" }, { "off", "on" } };
        return names[param - 13][v >= 0.5f ? 1 : 0];
    }
    if (param < 0 || param >= 13)
        return {};
    const int raw = rawMin (param) + (int) std::lround ((double) std::clamp (v, 0.0f, 1.0f) * (rawMax (param) - rawMin (param)));
    switch (param)
    {
        case 0:
        case 1: std::snprintf (t, sizeof (t), "%+.2f st", raw / 100.0); break;
        case 2: return raw == 0 ? "left" : (raw == 1 ? "right" : "left + right");
        case 3: std::snprintf (t, sizeof (t), "%.0f%%", raw / 24.0); break;
        case 4: std::snprintf (t, sizeof (t), "%.0f ms", raw / 100.0); break;
        case 5: std::snprintf (t, sizeof (t), "%d%%", raw); break;
        case 8: std::snprintf (t, sizeof (t), "%.0f ms", 1.0 + 999.0 * (raw - 1) / 767.0); break;
        case 10: return raw == 0 ? "off" : (raw == 1 ? "post" : (raw == 2 ? "pre" : "no shift"));
        case 11: std::snprintf (t, sizeof (t), "%.0f%%", raw * 100.0 / 65536.0); break;
        case 12: return raw == 0 ? "relative" : (raw == 1 ? "C4" : (raw == 2 ? "C5" : "C6"));
        default: std::snprintf (t, sizeof (t), "%d%%", raw); break;
    }
    return t;
}

namespace
{
class PitchShifterEffect final : public Effect
{
public:
    // `initial` (may be null): the slot's parameters, loaded in prepare() as process() would on its first block, so
    // the engine's latency is known before it (issue #113)
    explicit PitchShifterEffect (const EffectParams* initial = nullptr)
    {
        if (initial != nullptr)
            params0 = *initial;
        hasParams0 = initial != nullptr;
    }

    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        if (hasParams0 && ! loaded)
        {
            state = fromParams (params0), loaded = true;
            engine.setState (state);
        }
        engine.prepare (sampleRate);
    }

    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (bpm > 0 && bpm != tempo)
            engine.setTempo (tempo = bpm);
        const auto s = fromParams (p);
        if (! loaded || s.engine != state.engine || s.detector != state.detector || s.tempoSync != state.tempoSync)
        {
            // as FL's state load: every parameter re-sent, the engines re-initialised
            state = s, loaded = true;
            engine.setState (state);
            engine.prepare (rate);
        }
        else
            for (int i = 0; i < 13; ++i)
                if (s.params[(std::size_t) i] != state.params[(std::size_t) i])
                {
                    state.params[(std::size_t) i] = s.params[(std::size_t) i];
                    engine.setRaw (i, s.params[(std::size_t) i]);
                }
        engine.process (left, right, n);
    }

    int latencySamples() const noexcept override { return loaded ? engine.latencyFrames() : 0; }

private:
    Engine engine;
    State state;
    EffectParams params0 {};
    bool loaded = false, hasParams0 = false;
    double rate = 44100.0, tempo = 120.0;
};
} // namespace

std::unique_ptr<Effect> make (const EffectParams* params) { return std::make_unique<PitchShifterEffect> (params); }
} // namespace lynx::psfl
