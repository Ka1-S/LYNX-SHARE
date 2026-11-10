#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/PocketGrid.h"
#include "core/PocketTape.h"

// Pocket Wow & Flutter, Phaser, Flanger and Tremolo: FL Studio Mobile's modules 43, 14, 13 and 46, rebuilt from
// REA_rodada182c.md "F5 effects, round 189h" (clean-room: FLM's laws restated in our own words, no Image-Line code).
// All four share FLM's small LFO ("LFO-F" below). Like every Pocket engine that reads per-call values or the song
// position, they run on FL's song-tick grid (core/PocketGrid.h): FL calls a hosted FLM rack in chunks of 1/96 beat.

namespace lynx::pocket
{
namespace
{
constexpr double twoPi = 6.283185307179586, halfPi = 1.5707963267948966;
// the sync menus of Wow & Flutter and Tremolo (beats) and of Phaser and Flanger
constexpr double beats10[10] { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0 };
constexpr double beats13[13] { 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0 };

double frac (double x) noexcept { return x - (double) (long long) x; }
int index10 (float v) noexcept { return std::clamp ((int) (9.0f * v), 0, 9); }
int index13 (float v) noexcept { return std::clamp ((int) (12.0f * v), 0, 12); }

std::string format (const char* fmt, double v)
{
    char text[48];
    std::snprintf (text, sizeof (text), fmt, v);
    return text;
}
std::string percent (float v) { return format ("%.0f %%", (double) v * 100.0); }
std::string onOff (float v) { return v >= 0.5f ? "On" : "Off"; }
const char* shapeName (float v) noexcept
{
    static const char* const names[] { "Sine", "Saw", "Square" };
    return names[std::clamp ((int) std::lround (2.0f * v), 0, 2)];
}
const char* label10 (float v) noexcept
{
    static const char* const names[] { "1/8", "1/4", "3/8", "1/2", "3/4", "beat", "2 beats", "4 beats", "8 beats", "16 beats" };
    return names[index10 (v)];
}
const char* label13 (float v) noexcept
{
    // FLM's own labels: "1/1" is one beat and "1 bar" sixteen
    static const char* const names[] { "1/4", "3/8", "1/2", "3/4", "1/1", "2/1", "4/1", "8/1", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
    return names[index13 (v)];
}

// FLM's LFO-F: the value is taken from the phase before the step (shapes: sine, saw up, square, and Tremolo's shape 3,
// a skewed triangle bent by an exponent and blended with its sine), then the phase moves on by n steps
struct LfoF
{
    int shape = 0;
    float out = 0.0f;
    double ph = 0.0, off = 0.0, inc = 0.0, mult = 1.0, beats = 0.0;
    double P = 0.0, Sm = 0.0, S = 0.5, W = 0.0; // shape 3
    double shapeP = 0.0, shapeE = 1.0;            // 16^P for the last P

    void clear() noexcept
    {
        ph = off = inc = 0.0;
        out = 0.0f;
        mult = 1.0;
    }
    void tick (int n) noexcept
    {
        const auto p = std::fmod (ph + off, 1.0);
        switch (shape)
        {
            case 0: out = (float) std::sin (twoPi * p); break;
            case 1: out = (float) (2.0 * p - 1.0); break;
            case 2: out = p > 0.5 ? 1.0f : -1.0f; break;
            case 3:
            {
                const auto tri = std::min ((1.0 - p) / (1.0 - S), p / S);
                const auto k = 1.0 / (1.0 - std::abs (W));
                auto u = (2.0 * tri - 1.0) * k - k * W;
                u = std::min (std::max (u, -1.0), 1.0);
                if (P != shapeP)
                    shapeP = P, shapeE = (double) std::pow (16.0f, (float) P);
                // FLM bends |u| by 16^P inside sin(), takes asin() back and blends cos((1 − x)·π/2) = sin(x·π/2) with
                // x: the same values (to double rounding, below the float output) with one sin() per step
                const auto b = shapeE == 1.0 ? std::abs (u) : std::pow (std::abs (u), shapeE);
                const auto x = u >= 0.0 ? b : -b;
                out = (float) (std::sin (x * halfPi) * Sm + (1.0 - Sm) * x);
                break;
            }
            default: break;
        }
        const auto t = (double) n * inc + ph;
        ph = t - (double) (long long) t;
    }
    void freeRate (double hz, double fs) noexcept { inc = hz / fs; }
    // the tempo-synced step; a song position (in beats, >= 0) puts the phase where the song is
    void synced (double bpm, double fs, double songBeats) noexcept
    {
        const auto len = beats * mult;
        const auto period = bpm > 0.0 ? len * 60.0 / bpm : 0.0;
        inc = period * fs != 0.0 ? 1.0 / (period * fs) : 0.0;
        if (songBeats >= 0.0 && len > 0.0)
            ph = frac (songBeats / len);
    }
};

// FLM's parameter setters act once when a value changes (a change resets an LFO, restarts a timer…): the engines keep
// the last values and call their setter for each one that moved, in FLM's order (the first block = FLM loading a preset)
template <int N>
struct Changes
{
    std::array<float, N> last {};
    bool loaded = false;
    template <typename F>
    void apply (const EffectParams& p, F&& set)
    {
        for (int k = 0; k < N; ++k)
            if (! loaded || p[(std::size_t) k] != last[(std::size_t) k])
            {
                last[(std::size_t) k] = p[(std::size_t) k];
                set (k, p[(std::size_t) k]);
            }
        loaded = true;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Wow & Flutter (FLM module 43): a 100 % wet modulated delay. Two sine LFO-Fs set the delay: wow up to 142 ms × the
// wow amount (an exponential law), flutter up to 3 ms on top; read with linear interpolation. Random re-picks the wow
// rate (or its sync length) inside a window set by Random Amount, every Random Rate period.
// FLM times the random picks with the wall clock; LYNX counts samples so that an export is the same at any speed
// (a deviation: FLM picks less often per audio second when it renders faster than real time).
class WowFlutter final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const auto size = 1 << (int) std::ceil (std::log2 (0.145f * (float) fs));
        bufL.assign ((std::size_t) size, 0.0f);
        bufR.assign ((std::size_t) size, 0.0f);
        mask = size - 1;
        wd = (float) (0.142 * fs);
        fd = (float) (0.003 * fs);
        c = (float) (1.0 - 44.1 / fs);
        reset();
    }
    void reset() noexcept override
    {
        // FLM's reset: the rings cleared, the random timer restarted; LFOs and smoothers go on
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        w = 0;
        lastPick = clock;
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (bufL.empty())
            prepare (fs);
        changes.apply (p, [&] (int k, float v) { set (k, v, p, bpm); });
        const auto block = grid.begin (songBeats, bpm, fs, n);
        block.forEach ([&] (int at, int m, double beats, bool continuing) {
            if (! continuing)
                startCall (beats, bpm);
            run (left + at, right != nullptr ? right + at : nullptr, m);
        });
    }

private:
    void set (int k, float v, const EffectParams& p, double bpm) noexcept
    {
        constexpr bool hostFlag = true; // OPEN in FLM (a host flag, probably "audio running"): new rates glide
        syncIndex = index10 (p[1]);
        switch (k)
        {
            case 0: wowHz = 0.1f + 4.9f * v; pick (hostFlag); break;
            case 1: wow.clear(); wowBeats = beats10[index10 (v)]; break;
            case 2: wowAmt = (std::exp (5.0f * v) - 1.0f) * 0.006783654906304231f; if (hostFlag) pick (true); break;
            case 3: flHz = 5.0f + 15.0f * v; break;
            case 4: flBeats = beats10[index10 (v)]; break;
            case 5: flAmt = v; break;
            case 6:
            {
                const auto on = v > 0.5f;
                if (on && ! sync)
                {
                    wowBeats = beats10[index10 (p[1])];
                    flBeats = beats10[index10 (p[4])];
                }
                sync = on;
                pick (false);
                break;
            }
            case 7: interval = 1.0 / (0.1 + 9.9 * (double) v); lastPick = clock; break;
            case 8:
            {
                // the last written of Random Rate / its sync length sets the interval, whatever Sync is (FLM)
                static constexpr double r[10] { 7.5, 15.0, 22.5, 30.0, 45.0, 60.0, 120.0, 240.0, 480.0, 960.0 };
                const auto b = (int) bpm;
                interval = b > 0 ? r[index10 (v)] / (double) b : 0.0;
                lastPick = clock;
                wow.clear();
                break;
            }
            case 9: rAmt = v; rawRandom = v; pick (hostFlag); break;
            default: break;
        }
    }
    float uniform (float lo, float hi) noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return lo + (float) (rng >> 8) * (1.0f / 16777216.0f) * (hi - lo);
    }
    void pick (bool glide) noexcept
    {
        if (! sync)
        {
            const auto rate = wowHz;
            const auto lo = rate == 0.1f ? rate : 0.1f + (rate - 0.1f) * (1.0f - rAmtS);
            const auto hi = rate == 5.0f ? rate : rate + (5.0f - rate) * rAmtS;
            rateT = uniform (lo, hi);
            if (! glide)
                rateS = rateT;
        }
        else
        {
            const auto i = syncIndex;
            float lo, hi;
            if (i == 0)
                lo = 0.0f, hi = 9.0f * rAmtS;
            else if (i == 9)
                lo = 9.0f * (1.0f - rAmtS), hi = 9.0f;
            else
                lo = (float) i * (1.0f - rAmtS), hi = (float) (9 - i) * rAmtS + (float) i;
            const auto j = (int) std::trunc (uniform (lo, hi));
            wowBeats = j >= 0 && j <= 9 ? beats10[j] : 0.125;
        }
        lastPick = clock;
    }
    void startCall (double beats, double bpm) noexcept
    {
        if (! sync)
        {
            wow.freeRate ((double) rateS, fs);
            fl.freeRate ((double) flHz, fs);
            return;
        }
        wow.beats = wowBeats;
        fl.beats = flBeats;
        wow.synced (bpm, fs, rawRandom == 0.0f ? beats : -1.0);
        fl.synced (bpm, fs, beats);
    }
    void run (float* left, float* right, int n) noexcept
    {
        const auto g = 1.0f - c;
        for (int i = 0; i < n; ++i)
        {
            if ((double) (clock - lastPick) / fs >= interval)
                pick (true);
            bufL[(std::size_t) w] = left[i];
            bufR[(std::size_t) w] = right != nullptr ? right[i] : left[i];
            w = (w + 1) & mask;
            wowAmtS = g * wowAmt + c * wowAmtS;
            auto d1 = (wow.out + 1.0f) * wd * wowAmtS * 0.5f;
            d1 = std::min (d1, wd);
            if (! (d1 > 0.0f))
                d1 = 0.0f;
            flAmtS = g * flAmt + c * flAmtS;
            auto d = (fl.out + 1.0f) * fd * flAmtS * 0.5f + d1;
            d = std::min (d, d1 + fd);
            if (! (d > d1))
                d = d1;
            const auto k = (int) d;
            const auto f = d - (float) k;
            const auto a = (std::size_t) ((w - k - 2) & mask), b = (std::size_t) ((w - k - 1) & mask);
            left[i] = f * bufL[a] + (1.0f - f) * bufL[b];
            if (right != nullptr)
                right[i] = f * bufR[a] + (1.0f - f) * bufR[b];
            wow.tick (1);
            fl.tick (1);
            rateS = g * rateT + c * rateS;
            rAmtS = g * rAmt + c * rAmtS;
            ++clock;
        }
    }

    double fs = 44100.0, songBeats = -1.0;
    TickGrid grid;
    Changes<10> changes;
    std::vector<float> bufL, bufR;
    int mask = 0, w = 0;
    float wd = 0.0f, fd = 0.0f, c = 0.0f;
    LfoF wow, fl;
    float wowHz = 1.0f, flHz = 6.0f, wowAmt = 0.0f, flAmt = 0.0f, rAmt = 0.0f, rawRandom = 0.0f;
    float wowAmtS = 0.0f, flAmtS = 0.0f, rateS = 0.0f, rateT = 0.0f, rAmtS = 0.0f;
    double wowBeats = 0.25, flBeats = 0.125, interval = 100.0;
    long long clock = 0, lastPick = 0;
    int syncIndex = 1;
    bool sync = false;
    std::uint32_t rng = 0x2545f491u;
};

// ---------------------------------------------------------------------------------------------------------------
// Phaser (FLM module 14): six first-order all-passes a = (1 − c)/(1 + c), c = f/fs (no prewarp) between 50 Hz and
// min(16 kHz, 0.499 fs), feedback from the last stage into the first, the wet added on top of the dry. Stereo reads the
// right side's LFO ahead in phase.
class Phaser final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        lo = (float) (50.0 / fs);
        hi = std::min ((float) (16000.0 / fs), 0.499f);
        reset();
    }
    void reset() noexcept override
    {
        for (auto& c : ch)
            c = {};
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        beats = beats13[index13 (p[0])];
        depth = 0.25f * p[1] * p[1] * p[1];
        fb = 0.95f * p[2];
        base = p[3] * p[3];
        mix = p[4] * p[4];
        sync = p[5] != 0.0f;
        const auto s = (double) p[6];
        rateHz = (float) (9.9 * s * s * s + 0.1);
        lfo.shape = std::clamp ((int) std::lround (2.0f * p[7]), 0, 2);
        st = 0.5f * p[8];
        const auto block = grid.begin (songBeats, bpm, fs, n);
        block.forEach ([&] (int at, int m, double songAt, bool continuing) {
            if (! continuing)
            {
                if (sync)
                {
                    lfo.beats = beats;
                    lfo.synced (bpm, fs, songAt);
                }
                else
                    lfo.freeRate ((double) rateHz, fs);
            }
            run (left + at, right != nullptr ? right + at : nullptr, m);
        });
    }

private:
    struct Channel
    {
        std::array<float, 6> s {};
        float y = 0.0f;
    };
    float coef (float o) const noexcept
    {
        auto c = (depth * o + base) * (hi - lo) + lo;
        c = std::max (std::min (c, hi), lo);
        return (1.0f - c) / (1.0f + c);
    }
    float stage (Channel& c, float x, float a) const noexcept
    {
        auto u = (float) ((double) x + 1e-30) + fb * c.y;
        for (auto& s : c.s)
        {
            const auto y = s - a * u;
            s = a * y + u;
            u = y;
        }
        c.y = u;
        return x + mix * u;
    }
    void run (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float aL, aR;
            if (st == 0.0f)
            {
                lfo.tick (1);
                aL = aR = coef (lfo.out);
            }
            else
            {
                const auto phi = lfo.ph;
                lfo.tick (1);
                const auto oL = lfo.out;
                const auto next = lfo.ph;
                lfo.ph = frac (phi + (double) st);
                lfo.tick (1);
                const auto oR = lfo.out;
                lfo.ph = next;
                aL = coef (oL);
                aR = coef (oR);
            }
            const auto xr = right != nullptr ? right[i] : left[i];
            left[i] = stage (ch[0], left[i], aL);
            const auto yr = stage (ch[1], xr, aR);
            if (right != nullptr)
                right[i] = yr;
        }
    }

    double fs = 44100.0, songBeats = -1.0, beats = 0.375;
    TickGrid grid;
    LfoF lfo;
    std::array<Channel, 2> ch {};
    float lo = 0.0f, hi = 0.0f, depth = 0.0f, fb = 0.0f, base = 0.0f, mix = 0.0f, rateHz = 0.1f, st = 0.0f;
    bool sync = true;
};

// ---------------------------------------------------------------------------------------------------------------
// Flanger (FLM module 13): FLM's tape (core/PocketTape.h) as a 5 ms loop whose length the LFO-F sets every sample
// (the tape's own 0.1 % speed glide smooths the sweep); feedback and the wet can be inverted; the wet adds to the dry.
class Flanger final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const auto size = std::max (4, (int) (fs * 0.005));
        for (auto& t : tapes)
        {
            t.allocate (size);
            t.setDelay (0.5);
        }
        reset();
    }
    void reset() noexcept override
    {
        // no pre-roll: the echo stays 0 until one loop is recorded
        for (auto& t : tapes)
            t.clear();
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (tapes[0].w.points.empty())
            prepare (fs);
        beats = beats13[index13 (p[0])];
        sync = p[1] != 0.0f;
        const auto s = (double) p[2];
        rateHz = (float) (9.9 * s * s * s + 0.1);
        lfo.shape = std::clamp ((int) std::lround (2.0f * p[3]), 0, 2);
        const auto h = 0.5f * (p[5] * p[5]);
        hA = h * p[4];
        half = h;
        const auto fb = p[7] != 0.0f ? -p[6] : p[6];
        for (auto& t : tapes)
            t.fb = fb;
        st = 0.5f * p[8];
        const auto m = p[9] * p[9];
        wet = p[10] != 0.0f ? -m : m;
        const auto block = grid.begin (songBeats, bpm, fs, n);
        block.forEach ([&] (int at, int len, double songAt, bool continuing) {
            if (! continuing)
            {
                if (sync)
                {
                    lfo.beats = beats;
                    lfo.synced (bpm, fs, songAt);
                }
                else
                    lfo.freeRate ((double) rateHz, fs);
            }
            run (left + at, right != nullptr ? right + at : nullptr, len);
        });
    }

private:
    float position (float o) const noexcept
    {
        auto x = 0.5f * o * hA + half;
        if (x <= 0.0f)
            x = 0.0f;
        if (x >= 1.0f)
            x = 1.0f;
        return x;
    }
    void run (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float xL, xR;
            if (st == 0.0f)
            {
                lfo.tick (1);
                xL = xR = position (lfo.out);
            }
            else
            {
                const auto phi = lfo.ph;
                lfo.tick (1);
                const auto oL = lfo.out;
                const auto next = lfo.ph;
                lfo.ph = frac (phi + (double) st);
                lfo.tick (1);
                const auto oR = lfo.out;
                lfo.ph = next;
                xL = position (oL);
                xR = position (oR);
            }
            tapes[0].setDelay ((double) xL);
            tapes[1].setDelay ((double) xR);
            const auto inL = left[i], inR = right != nullptr ? right[i] : left[i];
            const auto eL = tapes[0].tick (inL), eR = tapes[1].tick (inR);
            left[i] = inL + eL * wet;
            if (right != nullptr)
                right[i] = inR + eR * wet;
        }
    }

    double fs = 44100.0, songBeats = -1.0, beats = 1.0;
    TickGrid grid;
    LfoF lfo;
    std::array<tape::Tape, 2> tapes;
    float rateHz = 1.0f, hA = 0.0f, half = 0.0f, st = 0.0f, wet = 0.0f;
    bool sync = false;
};

// ---------------------------------------------------------------------------------------------------------------
// Tremolo (FLM module 46): a gain between Min and Max from two LFO-F shape-3 waves (left, right + Stereo Offset);
// the swing reaches 99 % of the range (0.495), and a one-pole at ~333 Hz smooths the gain.
class Tremolo final : public Effect
{
public:
    Tremolo()
    {
        lfo[0].shape = lfo[1].shape = 3;
    }
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        cR = (float) (1.0 - 44.1 / fs);
        cG = (float) std::exp (-2094.395084187993 / fs);
        reset();
    }
    void reset() noexcept override
    {
        // FLM's reset: both LFOs restart (their shape kept); the smoothers go on
        for (auto& l : lfo)
            l.clear();
        grid.reset();
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        changes.apply (p, [&] (int k, float v) { set (k, v); });
        const auto block = grid.begin (songBeats, bpm, fs, n);
        block.forEach ([&] (int at, int m, double songAt, bool continuing) {
            if (! continuing)
            {
                for (auto& l : lfo)
                {
                    if (sync)
                    {
                        l.beats = storedBeats;
                        l.synced (bpm, fs, songAt);
                    }
                    else
                        l.freeRate ((double) rateS, fs);
                }
                lfo[0].off = (double) po;
                lfo[1].off = (double) po + (double) so;
            }
            run (left + at, right != nullptr ? right + at : nullptr, m);
        });
    }

private:
    void set (int k, float v) noexcept
    {
        switch (k)
        {
            case 0: rateT = 0.1f + 19.9f * v; break;
            case 1:
                storedBeats = beats10[index10 (v)];
                for (auto& l : lfo)
                    l.clear();
                break;
            case 2: gMax = v; if (v < gMin) gMin = v; break;
            case 3: gMin = v; if (v > gMax) gMax = v; break;
            case 4: po = v; break;
            case 5: so = v; break;
            case 6: sync = v > 0.5f; break;
            case 7: for (auto& l : lfo) l.P = (double) (2.0f * v - 1.0f); break;
            case 8: for (auto& l : lfo) l.Sm = (double) (0.998f * v + 0.001f); break;
            case 9: for (auto& l : lfo) l.S = (double) (0.998f * v + 0.001f); break;
            case 10: for (auto& l : lfo) l.W = (double) (0.999f - 1.998f * v); break;
            default: break;
        }
    }
    float gain (float o) const noexcept
    {
        auto g = (o + 1.0f) * (gMax - gMin) * 0.495f + gMin;
        if (g > gMax)
            g = gMax;
        else if (g < gMin)
            g = gMin;
        return g;
    }
    void run (float* left, float* right, int n) noexcept
    {
        const auto kG = 1.0f - cG, kR = 1.0f - cR;
        for (int i = 0; i < n; ++i)
        {
            zL = kG * gain (lfo[0].out) + cG * zL;
            zR = kG * gain (lfo[1].out) + cG * zR;
            const auto xr = right != nullptr ? right[i] : left[i];
            left[i] *= zL;
            if (right != nullptr)
                right[i] = zR * xr;
            lfo[0].tick (1);
            lfo[1].tick (1);
            rateS = kR * rateT + cR * rateS;
        }
    }

    double fs = 44100.0, songBeats = -1.0, storedBeats = 0.125;
    TickGrid grid;
    Changes<11> changes;
    std::array<LfoF, 2> lfo;
    float cR = 0.0f, cG = 0.0f, rateT = 1.0f, rateS = 0.0f, zL = 0.0f, zR = 0.0f;
    float gMax = 1.0f, gMin = 0.0f, po = 0.0f, so = 0.0f;
    bool sync = false;
};
} // namespace

std::unique_ptr<Effect> makeWowFlutter() { return std::make_unique<WowFlutter>(); }
std::unique_ptr<Effect> makePhaser() { return std::make_unique<Phaser>(); }
std::unique_ptr<Effect> makeFlanger() { return std::make_unique<Flanger>(); }
std::unique_ptr<Effect> makeTremolo() { return std::make_unique<Tremolo>(); }

std::string wowFlutterParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.2f Hz", (double) (0.1f + 4.9f * v));
        case 1: case 4: case 8: return label10 (v);
        case 3: return format ("%.2f Hz", (double) (5.0f + 15.0f * v));
        case 6: return onOff (v);
        case 7: return format ("%.1f Hz", (double) (0.1f + 9.9f * v));
        default: return percent (v);
    }
}

std::string phaserParamText (int param, float v)
{
    switch (param)
    {
        case 0: return label13 (v);
        case 3: return format ("%.0f Hz", (double) (15950.0f * v * v + 50.0f));
        case 5: return onOff (v);
        case 6: return format ("%.2f Hz", 9.9 * (double) v * v * v + 0.1);
        case 7: return shapeName (v);
        default: return percent (v);
    }
}

std::string flangerParamText (int param, float v)
{
    switch (param)
    {
        case 0: return label13 (v);
        case 1: case 7: case 10: return onOff (v);
        case 2: return format ("%.2f Hz", 9.9 * (double) v * v * v + 0.1);
        case 3: return shapeName (v);
        case 5: return format ("%.2f ms", (double) (2.5f * v * v));
        default: return percent (v);
    }
}

std::string tremoloParamText (int param, float v)
{
    switch (param)
    {
        case 0: return format ("%.2f Hz", (double) (0.1f + 19.9f * v));
        case 1: return label10 (v); // FLM's own text indexes the labels with the beat value (a bug); the menu's label is shown
        case 4: case 5: return format ("%.2f", (double) (360.0f * v));
        case 6: return onOff (v);
        case 7: case 8: case 10: return format ("%.0f", (double) (100.0f * v));
        case 9: return format ("%.0f", (double) (200.0f * v - 100.0f));
        default: return percent (v);
    }
}
} // namespace lynx::pocket
