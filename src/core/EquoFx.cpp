// FL Studio's EQUO as an exact engine (docs/analise/REA_rodada61.md). Clean-room: our own code, written from the
// behaviour documented there.

#include "core/StateHandoff.h"
#include "core/EquoFx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Denormals.h"
#include "core/Effects.h"
#include "core/Instruments.h"

// The plugin's arithmetic is plain SSE (float and double) with one x87 pan law; fusing a·b + c into one FMA would change
// the results, so contraction stays off in this file.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::equofx
{
namespace
{
constexpr int maxBlock = 512;
constexpr double twoPi = 6.283185307179586;
constexpr double ln2 = 0.69314718055994529, halfLn2 = 0.34657359027997264, ln3 = 1.0986122886681098;
constexpr float flushBelow = 5.9604644775390625e-08f; // 2^-24

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

float f32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    float v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

std::uint32_t bitsOf (float v) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &v, 4);
    return u;
}

// normalised value (0…2) → volume / pan, and back (§2.3)
float toValue (int t, float v) noexcept
{
    if (t == 1)
        return v - 1.0f;
    return (float) ((double) (float) (std::exp ((double) v * ln3) - 1.0) * 0.5);
}

float toNorm (int t, float g) noexcept
{
    if (t == 1)
        return g + 1.0f;
    const float a = (float) ((double) g / 0.5) + 1.0f;
    return (float) (std::log ((double) a) / ln3);
}

double sinhRtl (double x) noexcept
{
    if (std::abs (x) <= 1.0e-12)
        return 0.0;
    return (std::exp (x) - std::exp (-x)) / 2.0;
}

// the plugin's run-time sine/cosine round like the extended-precision results (§2.4)
double sinRtl (double x) noexcept { return (double) std::sin ((long double) x); }
double cosRtl (double x) noexcept { return (double) std::cos ((long double) x); }

// one gain stage's ramp (§2.6): returns the increment per frame, sets `start` (the gain at frame 0) and moves `now`
float ramp (float& now, float& start, float target, float maxStep, int n) noexcept
{
    start = now;
    float inc = target - start;
    if (bitsOf (inc) != 0)
    {
        now = target;
        inc = inc / (float) n;
        if (std::abs (inc) > maxStep)
        {
            inc = std::copysign (maxStep, inc);
            now = start + inc * (float) n;
            if ((bitsOf (now) & 0x7fffffffu) < bitsOf (flushBelow))
            {
                now = 0.0f;
                inc = -start / (float) n;
            }
        }
    }
    return inc;
}

// the per-frame gains of a ramp: even and odd frames on separate running sums for each group of eight frames, the
// remaining frames one by one from the even sum (§2.6)
template <typename Fn>
void rampFrames (int n, float g0, float inc, Fn&& apply) noexcept
{
    float even = g0 + 0.0f, odd = g0 + inc;
    const float two = inc + inc;
    int i = 0;
    for (; i + 8 <= n; i += 8)
    {
        apply (i, even);
        apply (i + 1, odd);
        float e = even, o = odd;
        for (int k = 2; k < 8; k += 2)
        {
            e = e + two, o = o + two;
            apply (i + k, e);
            apply (i + k + 1, o);
        }
        even = e + two, odd = o + two;
    }
    for (; i < n; ++i)
    {
        apply (i, even);
        even = even + inc;
    }
}

// 2:1 polyphase half-band (shared with Parametric EQ 2, REA_rodada50 §2.7); 24 doubles per stage: outputs A1 A2 A3
// B1 B2 B3 (L,R pairs), then the input delays
constexpr double hbA[3] = { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
constexpr double hbB[3] = { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };

void flushHalfBand (double* s) noexcept
{
    for (int k = 0; k < 12; ++k)
        if (std::abs (s[k]) < (double) flushBelow)
            s[k] = 0.0;
}

void upsample (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;
            double* d = s + 12 + c;
            const double v = in[2 * i + c];
            const double a1 = d[0] + (v - o[0]) * hbA[0];
            d[0] = v, o[0] = a1;
            const double b1 = d[6] + (v - o[6]) * hbB[0];
            d[6] = v, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            o[10] = b3;
            d[4] = a2, d[10] = b2;
            out[4 * i + c] = (float) b3;
            out[4 * i + 2 + c] = (float) a3;
        }
    flushHalfBand (s);
}

void downsample (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;
            double* d = s + 12 + c;
            const double e = in[4 * i + c], od = in[4 * i + 2 + c];
            const double a1 = d[0] + (e - o[0]) * hbA[0];
            d[0] = e, o[0] = a1;
            const double b1 = d[6] + (od - o[6]) * hbB[0];
            d[6] = od, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            d[4] = a2, o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            d[10] = b2, o[10] = b3;
            out[2 * i + c] = (float) ((b3 + a3) * 0.5);
        }
    flushHalfBand (s);
}

struct BandFilter
{
    float freq = 0;
    bool off = false;     // at or above the processing Nyquist frequency
    bool doubled = false; // run at twice the processing rate (sample-and-hold up, pair average down)
    double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    double s[8] {}; // x1 L/R, x2 L/R, y1 L/R, y2 L/R
    float vol[2] {}, send[2] {}; // the gain stages' current gains (L, R)
};
} // namespace

struct Engine::Impl
{
    State st;
    double rate = 44100.0;
    bool hq = false;
    int factor = 1, order = 0; // design-rate factor and the resampler order
    float maxStep = 0.001f;
    int silenceReset = 8820, silence = 0;
    bool snap = false;
    float norm = 0, volume = 1, volumeNow = 0;
    std::array<BandFilter, numBands> band {};
    std::array<std::array<float, numTargets>, numBands> value {}, now {}; // targets and the values in use
    // the motion smoother's states: FL's plugin creates them at volume 1, pan 0, send 1 and a state load does not touch
    // them, so the first ticks after a load glide from there (§2.5; FL's render of the render test, REA_rodada170i §8)
    std::array<std::array<double, numTargets>, numBands> glide = [] {
        std::array<std::array<double, numTargets>, numBands> g {};
        for (auto& b : g)
            b = { 1.0, 0.0, 1.0 };
        return g;
    }();
    double samplesPerTick = 0, tickLeft = 0, smoothCoef = 0;
    bool smoothOn = false;
    std::vector<std::array<double, 24>> up, down, sendDown;
    std::vector<float> io, x, up2, acc, tmp, sendAcc, sendIo;
    float* sendOut = nullptr; // the interleaved buffer the send goes to this block (null = not routed)

    void configure() noexcept
    {
        order = hq ? 1 : std::max (0, st.oversample);
        factor = 1 << order;
        const int r = (int) rate * factor;
        maxStep = (float) (44100.0 / (double) r * 0.001);
        up.assign ((std::size_t) order, {});
        down.assign ((std::size_t) order, {});
        sendDown.assign ((std::size_t) order, {});
        const auto cap = (std::size_t) (maxBlock * (1 << std::max (order, 1)) * 4);
        x.assign (cap, 0.0f), up2.assign (cap, 0.0f), acc.assign (cap, 0.0f), tmp.assign (cap, 0.0f), io.assign (maxBlock * 2, 0.0f);
        sendAcc.assign (cap, 0.0f), sendIo.assign (maxBlock * 2, 0.0f);
        design();
        computeValues();
        computeVolume();
        computeSmoother();
    }

    // band layout and filters (§2.4)
    void design() noexcept
    {
        const float w = (float) ((double) st.bandwidth / 100.0);
        const float bw = w / 3.0f;
        norm = (float) std::exp ((double) (0.115129255f * -3.0f)) / w;
        const int r = (int) rate * factor;
        const float nyquist = (float) ((double) r / 2.0);
        const float doubleAbove = (float) ((double) (factor * 22100) / 2.0);
        const double k = (1.0 / (double) r) * twoPi;
        for (int b = 0; b < numBands; ++b)
        {
            auto& f = band[(std::size_t) b];
            f.freq = (float) (std::exp (((double) b / 3.0) * ln2) * 20.0);
            f.off = f.freq >= nyquist;
            if (f.off)
                continue;
            f.doubled = f.freq > doubleAbove;
            const float fd = f.doubled ? f.freq * 0.5f : f.freq;
            const double om = (double) fd * k;
            const double sn = sinRtl (om), cs = cosRtl (om);
            const double alpha = sn * sinhRtl (((halfLn2 * (double) bw) * om) / sn);
            const double dn = 1.0 / (alpha + 1.0);
            f.b0 = dn * alpha;
            f.b2 = dn * -alpha;
            f.a1 = (dn * cs) * -2.0;
            f.a2 = dn * (1.0 - alpha);
        }
    }

    // each band's volume / pan / send from the banks (§2.3)
    void computeValues() noexcept
    {
        const float shift = (float) ((double) st.knob[0] * 0.0025);
        const float mix = 1.0f - (float) st.knob[1] * 0.0078125f;
        const float morph = (float) ((double) st.knob[2] * 0.001);
        for (int b = 0; b < numBands; ++b)
            for (int t = 0; t < numTargets; ++t)
            {
                const auto& tg = st.target[(std::size_t) b][(std::size_t) t];
                float r;
                if (tg.mode == 1)
                    r = toValue (t, 1.0f);
                else if (tg.mode == 2)
                    r = toValue (t, tg.value);
                else
                {
                    float pos = (float) b - shift;
                    if (! (pos > 0.0f))
                        pos = 0.0f;
                    const int bi = (int) pos;
                    const float bf = pos - (float) bi;
                    const int mi = (int) morph;
                    const float mf = morph - (float) mi;
                    auto mixed = [&] (int bank, int bb) {
                        const float v = st.cell (bank, std::min (bb, 30), t);
                        return toValue (t, v + (1.0f - v) * mix);
                    };
                    auto bankValue = [&] (int bank) {
                        float v = mixed (bank, bi);
                        if (bf != 0.0f)
                            v = v + (mixed (bank, bi + 1) - v) * bf;
                        return v;
                    };
                    float g = bankValue (std::min (mi, numBanks - 1));
                    if (mf != 0.0f)
                    {
                        const float g2 = bankValue (std::min (mi + 1, numBanks - 1));
                        const float n1 = toNorm (t, g), n2 = toNorm (t, g2);
                        g = toValue (t, n1 + (n2 - n1) * mf);
                    }
                    const float master = toValue (t, st.cell (8, b, t));
                    if (t == 1)
                    {
                        r = g + master;
                        r = -1.0f > r ? -1.0f : (r > 1.0f ? 1.0f : r);
                    }
                    else
                        r = g * master;
                }
                value[(std::size_t) b][(std::size_t) t] = r;
            }
    }

    // motion smoothness (§2.8): a one-pole per tick, time = (11^(|knob|/1024) − 1)·40000 / samples per tick
    void computeSmoother() noexcept
    {
        const double spt = samplesPerTick > 0 ? samplesPerTick : 196.875;
        const float k = (float) std::abs (st.knob[3]) * 0.0009765625f;
        const float e = (float) (std::exp ((double) k * std::log (11.0)) - 1.0);
        const float t = (float) (((double) e * 40000.0) / (double) (float) spt);
        smoothOn = (double) t > 0.0;
        smoothCoef = smoothOn ? std::exp (-4.6051701859880918 / std::max (1.0, (double) t)) : 0.0;
    }

    void tick() noexcept
    {
        for (int b = 0; b < numBands; ++b)
            for (int t = 0; t < numTargets; ++t)
            {
                const double target = value[(std::size_t) b][(std::size_t) t];
                double& g = glide[(std::size_t) b][(std::size_t) t];
                if (smoothOn)
                {
                    g = target + (g - target) * smoothCoef;
                    now[(std::size_t) b][(std::size_t) t] = (float) g;
                }
                else
                    now[(std::size_t) b][(std::size_t) t] = (float) target;
            }
    }

    void settle (bool glides) noexcept
    {
        now = value;
        if (glides)
            for (int b = 0; b < numBands; ++b)
                for (int t = 0; t < numTargets; ++t)
                    glide[(std::size_t) b][(std::size_t) t] = value[(std::size_t) b][(std::size_t) t];
    }

    void computeVolume() noexcept
    {
        const float x = (float) st.knob[4] * 0.0078125f;
        volume = x == 1.0f ? 1.0f : (float) ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
    }

    void clearFilters() noexcept
    {
        for (auto& f : band)
            std::fill (std::begin (f.s), std::end (f.s), 0.0);
    }

    void fresh() noexcept
    {
        clearFilters();
        for (auto& f : band)
            f.vol[0] = f.vol[1] = f.send[0] = f.send[1] = 0.0f;
        for (auto& s : up)
            s.fill (0.0);
        for (auto& s : down)
            s.fill (0.0);
        for (auto& s : sendDown)
            s.fill (0.0);
        volumeNow = 0.0f;
        snap = false;
        silence = 0;
    }

    static void runFilter (BandFilter& f, const float* in, float* out, int n) noexcept
    {
        double x1l = f.s[0], x1r = f.s[1], x2l = f.s[2], x2r = f.s[3], y1l = f.s[4], y1r = f.s[5], y2l = f.s[6], y2r = f.s[7];
        for (int i = 0; i < n; ++i)
        {
            const double xl = in[2 * i], xr = in[2 * i + 1];
            const double tl = (x2l * f.b2 - y2l * f.a2) - y1l * f.a1;
            const double tr = (x2r * f.b2 - y2r * f.a2) - y1r * f.a1;
            const double yl = xl * f.b0 + tl, yr = xr * f.b0 + tr;
            x2l = x1l, x2r = x1r, x1l = xl, x1r = xr;
            y2l = y1l, y2r = y1r, y1l = yl, y1r = yr;
            out[2 * i] = (float) yl;
            out[2 * i + 1] = (float) yr;
        }
        f.s[0] = x1l, f.s[1] = x1r, f.s[2] = x2l, f.s[3] = x2r, f.s[4] = y1l, f.s[5] = y1r, f.s[6] = y2l, f.s[7] = y2r;
    }

    // dst += src · gain (L, R), the gains ramping towards the target (§2.6)
    void addGained (const float* src, float* dst, int n, float tl, float tr, float* now) const noexcept
    {
        float sl, sr;
        const float il = ramp (now[0], sl, tl, maxStep, n);
        const float ir = ramp (now[1], sr, tr, maxStep, n);
        if (bitsOf (il) == 0 && bitsOf (ir) == 0)
        {
            if (bitsOf (sl) == 0 && bitsOf (sr) == 0)
                return;
            if (sl == 1.0f && sr == 1.0f)
                for (int i = 0; i < 2 * n; ++i)
                    dst[i] = src[i] + dst[i];
            else
                for (int i = 0; i < n; ++i)
                {
                    dst[2 * i] = src[2 * i] * sl + dst[2 * i];
                    dst[2 * i + 1] = src[2 * i + 1] * sr + dst[2 * i + 1];
                }
            return;
        }
        rampFrames (n, sl, il, [&] (int i, float g) { dst[2 * i] = src[2 * i] * g + dst[2 * i]; });
        rampFrames (n, sr, ir, [&] (int i, float g) { dst[2 * i + 1] = src[2 * i + 1] * g + dst[2 * i + 1]; });
    }

    void block (float* buf, int n) noexcept
    {
        // silence gate (§2.7)
        bool silent = true;
        for (int i = 0; i < 2 * n && silent; ++i)
            silent = (bitsOf (buf[i]) << 1) <= (bitsOf (1.0e-6f) << 1);
        silence = silent ? silence - n : silenceReset;
        if (! (-n < silence))
        {
            snap = true;
            return; // passed through unchanged
        }
        float* in = buf;
        int m = n;
        const bool resample = st.oversample > 0 && order > 0;
        if (resample)
        {
            const float* src = buf;
            for (int k = order - 1; k >= 0; --k)
            {
                float* dst = (k & 1) == (order & 1) ? x.data() : up2.data();
                upsample (up[(std::size_t) k].data(), src, dst, m);
                src = dst;
                m *= 2;
            }
            in = const_cast<float*> (src);
        }
        // the send (§2.1 step 6): a second sum, started like the main one, for the mixer track the state names
        const bool sendOn = st.sendTrack >= 0;
        if (sendOn)
        {
            if (st.addMode)
                std::copy (in, in + 2 * m, sendAcc.begin());
            else
                std::fill (sendAcc.begin(), sendAcc.begin() + 2 * m, 0.0f);
        }
        if (st.addMode)
            std::copy (in, in + 2 * m, acc.begin());
        else
            std::fill (acc.begin(), acc.begin() + 2 * m, 0.0f);

        for (int b = 0; b < numBands; ++b)
        {
            auto& f = band[(std::size_t) b];
            const auto& tv = st.target[(std::size_t) b];
            const bool mainOn = tv[0].mode != 1 || tv[1].mode != 1;
            if (f.off || ! (mainOn || (sendOn && tv[2].mode != 1)))
                continue; // (a band whose only active target is the send runs while a send track is set)
            if (! f.doubled)
                runFilter (f, in, tmp.data(), m);
            else
            {
                for (int i = m - 1; i >= 0; --i)
                {
                    tmp[(std::size_t) (4 * i)] = tmp[(std::size_t) (4 * i + 2)] = in[2 * i];
                    tmp[(std::size_t) (4 * i + 1)] = tmp[(std::size_t) (4 * i + 3)] = in[2 * i + 1];
                }
                runFilter (f, tmp.data(), tmp.data(), 2 * m);
                for (int i = 0; i < m; ++i)
                {
                    tmp[(std::size_t) (2 * i)] = (tmp[(std::size_t) (4 * i)] + tmp[(std::size_t) (4 * i + 2)]) * 0.5f;
                    tmp[(std::size_t) (2 * i + 1)] = (tmp[(std::size_t) (4 * i + 1)] + tmp[(std::size_t) (4 * i + 3)]) * 0.5f;
                }
            }
            if (! st.sendOnly && tv[0].mode != 1)
            {
                // pan law: the side the pan points to keeps the level, the other is scaled by 1 − |pan| (computed in
                // extended precision and rounded once: exact in double)
                const float pan = now[(std::size_t) b][1];
                const float level = now[(std::size_t) b][0] * norm;
                float l, r;
                if ((bitsOf (pan) & 0x80000000u) == 0)
                    r = level, l = (float) ((1.0 - (double) pan) * (double) level);
                else
                    l = level, r = (float) (((double) pan + 1.0) * (double) level);
                if (st.addMode)
                    l = l - norm, r = r - norm;
                if (snap)
                    f.vol[0] = l, f.vol[1] = r;
                addGained (tmp.data(), acc.data(), m, l, r, f.vol);
            }
            if (sendOn && tv[2].mode != 1)
            {
                // the send's gain stage: the band's pan with its send level (the same law as the volume's)
                const float pan = now[(std::size_t) b][1];
                const float level = now[(std::size_t) b][2] * norm;
                float l, r;
                if ((bitsOf (pan) & 0x80000000u) == 0)
                    r = level, l = (float) ((1.0 - (double) pan) * (double) level);
                else
                    l = level, r = (float) (((double) pan + 1.0) * (double) level);
                if (st.addMode)
                    l = l - norm, r = r - norm;
                if (snap)
                    f.send[0] = l, f.send[1] = r;
                addGained (tmp.data(), sendAcc.data(), m, l, r, f.send);
            }
        }

        if (sendOn)
        {
            // down to the host rate with the send's own resampler, then handed to the track (copied; the tap is ours)
            float* src = sendAcc.data();
            if (resample)
            {
                int k = m;
                for (int s = order - 1; s >= 0; --s)
                {
                    k /= 2;
                    downsample (sendDown[(std::size_t) s].data(), src, src, k);
                }
            }
            if (sendOut != nullptr)
                std::copy (src, src + 2 * n, sendOut);
        }

        if (st.sendOnly)
            ; // the input stays in buf
        else if (! resample)
            std::copy (acc.begin(), acc.begin() + 2 * n, buf);
        else
        {
            float* src = acc.data();
            int k = m;
            for (int s = order - 1; s >= 0; --s)
            {
                k /= 2;
                float* dst = s == 0 ? buf : src;
                downsample (down[(std::size_t) s].data(), src, dst, k);
            }
        }

        // output volume
        if (snap)
        {
            volumeNow = volume;
            snap = false;
        }
        float start;
        const float inc = ramp (volumeNow, start, volume, maxStep / (float) factor, n);
        if (bitsOf (inc) == 0)
        {
            if (bitsOf (start) == 0)
                std::fill (buf, buf + 2 * n, 0.0f);
            else if (start != 1.0f)
                for (int i = 0; i < 2 * n; ++i)
                    buf[i] *= start;
        }
        else
            rampFrames (n, start, inc, [&] (int i, float g) { buf[2 * i] *= g, buf[2 * i + 1] *= g; });
    }
};

Engine::Engine() : d (std::make_unique<Impl>()) { d->configure(); }
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, bool hqRender) noexcept
{
    d->rate = sampleRate > 0 ? std::round (sampleRate) : 44100.0;
    d->hq = hqRender;
    d->silenceReset = (int) std::nearbyint (d->rate / 5.0);
    d->configure();
    d->fresh();
}

void Engine::set (const State& s) noexcept
{
    d->st = s;
    d->configure();
    d->fresh();
    d->settle (false); // (a load leaves the smoother's states: §2.5)
}

void Engine::setKnobs (const State& s) noexcept
{
    const bool layout = s.bandwidth != d->st.bandwidth || s.oversample != d->st.oversample;
    d->st.knob = s.knob;
    d->st.bandwidth = s.bandwidth, d->st.oversample = s.oversample;
    d->st.addMode = s.addMode, d->st.sendOnly = s.sendOnly;
    d->st.sendTrack = s.sendTrack;
    if (layout)
    {
        const auto upKeep = d->up, downKeep = d->down;
        d->configure();
        for (std::size_t i = 0; i < d->up.size() && i < upKeep.size(); ++i)
            d->up[i] = upKeep[i], d->down[i] = downKeep[i];
    }
    else
    {
        d->computeValues();
        d->computeVolume();
        d->computeSmoother();
    }
}

void Engine::setBands (const State& s) noexcept
{
    d->st.table = s.table;
    d->st.target = s.target;
    d->computeValues();
    d->computeVolume();
    d->computeSmoother();
}

void Engine::reset() noexcept
{
    d->fresh();
    d->settle (false); // (like FL's Flush: the values in use go to their targets, the smoother's states stay)
    d->tickLeft = 0;
}

void Engine::flush() noexcept
{
    d->clearFilters();
    d->settle (false);
    d->snap = true;
}

float Engine::bandValue (int b, int t) const noexcept { return d->now[(std::size_t) b][(std::size_t) t]; }

void Engine::tick() noexcept { d->tick(); }

void Engine::setSamplesPerTick (double spt) noexcept
{
    if (spt == d->samplesPerTick)
        return;
    if (d->samplesPerTick <= 0 || spt <= 0)
        d->tickLeft = 0;
    d->samplesPerTick = spt;
    d->computeSmoother();
}

void Engine::startFromConstruction() noexcept
{
    for (auto& g : d->glide)
        g = { 1.0, 0.0, 1.0 };
}

void Engine::process (float* left, float* right, int n, float* sendLeft, float* sendRight) noexcept
{
    ScopedFlushDenormals ftz;
    for (int i0 = 0; i0 < n;)
    {
        int m = std::min (maxBlock, n - i0);
        if (d->samplesPerTick > 0)
        {
            // FL calls the plugin's NewTick once per song tick, between render calls
            if (d->tickLeft <= 0)
            {
                d->tick();
                d->tickLeft += d->samplesPerTick;
            }
            m = std::min (m, std::max (1, (int) std::ceil (d->tickLeft)));
            d->tickLeft -= m;
        }
        for (int i = 0; i < m; ++i)
            d->io[(std::size_t) (2 * i)] = left[i0 + i], d->io[(std::size_t) (2 * i + 1)] = right[i0 + i];
        const bool sending = sendLeft != nullptr && sendRight != nullptr;
        d->sendOut = sending ? d->sendIo.data() : nullptr;
        if (sending)
            std::fill (d->sendIo.begin(), d->sendIo.begin() + 2 * m, 0.0f); // (a silent block sends nothing)
        d->block (d->io.data(), m);
        for (int i = 0; i < m; ++i)
            left[i0 + i] = d->io[(std::size_t) (2 * i)], right[i0 + i] = d->io[(std::size_t) (2 * i + 1)];
        if (sending)
            for (int i = 0; i < m; ++i)
                sendLeft[i0 + i] += d->sendIo[(std::size_t) (2 * i)], sendRight[i0 + i] += d->sendIo[(std::size_t) (2 * i + 1)];
        i0 += m;
    }
}

namespace
{
constexpr std::size_t sendTrackAt = 4 + numBands * numTargets * 8 + numBanks * numBands * numTargets * 4 + 5 * 4 + 8;
}

int sendInsert (const std::vector<std::uint8_t>& state) noexcept
{
    const auto s = parse (state);
    return s && s->sendTrack >= 0 ? s->sendTrack + 1 : 0;
}

bool setSendInsert (std::vector<std::uint8_t>& state, int insert) noexcept
{
    if (! parse (state))
        return false;
    const std::int32_t v = insert > 0 ? insert - 1 : -1;
    std::memcpy (state.data() + sendTrackAt, &v, 4);
    return true;
}

std::optional<State> parse (const std::vector<std::uint8_t>& b)
{
    constexpr std::size_t body = 4 + numBands * numTargets * 8 + 0xD14 + 0x14 + 0x17;
    if (b.size() < body)
        return std::nullopt;
    State s;
    s.version = i32At (b, 0);
    if (s.version < 3 || s.version > 4)
        return std::nullopt;
    std::size_t o = 4;
    for (auto& band : s.target)
        for (auto& t : band)
        {
            t.value = f32At (b, o);
            t.mode = i32At (b, o + 4);
            o += 8;
        }
    for (auto& v : s.table)
    {
        v = f32At (b, o);
        o += 4;
    }
    for (auto& k : s.knob)
    {
        k = i32At (b, o);
        o += 4;
    }
    s.oversample = i32At (b, o);
    s.bandwidth = i32At (b, o + 4);
    s.sendTrack = i32At (b, o + 8);
    s.sendOnly = b[o + 20] != 0;
    s.addMode = b[o + 21] != 0;
    return s;
}

namespace
{
struct Range
{
    int min, max;
};
constexpr Range knobRange[5] = { { -12000, 12000 }, { -128, 128 }, { 0, 7000 }, { 0, 1024 }, { 0, 256 } };

int rawOf (float v, Range r) noexcept
{
    return r.min + (int) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (r.max - r.min));
}
} // namespace

void toParams (const State& s, EffectParams& p) noexcept
{
    for (int i = 0; i < 5; ++i)
        p[(std::size_t) i] = (float) (s.knob[(std::size_t) i] - knobRange[i].min) / (float) (knobRange[i].max - knobRange[i].min);
    p[5] = (float) (s.bandwidth - 80) / 420.0f;
    p[6] = s.oversample > 0 ? 1.0f : 0.0f;
    p[7] = s.addMode ? 1.0f : 0.0f;
    p[8] = s.sendOnly ? 1.0f : 0.0f;
}

void applyParams (const EffectParams& p, State& s) noexcept
{
    for (int i = 0; i < 5; ++i)
        s.knob[(std::size_t) i] = rawOf (p[(std::size_t) i], knobRange[i]);
    s.bandwidth = rawOf (p[5], { 80, 500 });
    const bool ovs = p[6] >= 0.5f;
    if (ovs != (s.oversample > 0))
        s.oversample = ovs ? 1 : 0;
    s.addMode = p[7] >= 0.5f;
    s.sendOnly = p[8] >= 0.5f;
}

std::string paramText (int param, float v)
{
    char t[48];
    if (param >= 0 && param < 5)
    {
        const int raw = rawOf (v, knobRange[param]);
        switch (param)
        {
            case 0: std::snprintf (t, sizeof (t), "%+.2f bands", raw * 0.0025); return t;
            case 1: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw * 100.0 / 128.0)); return t;
            case 2: std::snprintf (t, sizeof (t), "bank %.2f", 1.0 + raw * 0.001); return t;
            case 3: std::snprintf (t, sizeof (t), "%d%%", (int) std::lround (raw * 100.0 / 1024.0)); return t;
            default:
            {
                const float x = (float) raw * 0.0078125f;
                const double g = x == 1.0f ? 1.0 : (std::exp ((double) (x * 2.39789534f)) - 1.0) * 0.1;
                if (g <= 0.0)
                    return "-inf dB";
                std::snprintf (t, sizeof (t), "%+.1f dB", 20.0 * std::log10 (g));
                return t;
            }
        }
    }
    if (param == 5)
    {
        std::snprintf (t, sizeof (t), "%d%%", rawOf (v, { 80, 500 }));
        return t;
    }
    if (param >= 6 && param <= 8)
        return v >= 0.5f ? "on" : "off";
    return {};
}

namespace
{
class EquoEffect final : public Effect
{
public:
    // FL's "HQ non-realtime" process mode (§2.2) is the export's "HQ for all plugins", not every render: FL's
    // command-line render of the render test "EQUO" matches the plain design, not the doubled one (REA_rodada170i §8)
    static bool hqRender() noexcept { return offlineRendering() && hqForAllPlugins(); }

    explicit EquoEffect (const std::vector<std::uint8_t>& flState)
    {
        if (auto s = parse (flState))
            state = *s, valid = true;
        engine.set (state);
    }

    void prepare (double sampleRate) noexcept override
    {
        rate = sampleRate;
        rendering = hqRender();
        engine.prepare (sampleRate, rendering);
        engine.set (state);
    }

    void reset() noexcept override { engine.reset(); }

    // the editor drew in the band table: the running engine glides to the new values
    bool acceptState (const std::vector<std::uint8_t>& bytes) noexcept override
    {
        const auto s = parse (bytes);
        return s && valid && incoming.offer (*s);
    }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (! valid)
            return; // not an EQUO state: pass the signal through
        if (incoming.take (next))
        {
            state.table = next.table;
            state.target = next.target;
            engine.setBands (state);
            if (next.sendTrack != state.sendTrack)
            {
                state.sendTrack = next.sendTrack; // the editor's send track (kept with EffectSlot::sendTracks[0])
                engine.setKnobs (state);
            }
        }
        if (hqRender() != rendering)
        {
            rendering = hqRender();
            engine.prepare (rate, rendering);
            engine.set (state);
        }
        if (! haveParams || p != lastParams)
        {
            lastParams = p, haveParams = true;
            auto s = state;
            applyParams (p, s);
            if (s.knob != state.knob || s.bandwidth != state.bandwidth || s.oversample != state.oversample
                || s.addMode != state.addMode || s.sendOnly != state.sendOnly)
            {
                state = s;
                engine.setKnobs (state);
            }
        }
        if (bpm > 0)
            engine.setSamplesPerTick (rate * 60.0 / (bpm * 96.0)); // FL's default 96 ticks per beat (not confirmed)
        engine.process (left, right, n, sendLeft, sendRight);
    }

    // the send's destination insert (EffectSlot::sendTracks[0]), or null = not routed this block
    void setSendOutput (float* l, float* r) noexcept override { sendLeft = l, sendRight = r; }

private:
    State state, next;
    StateHandoff<State> incoming;
    bool valid = false, rendering = false, haveParams = false;
    double rate = 44100.0;
    float *sendLeft = nullptr, *sendRight = nullptr;
    EffectParams lastParams {};
    Engine engine;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState)
{
    return std::make_unique<EquoEffect> (flState);
}
} // namespace lynx::equofx
