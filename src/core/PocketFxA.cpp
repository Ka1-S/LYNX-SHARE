#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/PocketGrid.h"

// Pocket Autoduck, Chorus, Spacer and Stereoizer: FL Studio Mobile's modules 29, 11, 34 and 15, rebuilt from FLM's laws
// as read in round 189h (REA_rodada182c.md "F5 effects, round 189h"). Clean-room: the formulas in our own words, no
// Image-Line code. Single precision where FLM computes in single precision.

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
float flush (float x) noexcept { return x > -1e-15f && x < 1e-15f ? 0.0f : x; }

// ---------------------------------------------------------------------------------------------------------------
// Autoduck (FLM module 29): a gain curve over every beat of the song. The beat phase comes from the song position at the
// start of each call (FL calls a hosted FLM module once per song tick, core/PocketGrid.h) and moves on by BPM/60/fs per
// sample inside it. Per sample: g = 1 − env(phase)·√Ducking, then a one-pole of 0.01 per sample.
class Autoduck final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }
    void reset() noexcept override
    {
        ph = 0.0;
        gPrev = -1.0f;
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }
    void setPlaying (bool on) noexcept override { playing = on; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        take (p);
        auto dep = ducking;
        if (dep != 0.0f)
            dep = std::sqrt (dep);
        const auto inc = 0.016666666666666666 / fs;
        const auto block = grid.begin (playing ? songBeats : -1.0, bpm, fs, n);
        block.forEach ([&] (int at, int m, double beats, bool continuing) {
            if (beats >= 0.0 && ! continuing)
                ph = beats - (double) (int) beats;
            for (int i = at; i < at + m; ++i)
            {
                auto g = 1.0f - env ((float) ph) * dep;
                ph = inc * bpm + ph;
                if (ph >= 1.0)
                    ph -= 1.0;
                if (gPrev >= 0.0f)
                    g = flush ((g - gPrev) * 0.01f + gPrev);
                gPrev = g;
                left[i] *= g;
                if (right != nullptr)
                    right[i] *= g;
            }
        });
    }

    void take (const EffectParams& p) noexcept
    {
        ducking = p[0], attack = p[1], curve = p[2], time = p[3], shift = p[4], hold = p[5];
        shape = (int) (p[6] * 5.0f);
    }
    // the duck envelope at beat phase x: a rise over Attack, a hold, a fall to the end of Time, then nothing
    float env (float pos) const noexcept
    {
        const auto a = (float) ((double) (attack * 0.5f) + 0.001);
        const auto len = std::fmax (0.001f, time);
        const auto h = hold * len;
        auto x = pos - (shift - 0.5f);
        if (x < 0.0f)
            x += 1.0f;
        else if (x > 1.0f)
            x += -1.0f;
        float e;
        if (x < a)
            e = x / a;
        else if (x < len)
            e = x < a + h ? 1.0f : 1.0f - ((x - a) - h) / ((len - a) - h);
        else
            e = 0.0f;
        if (e >= 1.0f)
            e = 1.0f;
        if (e <= 0.0f)
            return 0.0f;
        const auto c = curve;
        if (x < a || shape == 0)
            return std::pow (e, (c + 1.0f) * 0.5f);
        switch (shape)
        {
            case 1: return std::pow (e, c * 0.9f + 0.1f);
            case 2: return std::pow (e, (1.0f - c) + (1.0f - c) + 1.0f);
            case 3: return std::pow (e, (1.0f - c) * 5.0f + 1.0f);
            case 4:
            case 5:
            {
                const auto q = (shape == 4 ? 2.0f : 5.0f) * (1.0f - c) + 1.0f;
                const auto upper = e >= 0.5f;
                if (upper)
                    e = 1.0f - e;
                const auto r = (double) std::pow (e, q) * std::pow (0.5, (double) (1.0f - q));
                return upper ? 1.0f - (float) r : (float) r;
            }
            default: return e;
        }
    }

private:
    double fs = 44100.0, ph = 0.0, songBeats = -1.0;
    bool playing = true;
    float gPrev = -1.0f;
    float ducking = 0.501f, attack = 0.1f, curve = 0.501f, time = 1.0f, shift = 0.5f, hold = 0.0f;
    int shape = 0;
    TickGrid grid;
};

// ---------------------------------------------------------------------------------------------------------------
// Chorus (FLM module 11): one 10 ms ring of the mono sum L + R and two taps on two sines (LFO 2 runs 0.1 Hz faster).
// Tap 1 sits at N·d·u² samples into the ring, u = sin·d + ½; tap 2 (as FLM runs it) cross-fades between the current sum
// and the one N − 1 samples old. The dry stays; L += (t1 + t2·(1 − Stereo))·Mix², R += (t2 + t1·(1 − Stereo))·Mix².
class Chorus final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        size = std::max (2, (int) (fs * 0.01));
        buf.assign ((std::size_t) size, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        ph1 = 0.62f;
        ph2 = 0.1f;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (buf.empty())
            prepare (fs);
        const auto rate = p[0] * p[0] * 4.0f + 0.2f;
        const auto d = p[1] * 0.35999998f + 0.040000003f;
        const auto cross = 1.0f - p[2];
        const auto mix = p[3] * p[3];
        const auto inc1 = (float) ((double) rate * (1.0 / fs));
        const auto inc2 = (float) ((double) (rate + 0.1f) * (1.0 / fs));
        const auto len = (float) size;
        float* const data = buf.data();
        const auto at = [&] (int i) noexcept { return i >= size ? i - size : i; };
        for (int i = 0; i < n; ++i)
        {
            if (w >= size)
                w = 0;
            const auto l = left[i], r = right != nullptr ? right[i] : left[i];
            data[w] = l + r;
            const auto u = (float) (std::sin (twoPi * (double) ph1) * (double) d + 0.5);
            const auto pos = len * d * u * u;
            const auto k = (int) pos;
            const auto f = pos - (float) k;
            auto i0 = at (w + k);
            const auto t1 = (1.0f - f) * data[i0] + f * data[at (i0 + 1)];
            const auto u2 = (float) (std::sin (twoPi * (double) ph2) * (double) d + 0.5);
            const auto k2 = (int) u2;
            const auto f2 = u2 - (float) k2;
            i0 = at (w + k2);
            const auto t2 = (1.0f - f2) * data[i0] + f2 * data[at (i0 + 1)];
            ph1 += inc1;
            ph2 += inc2;
            ph1 = ph1 - (float) (int) ph1;
            ph2 = ph2 - (float) (int) ph2;
            left[i] = l + (t1 + t2 * cross) * mix;
            if (right != nullptr)
                right[i] = r + (t2 + t1 * cross) * mix;
            ++w;
        }
    }

private:
    double fs = 44100.0;
    int size = 441, w = 0;
    float ph1 = 0.62f, ph2 = 0.1f;
    std::vector<float> buf;
};

// ---------------------------------------------------------------------------------------------------------------
// Spacer (FLM module 34): an L/R delay (one side, up to 300 ms, cubic law), L/R inversion, separation (narrow above ½,
// widen below), pan, then an M/S (or, in Input M/S mode, L/R) pair with its own delay (up to 100 ms, fifth-power law)
// and inversion, decoded back to L/R (or left as M/S in Output M/S mode). The four continuous controls glide by 0.1 % per
// sample.
class Spacer final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        if (rate != fs || ring[0].empty())
        {
            fs = rate;
            maxLR = (float) fs * 0.3f;
            maxMS = (float) fs * 0.1f;
            const auto pow2 = [] (float v) { return (std::size_t) std::pow (2.0, std::ceil (std::log2 ((double) std::max (v, 4.0f)))); };
            for (int c = 0; c < 4; ++c)
                ring[(std::size_t) c].assign (pow2 (c < 2 ? maxLR : maxMS), 0.0f);
            maskLR = (int) ring[0].size() - 1;
            maskMS = (int) ring[2].size() - 1;
        }
        reset();
    }
    // FLM clears only part of a ring when it resets at the same rate; LYNX clears it all (no stale echo after a stop).
    // The glides are FLM's: never reset.
    void reset() noexcept override
    {
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.0f);
        wLR = wMS = 0;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (ring[0].empty())
            prepare (fs);
        if (right == nullptr)
            return;
        const float target[4] { p[0], p[1], p[2], p[4] }; // L/R delay, pan, separation, M/S delay
        const auto invLR = (int) std::round (3.0f * p[3]), invMS = (int) std::round (3.0f * p[5]);
        const auto mode = (int) std::round (2.0f * p[6]);
        float* const rl = ring[0].data();
        float* const rr = ring[1].data();
        float* const ra = ring[2].data();
        float* const rb = ring[3].data();
        for (int i = 0; i < n; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                auto& s = smooth[(std::size_t) k];
                s = (target[k] - s) * 0.001f + s;
                if (std::abs (target[k] - s) < 1e-9f)
                    s = target[k];
            }
            const auto x = ((smooth[0] - 0.5f) + smooth[0]) - 0.5f;
            const auto c = x * x * x;
            rl[wLR] = left[i];
            rr[wLR] = right[i];
            wLR = (wLR + 1) & maskLR;
            auto ld = read (rl, maskLR, wLR, maxLR, maxLR * c);
            auto rd = read (rr, maskLR, wLR, maxLR, -(maxLR * c));
            if ((invLR & 1) != 0)
                ld = -ld;
            if ((invLR & 2) != 0)
                rd = -rd;
            const auto sep = smooth[2];
            if (sep >= 0.5f)
            {
                const auto m = (rd + ld) * 0.5f, dif = ld - rd;
                const auto t = dif * 0.5f - (dif * sep - dif * 0.5f);
                ld = m + t;
                rd = m - t;
            }
            else
            {
                const auto k = 0.5f - sep;
                ld = ld - k * rd;
                rd = rd - k * ld;
            }
            const auto pan = smooth[1];
            if (pan != 0.5f)
            {
                ld *= std::fmin (1.0f, (1.0f - pan) * 2.0f);
                rd *= std::fmin (1.0f, pan * 2.0f);
            }
            float a, b;
            if (mode != 2)
            {
                a = (ld + rd) * 0.5f;
                b = (ld - rd) * 0.5f;
            }
            else
            {
                a = ld;
                b = rd;
            }
            if ((invMS & 1) != 0)
                a = -a;
            if ((invMS & 2) != 0)
                b = -b;
            ra[wMS] = a;
            rb[wMS] = b;
            wMS = (wMS + 1) & maskMS;
            const auto y = ((smooth[3] - 0.5f) + smooth[3]) - 0.5f;
            const auto yy = y * y;
            const auto c5 = (yy * yy) * y;
            const auto ad = read (ra, maskMS, wMS, maxMS, maxMS * c5);
            const auto bd = read (rb, maskMS, wMS, maxMS, -(maxMS * c5));
            if (mode == 1)
            {
                left[i] = ad;
                right[i] = bd;
            }
            else
            {
                left[i] = ad + bd;
                right[i] = ad - bd;
            }
        }
    }

private:
    // D samples back from the sample just written (D = 0: that sample), linear between two
    static float read (const float* r, int mask, int w, float dMax, float amount) noexcept
    {
        const auto d = std::fmax (0.0f, std::fmin (dMax, amount));
        const auto k = (int) d;
        const auto f = d - (float) k;
        return (1.0f - f) * r[(w - k - 1) & mask] + f * r[(w - k - 2) & mask];
    }

    double fs = 0.0;
    float maxLR = 13230.0f, maxMS = 4410.0f;
    std::array<std::vector<float>, 4> ring; // L, R, then the M/S pair
    int maskLR = 0, maskMS = 0, wLR = 0, wMS = 0;
    std::array<float, 4> smooth { 0.5f, 0.5f, 0.5f, 0.5f };
};

// ---------------------------------------------------------------------------------------------------------------
// Stereoizer (FLM module 15): the mono sum, delayed by 0.5…50 ms (Timbre³) and filtered by a one-pole (Focus: low-pass
// below ½, high-pass above), added to the left and taken from the right at Strength²; then one side delayed by up to
// 25 ms (L/R Delay, cubic) and a linear balance (Pan) that glides by 1 % per sample. The coefficients are worked out
// at the start of a call after a control moved; the balance's glide is decided per call (FL calls once per song tick).
class Stereoizer final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        sizeH = std::max (2, (int) (fs * 0.05000000074505806));
        sizeD = std::max (2, (int) (fs * 0.02500000037252903));
        haas.assign ((std::size_t) sizeH, 0.0f);
        for (auto& d : side)
            d.assign ((std::size_t) sizeD, 0.0f);
        last[0] = -1.0f; // coefficients again at this rate
        reset();
    }
    void reset() noexcept override
    {
        std::fill (haas.begin(), haas.end(), 0.0f);
        for (auto& d : side)
            std::fill (d.begin(), d.end(), 0.0f);
        wH = wD = 0;
        z = 0.0f;
        cL = cR = -1.0f;
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }
    void setPlaying (bool on) noexcept override { playing = on; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (haas.empty())
            prepare (fs);
        if (right == nullptr)
            return;
        update (p);
        const auto block = grid.begin (playing ? songBeats : -1.0, bpm, fs, n);
        block.forEach ([&] (int at, int m, double, bool) { run (left + at, right + at, m); });
    }

private:
    void update (const EffectParams& p) noexcept
    {
        std::array<float, stereoizerParams> now {};
        std::copy_n (p.begin(), stereoizerParams, now.begin());
        if (now == last)
            return;
        last = now;
        str = now[0] * now[0];
        haasOn = str > 0.001f;
        const auto d0 = (int) (fs * 0.0005);
        delay = (float) (sizeH - d0 - 1) * std::pow (now[1], 3.0f) + (float) d0;
        const auto f = 1.0f - now[2];
        highPass = (f + f) - 1.0f < 0.0f;
        const auto q = highPass ? 0.99f - f * 1.98f : 1.0f - (f * 1.6f - 0.8f);
        kz = q * q * q;
        gL = std::clamp (2.0f * (1.0f - now[4]), 0.0f, 1.0f);
        gR = std::clamp (now[4] * 2.0f, 0.0f, 1.0f);
        gainOn = false;
        if (gL >= 0.99f)
            gL = 1.0f;
        else
            gainOn = true;
        if (gR >= 0.99f)
            gR = 1.0f;
        else
            gainOn = true;
        const auto v = now[3] * 2.0f - 1.0f;
        const auto c = -(v * v * v);
        dL = c > 0.0f ? (float) (sizeD - 1) * c : 0.0f;
        dR = c < 0.0f ? -((float) (sizeD - 1) * c) : 0.0f;
        delayL = dL > 0.9f;
        delayR = dR > 0.9f;
    }
    static float read (const float* r, int size, int w, float d) noexcept
    {
        auto pos = (float) w - d;
        if (pos < 0.0f)
            pos += (float) size;
        const auto i = (int) pos;
        const auto j = i + 1 == size ? 0 : i + 1;
        const auto f = pos - (float) i;
        return (1.0f - f) * r[i] + f * r[j];
    }
    void run (float* left, float* right, int n) noexcept
    {
        if (cL < 0.0f)
            cL = gL;
        if (cR < 0.0f)
            cR = gR;
        const auto moving = cL != gL || cR != gR;
        float* const h = haas.data();
        float* const dl = side[0].data();
        float* const dr = side[1].data();
        for (int i = 0; i < n; ++i)
        {
            auto l = left[i], r = right[i];
            const auto sum = l + r;
            if (haasOn)
            {
                const auto x = read (h, sizeH, wH, delay);
                z = flush ((x - z) * kz + z);
                const auto s = highPass ? x - z : z;
                r = r + s * -str;
                l = l + s * str;
            }
            auto outL = delayL ? read (dl, sizeD, wD, dL) : l;
            auto outR = delayR ? read (dr, sizeD, wD, dR) : r;
            h[wH] = sum;
            if (++wH == sizeH)
                wH = 0;
            dl[wD] = l;
            dr[wD] = r;
            if (++wD == sizeD)
                wD = 0;
            if (gainOn)
            {
                if (moving)
                {
                    cL = (gL - cL) * 0.01f + cL;
                    cR = (gR - cR) * 0.01f + cR;
                }
                outL *= cL;
                outR *= cR;
            }
            left[i] = outL;
            right[i] = outR;
        }
        if (std::abs (cL - gL) < 0.001f)
            cL = gL;
        if (std::abs (cR - gR) < 0.001f)
            cR = gR;
    }

    double fs = 44100.0, songBeats = -1.0;
    bool playing = true;
    int sizeH = 2205, sizeD = 1102, wH = 0, wD = 0;
    std::vector<float> haas;
    std::array<std::vector<float>, 2> side;
    std::array<float, stereoizerParams> last { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    float str = 0.0f, delay = 0.0f, kz = 1.0f, z = 0.0f, gL = 1.0f, gR = 1.0f, cL = -1.0f, cR = -1.0f, dL = 0.0f, dR = 0.0f;
    bool haasOn = false, highPass = false, gainOn = false, delayL = false, delayR = false;
    TickGrid grid;
};
} // namespace

std::unique_ptr<Effect> makeAutoduck() { return std::make_unique<Autoduck>(); }
std::unique_ptr<Effect> makeChorus() { return std::make_unique<Chorus>(); }
std::unique_ptr<Effect> makeSpacer() { return std::make_unique<Spacer>(); }
std::unique_ptr<Effect> makeStereoizer() { return std::make_unique<Stereoizer>(); }

float autoduckEnvelope (const EffectParams& p, float phase) noexcept
{
    Autoduck a;
    a.take (p);
    return a.env (phase);
}

const char* autoduckShapeName (int shape) noexcept
{
    static const char* const names[] { "Accel", "Accel 2", "Decel", "Decel 2", "S-Curve", "S-Curve 2" };
    return names[std::clamp (shape, 0, 5)];
}

std::string autoduckHoldText (float hold, float time) { return format ("%.0f %%", (double) (std::fmax (0.001f, time) * hold * 100.0f)); }

std::string autoduckParamText (int param, float v, double bpm)
{
    switch (param)
    {
        case 0: return format ("%.0f %%", (double) (int) (v * 100.0f));
        case 1:
        {
            const auto a = (float) ((double) (0.5f * v) + 0.001);
            return format ("%.0f ms", 60000.0 / (bpm > 0.0 ? bpm : 120.0) * (double) a);
        }
        case 2: return v == 1.0f ? std::string ("Linear") : format ("%.0f %%", (double) (100.0f - 100.0f * v));
        case 3: return format ("%.0f %%", (double) (100.0f * v));
        case 4: return format ("%.0f %%", (double) (100.0f * v - 50.0f));
        case 5: return autoduckHoldText (v, 1.0f);
        case 6: return autoduckShapeName ((int) (v * 5.0f));
        default: return format ("%.0f %%", (double) (100.0f * v));
    }
}

std::string chorusParamText (int param, float v)
{
    if (param == 0)
        return format ("%.2f Hz", (double) (v * v * 4.0f + 0.2f));
    return format ("%.0f %%", (double) (100.0f * v));
}

std::string spacerParamText (int param, float v)
{
    const auto timeText = [] (double ms) {
        return ms < 1.0 ? format ("%.2f ms", ms) : ms >= 10.0 ? format ("%.0f ms", ms) : format ("%.1f ms", ms);
    };
    switch (param)
    {
        case 0:
        {
            const auto x = 2.0 * std::abs ((double) v - 0.5);
            const auto ms = x * x * x * 300.0;
            return ms < 0.01 ? std::string ("off") : timeText (ms);
        }
        case 1: return v == 0.5f ? std::string ("Center") : format ("%.0f %%", std::abs ((double) v - 0.5) * 200.0);
        case 2:
            return v == 0.5f ? std::string ("off")
                             : (v < 0.5f ? std::string ("-") : std::string ("+")) + format ("%.0f %%", std::abs ((double) v - 0.5) * 200.0);
        case 3:
        {
            static const char* const names[] { "none", "left", "right", "both" };
            return names[std::clamp ((int) std::round (3.0f * v), 0, 3)];
        }
        case 4:
        {
            const auto x = 2.0 * std::abs ((double) v - 0.5);
            const auto ms = x * x * x * x * x * 100.0;
            if (ms < 500.0 / 44100.0) // FLM: half a sample at the running rate
                return "off";
            if (ms < 0.01)
                return format ("%.2f us", ms * 1000.0);
            if (ms < 0.1)
                return format ("%.1f us", ms * 1000.0);
            if (ms < 1.0)
                return format ("%.0f us", ms * 1000.0);
            return timeText (ms);
        }
        case 5:
        {
            // FLM reads the L/R inversion here (a display slip); LYNX shows the M/S inversion's own value
            static const char* const names[] { "none", "mid", "side", "both" };
            return names[std::clamp ((int) std::round (3.0f * v), 0, 3)];
        }
        case 6:
        {
            static const char* const names[] { "Standard", "Output M/S", "Input M/S" };
            return names[std::clamp ((int) std::round (2.0f * v), 0, 2)];
        }
        default: return format ("%.0f %%", (double) (100.0f * v));
    }
}

std::string stereoizerParamText (int param, float v)
{
    if (param <= 1)
        return format ("%.0f", (double) (int) (100.0f * v));
    return format ("%.0f%%", (double) (int) (200.0f * v - 100.0f));
}
} // namespace lynx::pocket
