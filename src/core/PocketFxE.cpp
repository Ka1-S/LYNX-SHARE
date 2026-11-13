#include "core/PocketFx.h"

// Pocket EQ, Pocket Graphic EQ and Pocket Filter: FL Studio Mobile's Equalizer ("Param EQ", module 3), Graphic EQ
// (module 7) and Filter (module 9), rebuilt in round 190dw from FLM's laws (the study notes are
// /mnt/e/lynx-work/flm190dw/rea/spec-eqfilter.md; summary in docs/analise/FLM_FORMAT.md §7). Clean-room: the formulas
// and the processing order restated in our own words, no Image-Line code or data.
//
// FLM runs these modules as small four-lane filter pipelines in single precision, and the pipelines are part of the
// sound: a band hands its output to the next one a step later, so the Equalizer is 1.5 samples late, the Graphic EQ 8
// and the Filter one sample per pair of biquads. FLM reports no latency for them and neither does LYNX (a .flm mix
// was made with these delays in it). Every value below is computed in the precision FLM uses; contraction into FMAs
// stays off so the results match the module's sample for sample.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace lynx::pocket
{
namespace
{
constexpr double tiny = 1.0e-30; // added to every input sample before the filters (keeps the feedback out of denormals)

float flushSmall (float c) noexcept { return c < 1e-09f && c > -1e-09f ? 0.0f : c; }

std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}

// FLM's module wrapper stops calling a module once its input has been silent for longer than the module's tail
// (Equalizer and Graphic EQ 10 s, Filter 6 s), lets the buffer through untouched, and clears the module's state when
// the input comes back. A freshly prepared module starts in the stopped state.
struct SilenceGate
{
    long long count = 0, tail = 441000;
    bool pendingReset = false;

    void prepare (double fs, double seconds) noexcept
    {
        tail = (long long) (fs * seconds);
        count = tail + 1;
        pendingReset = false;
    }
    // true = run the module on this block; clear = the module's state must be cleared first
    bool run (const float* l, const float* r, int n, bool& clear) noexcept
    {
        clear = pendingReset;
        pendingReset = false;
        bool active = false;
        for (int i = 0; i < n && ! active; ++i)
            active = l[i] != 0.0f || (r != nullptr && r[i] != 0.0f);
        if (active)
        {
            count = 0;
            return true;
        }
        if (count > tail)
            return false;
        count += n;
        if (count <= tail)
            return true;
        pendingReset = true;
        return false;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The band design FLM's Equalizer and Graphic EQ share: the cookbook's shelf / peak forms with the bandwidth in octaves,
// but with the amplitude A = 10^(dB/20) where the cookbook has 10^(dB/40) (a band moves twice the dB its knob shows).
// Coefficients are normalised by a0 and anything below 1e-9 in size becomes 0.
struct Coefs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

Coefs designBand (int kind, float hz, float db, float octaves, float rate) noexcept
{
    const float A = std::pow (10.0f, db * 0.05f);
    float w = (hz * 6.283185f) / rate;
    if (w >= 3.0f)
        w = 3.0f;
    if (w <= 0.0f)
        w = 0.0f;
    const float s = std::sin (w);
    const double sh = std::sinh (((double) octaves * (double) 0.3465736f * (double) w) / (double) s);
    const float twoSh = (float) (sh + sh);
    const float k = std::sqrt (A) * twoSh;
    Coefs c;
    float a0;
    if (kind == 0) // low shelf
    {
        const float cs = std::cos (w);
        const float twoRootAlpha = s * k;
        const float am = A - 1.0f, ap = A + 1.0f;
        const float m = ap - am * cs, p = am * cs + ap, apc = ap * cs;
        a0 = twoRootAlpha + p;
        c.b2 = (m - twoRootAlpha) * A;
        c.b0 = (m + twoRootAlpha) * A;
        c.a1 = am * -2.0f - (apc + apc);
        c.b1 = (A + A) * (am - apc);
        c.a2 = p - s * k;
    }
    else if (kind == 1) // peak
    {
        const float alpha = s * twoSh * 0.5f;
        const float alphaA = alpha * A, alphaOverA = alpha / A;
        a0 = alphaOverA + 1.0f;
        c.b0 = alphaA + 1.0f;
        const float cs = std::cos (w);
        c.a1 = cs * -2.0f;
        c.b1 = cs * -2.0f;
        c.a2 = 1.0f - alphaOverA;
        c.b2 = 1.0f - alphaA;
    }
    else // high shelf
    {
        const float cs = std::cos (w);
        const float twoRootAlpha = s * k;
        const float am = A - 1.0f, ap = A + 1.0f;
        const float p = am * cs + ap, m = ap - am * cs;
        a0 = m + twoRootAlpha;
        c.b0 = (twoRootAlpha + p) * A;
        const float q = am - ap * cs;
        c.b2 = (p - twoRootAlpha) * A;
        c.b1 = (ap * cs + am) * A * -2.0f;
        c.a1 = q + q;
        c.a2 = m - s * k;
    }
    const float inv = 1.0f / a0;
    c.b0 = flushSmall (inv * c.b0);
    c.b1 = flushSmall (inv * c.b1);
    c.b2 = flushSmall (inv * c.b2);
    c.a1 = flushSmall (inv * c.a1);
    c.a2 = flushSmall (inv * c.a2);
    return c;
}

// one Direct-Form-I step in FLM's order of operations
inline float biquad (const Coefs& c, float x0, float x1, float x2, float y1, float y2) noexcept
{
    return ((x0 * c.b0 + x1 * c.b1 + c.b2 * x2) - c.a1 * y1) - c.a2 * y2;
}

// ---------------------------------------------------------------------------------------------------------------
// Equalizer (FLM module 3). Four bands; band b: gain (v − ½)·24 dB as shown (twice that applied), frequency
// 2^(9.25v + 5) Hz, bandwidth 7v + ¼ octaves (at least 0.0001), kind ⌊2v⌋ (low shelf, peak, high shelf), channel ⌊4v⌋
// (both; left only; right only; left + / right −; left − / right +). The bands are designed at twice the running rate
// and run 2× oversampled: each input sample is fed twice (held), every second output is kept. The four bands are one
// pipeline: the input enters band 4, and each band takes the output its neighbour produced one half-sample earlier.
class Equalizer final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        gate.prepare (fs, 10.0);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& ch : st)
            ch = {};
        dirty = true;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        for (int k = 0; k < eqParams; ++k)
            if (p[(std::size_t) k] != last[(std::size_t) k])
                dirty = true;
        bool clear = false;
        const bool on = gate.run (left, right, n, clear);
        if (clear)
            for (auto& ch : st)
                ch = {};
        if (! on)
            return;
        if (dirty)
            design (p);
        for (int i = 0; i < n; ++i)
        {
            const auto xl = (float) ((double) left[i] + tiny);
            const auto xr = (float) ((double) (right != nullptr ? right[i] : left[i]) + tiny);
            step (st[0], coef[0], xl);
            step (st[1], coef[1], xr);
            left[i] = step (st[0], coef[0], xl);
            const auto yr = step (st[1], coef[1], xr);
            if (right != nullptr)
                right[i] = yr;
        }
    }

    // lane k = band k; the pipeline runs from lane 3 (the input) down to lane 0 (the output)
    struct Lanes
    {
        std::array<float, 4> x1 {}, x2 {}, y1 {}, y2 {};
    };

    static float step (Lanes& s, const std::array<Coefs, 4>& c, float x) noexcept
    {
        std::array<float, 4> in { s.y1[1], s.y1[2], s.y1[3], x }, y {};
        for (int k = 0; k < 4; ++k)
            y[(std::size_t) k] = biquad (c[(std::size_t) k], in[(std::size_t) k], s.x1[(std::size_t) k], s.x2[(std::size_t) k],
                                         s.y1[(std::size_t) k], s.y2[(std::size_t) k]);
        s.x2 = s.x1;
        s.x1 = in;
        s.y2 = s.y1;
        s.y1 = y;
        return y[0];
    }

    void design (const EffectParams& p) noexcept
    {
        dirty = false;
        for (int k = 0; k < eqParams; ++k)
            last[(std::size_t) k] = p[(std::size_t) k];
        const auto rate = (float) (fs + fs);
        for (int b = 0; b < 4; ++b)
        {
            const auto kind = std::clamp ((int) (p[(std::size_t) (4 * b + 3)] * 2.0f), 0, 2);
            auto hz = (float) std::pow (2.0, (double) p[(std::size_t) (4 * b + 1)] * 9.25 + 5.0);
            if (hz <= 0.0f)
                hz = 0.0f;
            auto oct = p[(std::size_t) (4 * b + 2)] * 7.0f + 0.25f;
            if (oct <= 0.0001f)
                oct = 0.0001f;
            const auto db = (p[(std::size_t) (4 * b)] - 0.5f) * 24.0f;
            const auto mode = (int) (p[(std::size_t) (16 + b)] * 4.0f);
            float gl = 1.0f, gr = 1.0f;
            switch (mode)
            {
                case 1: gr = 0.0f; break;
                case 2: gl = 0.0f; break;
                case 3: gr = -1.0f; break;
                case 4: gl = -1.0f; break;
                default: break;
            }
            coef[0][(std::size_t) b] = designBand (kind, hz, db * gl, oct, rate);
            coef[1][(std::size_t) b] = designBand (kind, hz, db * gr, oct, rate);
        }
    }

private:
    double fs = 44100.0;
    std::array<Lanes, 2> st {};
    std::array<std::array<Coefs, 4>, 2> coef {};
    std::array<float, eqParams> last {};
    bool dirty = true;
    SilenceGate gate;
};

// ---------------------------------------------------------------------------------------------------------------
// Graphic EQ (FLM module 7): eight bands at 80, 160, 375, 750, 1500, 3000, 6000 and 12000 Hz — a low shelf, six peaks
// and a high shelf, one octave wide, 24v − 12 dB as shown (twice that applied), designed at the running rate. FLM
// runs them as four pairs; inside a pair the second band takes the first band's output from two samples earlier, the
// pairs follow each other within the sample: 8 samples of delay in all.
constexpr float graphicCentres[8] { 80.0f, 160.0f, 375.0f, 750.0f, 1500.0f, 3000.0f, 6000.0f, 12000.0f };

class GraphicEq final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        gate.prepare (fs, 10.0);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& ch : st)
            ch = {};
        dirty = true;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        for (int k = 0; k < graphicEqParams; ++k)
            if (p[(std::size_t) k] != last[(std::size_t) k])
                dirty = true;
        bool clear = false;
        const bool on = gate.run (left, right, n, clear);
        if (clear)
            for (auto& ch : st)
                ch = {};
        if (! on)
            return;
        if (dirty)
        {
            dirty = false;
            for (int b = 0; b < 8; ++b)
            {
                last[(std::size_t) b] = p[(std::size_t) b];
                const auto kind = b == 0 ? 0 : b == 7 ? 2 : 1;
                coef[(std::size_t) b] = designBand (kind, graphicCentres[b], p[(std::size_t) b] * 24.0f - 12.0f, 1.0f, (float) fs);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const auto xl = (float) ((double) left[i] + tiny);
            const auto xr = (float) ((double) (right != nullptr ? right[i] : left[i]) + tiny);
            left[i] = run (st[0], xl);
            const auto yr = run (st[1], xr);
            if (right != nullptr)
                right[i] = yr;
        }
    }

    // per pair g: lane 0 = band 2g, lane 1 = band 2g + 1
    struct Pair
    {
        std::array<float, 2> x1 {}, x2 {}, y1 {}, y2 {};
    };

    float run (std::array<Pair, 4>& s, float x) noexcept
    {
        for (int g = 0; g < 4; ++g)
        {
            auto& q = s[(std::size_t) g];
            const auto in1 = q.y2[0]; // the first band's output two samples back
            const auto y0 = biquad (coef[(std::size_t) (2 * g)], x, q.x1[0], q.x2[0], q.y1[0], q.y2[0]);
            const auto y1 = biquad (coef[(std::size_t) (2 * g + 1)], in1, q.x1[1], q.x2[1], q.y1[1], q.y2[1]);
            q.x2 = q.x1;
            q.x1 = { x, in1 };
            q.y2 = q.y1;
            q.y1 = { y0, y1 };
            x = y1;
        }
        return x;
    }

private:
    double fs = 44100.0;
    std::array<std::array<Pair, 4>, 2> st {};
    std::array<Coefs, 8> coef {};
    std::array<float, graphicEqParams> last {};
    bool dirty = true;
    SilenceGate gate;
};

// ---------------------------------------------------------------------------------------------------------------
// Filter (FLM module 9): frequency 2^(10.3v + 4) Hz, Q = 2v² + 0.8, type ⌊2v⌋ (low-pass, band-pass, high-pass), slope
// ⌊2v⌋ = 1, 2 or 4 pairs of identical biquads (24 / 48 / 96 dB per octave). Low / high-pass: feedback at α = sin ω/Q,
// feed-forward (1 ∓ cos ω)/(2 + sin ω/Q) on the zeros 1, ±2, 1 (the cookbook's filter at Q/2 with its level offset
// (1 + s/Q)/(1 + s/2Q)); the low-pass output is × 0.8^pairs. Band-pass: the cookbook's constant-skirt form in double
// precision (peak level Q). Inside a pair the second biquad takes the first one's output of the sample before; the pairs
// follow each other within the sample. The level 0.8^pairs is only recomputed with the coefficients (a Frequency, Q or
// Type change), as FLM does.
class Filter final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        gate.prepare (fs, 6.0);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& ch : st)
            ch = {};
        dirty = true;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (p[0] != last[0] || p[1] != last[1] || p[2] != last[2])
            dirty = true;
        const auto slope = (int) (p[3] + p[3]);
        if (slope == 0)
            pairs = 1;
        else if (slope == 1)
            pairs = 2;
        else if (slope == 2)
            pairs = 4;
        bool clear = false;
        const bool on = gate.run (left, right, n, clear);
        if (clear)
            for (auto& ch : st)
                ch = {};
        if (! on)
            return;
        if (dirty)
            design (p);
        for (int i = 0; i < n; ++i)
        {
            const auto xl = (float) ((double) left[i] + tiny);
            const auto xr = (float) ((double) (right != nullptr ? right[i] : left[i]) + tiny);
            left[i] = run (st[0], xl) * level;
            const auto yr = run (st[1], xr) * level;
            if (right != nullptr)
                right[i] = yr;
        }
    }

    struct Pair
    {
        std::array<float, 2> x1 {}, x2 {}, y1 {}, y2 {};
    };

    float run (std::array<Pair, 4>& s, float x) const noexcept
    {
        for (int g = 0; g < pairs; ++g)
        {
            auto& q = s[(std::size_t) g];
            const std::array<float, 2> in { x, q.y1[0] };
            std::array<float, 2> y {};
            for (int k = 0; k < 2; ++k)
            {
                const auto j = (std::size_t) k;
                if (type == 1)
                    y[j] = ((in[j] - q.x2[j]) * c.b0 + c.a1 * q.y1[j]) - c.a2 * q.y2[j];
                else
                    y[j] = ((zero * q.x1[j] + in[j] + q.x2[j]) * c.b0 - c.a1 * q.y1[j]) - c.a2 * q.y2[j];
            }
            q.x2 = q.x1;
            q.x1 = in;
            q.y2 = q.y1;
            q.y1 = y;
            x = y[1];
        }
        return x;
    }

    void design (const EffectParams& p) noexcept
    {
        dirty = false;
        for (int k = 0; k < 3; ++k)
            last[(std::size_t) k] = p[(std::size_t) k];
        const auto hz = (float) std::pow (2.0, (double) p[0] * 10.3 + 4.0);
        const auto q = p[1] * p[1] + p[1] * p[1] + 0.8f;
        type = (int) (p[2] + p[2]);
        const auto rate = (float) fs;
        if (type == 1)
        {
            const double w = ((double) hz * 6.283185307179586) / (double) rate;
            const double s = std::sin (w), cs = std::cos (w);
            const double inv = 1.0 / ((0.5 / (double) q) * s + 1.0);
            c.b0 = flushSmall ((float) ((double) (float) (s * 0.5) * inv));
            c.a1 = flushSmall ((float) ((double) (float) (cs + cs) * inv));
            c.a2 = flushSmall ((float) ((double) (float) (1.0 - s / (double) (q + q)) * inv));
            level = 1.0f;
        }
        else if (type == 0 || type == 2)
        {
            const float w = (hz * 6.2831855f) / rate;
            const float s = std::sin (w), cs = std::cos (w);
            const float inv = 1.0f / (s + q);
            c.b0 = flushSmall ((type == 0 ? 1.0f - cs : cs + 1.0f) / (s / q + 2.0f));
            c.a1 = flushSmall (cs * inv * q * -2.0f);
            c.a2 = flushSmall ((q - s) * inv);
            zero = type == 0 ? 2.0f : -2.0f;
            level = type == 0 ? (float) std::pow ((double) 0.8f, (double) pairs) : 1.0f;
        }
    }

private:
    double fs = 44100.0;
    std::array<std::array<Pair, 4>, 2> st {};
    Coefs c;
    float zero = 2.0f, level = 0.8f;
    int type = 0, pairs = 2;
    std::array<float, 3> last { -1.0f, -1.0f, -1.0f };
    bool dirty = true;
    SilenceGate gate;
};
} // namespace

std::unique_ptr<Effect> makeEq() { return std::make_unique<Equalizer>(); }
std::unique_ptr<Effect> makeGraphicEq() { return std::make_unique<GraphicEq>(); }
std::unique_ptr<Effect> makeFilter() { return std::make_unique<Filter>(); }

int eqKind (float v) noexcept { return std::clamp ((int) (v * 2.0f), 0, 2); }
int eqChannelMode (float v) noexcept { return std::clamp ((int) (v * 4.0f), 0, 4); }
double eqHz (float v) noexcept { return std::pow (2.0, (double) v * 9.25 + 5.0); }
double eqOctaves (float v) noexcept { return std::max ((double) (v * 7.0f + 0.25f), 0.0001); }
double filterHzOf (float v) noexcept { return std::pow (2.0, (double) v * 10.3 + 4.0); }
double filterQOf (float v) noexcept { return (double) (v * v + v * v + 0.8f); }
int filterType (float v) noexcept { return std::clamp ((int) (v + v), 0, 2); }
int filterPairs (float v) noexcept
{
    const auto s = (int) (v + v);
    return s <= 0 ? 1 : s == 1 ? 2 : 4;
}

std::array<double, 5> eqBandCoefficients (int kind, double hz, double shownDb, double octaves, double rate) noexcept
{
    const auto c = designBand (kind, (float) hz, (float) shownDb, (float) octaves, (float) rate);
    return { c.b0, c.b1, c.b2, c.a1, c.a2 };
}

namespace
{
std::string hzText (double hz) { return hz < 1000.0 ? format ("%.1f Hz", hz) : format ("%.2f kHz", hz * 0.001); }
} // namespace

std::string eqParamText (int param, float v)
{
    if (param < 0 || param >= eqParams)
        return {};
    if (param >= 16)
    {
        static const char* const names[] { "Both", "Left", "Right", "Left + / right -", "Left - / right +" };
        return names[eqChannelMode (v)];
    }
    switch (param % 4)
    {
        case 0: return format ("%.1f dB", (double) (v * 24.0f - 12.0f));
        case 1: return hzText (eqHz (v));
        case 2: return format ("%.2f oct", eqOctaves (v));
        default:
        {
            static const char* const names[] { "Low shelf", "Peak", "High shelf" };
            return names[eqKind (v)];
        }
    }
}

std::string graphicEqParamText (int param, float v)
{
    if (param < 0 || param >= graphicEqParams)
        return {};
    return format ("%.1f dB", (double) (v * 24.0f - 12.0f));
}

std::string filterParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.1f Hz", filterHzOf (v));
        case 1: return format ("%.2f", filterQOf (v));
        case 2:
        {
            static const char* const names[] { "LP", "BP", "HP" };
            return names[filterType (v)];
        }
        case 3:
        {
            static const char* const names[] { "24 dB/O", "48 dB/O", "96 dB/O" };
            const auto p = filterPairs (v);
            return names[p == 1 ? 0 : p == 2 ? 1 : 2];
        }
        default: return {};
    }
}
} // namespace lynx::pocket
