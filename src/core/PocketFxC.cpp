#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "core/Denormals.h"
#include "core/PocketGrid.h"
#include "core/SpreaderEngine.h"

// Pocket Waveshaper, Pocket Compressor and Pocket Spreader: FL Studio Mobile's Waveshaper (module 41), Compressor
// (module 8) and Spreader (module 44), rebuilt from REA_rodada182c.md "F5 effects, round 189h" (clean-room: FLM's laws
// restated as formulas, no Image-Line code or data). Single precision like FLM, except where FLM itself uses doubles.

namespace lynx::pocket
{
namespace
{
constexpr double twoPi = 6.283185307179586;

std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}
std::string base (float v) { return format ("%.0f", (double) (int) (v * 100.0f)); } // FLM's own text for a plain knob
std::string hzText (double hz) { return hz >= 1000.0 ? format ("%.2f kHz", hz * 0.001) : format ("%.1f Hz", hz); }
float flush (float x) noexcept { return x > -1e-9f && x < 1e-9f ? 0.0f : x; }
double frac (double x) noexcept { return x - std::floor (x); }

// =================================================================================================================
// Waveshaper (FLM module 41)
// =================================================================================================================

// FLM's one-pole parameter smoother: s ← a·s + (1 − a)·target once per sample, a = 1 − 44.1/fs, from 0
struct Smooth
{
    float s = 0.0f;
    float next (float a, float target) noexcept { return s = a * s + (1.0f - a) * target; }
};

// the High Cut / Low Cut 2-pole (Q 0.7): FLM's own normalisation (feed-forward over s/Q + 2, feedback over s + Q)
struct CutFilter
{
    float b = 0.0f, a1 = 0.0f, a2 = 0.0f, lastHz = -1.0f;
    std::array<float, 2> x1 {}, x2 {}, y1 {}, y2 {};
    void design (float hz, double fs, bool high) noexcept
    {
        if (hz == lastHz)
            return;
        lastHz = hz;
        constexpr float q = 0.7f;
        const auto w = (float) (twoPi * (double) hz / (double) (float) fs);
        const auto s = std::sin (w), c = std::cos (w);
        const auto k = 1.0f / (s + q);
        b = flush ((high ? 1.0f + c : 1.0f - c) / (s / q + 2.0f));
        a1 = flush (-2.0f * c * k * q);
        a2 = flush ((q - s) * k);
    }
    float tick (int ch, float x, bool high) noexcept
    {
        x = flush (x);
        const auto ff = high ? x - 2.0f * x1[(std::size_t) ch] + x2[(std::size_t) ch] : x + 2.0f * x1[(std::size_t) ch] + x2[(std::size_t) ch];
        const auto y = flush (b * ff - a1 * y1[(std::size_t) ch] - a2 * y2[(std::size_t) ch]);
        x2[(std::size_t) ch] = x1[(std::size_t) ch];
        x1[(std::size_t) ch] = x;
        y2[(std::size_t) ch] = y1[(std::size_t) ch];
        y1[(std::size_t) ch] = y;
        return y;
    }
};

// the EQ bands: the cookbook's bandwidth form with A = 10^(dB/20) (FLM's, twice the cookbook's dB), ω capped at 3
struct EqBand
{
    enum Type { lowShelf, peak, highShelf };
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    std::array<float, 2> x1 {}, x2 {}, y1 {}, y2 {};
    void design (Type type, float hz, float db, float bw, double fs) noexcept
    {
        const auto a = std::pow (10.0f, db * 0.05f);
        const auto w = (float) std::clamp ((double) hz * 6.283185 / fs, 0.0, 3.0);
        const auto sn = std::sin (w), cs = std::cos (w);
        const auto alpha = sn * std::sinh (0.3465736f * bw * w / sn);
        float nb0, nb1, nb2, na0, na1, na2;
        if (type == peak)
        {
            nb0 = 1.0f + alpha * a;
            nb1 = -2.0f * cs;
            nb2 = 1.0f - alpha * a;
            na0 = 1.0f + alpha / a;
            na1 = -2.0f * cs;
            na2 = 1.0f - alpha / a;
        }
        else
        {
            const auto sq = 2.0f * std::sqrt (a) * alpha, ap = a + 1.0f, am = a - 1.0f;
            if (type == lowShelf)
            {
                nb0 = a * (ap - am * cs + sq);
                nb1 = 2.0f * a * (am - ap * cs);
                nb2 = a * (ap - am * cs - sq);
                na0 = ap + am * cs + sq;
                na1 = -2.0f * (am + ap * cs);
                na2 = ap + am * cs - sq;
            }
            else
            {
                nb0 = a * (ap + am * cs + sq);
                nb1 = -2.0f * a * (am + ap * cs);
                nb2 = a * (ap + am * cs - sq);
                na0 = ap - am * cs + sq;
                na1 = 2.0f * (am - ap * cs);
                na2 = ap - am * cs - sq;
            }
        }
        b0 = flush (nb0 / na0);
        b1 = flush (nb1 / na0);
        b2 = flush (nb2 / na0);
        a1 = flush (na1 / na0);
        a2 = flush (na2 / na0);
    }
    float tick (int ch, float x) noexcept
    {
        const auto c = (std::size_t) ch;
        x = flush (x);
        const auto y = flush (b0 * x + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c]);
        x2[c] = x1[c];
        x1[c] = x;
        y2[c] = y1[c];
        y1[c] = y;
        return y;
    }
};

// Crossover's and Rectify's curve: points (x, y, curve) sorted by x between two end points at x = −1 and x = 1. Between
// two points the shape bends by the right-hand point's curve; outside the ends, the end's y.
struct Curve
{
    struct Point
    {
        float x, y, c;
    };
    std::array<Point, 9> p {};
    int n = 0;
    float eval (float x) const noexcept
    {
        int hi = 0; // the first point right of x
        while (hi < n && ! (p[(std::size_t) hi].x > x))
            ++hi;
        if (hi == 0)
            return p[0].y;
        if (hi == n)
            return p[(std::size_t) n - 1].y;
        const auto& p0 = p[(std::size_t) hi - 1];
        const auto& p1 = p[(std::size_t) hi];
        auto t = p0.x; // FLM leaves t at p0's x when both points sit on the same x
        if (p0.x != p1.x)
            t = (x - p0.x) / (p1.x - p0.x);
        const auto w = p1.c <= 0.0f ? 1.0f - std::pow (1.0f - t, p1.c + 1.0f) : std::pow (t, 1.0f - p1.c);
        return (p1.y - p0.y) * w + p0.y;
    }
};

// FLM's seven curves. Each simple one keeps an X and Y target and their own smoothed values (Xs, Ys); the two curve
// shapes smooth their point targets and write them into the points every sample.
class Shapes
{
public:
    static constexpr int count = 7;
    void prepare (double fs) noexcept { a = (float) (1.0 - 44.1 / fs); }
    void init() noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            simple[(std::size_t) k] = k == 4 ? Simple { 0.5f, 0.5f, 3.0f, 3.0f } : Simple {};
        }
        // Crossover, in x order: the ends, k5, k0, k3, k6, k2, k1, k4
        cross.n = 9;
        cross.p = { { { -1.0f, -1.0f, 0.0f }, { -0.75f, -0.5f, 0.0f }, { -0.25f, -0.1f, 0.0f }, { -0.06f, -0.0f, 0.5f }, { 0.0f, 0.0f, -0.3f },
                      { 0.06f, 0.0f, 0.3f }, { 0.25f, 0.1f, -0.3f }, { 0.75f, 0.5f, 0.0f }, { 1.0f, 1.0f, 0.0f } } };
        crossT.fill (0.0f);
        crossS.fill (0.0f);
        // Rectify: the ends (the first end's y preset to 1), k0, k1, k2
        rect.n = 5;
        rect.p = { { { -1.0f, 1.0f, 0.0f }, { -0.999f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.999f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } };
        rectT.fill (0.0f);
        rectS.fill (0.0f);
    }
    void setX (int k, float v) noexcept
    {
        auto& s = simple[(std::size_t) k];
        switch (k)
        {
            case 0:
            case 1: s.x = 2.0f * v - 1.0f; break;
            case 2:
            {
                constexpr float ca = 0.01f, cb = 0.1f;
                const auto u = v <= 0.5f ? v : 1.0f - v;
                crossT[0] = std::clamp (ca + 2.0f * (cb - ca) * u, std::min (ca, cb), std::max (ca, cb));
                crossT[1] = std::clamp (-ca + 2.0f * (-cb + ca) * u, -cb, -ca);
                const auto p = v - 1.0f;
                crossT[2] = std::clamp (0.13f - p * 0.12f, 0.13f, 0.25f);
                crossT[3] = std::clamp (-0.13f - p * -0.12f, -0.25f, -0.13f);
                break;
            }
            case 3: s.x = (2.0f - 4.0f * v) / s.ys; break;
            case 4: s.x = 0.9f * v + 0.1f; break;
            case 5:
                rectT[0] = std::clamp (4.0f * v - 1.0f, -1.0f, 1.0f);
                rectT[1] = std::clamp (2.0f * (v - 0.5f) * 0.999f - 0.999f, -0.999f, 0.0f);
                break;
            default: s.x = 0.39999998f * v + 0.6f; break;
        }
    }
    void setY (int k, float v) noexcept
    {
        auto& s = simple[(std::size_t) k];
        switch (k)
        {
            case 0: s.y = std::clamp (9.0f * v + 1.0f, 1.0f, 10.0f); break;
            case 1: s.y = 9.0f * v + 1.0f; break;
            case 2:
            {
                constexpr float cc = 0.02f, cd = 0.9f;
                const auto h = (v < 0.5f ? 0.0f : 1.0f) - 0.5f;
                const auto p = v - 0.5f;
                crossT[4] = std::clamp ((cd - cc) * p / h + cc, cc, cd);
                crossT[5] = std::clamp ((-cd + cc) * p / h - cc, -cd, -cc);
                crossT[6] = std::clamp (0.75f - p * -0.5f, 0.5f, 0.75f);
                crossT[7] = std::clamp (-0.75f - 2.0f * p * 0.25f, -0.75f, -0.5f);
                crossT[8] = std::clamp (-1.0f - 2.0f * p * 0.37f, -1.0f, -0.63f);
                crossT[9] = std::clamp (1.0f - p * -0.74f, 0.63f, 1.0f);
                crossT[10] = std::clamp (p * -0.32f, 0.0f, 0.16f);
                break;
            }
            case 3: s.y = 7.0f * v + 1.0f; break;
            case 4: s.y = 0.59999996f * v + 0.1f; break;
            case 5:
                rectT[2] = std::clamp (2.0f * (v - 0.5f) * 0.8f, -1.0f, 1.0f);
                rectT[3] = std::clamp (2.0f * (v - 0.5f) * -0.8f, -1.0f, 1.0f);
                break;
            default: s.y = v > 0.5f ? 2.0f * (v - 0.5f) * 3.97f + 0.03f : -0.03f - (v - 0.5f) * -7.94f; break;
        }
    }
    void tick (int k) noexcept
    {
        if (k == 2)
        {
            for (std::size_t i = 0; i < crossT.size(); ++i)
                crossS[i] = (1.0f - a) * crossT[i] + a * crossS[i];
            const auto c1 = [] (float v) { return std::clamp (v, -1.0f, 1.0f); };
            cross.p[5].x = c1 (crossS[0]); // k2
            cross.p[3].x = c1 (crossS[1]); // k3
            cross.p[6].x = c1 (crossS[2]); // k1
            cross.p[2].x = c1 (crossS[3]); // k0
            cross.p[5].y = c1 (crossS[4]);
            cross.p[3].y = c1 (crossS[5]);
            cross.p[7].y = c1 (crossS[6]); // k4
            cross.p[1].y = c1 (crossS[7]); // k5
            cross.p[0].y = crossS[8];      // the first end, unclamped
            cross.p[8].c = crossS[10];
            cross.p[8].y = crossS[9];
        }
        else if (k == 5)
        {
            for (std::size_t i = 0; i < rectT.size(); ++i)
                rectS[i] = (1.0f - a) * rectT[i] + a * rectS[i];
            rect.p[0].y = rectS[0];
            rect.p[1].x = std::clamp (rectS[1], -1.0f, 1.0f);
            rect.p[1].y = std::clamp (rectS[0], -1.0f, 1.0f);
            rect.p[2].c = std::clamp (rectS[2], -0.99f, 0.99f);
            rect.p[3].c = std::clamp (rectS[3], -0.99f, 0.99f);
        }
        else
        {
            auto& s = simple[(std::size_t) k];
            s.xs = a * s.xs + (1.0f - a) * s.x;
            s.ys = a * s.ys + (1.0f - a) * s.y;
        }
    }
    float process (int k, float x) const noexcept
    {
        const auto& s = simple[(std::size_t) k];
        switch (k)
        {
            case 0: return soft (x, s.ys, s.xs);
            case 1:
            {
                const auto t = std::tanh (s.ys);
                return ((std::tanh (s.ys * x - s.xs) + std::tanh (s.xs)) / t * t) / (std::tanh (std::abs (s.xs)) + 1.0f);
            }
            case 2: return cross.eval (x);
            case 3: return std::sin ((x + s.xs) * s.ys);
            case 4:
            {
                auto t = ((x + 1.0f) * 0.5f) / s.ys;
                t -= std::trunc (t);
                return t < s.xs ? 2.0f * t / s.xs - 1.0f : 2.0f * (1.0f - t) / (1.0f - s.xs) - 1.0f;
            }
            case 5: return rect.eval (x);
            default:
            {
                const auto e = std::exp (std::pow (20.0, 1.0 - (double) s.xs));
                const auto x2 = (double) (x * x);
                return (float) (std::exp (-std::abs (e * x2)) * std::sin ((double) s.ys * twoPi * (double) x) + (double) x);
            }
        }
    }

private:
    struct Simple
    {
        float x = 0.0f, xs = 0.0f, y = 1.0f, ys = 1.0f;
    };
    // Soft: k = Ys, b = Xs (at least 1e−4 away from 0); a normalised asymmetric x/√(x² + 1)
    static float soft (float x, float k, float b) noexcept
    {
        if (std::abs ((double) b) < 1e-4)
            b = 1e-4f;
        const auto sg = b > 0.0f ? 1.0f : (b < 0.0f ? -1.0f : 0.0f);
        const auto sb = b >= 0.0f ? 1.0f : -1.0f;
        const auto p = ((sg / std::abs (b) + sg) / k) * k;
        const auto d0 = (double) (sb * k - b);
        const auto an = std::abs ((double) (sb * k) / std::sqrt (d0 * d0 + 1.0));
        const auto d1 = (double) (p - b);
        const auto nn = (float) std::abs ((double) p / (std::sqrt (d1 * d1 + 1.0) * an));
        const auto d2 = (double) (k * x - b);
        return (float) ((double) (k * x) / ((double) nn * (std::sqrt (d2 * d2 + 1.0) * an)));
    }

    float a = 0.999f;
    std::array<Simple, count> simple {};
    Curve cross, rect;
    std::array<float, 11> crossT {}, crossS {};
    std::array<float, 4> rectT {}, rectS {};
};

constexpr double lfoBeats[10] { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0 };

class Waveshaper final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        a = (float) (1.0 - 44.1 / fs);
        aAuto = (float) std::exp (-2.5132741228718345 / fs);
        dcPole = (float) std::exp (-219.9114857512855 / fs);
        shapes.prepare (fs);
        if (! constructed)
        {
            // FLM builds its module once: smoothers, shapes, filters and the LFO start from their constructed state
            constructed = true;
            shapes.init();
            for (auto& s : sm)
                s = {};
            hc = lc = {};
            for (auto& e : eq)
                e = {};
            dcX = dcY = {};
            ph = 0.0;
            lfoOut = 0.0f;
            used = {};
        }
        first = true;
        grid.reset();
    }
    void reset() noexcept override
    {
        // the transport's reset is FLM's sample-rate call: only the first-block flag (filter and EQ states stay)
        first = true;
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (right == nullptr)
            return;
        ScopedFlushDenormals ftz;
        setParams (p);
        // FL calls a hosted FLM module once per song tick (1/96 beat): the LFO, X / Y and the auto-gain level are read
        // once per call (core/PocketGrid.h)
        const auto block = grid.begin (songBeats, bpm, fs, n);
        block.forEach ([&] (int at, int m, double beats, bool continuing) {
            for (int done = 0; done < m;)
            {
                const auto k = std::min (m - done, maxFrames);
                run (left + at + done, right + at + done, k, done == 0 ? beats : -1.0, continuing || done > 0, bpm);
                done += k;
            }
        });
    }

private:
    enum
    {
        sDrive,
        sPost,
        sSpread,
        sMix,
        sHighCut,
        sLowCut,
        sBass,
        sMidHz,
        sMidDb,
        sTreble,
        sLfoX,
        sLfoY,
        smoothers
    };
    static constexpr int maxFrames = 2048;

    void setParams (const EffectParams& p) noexcept
    {
        shape = std::clamp ((int) std::round (6.0f * p[0]), 0, 6);
        autoGain = p[1] != 0.0f;
        x = p[2];
        y = p[3];
        target[sDrive] = p[4];
        target[sPost] = (p[5] - 0.5f) * 36.0f;
        target[sSpread] = p[6] * p[6] * p[6] * p[6];
        target[sMix] = p[7];
        target[sHighCut] = 20.0f * std::pow (1000.0f, p[8]);
        target[sLowCut] = 20.0f * std::pow (1000.0f, p[9]);
        target[sBass] = (p[10] - 0.5f) * 36.0f;
        target[sMidHz] = 500.0f * std::pow (4.0f, p[11]);
        target[sMidDb] = (p[12] - 0.5f) * 36.0f;
        target[sTreble] = (p[13] - 0.5f) * 36.0f;
        wave = std::clamp ((int) (p[14] * 2.0f), 0, 2);
        sync = p[15] != 0.0f;
        const auto r = (int) (9.0f * p[16]);
        syncBeats = r >= 0 && r <= 9 ? lfoBeats[r] : 1.0;
        freeHz = (float) (p[17] * p[17] * p[17] * 9.9f + 0.02f);
        target[sLfoX] = p[18];
        target[sLfoY] = p[19];
    }

    // one FLM call of m ≤ 2048 frames
    void run (float* l, float* r, int m, double beats, bool continuing, double bpm) noexcept
    {
        // the LFO, then X and Y from its last value
        double inc = (double) freeHz / fs;
        if (sync)
        {
            const auto t = syncBeats * 60.0 / bpm;
            inc = t > 0.0 && bpm > 0.0 ? 1.0 / (t * fs) : 0.0;
            if (beats >= 0.0 && ! continuing && syncBeats > 0.0)
                ph = frac (beats / syncBeats);
        }
        for (int i = 0; i < m; ++i)
        {
            sm[sLfoX].next (a, target[sLfoX]);
            sm[sLfoY].next (a, target[sLfoY]);
            const auto q = frac (ph);
            lfoOut = wave == 0 ? (float) std::sin (twoPi * q) : wave == 1 ? (float) (2.0 * q - 1.0) : (q > 0.5 ? 1.0f : -1.0f);
            ph = frac (ph + inc);
        }
        shapes.setX (shape, std::clamp (x + sm[sLfoX].s * lfoOut * 0.5f, 0.0f, 1.0f));
        shapes.setY (shape, std::clamp (y + sm[sLfoY].s * lfoOut * 0.5f, 0.0f, 1.0f));

        float sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            wet[0][(std::size_t) i] = l[i];
            wet[1][(std::size_t) i] = r[i];
            sumL += l[i] * l[i];
            sumR += r[i] * r[i];
        }
        const auto rmsIn = std::sqrt (sumL / (float) m) + std::sqrt (sumR / (float) m);

        for (int i = 0; i < m; ++i)
        {
            hc.design (sm[sHighCut].next (a, target[sHighCut]), fs, false);
            auto wl = hc.tick (0, wet[0][(std::size_t) i], false), wr = hc.tick (1, wet[1][(std::size_t) i], false);
            lc.design (sm[sLowCut].next (a, target[sLowCut]), fs, true);
            wl = lc.tick (0, wl, true);
            wr = lc.tick (1, wr, true);
            const auto g = std::pow (10.0f, 2.0f * sm[sDrive].next (a, target[sDrive]));
            const auto sp = sm[sSpread].next (a, target[sSpread]);
            shapes.tick (shape);
            const auto yl = shapes.process (shape, g * wl - sp), yr = shapes.process (shape, g * wr + sp);
            dcY[0] = dcPole * dcY[0] + (yl - dcX[0]);
            dcX[0] = yl;
            dcY[1] = dcPole * dcY[1] + (yr - dcX[1]);
            dcX[1] = yr;
            wet[0][(std::size_t) i] = dcY[0];
            wet[1][(std::size_t) i] = dcY[1];
        }

        sumL = sumR = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            sumL += wet[0][(std::size_t) i] * wet[0][(std::size_t) i];
            sumR += wet[1][(std::size_t) i] * wet[1][(std::size_t) i];
        }
        const auto rmsOut = 0.5f * (std::sqrt (sumL / (float) m) + std::sqrt (sumR / (float) m));
        const auto agTarget = (rmsIn / std::max (0.06309573f, rmsOut)) * 0.5f;
        if (first)
        {
            ag1 = agTarget;
            ag2 = autoGain ? agTarget : 1.0f;
        }
        for (int i = 0; i < m; ++i)
        {
            auto v = 1.0f;
            if (autoGain)
                v = ag1 = aAuto * ag1 + (1.0f - aAuto) * agTarget;
            ag2 = a * ag2 + (1.0f - a) * v;
            wet[0][(std::size_t) i] *= ag2;
            wet[1][(std::size_t) i] *= ag2;
        }

        for (int i = 0; i < m; ++i)
        {
            const auto bass = sm[sBass].next (a, target[sBass]);
            if (std::abs (bass - used.bass) > 0.001f)
            {
                used.bass = bass;
                eq[0].design (EqBand::peak, 100.0f, bass, 3.75f, fs);
            }
            const auto midHz = sm[sMidHz].next (a, target[sMidHz]), midDb = sm[sMidDb].next (a, target[sMidDb]);
            if (std::abs (midHz - used.midHz) > 0.001f || std::abs (midDb - used.midDb) > 0.001f)
            {
                used.midHz = midHz;
                used.midDb = midDb;
                eq[1].design (EqBand::peak, std::max (midHz, 0.0f), midDb, 3.75f, fs);
            }
            const auto treble = sm[sTreble].next (a, target[sTreble]);
            if (std::abs (treble - used.treble) > 0.001f)
            {
                used.treble = treble;
                eq[2].design (EqBand::highShelf, 3300.0f, treble, 1.0f, fs);
            }
            const auto post = std::pow (10.0f, sm[sPost].next (a, target[sPost]) * 0.05f);
            const auto mix = sm[sMix].next (a, target[sMix]);
            const auto wetGain = std::sqrt (mix), dryGain = std::sqrt (1.0f - mix);
            for (int c = 0; c < 2; ++c)
            {
                auto v = wet[(std::size_t) c][(std::size_t) i];
                v = eq[2].tick (c, eq[1].tick (c, eq[0].tick (c, v)));
                v *= post;
                auto& io = c == 0 ? l[i] : r[i];
                io = wetGain * v + dryGain * io;
            }
        }
        first = false;
    }

    double fs = 44100.0, songBeats = -1.0, syncBeats = 1.0, ph = 0.0;
    float a = 0.999f, aAuto = 0.99994f, dcPole = 0.995f;
    bool constructed = false, first = true, autoGain = false, sync = false;
    int shape = 0, wave = 0;
    float x = 0.5f, y = 0.5f, freeHz = 0.02f, lfoOut = 0.0f, ag1 = 1.0f, ag2 = 1.0f;
    std::array<float, smoothers> target {};
    std::array<Smooth, smoothers> sm {};
    struct
    {
        float bass = 0.0f, midHz = 0.0f, midDb = 0.0f, treble = 0.0f;
    } used;
    Shapes shapes;
    CutFilter hc, lc;
    std::array<EqBand, 3> eq {};
    std::array<float, 2> dcX {}, dcY {};
    std::array<std::array<float, maxFrames>, 2> wet {};
    TickGrid grid;
};

// =================================================================================================================
// Compressor (FLM module 8)
// =================================================================================================================

// A peak detector with hold on max(|L|, |R|) × Input gain (the detector only), a one-pole attack on the held peak, a
// multiplicative release after the hold, and a gain curve that is linear in amplitude above the threshold, through
// (T, T) and (1, t2) where t2 is the dB ratio's output for a 0 dBFS input.
class Compressor final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        last.fill (-1.0f);
        reset();
    }
    void reset() noexcept override
    {
        hold = 0;
        peak = env = 0.0f;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        update (p);
        for (int i = 0; i < n; ++i)
        {
            const auto l = left[i], r = right != nullptr ? right[i] : l;
            const auto det = std::max (std::abs (l) * inGain, std::abs (r) * inGain);
            if (det > peak)
            {
                hold = holdN;
                peak = det;
            }
            else if (hold >= 1)
                --hold;
            else
                peak *= rel;
            env += (peak - env) * atk;
            auto g = env >= thr ? ((env - thr) * slope + thr) / env : 1.0f;
            if (autoGain)
                g *= makeup;
            g *= level;
            left[i] = l * g;
            if (right != nullptr)
                right[i] = r * g;
        }
    }

private:
    void update (const EffectParams& p) noexcept
    {
        std::array<float, compressorParams> now {};
        std::copy_n (p.begin(), compressorParams, now.begin());
        if (now == last)
            return;
        last = now;
        const auto t = compressorTransfer (now[0], now[1]);
        thr = t.threshold;
        slope = t.slope;
        makeup = t.makeup;
        const auto ta = 0.05f * now[2] * now[2];
        atk = (double) ta >= 1e-6 ? (float) (1.0 - std::pow (0.1, 1.0 / ((double) ta * fs))) : 1.0f;
        const auto tr = 0.49f * now[3] * now[3] + 0.01f;
        rel = (float) std::pow (0.1, 1.0 / ((double) tr * fs));
        autoGain = now[6] >= 0.5f;
        level = std::pow (10.0f, 2.0f * now[4] - (autoGain ? 1.0f : 0.0f));
        holdN = (int) ((double) (0.05f * now[5] * now[5]) * fs);
        inGain = std::pow (10.0f, 2.0f * now[7] - 1.0f);
    }

    double fs = 44100.0;
    std::array<float, compressorParams> last {};
    float thr = 1.0f, slope = 1.0f, makeup = 1.0f, level = 1.0f, atk = 1.0f, rel = 0.0f, inGain = 1.0f;
    bool autoGain = true;
    int holdN = 0, hold = 0;
    float peak = 0.0f, env = 0.0f;
};

// =================================================================================================================
// Spreader (FLM module 44): FL's Spreader engine behind FLM's control mapping
// =================================================================================================================

class Spreader final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        const auto rate = (int) std::lround (sampleRate > 0.0 ? sampleRate : 44100.0);
        // FLM reports the engine's longest delay as the module's latency and FL compensates it (A/B, round 189h: FL's
        // render leads by exactly these 280 samples at 44.1 kHz), although the dry and low paths are not delayed
        latency = (int) ((float) std::clamp (rate, 1, 192000) * 0.0063492064364254475f);
        engine.constants (rate);
        engine.clear(); // FLM re-inits its Faust engine on every sample-rate call: Enable fades in again from 0
    }
    void reset() noexcept override { engine.clear(); }
    int latencySamples() const noexcept override { return latency; }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (right == nullptr)
            return;
        // FLM's zones: width 2v (unclamped: past 0.5 the delay stays at its maximum and only its level rises),
        // low bypass 10·1000^v Hz, mono bass v (continuous), separation v, enable 1
        engine.width = 2.0f * p[0];
        engine.lowBypass = 10.0f * std::pow (1000.0f, p[1]);
        engine.monoBass = p[2];
        engine.separation = p[3];
        engine.enable = 1.0f;
        ScopedFlushDenormals ftz;
        for (int done = 0; done < n;)
        {
            const auto m = std::min (chunk, n - done);
            engine.compute (left + done, right + done, outL.data(), outR.data(), m);
            for (int i = 0; i < m; ++i)
            {
                // the fixed 15 kHz section diverges where it passes Nyquist (rates below ~30 kHz): keep NaN out
                left[done + i] = std::isfinite (outL[(std::size_t) i]) ? outL[(std::size_t) i] : 0.0f;
                right[done + i] = std::isfinite (outR[(std::size_t) i]) ? outR[(std::size_t) i] : 0.0f;
            }
            done += m;
        }
    }

private:
    static constexpr int chunk = 512;
    int latency = 280;
    stereofx::SpreaderEngine engine;
    std::array<float, chunk> outL {}, outR {};
};
} // namespace

// ------------------------------------------------------------------------------------------------- factories, texts

CompressorTransfer compressorTransfer (float thresh, float ratio) noexcept
{
    CompressorTransfer c;
    c.thresholdDb = (1.0f - thresh) * -40.0f;
    c.threshold = c.thresholdDb >= -60.0f ? std::pow (10.0f, c.thresholdDb * 0.05f) : 0.0f;
    c.ratio = 19.0f * ratio * ratio + 1.0f;
    if (c.ratio >= 20.0f)
    {
        c.slope = 0.0f;
        c.makeup = 1.0f / c.threshold;
        return c;
    }
    const auto d = c.thresholdDb - c.thresholdDb / c.ratio;
    const auto t2 = d >= -60.0f ? std::pow (10.0f, d * 0.05f) : 0.0f;
    const auto q = t2 - c.threshold != 0.0f ? (1.0f - c.threshold) / (t2 - c.threshold) : 1.0f;
    c.slope = 1.0f / q;
    c.makeup = 1.0f / (c.slope * (1.0f - c.threshold) + c.threshold);
    return c;
}

float waveshaperCurve (int shape, float x, float xKnob, float yKnob) noexcept
{
    Shapes s;
    s.prepare (44100.0);
    s.init();
    shape = std::clamp (shape, 0, Shapes::count - 1);
    for (int i = 0; i < 40000; ++i)
    {
        if (i % 256 == 0)
        {
            s.setX (shape, xKnob);
            s.setY (shape, yKnob);
        }
        s.tick (shape);
    }
    return s.process (shape, x);
}

std::unique_ptr<Effect> makeWaveshaper() { return std::make_unique<Waveshaper>(); }
std::unique_ptr<Effect> makeCompressor() { return std::make_unique<Compressor>(); }
std::unique_ptr<Effect> makeSpreader() { return std::make_unique<Spreader>(); }

const char* waveshaperShapeName (int shape) noexcept
{
    static const char* const names[] { "Soft", "Tanh", "Crossover", "Fold", "Octaver", "Rectify", "CrossFold" };
    return names[std::clamp (shape, 0, 6)];
}

std::string waveshaperParamText (int param, float v)
{
    switch (param)
    {
        case 0: return waveshaperShapeName ((int) std::round (6.0f * v));
        case 1:
        case 15: return v != 0.0f ? "On" : "Off";
        case 5:
        case 10:
        case 12:
        case 13: return format ("%.1f dB", (double) ((v - 0.5f) * 36.0f));
        case 8:
        case 9: return hzText ((double) (20.0f * std::pow (1000.0f, v)));
        case 11: return hzText ((double) (500.0f * std::pow (4.0f, v)));
        case 14:
        {
            static const char* const shapes[] { "Sine", "Saw", "Square" };
            return shapes[std::clamp ((int) (v * 2.0f), 0, 2)];
        }
        case 16:
        {
            static const char* const rates[] { "1/8", "1/4", "3/8", "1/2", "3/4", "beat", "2 beats", "bar", "2 bars", "4 bars" };
            return rates[std::clamp ((int) (v * 9.0f), 0, 9)];
        }
        case 17: return format ("%.2f Hz", (double) (float) (v * v * v * 9.9f + 0.02f));
        case 18:
        case 19: return format ("%.1f %%", (double) v * 100.0);
        default: return base (v);
    }
}

std::string compressorParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.1f dB", (double) ((1.0f - v) * -40.0f));
        case 1:
        {
            const auto r = 19.0f * v * v + 1.0f;
            return r == 20.0f ? "+inf" : format ("%.1f:1", (double) r);
        }
        case 2: return format ("%.1f ms", 1000.0 * (double) (0.05f * v * v));
        case 3: return format ("%.1f ms", 1000.0 * (double) (0.49f * v * v + 0.01f));
        case 4: return format ("%.1f dB", (double) (40.0f * v - 20.0f)); // with Auto gain on (the default); off, +20 dB
        case 5: return format ("%.1f ms", 1000.0 * (double) (0.05f * v * v));
        case 6: return v >= 0.5f ? "On" : "Off";
        case 7: return format ("%.1f dB", (double) (40.0f * v - 20.0f));
        case 8: return "Off"; // FLM's RMS switch: its setter always clears it
        default: return base (v);
    }
}

std::string compressorLevelText (float level, bool autoGain)
{
    return format ("%.1f dB", (double) (40.0f * level - (autoGain ? 20.0f : 0.0f)));
}

std::string spreaderParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.1f", (double) (2.0f * v));
        case 1:
        {
            const auto f = 10.0f * std::pow (1000.0f, v);
            if (f >= 1000.0f)
                return std::abs (f - 10000.0f) >= 0.1f ? format ("%.1f kHz", (double) f * 0.001) : "10 kHz";
            return format ("%1.f Hz", (double) f);
        }
        case 3:
        {
            const auto x = 200.0f * v - 100.0f;
            if (std::abs (x) < 0.1f)
                return "Original";
            return format (std::abs (x) >= 10.0f ? "%1.f %%" : "%.1f %%", (double) x);
        }
        default: return base (v);
    }
}
} // namespace lynx::pocket
