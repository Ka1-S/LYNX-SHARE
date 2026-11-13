#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Denormals.h"
#include "core/PocketTape.h"

// Pocket Comb Filter: FL Studio Mobile's Comb Filter (module 40), rebuilt from docs/analise/REA_rodada190by.md
// (clean-room: FLM's laws restated as formulas, no Image-Line code or data). Single precision like FLM, except where
// FLM itself uses doubles (the tape's phase and speed).

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

// =================================================================================================================
// Comb Filter (FLM module 40)
// =================================================================================================================

// The pitch detector of the Detect Pitch mode: the louder of mid / one side, a 1250 Hz one-pole low-pass, blocks of
// 2048 samples; per block the cumulative-mean-normalised squared-difference function over a 1024-sample window
// (lags 0…1023, by FFT), its first dip below 0.15, a parabola through the dip, then a filter against outliers.
class CombDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        meterB = std::exp (-1.0f / (0.1f * fs));
        lowK = (float) std::exp (-twoPi * 1250.0 / sampleRate);
        averageK = std::exp (-4.0f * (float) block / fs);
        for (int i = 0; i < block; ++i)
        {
            const auto a = -twoPi * (double) i / (double) block;
            cosTable[(std::size_t) i] = (float) std::cos (a);
            sinTable[(std::size_t) i] = (float) std::sin (a);
        }
        for (int i = 0; i < block; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            reversed[(std::size_t) i] = (std::uint16_t) r;
        }
        reset();
    }
    void reset() noexcept
    {
        msL = msR = msMid = msSide = 0.0f;
        first = true;
        selector = 0;
        low = 0.0f;
        fill = 0;
        mean = 0.0f;
        meanSet = false;
        ringA = ringB = {};
        atA = atB = 0;
        output = 0.0f;
    }
    // one input sample; the detected frequency in Hz (held between blocks; 0 until the first one)
    float tick (float l, float r) noexcept
    {
        const auto b = first ? 0.0f : meterB, a = 1.0f - b;
        first = false;
        const auto mid = 0.5f * (l + r), side = 0.5f * (l - r);
        msL = a * l * l + b * msL;
        msR = a * r * r + b * msR;
        msMid = a * mid * mid + b * msMid;
        msSide = a * side * side + b * msSide;
        auto x = mid;
        if (std::sqrt (msSide) > 1.5f * std::sqrt (msMid))
        {
            // one side, kept while it is the louder one or the other is at most 1.2 times louder
            const auto rl = std::sqrt (msL), rr = std::sqrt (msR);
            const auto current = selector > 0 ? rr : rl, other = selector > 0 ? rl : rr;
            if (! (current >= other || other <= 1.2f * current))
                selector = (int) (rr - rl);
            x = selector > 0 ? r : l;
        }
        low = lowK * low + (1.0f - lowK) * x;
        if (low > -1e-20f && low < 1e-20f)
            low = 0.0f;
        samples[(std::size_t) fill++] = low;
        if (fill == block)
        {
            fill = 0;
            analyse();
        }
        return output;
    }
    // (tests) the period of a full block, in samples; 0 = none
    float periodOf (const float* x) noexcept
    {
        std::copy (x, x + block, samples.begin());
        return period();
    }

private:
    static constexpr int block = 2048, half = 1024, bits = 11;

    void fft (std::array<float, block>& re, std::array<float, block>& im, bool inverse) noexcept
    {
        for (int i = 0; i < block; ++i)
            if (const int j = reversed[(std::size_t) i]; j > i)
                std::swap (re[(std::size_t) i], re[(std::size_t) j]), std::swap (im[(std::size_t) i], im[(std::size_t) j]);
        for (int len = 2; len <= block; len <<= 1)
        {
            const int step = block / len, h = len / 2;
            for (int i = 0; i < block; i += len)
                for (int k = 0; k < h; ++k)
                {
                    const auto c = cosTable[(std::size_t) (k * step)];
                    const auto s = inverse ? -sinTable[(std::size_t) (k * step)] : sinTable[(std::size_t) (k * step)];
                    const auto a = (std::size_t) (i + k), b = (std::size_t) (i + k + h);
                    const auto tr = re[b] * c - im[b] * s, ti = re[b] * s + im[b] * c;
                    re[b] = re[a] - tr, im[b] = im[a] - ti;
                    re[a] += tr, im[a] += ti;
                }
        }
    }
    float period() noexcept
    {
        // d(τ) = Σ x[j]² + Σ x[j + τ]² − 2 Σ x[j]·x[j + τ], j = 0…1023
        for (int i = 0; i < block; ++i)
        {
            aRe[(std::size_t) i] = samples[(std::size_t) i];
            bRe[(std::size_t) i] = i < half ? samples[(std::size_t) i] : 0.0f;
            aIm[(std::size_t) i] = bIm[(std::size_t) i] = 0.0f;
        }
        fft (aRe, aIm, false);
        fft (bRe, bIm, false);
        for (int i = 0; i < block; ++i)
        {
            const auto k = (std::size_t) i;
            const auto re = aRe[k] * bRe[k] + aIm[k] * bIm[k], im = aIm[k] * bRe[k] - aRe[k] * bIm[k]; // X · conj(K)
            aRe[k] = re, aIm[k] = im;
        }
        fft (aRe, aIm, true);
        double energy0 = 0.0;
        for (int j = 0; j < half; ++j)
            energy0 += (double) samples[(std::size_t) j] * (double) samples[(std::size_t) j];
        auto energy = energy0;
        double running = 0.0;
        yin[0] = 1.0f;
        int found = -1;
        for (int tau = 1; tau < half; ++tau)
        {
            energy += (double) samples[(std::size_t) (tau + half - 1)] * (double) samples[(std::size_t) (tau + half - 1)]
                      - (double) samples[(std::size_t) (tau - 1)] * (double) samples[(std::size_t) (tau - 1)];
            const auto d = std::max (0.0, energy0 + energy - 2.0 * (double) aRe[(std::size_t) tau] / (double) block);
            running += d;
            yin[(std::size_t) tau] = running > 0.0 ? (float) (d * (double) tau / running) : 1.0f;
            // (tested three lags behind the one being filled)
            if (const int at = tau - 3; tau > 4 && yin[(std::size_t) at] < 0.15f && yin[(std::size_t) at] < yin[(std::size_t) (at + 1)])
            {
                found = at;
                break;
            }
        }
        if (found < 0)
        {
            found = 0;
            for (int tau = 1; tau < half; ++tau)
                if (yin[(std::size_t) tau] < yin[(std::size_t) found])
                    found = tau;
        }
        if (found <= 0 || found >= half - 1)
            return (float) found;
        const auto s0 = yin[(std::size_t) (found - 1)], s1 = yin[(std::size_t) found], s2 = yin[(std::size_t) (found + 1)];
        const auto bend = s0 - 2.0f * s1 + s2;
        return bend != 0.0f ? (float) found + 0.5f * (s0 - s2) / bend : (float) found;
    }
    void analyse() noexcept
    {
        double sum = 0.0;
        for (const auto v : samples)
            sum += (double) v * (double) v;
        if (! (10.0 * std::log10 (sum / (double) block + 1e-300) > -50.0))
            return; // too quiet: the last frequency stays
        const auto tau = period();
        const auto f0 = tau > 0.0f ? fs / tau : 0.0f;
        // against outliers: a slow average of the detections; a value three times off it is dropped, a jump of an
        // octave or more must be confirmed by the last three raw values
        mean = meanSet ? f0 * f0 * (1.0f - averageK) + averageK * mean : f0 * f0;
        meanSet = true;
        const auto average = std::sqrt (mean);
        const auto ratio = f0 > 0.0f && average > 0.0f ? std::max (f0 / average, average / f0) : 3.0f;
        if (f0 <= 0.0f || ratio >= 3.0f)
        {
            output = ringB[(std::size_t) ((atB + 2) % 3)];
            return;
        }
        ringA[(std::size_t) atA] = f0, atA = (atA + 1) % 3;
        const auto meanOf = [] (const std::array<float, 3>& r) { return (r[0] + r[1] + r[2]) / 3.0f; };
        const auto mb = meanOf (ringB), ma = meanOf (ringA);
        const auto near = [] (float a, float b) { return std::min (a, b) / std::max (a, b); };
        if (mb != 0.0f && near (mb, f0) <= 0.5f)
        {
            if (ma != 0.0f && near (mb, ma) < 0.5f)
                ringB[(std::size_t) atB] = f0, atB = (atB + 1) % 3;
            output = meanOf (ringB);
        }
        else
        {
            ringB[(std::size_t) atB] = f0, atB = (atB + 1) % 3;
            output = f0;
        }
    }

    float fs = 44100.0f, meterB = 0.0f, lowK = 0.0f, averageK = 0.0f;
    float msL = 0.0f, msR = 0.0f, msMid = 0.0f, msSide = 0.0f, low = 0.0f, mean = 0.0f, output = 0.0f;
    bool first = true, meanSet = false;
    int selector = 0, fill = 0, atA = 0, atB = 0;
    std::array<float, 3> ringA {}, ringB {};
    std::array<float, block> samples {}, aRe {}, aIm {}, bRe {}, bIm {}, cosTable {}, sinTable {};
    std::array<float, half> yin {};
    std::array<std::uint16_t, block> reversed {};
};

class Comb final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const auto n = std::max ((int) fs, 4);
        for (auto& t : tapes)
            t.allocate (n);
        detector.prepare (fs);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& t : tapes)
        {
            t.clear();
            t.setDelay (0.5);
        }
        detector.reset();
        glide = { 0.0f, 0.0f };
        snapNote = -1;
    }
    void setHeldNotes (const NoteMask* channels, int count) noexcept override
    {
        held = channels;
        heldCount = count;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (tapes[0].w.points.empty())
            prepare (fs);
        ScopedFlushDenormals flush;
        const auto mode = combMode (p[0]);
        const auto fb = p[2], mix = p[3];
        const auto octaves = mode != 0 ? combOffsetOctaves (p[4]) : 0;
        const auto c = combGlide (p[5]);
        const auto polarity = combPolarity (p[7]);
        const auto asymmetry = (p[8] - 0.5f) * 0.1f;
        tapes[0].fb = (polarity & 1) != 0 ? -fb : fb;
        tapes[1].fb = (polarity & 2) != 0 ? -fb : fb;
        float target = 0.0f;
        if (mode == 0)
            target = p[6] != 0.0f ? combNoteHz (combNote (p[9], p[10])) : combFreeHz (p[1]);
        else if (mode == 1)
        {
            // the lowest key held on the Channel Rack; the last one while none is held. The remembered key is the same
            // field the Pitch oct / Pitch note pair writes: before any key is held (and after either is moved) it is
            // that note (measured: a module with no key held plays E3 at the defaults, REA_rodada190by.md §2.3)
            if (const auto note = combNote (p[9], p[10]); note != snapNote)
                lowestKey = snapNote = note;
            for (int k = 0, found = 0; k < 128 && found == 0; ++k)
                for (int ch = 0; held != nullptr && ch < heldCount; ++ch)
                    if ((held[ch][(std::size_t) (k >> 6)] >> (k & 63)) & 1ull)
                    {
                        lowestKey = k;
                        found = 1;
                        break;
                    }
            target = combNoteHz (lowestKey);
        }
        const auto scale = std::ldexp (1.0f, octaves);
        for (int i = 0; i < n; ++i)
        {
            const auto inL = left[i], inR = right != nullptr ? right[i] : left[i];
            if (mode == 2)
                target = detector.tick (inL, inR);
            const auto f = std::clamp (mode != 0 ? target * scale : target, 1.0f, 20000.0f);
            const float goal[2] = { f * (1.0f - asymmetry), f * (1.0f + asymmetry) };
            const float in[2] = { inL, inR };
            float wet[2];
            for (std::size_t ch = 0; ch < 2; ++ch)
            {
                glide[ch] -= (glide[ch] - goal[ch]) * c;
                tapes[ch].setDelay (fs / ((double) glide[ch] * fs)); // (the period in seconds × the rate: 1 / frequency)
                wet[ch] = tapes[ch].tick (in[ch]);
            }
            left[i] = inL + mix * wet[0];
            if (right != nullptr)
                right[i] = inR + mix * wet[1];
        }
    }

private:
    double fs = 44100.0;
    std::array<tape::Tape, 2> tapes;
    std::array<float, 2> glide { 0.0f, 0.0f };
    CombDetector detector;
    const NoteMask* held = nullptr;
    int heldCount = 0, lowestKey = 48, snapNote = -1;
};

// =================================================================================================================
// Multiband Compressor (FLM module 36)
// =================================================================================================================

// FLM's parameter glide: 1 % of the way per sample, the first value taken as it is
struct Glide
{
    float s = 0.0f;
    bool set = false;
    float next (float target) noexcept
    {
        if (! set)
            s = target, set = true;
        else
            s += (target - s) * 0.01f;
        return s;
    }
    void reset() noexcept { set = false; }
};

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    float tick (float x) noexcept
    {
        auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (y > -1e-20f && y < 1e-20f)
            y = 0.0f;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
    void clear() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    void copy (const Biquad& o) noexcept { b0 = o.b0, b1 = o.b1, b2 = o.b2, a1 = o.a1, a2 = o.a2; }
};

// one crossover: a low-pass and a high-pass path per side, each two equal 12 dB sections at Q 0.707
struct Crossover
{
    Biquad lp[2][2], hp[2][2]; // [side][section]
    void design (float hz, float fs) noexcept
    {
        const auto w = (float) twoPi * hz / fs;
        const auto c = std::cos (w), alpha = std::sin (w) / (2.0f * 0.707f), norm = 1.0f / (1.0f + alpha);
        Biquad l, h;
        l.b0 = l.b2 = (1.0f - c) * 0.5f * norm, l.b1 = (1.0f - c) * norm;
        h.b0 = h.b2 = (1.0f + c) * 0.5f * norm, h.b1 = -(1.0f + c) * norm;
        l.a1 = h.a1 = -2.0f * c * norm, l.a2 = h.a2 = (1.0f - alpha) * norm;
        for (auto& side : lp)
            for (auto& q : side)
                q.copy (l);
        for (auto& side : hp)
            for (auto& q : side)
                q.copy (h);
    }
    void clear() noexcept
    {
        for (auto* path : { &lp, &hp })
            for (auto& side : *path)
                for (auto& q : side)
                    q.clear();
    }
    void tick (std::size_t side, float x, float& low, float& high) noexcept
    {
        low = lp[side][1].tick (lp[side][0].tick (x));
        high = hp[side][1].tick (hp[side][0].tick (x));
    }
};

struct MultibandBand
{
    Glide gin, gout, dry, width;
    float e1 = 0.0f, e2 = 0.0f;
    bool envelopeSet = false;
    void reset() noexcept
    {
        gin.reset(), gout.reset(), dry.reset(), width.reset();
        e1 = e2 = 0.0f;
        envelopeSet = false;
    }
};

class Multiband final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
        lowHz = highHz = -1.0f;
        reset();
    }
    void reset() noexcept override
    {
        for (auto& x : cross)
            x.clear();
        for (auto& b : bands)
            b.reset();
        master.reset();
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        ScopedFlushDenormals ftz;
        if (const auto f = multibandCrossoverHz (p[1]); f != lowHz)
            cross[0].design (lowHz = f, fs);
        if (const auto f = multibandCrossoverHz (p[2]); f != highHz)
        {
            highHz = f;
            cross[1].design (f, fs);
            cross[2].design (f, fs);
        }
        const auto masterTarget = multibandGain (p[0]);
        const auto dryTarget = p[4] * p[4];
        const auto solo = multibandSolo (p[3]);
        const auto mode = (int) (p[29] + 0.5f);
        struct Settings
        {
            float gin, thr, slope, ca, cr, gout, width;
            int state;
        } set[3];
        for (int j = 0; j < 3; ++j)
        {
            const auto* b = &p[(std::size_t) (5 + 7 * j)];
            const auto t = multibandTransfer (b[1], b[2]);
            set[j].gin = multibandGain (b[0]);
            set[j].thr = t.threshold;
            set[j].slope = t.slope;
            set[j].ca = multibandAttackCoefficient (b[3]);
            set[j].cr = multibandReleaseCoefficient (b[4]);
            set[j].state = std::clamp ((int) (2.0f * b[6]), 0, 2);
            set[j].gout = set[j].state == 2 || (solo != 0 && solo != j + 1) ? 0.0f : multibandGain (b[5]);
            set[j].width = p[(std::size_t) (26 + j)];
        }
        for (int i = 0; i < n; ++i)
        {
            float in[2] = { left[i], right != nullptr ? right[i] : left[i] };
            float band[3][2];
            for (std::size_t side = 0; side < 2; ++side)
            {
                float lowRaw, rest, ll, lh;
                cross[0].tick (side, in[side], lowRaw, rest);
                cross[1].tick (side, rest, band[1][side], band[2][side]);
                cross[2].tick (side, lowRaw, ll, lh);
                band[0][side] = ll + lh; // (the low band through the high crossover as an all-pass)
            }
            float sum[2] = { 0.0f, 0.0f };
            for (int j = 0; j < 3; ++j)
            {
                auto& b = bands[(std::size_t) j];
                const auto& q = set[j];
                auto l = band[j][0], r = band[j][1];
                if (q.state != 1) // (a bypassed band passes its split signal as it is)
                {
                    const auto gin = b.gin.next (q.gin), gout = b.gout.next (q.gout);
                    const auto d = std::max (std::fabs (gin * l), std::fabs (gin * r)) * gin;
                    b.e1 = d > b.e1 ? d : b.e1 - b.e1 * q.cr;
                    if (! b.envelopeSet)
                        b.e2 = b.e1, b.envelopeSet = true;
                    else
                        b.e2 += (b.e1 - b.e2) * q.ca;
                    const auto g = b.e2 >= q.thr && b.e2 > 0.0f ? (q.thr + (b.e2 - q.thr) * q.slope) / b.e2 : 1.0f;
                    if (! b.dry.set)
                        b.dry.next (dryTarget);
                    const auto k = (b.dry.s + g) * gin * gout;
                    l *= k, r *= k;
                    if (mode == 0)
                    {
                        auto w = b.width.next (q.width);
                        if (std::fabs (w - q.width) < 0.001f)
                            w = b.width.s = q.width;
                        const auto a = std::clamp (2.0f * w, 0.0f, 1.0f), m = std::clamp (2.0f * (1.0f - w), 0.0f, 1.0f);
                        const auto s = a * (l - r), mid = m * (l + r);
                        l = (mid + s) * 0.5f, r = (mid - s) * 0.5f;
                    }
                    else if (q.width <= 0.5f)
                    {
                        const auto mid = (l + r) * 0.5f, s = q.width * (l - r);
                        l = mid + s, r = mid - s;
                    }
                    else
                    {
                        const auto c = q.width - 0.5f;
                        l = l - c * r;
                        r = r - c * l;
                    }
                    b.dry.next (dryTarget);
                }
                sum[0] += l, sum[1] += r;
            }
            const auto g = master.next (masterTarget);
            left[i] = sum[0] * g;
            if (right != nullptr)
                right[i] = sum[1] * g;
        }
    }

private:
    float fs = 44100.0f, lowHz = -1.0f, highHz = -1.0f;
    Crossover cross[3];
    std::array<MultibandBand, 3> bands;
    Glide master;
};

// =================================================================================================================
// Tape Stop (FLM module 42)
// =================================================================================================================

class TapeStop final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = (float) (sampleRate > 0.0 ? sampleRate : 44100.0);
        std::size_t size = 4;
        while ((double) size < 5.0 * (double) fs)
            size <<= 1;
        for (auto& b : buffer)
            b.assign (size, 0.0f);
        mask = size - 1;
        decayA = 1.0f - 44.1f / fs;
        fadeC = std::exp (-(float) twoPi / (0.005f * fs));
        primed = false;
        reset();
    }
    void reset() noexcept override
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);
        write = 0;
        readPos = 0.0f;
        pos = len = 0;
        u = 1.0f, du = 0.0f;
        posB = 0, lenB = (int) (0.1f * fs);
        x = 0.0f, dx = lenB > 0 ? 1.0f / (float) lenB : 0.0f;
        last = decay = { 0.0f, 0.0f };
        pending = false;
        fade = 0.0f;
    }
    void setPlaying (bool playing) noexcept override { transport = playing; }
    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (buffer[0].empty())
            prepare (fs);
        const auto on = p[4] > 0.5f;
        if (! primed)
            playing = on, primed = true; // (a new module is in the state it is given: no ramp)
        if (! transport)
            return; // FLM's module only runs with the song: the audio passes, nothing is recorded
        ScopedFlushDenormals ftz;
        if (on != playing)
        {
            const auto synced = p[6] > 0.5f;
            const auto busy = pos != len;
            const auto tStart = (synced ? tapeStopSyncedSeconds (p[3], bpm) : tapeStopSeconds (p[1])) * (busy ? 1.0f - u : 1.0f);
            const auto tStop = (synced ? tapeStopSyncedSeconds (p[2], bpm) : tapeStopSeconds (p[0])) * (busy ? u : 1.0f);
            const auto from = busy ? u : (on ? 0.0f : 1.0f), to = on ? 1.0f : 0.0f;
            len = (int) ((on ? tStart : tStop) * fs);
            pos = 0;
            u = from;
            du = len > 0 ? (to - from) / (float) len : 0.0f;
            if (on)
            {
                lenB = (int) (std::min (tStart, 0.1f) * fs);
                posB = 0;
                x = 0.0f;
                dx = lenB > 0 ? 1.0f / (float) lenB : 0.0f;
            }
            playing = on;
        }
        const auto base = tapeStopBase (p[5]);
        const auto size = (float) (mask + 1);
        const auto tail = (int) (0.1f * fs);
        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { left[i], right != nullptr ? right[i] : left[i] };
            buffer[0][write] = in[0];
            buffer[1][write] = in[1];
            write = (write + 1) & mask;
            float out[2];
            if (pos == len)
            {
                if (playing)
                    out[0] = last[0] = in[0], out[1] = last[1] = in[1];
                else
                {
                    if (pending)
                        decay = last, pending = false;
                    for (std::size_t ch = 0; ch < 2; ++ch)
                    {
                        decay[ch] *= decayA;
                        last[ch] = decay[ch];
                        decay[ch] *= decayA;
                        if (decay[ch] > -1e-20f && decay[ch] < 1e-20f)
                            decay[ch] = 0.0f;
                        out[ch] = decay[ch];
                    }
                    fade = 0.0f;
                }
                readPos = (float) write;
            }
            else
            {
                const auto i0 = (std::size_t) readPos & mask, i1 = (i0 + 1) & mask;
                const auto frac = readPos - (float) (std::size_t) readPos;
                float tape[2] = { buffer[0][i0] + (buffer[0][i1] - buffer[0][i0]) * frac, buffer[1][i0] + (buffer[1][i1] - buffer[1][i0]) * frac };
                ++pos;
                const auto speed = tapeStopSpeed (base, u);
                u += du;
                if (playing)
                {
                    fade = (1.0f - fadeC) + fadeC * fade;
                    tape[0] *= fade, tape[1] *= fade;
                }
                float xf = 0.0f;
                if (len - pos <= tail)
                {
                    if (posB < lenB)
                        xf = x, x += dx, ++posB;
                    else
                        xf = 1.0f;
                }
                for (std::size_t ch = 0; ch < 2; ++ch)
                    out[ch] = last[ch] = playing ? xf * in[ch] + (1.0f - xf) * tape[ch] : tape[ch];
                pending = true;
                readPos += speed;
                if (readPos >= size)
                    readPos -= size;
            }
            left[i] = out[0];
            if (right != nullptr)
                right[i] = out[1];
        }
    }

private:
    float fs = 44100.0f, decayA = 0.999f, fadeC = 0.0f;
    std::array<std::vector<float>, 2> buffer;
    std::size_t mask = 0, write = 0;
    float readPos = 0.0f, u = 1.0f, du = 0.0f, x = 0.0f, dx = 0.0f, fade = 0.0f;
    int pos = 0, len = 0, posB = 0, lenB = 0;
    std::array<float, 2> last { 0.0f, 0.0f }, decay { 0.0f, 0.0f };
    bool playing = true, primed = false, pending = false, transport = true;
};
} // namespace

int combMode (float v) noexcept { return std::clamp ((int) (2.0f * v), 0, 2); }
float combFreeHz (float v) noexcept { return v * v * v * v * 2499.0f + 1.0f; }
int combNote (float octave, float note) noexcept { return 12 * (int) std::round (7.0f * octave) + (int) std::round (11.0f * note); }
float combNoteHz (int note) noexcept { return note >= 1 ? 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f) : 0.0f; }
int combOffsetOctaves (float v) noexcept { return (int) std::round ((v - 0.5f) * 4.0f); }
float combGlide (float v) noexcept { return (1.0f - v * v) * 0.1999f + 0.0001f; }
int combPolarity (float v) noexcept { return std::clamp ((int) (3.0f * v), 0, 3); }
double combDelaySamples (double hz, double sampleRate) noexcept
{
    return (double) (std::max ((int) sampleRate, 4) - 2) / hz + 3.0;
}
const char* combModeName (int mode) noexcept
{
    static const char* const names[] = { "Manual", "Lowest Note", "Detect Pitch" };
    return names[std::clamp (mode, 0, 2)];
}
float combDetectPeriod (const float* block2048, double sampleRate)
{
    auto d = std::make_unique<CombDetector>();
    d->prepare (sampleRate);
    return d->periodOf (block2048);
}

std::unique_ptr<Effect> makeComb() { return std::make_unique<Comb>(); }

float multibandGain (float v) noexcept { return std::pow (10.0f, (48.0f * v - 24.0f) / 20.0f); }
float multibandCrossoverHz (float v) noexcept { return 20.0f * std::pow (2.0f, 9.0f * v); }
MultibandTransfer multibandTransfer (float threshold, float ratio) noexcept
{
    MultibandTransfer t;
    t.thresholdDb = -48.0f * (1.0f - threshold);
    t.threshold = std::pow (10.0f, t.thresholdDb / 20.0f);
    t.ratio = 30.0f * ratio + 1.0f;
    t.slope = 1.0f / t.ratio;
    return t;
}
float multibandOutput (const MultibandTransfer& t, float level) noexcept
{
    return level >= t.threshold ? t.threshold + (level - t.threshold) * t.slope : level;
}
float multibandAttackSeconds (float v) noexcept { return 0.05f * v * v; }
float multibandReleaseSeconds (float v) noexcept { return 0.99f * v + 0.01f; }
// (FLM computes the ballistics for 44.1 kHz whatever the rate it runs at)
float multibandAttackCoefficient (float v) noexcept
{
    const auto t = multibandAttackSeconds (v);
    return t < 1e-6f ? 1.0f : 1.0f - std::pow (0.1f, 1.0f / (t * 44100.0f));
}
float multibandReleaseCoefficient (float v) noexcept { return 1.0f - std::pow (0.1f, 1.0f / (multibandReleaseSeconds (v) * 44100.0f)); }
int multibandSolo (float v) noexcept { return std::clamp ((int) (3.0f * v + 0.5f), 0, 3); }

std::unique_ptr<Effect> makeMultiband() { return std::make_unique<Multiband>(); }

float tapeStopSeconds (float v) noexcept { return std::max (5.0f * v, 0.005f); }
int tapeStopSyncedStep (float v) noexcept { return std::clamp ((int) std::round (9.0f * v), 0, 9); }
float tapeStopSyncedSeconds (float v, double bpm) noexcept
{
    static const float secondsTimesBpm[] = { 0.0f, 15.0f, 22.5f, 30.0f, 45.0f, 60.0f, 120.0f, 240.0f, 480.0f, 960.0f };
    const auto whole = (int) bpm; // (FLM reads the tempo as a whole number)
    return whole > 0 ? std::max (secondsTimesBpm[tapeStopSyncedStep (v)] / (float) whole, 0.005f) : 0.005f;
}
float tapeStopBase (float warp) noexcept
{
    const auto b = 2.0f * warp - 1.0f;
    const auto k = std::fabs (b) >= 0.005f ? 1.0f - std::pow (200.0f, b) : 0.005f;
    return 1.0f / (1.0f - k);
}
float tapeStopSpeed (float base, float u) noexcept { return (std::pow (base, u) - 1.0f) / (base - 1.0f); }

std::unique_ptr<Effect> makeTapeStop() { return std::make_unique<TapeStop>(); }

std::string tapeStopParamText (int param, float v)
{
    static const char* const steps[] = { "Off", "1/4 beat", "3/8 beat", "1/2 beat", "3/4 beat", "1 beat", "2 beats", "4 beats", "8 beats", "16 beats" };
    switch (param)
    {
        case 0: case 1:
        {
            const auto ms = std::round (5000.0f * v);
            return ms <= 0.0f ? "Off" : ms < 1000.0f ? format ("%.0f ms", (double) ms) : format ("%.2f s", (double) ms * 0.001);
        }
        case 2: case 3: return steps[tapeStopSyncedStep (v)];
        case 5: return format ("%.0f%%", (double) std::round ((2.0f * v - 1.0f) * 100.0f));
        default: return v > 0.5f ? "On" : "Off";
    }
}

std::string multibandParamText (int param, float v)
{
    const auto hz = [] (float f) { return f >= 1000.0f ? format ("%.1f kHz", (double) f * 0.001) : format ("%.1f Hz", (double) f); };
    if (param == 0)
        return format ("%.1f dB", (double) (48.0f * v - 24.0f));
    if (param == 1 || param == 2)
        return hz (multibandCrossoverHz (v));
    if (param == 3)
    {
        static const char* const names[] = { "Off", "Low", "Mid", "High" };
        return names[multibandSolo (v)];
    }
    if (param == 4)
        return format ("%.0f%%", (double) (100.0f * v * v));
    if (param >= 5 && param < 26)
        switch ((param - 5) % 7)
        {
            case 0: return format ("%.1f dB", (double) (48.0f * v - 24.0f));
            case 1: return format ("%.1f dB", (double) (-48.0f * (1.0f - v)));
            case 2: return format ("%.1f", (double) (30.0f * v + 1.0f));
            case 3: return format ("%.1f ms", (double) (1000.0f * multibandAttackSeconds (v)));
            case 4: return format ("%.1f ms", (double) (1000.0f * multibandReleaseSeconds (v)));
            case 5: return format ("%.1f dB", (double) ((2.0f * v - 1.0f) * 24.0f));
            default:
            {
                static const char* const names[] = { "On", "Bypass", "Mute" };
                return names[std::clamp ((int) (2.0f * v), 0, 2)];
            }
        }
    if (param >= 26 && param < 29)
        return format ("%.0f%%", (double) (200.0f * v));
    return (int) (v + 0.5f) != 0 ? "Mode 2" : "Mode 1";
}

std::string combParamText (int param, float v)
{
    static const char* const notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* const polarities[] = { "Both +", "Left -", "Right -", "Both -" };
    switch (param)
    {
        case 0: return combModeName (combMode (v));
        case 1:
        {
            const auto hz = (double) combFreeHz (v);
            return hz >= 1000.0 ? format ("%.2f kHz", hz * 0.001) : format ("%.1f Hz", hz);
        }
        case 4:
        {
            const auto o = combOffsetOctaves (v);
            return format (o > 0 ? "+%.0f Oct" : "%.0f Oct", (double) o);
        }
        case 6: return v != 0.0f ? "On" : "Off";
        case 7: return polarities[combPolarity (v)];
        case 8: return format ("%.1f %%", (double) ((v - 0.5f) * 10.0f));
        case 9: return format ("%.0f", (double) std::round (7.0f * v - 1.0f));
        case 10: return notes[std::clamp ((int) std::round (11.0f * v), 0, 11)];
        default: return format ("%.0f %%", (double) (100.0f * v));
    }
}
} // namespace lynx::pocket
