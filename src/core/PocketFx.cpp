#include "core/PocketFx.h"
#include "core/PocketTape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// FL Studio Mobile's laws, read in round 188h (REA_rodada182c.md "F5 effects, round 188h", with the addresses).

namespace lynx::pocket
{
namespace
{
std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}
std::string percent (float v) { return format ("%.0f%%", (double) v * 100.0); }

// ---------------------------------------------------------------------------------------------------------------
// Distortion (FLM module 6). Per sample and side: x = in + bias; the curve; a sample-and-hold every `crush` samples;
// × Gain; with a bias, a one-pole high-pass at 220.5/fs takes the offset back out. The Stereo knob shares the drive
// between the sides: left × min(1, 2s), right × min(1, 2(1 − s)).
class Distortion final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        dcCoef = (float) (220.5 / fs);
        reset();
    }
    void reset() noexcept override
    {
        hold = 0.0f;
        held = { 0.0f, 0.0f };
        dc = { 0.0f, 0.0f };
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        update (p);
        for (int i = 0; i < n; ++i)
        {
            float l = left[i], r = right != nullptr ? right[i] : left[i];
            l = shape (l + bias, 0);
            r = shape (r + bias, 1);
            if (hold > 0.0f)
            {
                l = held[0];
                r = held[1];
            }
            else
            {
                held = { l, r };
                hold += crush;
            }
            l *= gain;
            r *= gain;
            hold -= 1.0f;
            if (bias != 0.0f)
            {
                dc[0] = (l - dc[0]) * dcCoef + dc[0];
                l -= dc[0];
                dc[1] = (r - dc[1]) * dcCoef + dc[1];
                r -= dc[1];
            }
            left[i] = l;
            if (right != nullptr)
                right[i] = r;
        }
    }

private:
    float shape (float x, int side) const noexcept
    {
        switch (kind)
        {
            case 0: return (x * g[side]) / (std::abs (x) * k[side] + 1.0f);
            case 1: return std::tanh (x * k[side]) * g[side];
            case 2:
            {
                const auto y = x >= 0.0f ? 1.0f - std::pow (10.0f, -(x * k[side])) * (1.0f - x)
                                         : std::pow (10.0f, x * k[side]) * (x + 1.0f) - 1.0f;
                return g[side] * y;
            }
            default: return std::clamp (x * k[side], -1.0f, 1.0f);
        }
    }
    void update (const EffectParams& p) noexcept
    {
        std::array<float, distortionParams> now {};
        std::copy_n (p.begin(), distortionParams, now.begin());
        if (now == last)
            return;
        last = now;
        const auto drive = now[0];
        const auto sq = now[1] * now[1]; // FLM squares twice: (v²)² (its rounding sets where the holds fall)
        const auto c = std::clamp (sq * sq, 0.0f, 1.0f);
        crush = c * 256.0f + 1.0f;
        gain = std::pow (10.0f, now[2] * 24.0f * 0.05f - 1.2f);
        kind = distortionKind (now[4]);
        const auto fl = std::min (now[3] * 2.0f, 1.0f);
        const auto fr = std::min ((1.0f - now[3]) + (1.0f - now[3]), 1.0f);
        const auto b = (now[5] - 0.5f) + (now[5] - 0.5f);
        bias = b * b * b * b * b;
        const float f[2] { fl, fr };
        for (int s = 0; s < 2; ++s)
            switch (kind)
            {
                case 0:
                    g[s] = std::pow (10.0f, drive * f[s] * 5.0f);
                    k[s] = g[s] - 1.0f;
                    break;
                case 1:
                    k[s] = std::pow (10.0f, drive * f[s] * 4.0f) - 0.99f;
                    g[s] = 1.0f / std::tanh (k[s]);
                    break;
                case 2:
                    g[s] = 1.0f;
                    k[s] = drive * drive * 20.0f * f[s];
                    break;
                default:
                    k[s] = std::pow (10.0f, drive * drive * f[s] * 3.0f);
                    g[s] = 1.0f;
                    break;
            }
    }

    double fs = 44100.0;
    float dcCoef = 0.005f;
    std::array<float, distortionParams> last { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    int kind = 0;
    float k[2] {}, g[2] { 1.0f, 1.0f }, crush = 1.0f, gain = 1.0f, bias = 0.0f;
    float hold = 0.0f;
    std::array<float, 2> held {}, dc {};
};

// ---------------------------------------------------------------------------------------------------------------
// Limiter (legacy) (FLM module 5): a peak limiter with no look-ahead. The louder side's |x| is the level; above the
// threshold the gain drops at once to threshold/level; it then recovers over 1/rate seconds along
// g = 1 − cos²(πt/2)·(1 − g₀), rate = 100·(0.2 − 0.2v)² + 1 per second. Output = g × (output gain / threshold).
class LimiterClassic final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }
    void reset() noexcept override
    {
        t = 1.0f;
        g0 = 1.0f;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        const auto threshold = std::pow (10.0f, p[0] * 1.2f - 1.2f);
        const auto r = 0.19999999f - p[1] * 0.2f;
        const auto rate = r * r * 100.0f + 1.0f;
        const auto out = p[2] * 36.0f - 24.0f < -60.0f ? 0.0f : std::pow (10.0f, p[2] * 1.8f - 1.2f);
        const auto step = (float) ((double) rate / fs);
        const auto makeUp = out / threshold;
        for (int i = 0; i < n; ++i)
        {
            const auto level = std::max (std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
            const auto s = std::sin (t * 1.5707964f);
            auto g = 1.0f - (1.0f - s * s) * (1.0f - g0);
            if (threshold < g * level)
            {
                t = 0.0f;
                g = threshold / level;
                g0 = g;
            }
            g *= makeUp;
            left[i] *= g;
            if (right != nullptr)
                right[i] *= g;
            t = std::min (step + t, 1.0f);
        }
    }

private:
    double fs = 44100.0;
    float t = 1.0f, g0 = 1.0f;
};
// ---------------------------------------------------------------------------------------------------------------
// Limiter (FLM module 28): In Gain, a 2 ms delay line (reported as latency), the louder side's |x| after In Gain as
// the level. Above the threshold the floor g₀ = threshold/level is taken at once (from the next sample); the gain then
// rises back to 1 along g = 1 − cos(πt/2)(1 − g₀) in exactly Release seconds (a phase e from 1 down to 0, s = sin(eπ/2)
// in double precision). No make-up: the output is × Out Gain.
class Limiter final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        length = std::max (1, (int) (fs * 0.002));
        lineL.assign ((std::size_t) length, 0.0f);
        lineR.assign ((std::size_t) length, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        std::fill (lineL.begin(), lineL.end(), 0.0f);
        std::fill (lineR.begin(), lineR.end(), 0.0f);
        at = 0;
        e = 1.0f;
        g0 = 1.0f;
    }
    int latencySamples() const noexcept override { return length; }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (lineL.empty())
            prepare (fs);
        const auto gIn = std::pow (10.0f, p[0] * 2.4f - 1.2f);
        const auto gOut = std::pow (10.0f, p[1] * 2.4f - 1.2f);
        const auto r = (double) p[2];
        const auto release = (float) (r * r * r * r * 1.9 + 0.1);
        const auto thr = std::pow (10.0f, (p[3] * 24.0f - 24.0f) * 0.05f);
        const auto dec = (float) (1.0 / ((double) release * fs));
        for (int i = 0; i < n; ++i)
        {
            const auto xl = gIn * left[i], xr = gIn * (right != nullptr ? right[i] : left[i]);
            const auto level = std::max (std::abs (xl), std::abs (xr));
            float s = 0.0f;
            if (e != 0.0f)
            {
                s = (float) std::sin ((double) e * 1.5707963267948966);
                e -= dec;
                if (e <= 0.0f)
                {
                    e = 0.0f;
                    s = 0.0f;
                }
            }
            const auto g = (1.0f - g0) * (1.0f - s) + g0;
            if (thr < level * g)
            {
                e = 1.0f;
                g0 = thr / level;
            }
            const auto ol = lineL[(std::size_t) at], orr = lineR[(std::size_t) at];
            lineL[(std::size_t) at] = xl;
            lineR[(std::size_t) at] = xr;
            if (++at == length)
                at = 0;
            left[i] = ol * g * gOut;
            if (right != nullptr)
                right[i] = orr * g * gOut;
        }
    }

private:
    double fs = 44100.0;
    int length = 0, at = 0;
    std::vector<float> lineL, lineR;
    float e = 1.0f, g0 = 1.0f;
};
// ---------------------------------------------------------------------------------------------------------------
// Leveller (FLM module 20): a gain and balance trim. Gain p = v^0.6 → 72p − 60 dB (−60…+12); balance: left ×
// clamp(2(1 − pan)), right × clamp(2 pan). The gains glide to their targets by 1 % per sample (a block that starts at
// its targets doesn't move); the first block after a reset starts on them.
class Leveller final : public Effect
{
public:
    void prepare (double) noexcept override { reset(); }
    void reset() noexcept override { cl = cr = -1.0f; }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        const auto gain = std::pow (10.0f, std::pow (std::clamp (p[0], 0.0f, 1.0f), 0.6f) * 3.6f - 3.0f);
        const auto tl = gain * std::clamp (2.0f * (1.0f - p[1]), 0.0f, 1.0f);
        const auto tr = gain * std::clamp (2.0f * p[1], 0.0f, 1.0f);
        if (cl < 0.0f)
            cl = tl;
        if (cr < 0.0f)
            cr = tr;
        const auto moving = cl != tl || cr != tr;
        for (int i = 0; i < n; ++i)
        {
            if (moving)
            {
                cl += (tl - cl) * 0.01f;
                cr += (tr - cr) * 0.01f;
            }
            left[i] *= cl;
            if (right != nullptr)
                right[i] *= cr;
        }
    }

private:
    float cl = -1.0f, cr = -1.0f;
};
// ---------------------------------------------------------------------------------------------------------------
// Gate (FLM module 27). The input runs through two 256-sample rings (the output is the ring's oldest sample: 255
// samples of look-ahead); each time the ring wraps, the RMS of its 256 samples per side (dB) decides: a closed gate
// opens when a side is over the threshold (66v − 60 dB); an open one stays for Hold, then closes when both sides are
// under threshold − Asymmetry (66v² dB, not below −60). The envelope ramps linearly (Attack / Release ms) and is
// shaped by the curve of its direction (−1 = 1 + log₁₀₀₀ x, +1 = 0.001^(1 − x), read from 1024-point tables through a
// Catmull-Rom-form cubic whose "previous" point is the point itself); Mix sets the floor 10^(−3|2v − 1|) (below 0.5 the gate is inverted); the gain glides by 10 % per sample.
class Gate final : public Effect
{
public:
    Gate()
    {
        expo[0] = logc[0] = 0.0f;
        for (int i = 1; i < 1024; ++i)
        {
            const auto x = (float) i * 9.775171e-4f;
            expo[(std::size_t) i] = std::exp (-6.9077554f * (1.0f - x));
            logc[(std::size_t) i] = std::log (x) * 0.14476483f + 1.0f;
        }
    }
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }
    void reset() noexcept override
    {
        ringL.fill (0.0f);
        ringR.fill (0.0f);
        w = 0;
        open = armed = false;
        env = 0.0f;
        holdCount = 0;
        gPrev = 0.0f;
    }
    int latencySamples() const noexcept override { return ring - 1; }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        const auto thr = p[0] * 66.0f - 60.0f;
        const auto hyst = p[1] * p[1] * 66.0f;
        const auto aInc = (float) (1000.0 / ((double) (p[2] * 1000.0f) * fs));
        const auto ca = p[3] * 2.0f - 1.0f;
        const auto rDec = (float) (1000.0 / ((double) (p[4] * 1000.0f) * fs));
        const auto cr = p[5] * 2.0f - 1.0f;
        const auto m = p[6] * 2.0f - 1.0f;
        const auto floor = std::pow (10.0f, std::abs (m) * -60.0f * 0.05f);
        const auto invert = m < 0.0f;
        const auto holdN = (int) (p[7] * (float) fs);
        for (int i = 0; i < n; ++i)
        {
            if (holdCount != 0)
                --holdCount;
            ringL[(std::size_t) w] = left[i];
            ringR[(std::size_t) w] = right != nullptr ? right[i] : left[i];
            w = (w + 1) & (ring - 1);
            if (w == 0)
            {
                const auto dbL = level (ringL), dbR = level (ringR);
                if (! open)
                    open = dbL > thr || dbR > thr;
                if (open)
                {
                    if (! armed)
                    {
                        holdCount = holdN;
                        armed = true;
                    }
                    const auto close = std::max (thr - hyst, -60.0f);
                    if (holdCount == 0 && ! (dbL >= close || dbR >= close))
                        open = armed = false;
                }
            }
            float curve;
            if (open)
            {
                if (env < 1.0f)
                    env = std::min (env + aInc, 1.0f);
                curve = ca;
            }
            else
            {
                if (env > 0.0f)
                    env = std::max (env - rDec, 0.0f);
                curve = cr;
            }
            auto g = curve > 0.0f ? curve * table (expo, env) + (1.0f - curve) * env : (1.0f + curve) * env - curve * table (logc, env);
            if (invert)
                g = 1.0f - g;
            auto gain = (1.0f - floor) * g + floor;
            gain = gain * 0.1f + gPrev * 0.9f;
            gPrev = gain;
            left[i] = gain * ringL[(std::size_t) w];
            if (right != nullptr)
                right[i] = gain * ringR[(std::size_t) w];
        }
    }

private:
    static constexpr int ring = 256;
    static float level (const std::array<float, ring>& r) noexcept
    {
        float s[4] {};
        for (int k = 0; k < ring; k += 4)
            for (int j = 0; j < 4; ++j)
                s[j] += r[(std::size_t) (k + j)] * r[(std::size_t) (k + j)];
        const auto ms = ((s[0] + s[1]) + (s[2] + s[3])) / (float) ring;
        return ms > 0.0f ? 10.0f * std::log10 (ms) : -60.0f;
    }
    static float table (const std::array<float, 1024>& t, float x) noexcept
    {
        const auto pos = x * 1023.0f;
        const auto i = (int) pos;
        const auto f = pos - (float) i;
        const auto at = [&t] (int k) { return k < 0 ? 0.0f : k > 1023 ? 1.0f : t[(std::size_t) k]; };
        const auto y1 = at (i), y0 = y1, y2 = at (i + 1), y3 = at (i + 2); // FLM's cubic takes the point itself as its "previous" one
        const auto a1 = 0.5f * (y2 - y0);
        const auto a2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto a3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
        return ((a3 * f + a2) * f + a1) * f + y1;
    }

    double fs = 44100.0;
    std::array<float, 1024> expo {}, logc {};
    std::array<float, ring> ringL {}, ringR {};
    int w = 0, holdCount = 0;
    bool open = false, armed = false;
    float env = 0.0f, gPrev = 0.0f;
};
// ---------------------------------------------------------------------------------------------------------------
// Trance Delay (FLM module 16): a tempo-synced delay of a whole number of samples (1/4, 3/8, 1/2, 3/4 or 1 beat,
// up to 2 s). The loop holds fb·(input + echo), so the first echo is already × feedback; Mix² scales the echoes, the
// dry stays at unity; a one-pole in the loop is a low-pass below Filter 0.5 and a gentle high-pass above. Stereo: two
// lines; Mirror: lines of 2D and the left input delayed by D; Ping-Pong: the lines cross, with an input balance.
class TranceDelay final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        capacity = std::max (1, (int) (2.0 * fs));
        lineL.assign ((std::size_t) capacity, 0.0f);
        lineR.assign ((std::size_t) capacity, 0.0f);
        lineP.assign ((std::size_t) capacity, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        std::fill (lineL.begin(), lineL.end(), 0.0f);
        std::fill (lineR.begin(), lineR.end(), 0.0f);
        std::fill (lineP.begin(), lineP.end(), 0.0f);
        pl = pr = pp = 0;
        zl = zr = 0.0f;
        pan = -1.0f;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (lineL.empty())
            prepare (fs);
        static constexpr double beatsOf[5] { 0.25, 0.375, 0.5, 0.75, 1.0 };
        const auto k = (int) (p[0] * 4.0f);
        const auto beats = k >= 1 && k <= 4 ? beatsOf[k] : 0.25;
        const auto mode = std::clamp ((int) (p[4] * 2.0f + 0.5f), 0, 2);
        if (beats != lastBeats || mode != lastMode)
        {
            if (lastMode >= 0)
                reset(); // FLM clears its lines when Time or Mode changes
            lastBeats = beats;
            lastMode = mode;
        }
        const auto fb = p[1] * 0.99f;
        const auto wet = p[2] * p[2];
        const auto c = 2.0f * p[3];
        const auto lowPass = c <= 1.0f;
        const auto kf = lowPass ? c : (c - 1.0f) * (c - 1.0f) * (c - 1.0f) * (c - 1.0f);
        const auto d = beats * fs * (60.0 / (bpm > 0.0 ? bpm : 120.0));
        auto lenLR = mode == 2 ? (int) (d + d) : (int) d;
        auto lenP = mode == 2 ? (int) d : 0;
        lenLR = std::min (lenLR, capacity);
        lenP = std::min (lenP, capacity);
        const auto filter = [&] (float x, float& z) {
            const auto e = x + 1e-30f;
            z = z + (e - z) * kf;
            return lowPass ? z : e - z;
        };
        const auto target = p[5];
        const auto dryPan = p[6] >= 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const auto inL = left[i], inR = right != nullptr ? right[i] : left[i];
            float outL, outR;
            if (mode == 1)
            {
                if (pan < 0.0f)
                    pan = target;
                else if (pan != target)
                {
                    pan = pan + (target - pan) * 0.01f;
                    if (std::abs ((double) pan - (double) target) < 0.001)
                        pan = target;
                }
                const auto gR = std::clamp (2.0f * pan, 0.0f, 1.0f), gL = std::clamp (2.0f * (1.0f - pan), 0.0f, 1.0f);
                const auto aL = gL * inL, aR = gR * inR;
                if (pl >= lenLR)
                    pl = 0;
                const auto fl = filter (lineL[(std::size_t) pl], zl);
                outL = wet * fl + (dryPan ? aL : inL);
                if (pr >= lenLR)
                    pr = 0;
                const auto fr = filter (lineR[(std::size_t) pr], zr);
                outR = wet * fr + (dryPan ? aR : inR);
                lineR[(std::size_t) pr] = (fl + aL) * fb;
                lineL[(std::size_t) pl] = (fr + aR) * fb;
                ++pl;
                ++pr;
            }
            else
            {
                lineP[(std::size_t) pp] = inL;
                if (++pp >= lenP)
                    pp = 0;
                const auto xl = lineP[(std::size_t) pp], xr = inR;
                if (pl >= lenLR)
                    pl = 0;
                const auto fl = filter (lineL[(std::size_t) pl], zl);
                outL = fl * wet + xl;
                lineL[(std::size_t) pl] = (xl + fl) * fb;
                ++pl;
                if (pr >= lenLR)
                    pr = 0;
                const auto fr = filter (lineR[(std::size_t) pr], zr);
                outR = fr * wet + xr;
                lineR[(std::size_t) pr] = (xr + fr) * fb;
                ++pr;
            }
            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;
        }
    }

private:
    double fs = 44100.0, lastBeats = -1.0;
    int capacity = 0, pl = 0, pr = 0, pp = 0, lastMode = -1;
    std::vector<float> lineL, lineR, lineP;
    float zl = 0.0f, zr = 0.0f, pan = -1.0f;
};
// ---------------------------------------------------------------------------------------------------------------
// Tape Delay (FLM module 18): one "tape" per side. The tape is a loop whose phase advances by a speed (one loop = the
// delay); each sample is recorded with its time stamp, and the previous loop is played back by linear interpolation
// between its stamps. The speed glides to its target by 0.1 % per sample, so a Time change bends the pitch of what is
// on the tape. Time = v² s, Async shortens one side by up to half, the loop holds input + echo × Fdbk, the echo is
// heard × Mix × Fdbk.
class TapeDelay final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const auto n = std::max ((int) fs, 4);
        for (auto& t : tapes)
            t.allocate (n);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& t : tapes)
            t.clear();
        preroll = true;
    }
    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (tapes[0].w.points.empty())
            prepare (fs);
        const auto time = (double) p[0] * (double) p[0];
        const auto a = (double) p[2] - 0.5;
        const auto tl = a < 0.0 ? (1.0 + a) * time : time, tr = a > 0.0 ? (1.0 - a) * time : time;
        const auto fb = p[1];
        const auto wet = p[3] * p[1];
        tapes[0].setDelay (tl);
        tapes[1].setDelay (tr);
        for (auto& t : tapes)
            t.fb = fb;
        if (preroll)
        {
            // FLM runs 10 s of silence through a freshly set-up tape: only the time stamps it leaves matter (the
            // samples are zeros and the read cursor finds its place again from the start), so record without reading
            preroll = false;
            for (auto& t : tapes)
                for (int k = (int) (10.0 * fs); k > 0; --k)
                    t.advance (0.0f);
        }
        for (int i = 0; i < n; ++i)
        {
            const auto inL = left[i], inR = right != nullptr ? right[i] : left[i];
            const auto el = tapes[0].tick (inL), er = tapes[1].tick (inR);
            left[i] = inL + el * wet;
            if (right != nullptr)
                right[i] = inR + er * wet;
        }
    }

private:
    double fs = 44100.0;
    std::array<tape::Tape, 2> tapes;
    bool preroll = true;
};
} // namespace

const char* distortionKindName (int kind) noexcept
{
    static const char* const names[] { "Soft", "Tanh", "Exponential", "Hard clip" };
    return names[std::clamp (kind, 0, 3)];
}
int distortionKind (float v) noexcept { return std::clamp ((int) (v * 3.0f), 0, 3); }

std::unique_ptr<Effect> makeDistortion() { return std::make_unique<Distortion>(); }
std::unique_ptr<Effect> makeLimiterClassic() { return std::make_unique<LimiterClassic>(); }
std::unique_ptr<Effect> makeLimiter() { return std::make_unique<Limiter>(); }
std::unique_ptr<Effect> makeLeveller() { return std::make_unique<Leveller>(); }
std::unique_ptr<Effect> makeGate() { return std::make_unique<Gate>(); }
std::unique_ptr<Effect> makeTranceDelay() { return std::make_unique<TranceDelay>(); }
std::unique_ptr<Effect> makeTapeDelay() { return std::make_unique<TapeDelay>(); }

std::string distortionParamText (int param, float v)
{
    switch (param)
    {
        case 2: return format ("%.1f dB", (double) (v * 24.0f - 24.0f));
        case 3: return format ("%.0f", (double) (int) ((v - 0.5f) * 200.0f));
        case 4: return distortionKindName (distortionKind (v));
        case 5: return format ("%.0f", (double) (int) (v * 200.0f - 100.0f));
        default: return percent (v);
    }
}

std::string limiterClassicParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.1f dB", (double) (v * 24.0f - 24.0f));
        case 1:
        {
            const auto r = 0.19999999f - v * 0.2f;
            return format ("%.0f ms", 1000.0 / (double) (r * r * 100.0f + 1.0f));
        }
        case 2: return v * 36.0f - 24.0f < -60.0f ? std::string ("Off") : format ("%.1f dB", (double) (v * 36.0f - 24.0f));
        default: return percent (v);
    }
}

std::string levellerParamText (int param, float v)
{
    if (param == 1)
    {
        const auto i = (int) (2000.0f * v - 1000.0f);
        return i == 0 ? std::string ("mid") : format (i < 0 ? "%.1f%% L" : "%.1f%% R", std::abs (i) * 0.1);
    }
    const auto i = (int) ((std::pow (std::clamp (v, 0.0f, 1.0f), 0.6f) * 72.0f - 60.0f) * 10.0f);
    return i == 0 ? std::string ("0 dB") : format (i > 0 ? "+%.1f dB" : "%.1f dB", i * 0.1);
}
} // namespace lynx::pocket

namespace lynx::pocket
{
std::string limiterParamText (int param, float v)
{
    switch (param)
    {
        case 0: case 1: return format ("%.1f dB", (double) (v * 48.0f - 24.0f));
        case 2: return format ("%.2f s", (double) v * v * v * v * 1.9 + 0.1);
        case 3: return format ("%.1f dB", (double) (v * 24.0f - 24.0f));
        default: return percent (v);
    }
}
} // namespace lynx::pocket

namespace lynx::pocket
{
std::string gateParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.0f dB", (double) (v * 66.0f - 60.0f)); // what runs (FLM's own label reads 60v − 60)
        case 1: return format ("%.0f dB", (double) (v * v * 66.0f));
        case 2: case 4: case 7: return format ("%.0f ms", (double) (v * 1000.0f));
        case 3: case 5: return format ("%.1f", (double) (v * 2.0f - 1.0f));
        case 6: return format ("%.0f%%", (double) std::round (v * 200.0f - 100.0f));
        default: return percent (v);
    }
}
} // namespace lynx::pocket

namespace lynx::pocket
{
const char* tranceTimeName (float v) noexcept
{
    static const char* const names[] { "1/4", "3/8", "1/2", "3/4", "1/1" };
    const auto k = (int) (v * 4.0f);
    return k >= 1 && k <= 4 ? names[k] : names[0];
}

std::string tranceDelayParamText (int param, float v)
{
    switch (param)
    {
        case 0: return tranceTimeName (v);
        case 4:
        {
            static const char* const modes[] { "Stereo", "Ping-Pong", "Mirror" };
            return modes[std::clamp ((int) (v * 2.0f + 0.5f), 0, 2)];
        }
        case 6: return v >= 0.5f ? "On" : "Off";
        default: return format ("%.0f", (double) (int) (v * 100.0f));
    }
}
} // namespace lynx::pocket

namespace lynx::pocket
{
std::string tapeDelayParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.0f ms", (double) (int) (v * v * 1000.0f));
        case 2: return format ("%.0f%%", (double) (int) (v * 200.0f - 100.0f));
        default: return format ("%.0f%%", (double) (int) (v * 100.0f));
    }
}
} // namespace lynx::pocket
