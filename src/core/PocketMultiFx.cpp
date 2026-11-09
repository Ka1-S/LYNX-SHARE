#include "core/PocketFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/PocketTape.h"
#include "core/Reeverb2.h"

// Pocket MultiFX: FL Studio Mobile's MultiFX (module 4), rebuilt from REA_rodada182c.md "F5 effects, round 188h"
// (clean-room: FLM's laws restated, no Image-Line code). Eight effects on an X/Y pad with a song-locked LFO. Every effect
// has its own fade level: choosing a kind fades the old one out and the new one in (0.5 % per sample), and all of them run
// one after the other on the same buffer while they sound. In order: Flanger, Phaser, LP Filter, HP Filter, Delay,
// Reverb, Panner, Vox.

namespace lynx::pocket
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double beatsTable[10] { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0, 16.0 };

enum Type { flanger = 1, phaser = 2, lowPass = 3, highPass = 4, delayType = 5, reverbType = 6, panner = 7, vox = 9 };
constexpr int kindToType[8] { delayType, reverbType, lowPass, highPass, flanger, phaser, panner, vox };

double frac (double x) noexcept { return x - (double) (long long) x; }
float flush (float x) noexcept { return x > -1e-9f && x < 1e-9f ? 0.0f : x; }

// FLM's low- / high-pass biquad (the Filter module's: the cookbook's at Q/2 with its gain normalised for Q)
struct Biquad
{
    float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    void design (double hz, double q, double fs, bool high) noexcept
    {
        const auto w = (float) (twoPi * hz / fs);
        const auto s = std::sin (w), c = std::cos (w);
        const auto fq = (float) q;
        b0 = (high ? 1.0f + c : 1.0f - c) / (s / fq + 2.0f);
        a1 = -2.0f * fq * c / (s + fq);
        a2 = (fq - s) / (s + fq);
    }
};

class MultiFx final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        delayLength = std::max (2, (int) (fs * 1.5));
        for (auto& b : delayBuf)
            b.assign ((std::size_t) delayLength, 0.0f);
        panLength = std::max (5, (int) (fs * 0.002));
        for (auto& b : panBuf)
            b.assign ((std::size_t) panLength, 0.0f);
        flangerTape.allocate (std::max (4, (int) (fs * 0.005)));
        reverb = makeReeverb2();
        reverb->prepare (fs);
        for (auto& b : scratch)
            b.assign (2048, 0.0f);
        for (int t = 0; t < 10; ++t)
            resetSub (t);
    }
    void reset() noexcept override
    {
        for (int t = 0; t < 10; ++t)
            resetSub (t);
        freeSamples = 0.0;
        wasPlaying = false;
    }
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }

    void process (float* left, float* right, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (delayBuf[0].empty())
            prepare (fs);
        if (right == nullptr)
            return;
        setParams (p);
        // FLM inside FL runs in chunks of one song tick (1/96 beat) cut on the song's tick grid: each chunk reads its
        // own LFO value at its own song position (A/B, round 189h: the LFO settings went from 6 / 7 / 4 to 9 / 10 / 9)
        const auto tickLen = bpm >= 1.0 ? fs * 60.0 / (bpm * 96.0) : 0.0;
        if (tickLen < 1.0)
        {
            for (int t = 1; t < 10; ++t)
                if (gateOpen (t) && prepSub (t, songBeats, bpm, false))
                    runSub (t, left, right, n, true);
            return;
        }
        // A chunk that a host block boundary cuts goes on with the LFO value it started with (FL's chunks follow the
        // grid only); a chunk that starts on the grid gets its tick's exact position, so a square LFO's edge at a half
        // beat waits one chunk, as in FL.
        const auto playing = songBeats >= 0.0;
        auto start = playing ? songBeats * 96.0 * tickLen : freeSamples; // the block's first sample on the grid
        if (std::abs (start - std::round (start)) < 1e-6)
            start = std::round (start); // a whole sample (the grid's half-sample ties must not flip on a rounding error)
        const auto kHere = (long long) std::floor (start / tickLen + 0.5);
        const auto onGrid = (long long) std::floor ((double) kHere * tickLen + 0.5 - start) == 0;
        const auto contiguous = playing ? wasPlaying && std::abs (start - expected) < 0.25 : ! wasPlaying && started;
        const auto firstBeats = ! playing ? -1.0 : onGrid ? (double) kHere / 96.0 : songBeats;
        freeSamples = playing ? 0.0 : std::fmod (freeSamples + n, tickLen);
        expected = start + n;
        wasPlaying = playing;
        // The effects run one after the other and each only hears the one before it, so each can take the whole block
        // chunk by chunk before the next one starts (the same samples as chunk by chunk through all of them).
        for (int t = 1; t < 10; ++t)
        {
            if (! gateOpen (t))
                continue; // a closed effect stays closed until a block runs it
            if (t == reverbType)
            {
                // no LFO and nothing tied to the song: one pass for the block (the grid adds only calls)
                if (prepSub (t, -1.0, bpm, false))
                    runSub (t, left, right, n, true);
                continue;
            }
            auto beats = firstBeats;
            auto continuing = ! onGrid && contiguous;
            for (int done = 0; done < n;)
            {
                auto k = (long long) std::floor ((start + done) / tickLen);
                auto next = (int) std::floor ((double) k * tickLen + 0.5 - start);
                while (next <= done)
                    next = (int) std::floor ((double) ++k * tickLen + 0.5 - start);
                const auto m = std::min (next, n) - done;
                if (prepSub (t, beats, bpm, continuing) && gateOpen (t))
                    runSub (t, left + done, right + done, m, true);
                done += m;
                continuing = false;
                if (playing)
                    beats = (double) k / 96.0;
            }
        }
    }

    // FL tells a hosted FLM rack that its input always carries sound, so no effect skips silent chunks (A/B: the
    // Flanger's tape keeps gliding through silence; skipping them as FLM's own tracks do put the bursts at -3.5 dB
    // instead of -52.7 dB). An effect runs while it is chosen, fading or ringing out.
    bool gateOpen (int t) const noexcept
    {
        if (t == 8)
            return false; // FLM's ring modulator: never selectable in MultiFX
        const auto& s = subs[(std::size_t) t];
        return ! s.deselected || s.a > 0.0f || (t == delayType && s.count >= 0 && s.count <= 1.5 * fs)
               || (t == reverbType && s.count >= 0 && s.count <= 0.25 * fs);
    }
    // a chunk's LFO: the phase from the song position (unless the chunk goes on from the last block), the step per sample
    bool prepSub (int t, double beats, double bpm, bool continuing) noexcept
    {
        auto& s = subs[(std::size_t) t];
        held = continuing;
        const auto rate = t == delayType ? 2.0 * rateBeats : rateBeats;
        if (beats >= 0.0 && ! continuing)
        {
            const auto ph = rate > 0.0 ? (double) (float) (beats * 4.0) * (0.25 / rate) : s.ph;
            s.ph = frac (ph);
        }
        if (fs < 1.0 || bpm < 1.0)
            return false;
        const auto periodSec = (float) (rate * 60.0 / bpm);
        s.inc = periodSec == 0.0f ? 0.0 : (double) (1.0f / (periodSec * (float) fs));
        s.periodSec = periodSec;
        return true;
    }

    struct Sub
    {
        bool deselected = true;
        float a = 0.0f, target = 0.0f, w = 0.0f;
        double ph = 0.0, inc = 0.0;
        float periodSec = 0.0f, heldLfo = 0.0f;
        int count = 0;
        // per-kind state
        std::array<std::array<float, 4>, 2> x1 {}, x2 {}, y1 {}, y2 {}; // the filter cascade
        std::array<std::array<float, 8>, 2> vx1 {}, vx2 {}, vy1 {}, vy2 {};
        std::array<std::array<float, 6>, 2> ap {};
        std::array<float, 2> fbk {}, lp {}, hp {}, lpPrev {};
        int wp = 0, storedD = -1, dc = 1;
        float panS = -1.0f, distS = -1.0f;
        std::array<float, 8> vb0 {}, va1 {}, va2 {};
        std::array<float, 8> vhz {};
    };

    float lfo (Sub& s) noexcept
    {
        double v;
        if (shape == 0)
        {
            v = std::sin (twoPi * s.ph);
            s.ph = frac (s.ph + s.inc);
        }
        else if (shape == 1)
        {
            s.ph = frac (s.ph + s.inc);
            v = 1.0 - 2.0 * s.ph;
        }
        else
        {
            v = s.ph > 0.5 ? -1.0 : 1.0;
            s.ph = frac (s.ph + s.inc);
        }
        return (float) v * amount;
    }
    // one value per chunk: the phase then moves on by the chunk (the rest of a cut chunk keeps the value)
    float blockLfo (Sub& s, int n) noexcept
    {
        if (held)
        {
            s.ph = frac (s.ph + (double) n * s.inc);
            return s.heldLfo;
        }
        const auto v = lfo (s);
        s.ph = frac (s.ph + (double) (n - 1) * s.inc);
        s.heldLfo = v;
        return v;
    }

    void setParams (const EffectParams& p) noexcept
    {
        const auto kind = std::clamp ((int) (p[0] * 7.0f), 0, 7);
        if (kind != lastKind)
        {
            lastKind = kind;
            for (int t = 0; t < 10; ++t)
            {
                auto& s = subs[(std::size_t) t];
                const auto chosen = t == kindToType[kind];
                s.deselected = ! chosen;
                s.target = chosen ? 1.0f : 0.0f;
            }
            if (! started)
            {
                started = true;
                for (auto& s : subs)
                    s.a = s.target;
            }
        }
        x = std::clamp (p[1], 0.0f, 1.0f);
        y = std::clamp (p[2], 0.0f, 1.0f);
        rateBeats = (double) (float) beatsTable[std::clamp ((int) (p[3] * 9.0f), 0, 9)];
        amount = p[4] * p[4];
        shape = std::clamp ((int) (p[5] * 2.0f), 0, 3);
    }

    void resetSub (int t) noexcept
    {
        auto& s = subs[(std::size_t) t];
        const auto keepDeselected = s.deselected;
        const auto keepTarget = s.target;
        const auto ph = s.ph;
        s = Sub {};
        s.deselected = keepDeselected;
        s.target = keepTarget;
        s.a = keepTarget;
        s.ph = ph;
        s.count = (int) (fs * 10.0);
        if (t == delayType)
            for (auto& b : delayBuf)
                std::fill (b.begin(), b.end(), 0.0f);
        if (t == panner)
            for (auto& b : panBuf)
                std::fill (b.begin(), b.end(), 0.0f);
        if (t == flanger)
        {
            flangerTape.clear();
            flangerTape.fb = 0.0f;
        }
        if (t == reverbType && reverb != nullptr)
            reverb->reset();
    }

    // true = the effect still sounds (its tail keeps the next effects "active")
    bool runSub (int t, float* l, float* r, int n, bool active) noexcept
    {
        auto& s = subs[(std::size_t) t];
        const auto limit = t == delayType ? 1.5 * fs : (t == lowPass || t == highPass || t == panner) ? fs : 0.25 * fs;
        if (! active && s.count > limit)
        {
            s.a = s.target;
            return false;
        }
        const auto perBlockCount = t != delayType && t != reverbType;
        if (perBlockCount)
            s.count = active ? 0 : s.count + n;
        switch (t)
        {
            case flanger: runFlanger (s, l, r, n); break;
            case phaser: runPhaser (s, l, r, n); break;
            case lowPass: case highPass: runFilter (s, l, r, n, t == highPass); break;
            case delayType: runDelay (s, l, r, n); break;
            case reverbType: runReverb (s, l, r, n); break;
            case panner: runPanner (s, l, r, n); break;
            default: runVox (s, l, r, n); break;
        }
        // end of block
        if (t == delayType)
        {
            if (s.a >= 1e-4f)
                return true;
            if (s.count > 1.5 * fs)
            {
                resetSub (t);
                return false;
            }
            return true;
        }
        if (t == reverbType)
        {
            if (! s.deselected || s.a >= 1e-4f || s.count <= 0.25 * fs)
                return true;
            resetSub (t);
            return false;
        }
        if (t == panner)
        {
            if (s.count <= fs)
                return true;
            resetSub (t);
            return false;
        }
        if (s.deselected && s.a < 1e-4f)
        {
            resetSub (t);
            return false;
        }
        return true;
    }

    void fade (Sub& s) noexcept { s.a += (s.target - s.a) * 0.005f; }
    void countSample (Sub& s, float a, float b) noexcept { s.count = (std::abs (a) > 1e-4f || std::abs (b) > 1e-4f) ? 0 : s.count + 1; }

    // Delay: a line of about the LFO Rate in beats (halved until it fits 1.5 s); X² is the feedback and the echo level;
    // Y below 0.5 closes a one-pole low-pass in the loop, above it opens a one-pole high-pass; the LFO pans the echoes
    void runDelay (Sub& s, float* l, float* r, int n) noexcept
    {
        auto yy = 2.0f * y, hpAmount = 0.0f;
        if (yy > 1.0f)
        {
            hpAmount = yy - 1.0f;
            yy = 1.0f;
        }
        const auto cLp = yy * 0.99f + 0.01f, cHp = hpAmount * 0.45f, fb = x * x;
        auto d = (double) (s.periodSec * 0.33333334f);
        while (d > 1.0)
            d *= 0.5;
        const auto length = (int) ((double) (delayLength - 1) * d);
        if (s.storedD != -1 && s.storedD != length)
            resetSub (delayType);
        s.storedD = length;
        s.dc = std::clamp (length, 1, delayLength - 1);
        for (int i = 0; i < n; ++i)
        {
            fade (s);
            const auto v = lfo (s);
            float wet[2];
            float* io[2] { l, r };
            for (int c = 0; c < 2; ++c)
            {
                auto& buf = delayBuf[(std::size_t) c];
                const auto dl = buf[(std::size_t) s.wp];
                const auto u = fb * dl;
                s.lp[(std::size_t) c] += (u - s.lp[(std::size_t) c]) * cLp;
                s.hp[(std::size_t) c] += (s.lpPrev[(std::size_t) c] - s.hp[(std::size_t) c]) * cHp;
                wet[c] = s.lpPrev[(std::size_t) c] - s.hp[(std::size_t) c];
                s.lpPrev[(std::size_t) c] = s.lp[(std::size_t) c];
                buf[(std::size_t) s.wp] = s.a * io[c][i] + wet[c];
            }
            if (++s.wp >= s.dc)
                s.wp = 0;
            countSample (s, wet[0], wet[1]);
            l[i] += (1.0f + v) * wet[0];
            r[i] += (1.0f - v) * wet[1];
        }
    }

    // Reverb: FLM's plate (FL's Reeverb 2 plate, LYNX's exact engine at FLM's levels), wet only: decay 0.75·X³ (0.1…15 s),
    // damping Y², the wet level 0.5 + 0.2·decay, a full room and diffusion, low cut 317 Hz; the input × the fade level
    void runReverb (Sub& s, float* l, float* r, int n) noexcept
    {
        const auto dv = 0.75f * x * x * x;
        const auto raw = [] (float v, int lo, int hi) { return (int) std::floor ((float) (hi - lo) * std::clamp (v, 0.0f, 1.0f) + (float) lo + 0.5f); };
        reeverb2::Settings st;
        st.p[0] = raw (0.1f, 19, 3000);  // low cut
        st.p[1] = 221;                   // high cut
        st.p[2] = 0;                     // predelay
        st.p[3] = 100;                   // room
        st.p[4] = 100;                   // diffusion
        st.p[5] = raw (dv, 1, 200);      // decay
        st.p[6] = raw (y * y, 5, 221);   // damping
        st.p[7] = 100;                   // bass ×1
        st.p[8] = 500;                   // crossover
        st.p[9] = 0;                     // separation 100 (FL's f = 0)
        st.p[10] = 0;                    // dry off
        st.p[11] = 0;                    // early reflections muted
        const auto wetTenths = raw (0.5f + 0.2f * dv, -361, 60);
        const auto gain = wetTenths <= -361 ? 0.0 : std::pow (10.0, wetTenths / 200.0) * 0.3;
        st.p[12] = gain <= 0.0 ? 0 : std::clamp ((int) std::lround (256.0 * std::log (10.0 * gain + 1.0) / 2.3978953), 0, 160);
        st.p[13] = 684;
        st.p[14] = 0;
        reeverb2::toParams (st, reverbParams);
        for (int done = 0; done < n;)
        {
            const auto m = std::min (n - done, 2048);
            for (int i = 0; i < m; ++i)
            {
                fade (s);
                scratch[0][(std::size_t) i] = s.a * l[done + i];
                scratch[1][(std::size_t) i] = s.a * r[done + i];
            }
            reverb->process (scratch[0].data(), scratch[1].data(), m, reverbParams, 120.0);
            for (int i = 0; i < m; ++i)
            {
                l[done + i] += scratch[0][(std::size_t) i];
                r[done + i] += scratch[1][(std::size_t) i];
                countSample (s, l[done + i], r[done + i]);
            }
            done += m;
        }
    }

    // LP / HP Filter: four equal biquads, each fed by the previous one's output of the sample before (48 dB per octave);
    // LP 2^(8.28v + 6) Hz, HP 2^(7.424v + 5) Hz, v = X + LFO; Q = 2Y² + 1
    void runFilter (Sub& s, float* l, float* r, int n, bool high) noexcept
    {
        const auto v = std::clamp (blockLfo (s, n) + x, 0.0f, 1.0f);
        const auto hz = high ? std::pow (2.0f, 7.424f * v + 5.0f) : std::pow (2.0f, 8.28f * v + 6.0f);
        Biquad q;
        q.design ((double) hz, (double) (2.0f * y * y + 1.0f), fs, high);
        const auto sign = high ? -2.0f : 2.0f;
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            fade (s);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& x1 = s.x1[(std::size_t) c];
                auto& x2 = s.x2[(std::size_t) c];
                auto& y1 = s.y1[(std::size_t) c];
                auto& y2 = s.y2[(std::size_t) c];
                const auto xin = io[c][i] + 1e-30f;
                for (int k = 3; k >= 0; --k)
                {
                    const auto in = k == 0 ? xin : y1[(std::size_t) (k - 1)];
                    const auto yk = q.b0 * (in + sign * x1[(std::size_t) k] + x2[(std::size_t) k]) - q.a1 * y1[(std::size_t) k] - q.a2 * y2[(std::size_t) k];
                    x2[(std::size_t) k] = x1[(std::size_t) k];
                    x1[(std::size_t) k] = in;
                    y2[(std::size_t) k] = y1[(std::size_t) k];
                    y1[(std::size_t) k] = yk;
                }
                out[c] = (1.0f - s.a) * xin + s.a * y1[3];
                io[c][i] = out[c];
            }
            if (high)
                countSample (s, out[0], out[1]);
        }
    }

    // Flanger: the mono −(L + R)/2 through FLM's tape of up to 5 ms (feedback −0.96·Y), delay (1 − (X + LFO/2))⁴ of it,
    // added to both sides
    void runFlanger (Sub& s, float* l, float* r, int n) noexcept
    {
        flangerTape.fb = (float) ((double) y * -0.96);
        const auto u = 0.5f * blockLfo (s, n) + x;
        auto dd = (1.0f - u) * (1.0f - u) * (1.0f - u) * (1.0f - u);
        if (! (dd >= 0.0f))
            dd = 0.0f;
        dd = std::min (dd, 1.0f);
        flangerTape.target = 1.0 / ((double) dd * (flangerTape.n - 2) + 2.0);
        for (int i = 0; i < n; ++i)
        {
            fade (s);
            const auto in = (-0.5f * r[i] - 0.5f * l[i]) * s.a;
            const auto yv = flangerTape.tick (in);
            l[i] += yv;
            r[i] += yv;
        }
    }

    // Phaser: six first-order allpasses swept from 50 Hz to 20 kHz by X²·(LFO + 1), feedback 0.98·(0.6Y)^⅓
    void runPhaser (Sub& s, float* l, float* r, int n) noexcept
    {
        const auto h = 0.5 / fs;
        const auto lo = (float) (h * 100.0), hi = (float) (h * 40000.0);
        const auto g = (float) (0.98 * (double) std::pow (0.6f * y, 0.33333334f));
        auto p = x * x * (blockLfo (s, n) + 1.0f) * (hi - lo) + lo;
        if ((double) p < 0.001)
            p = 0.001f;
        else if (p > 0.499f)
            p = 0.499f;
        const auto k = (1.0f - p) / (1.0f + p);
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            fade (s);
            s.w += (1.0f - s.w) * 0.005f;
            for (int c = 0; c < 2; ++c)
            {
                const auto xin = (float) ((double) (s.a * io[c][i]) + 1e-30);
                auto z = xin + g * s.fbk[(std::size_t) c];
                for (auto& st : s.ap[(std::size_t) c])
                {
                    const auto yv = -k * z + st;
                    st = k * yv + z;
                    z = yv;
                }
                s.fbk[(std::size_t) c] = z;
                io[c][i] = io[c][i] + s.w * z;
            }
        }
    }

    // Panner: X + LFO pans by delaying (up to ~2 ms) and, by √Y, lowering the far side
    void runPanner (Sub& s, float* l, float* r, int n) noexcept
    {
        const auto dist = std::sqrt (y);
        const auto pan = blockLfo (s, n) + x;
        if (s.panS < 0.0f)
            s.panS = pan;
        if (s.distS < 0.0f)
            s.distS = dist;
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
                panBuf[(std::size_t) c][(std::size_t) s.wp] = io[c][i];
            fade (s);
            s.panS += (pan - s.panS) * 0.005f;
            s.distS += (dist - s.distS) * 0.005f;
            const auto pc = std::clamp (s.panS, 0.0f, 1.0f);
            const auto gR = std::max (2.0f * pc - 1.0f, 0.0f), gL = std::max (1.0f - 2.0f * pc, 0.0f);
            const float d[2] { (float) (panLength - 4) * gR + 1.0f, (float) (panLength - 4) * gL + 1.0f };
            const float gfar[2] { gR, gL };
            for (int c = 0; c < 2; ++c)
            {
                const auto whole = (int) d[c];
                const auto f = d[c] - (float) whole;
                const auto& b = panBuf[(std::size_t) c];
                const auto at = [&] (int back) { auto k = s.wp - back; while (k < 0) k += panLength; return b[(std::size_t) k]; };
                const auto val = f * at (whole + 1) + (1.0f - f) * at (whole);
                const auto xc = io[c][i];
                io[c][i] = val * s.a * (1.0f - s.distS * gfar[c]) + (xc - s.a * xc);
            }
            if (++s.wp >= panLength)
                s.wp = 0;
        }
    }

    // Vox: eight band-passes (Q 12) per side on formants blended across three vowels by X + LFO, scaled by
    // 2^(1.7(Y − 0.5)) ("Size")
    void runVox (Sub& s, float* l, float* r, int n) noexcept
    {
        static constexpr float vowels[3][8] { { 250, 700, 2500, 3500, 4200, 8500, 10000, 12000 },
                                              { 700, 1250, 2500, 3500, 4200, 8500, 10000, 12000 },
                                              { 250, 2500, 2700, 3500, 4200, 8500, 10000, 12000 } };
        const auto v = std::clamp (blockLfo (s, n) + x, 0.0f, 1.0f);
        const auto sv = 3.0f * v;
        const auto e = (int) sv;
        const auto fr = sv - (float) e;
        const auto seg = e < 3 ? e : 0;
        const auto scale = std::pow (2.0f, (y - 0.5f) * 1.7f);
        const auto g = 1.0f - 0.125f * (float) seg;
        for (int k = 0; k < 8; ++k)
        {
            const auto from = vowels[seg][k], to = vowels[(seg + 1) % 3][k];
            const auto hz = (from * (1.0f - fr) + to * fr) * scale;
            if (hz != s.vhz[(std::size_t) k])
            {
                s.vhz[(std::size_t) k] = hz;
                const auto w = twoPi * (double) hz / fs;
                const auto sn = std::sin (w), cs = std::cos (w);
                const auto norm = 1.0 / (1.0 + sn / 24.0);
                const auto clean = [] (double c) { return std::abs (c) < 1e-9 ? 0.0f : (float) c; };
                s.vb0[(std::size_t) k] = clean ((double) (float) (sn / 2.0) * norm);
                s.va1[(std::size_t) k] = clean ((double) (float) (2.0 * cs) * norm);
                s.va2[(std::size_t) k] = clean ((1.0 - sn / 24.0) * norm);
            }
        }
        float* io[2] { l, r };
        for (int i = 0; i < n; ++i)
        {
            fade (s);
            s.w += (1.0f - s.w) * 0.005f;
            const auto m = s.a * s.w;
            for (int c = 0; c < 2; ++c)
            {
                const auto xin = flush (io[c][i]);
                float sum = 0.0f;
                for (std::size_t k = 0; k < 8; ++k)
                {
                    const auto yv = flush ((xin - s.vx2[(std::size_t) c][k]) * s.vb0[k] + s.va1[k] * s.vy1[(std::size_t) c][k] - s.va2[k] * s.vy2[(std::size_t) c][k]);
                    s.vx2[(std::size_t) c][k] = s.vx1[(std::size_t) c][k];
                    s.vx1[(std::size_t) c][k] = xin;
                    s.vy2[(std::size_t) c][k] = s.vy1[(std::size_t) c][k];
                    s.vy1[(std::size_t) c][k] = yv;
                    sum += g * yv;
                }
                io[c][i] = (1.0f - m) * io[c][i] + m * sum;
            }
        }
    }

    double fs = 44100.0, songBeats = -1.0, rateBeats = 1.0, freeSamples = 0.0, expected = 0.0;
    bool wasPlaying = false, held = false;
    float x = 0.5f, y = 0.5f, amount = 0.0f;
    int shape = 0, lastKind = -1;
    bool started = false;
    std::array<Sub, 10> subs {};
    int delayLength = 2, panLength = 5;
    std::array<std::vector<float>, 2> delayBuf, panBuf, scratch;
    tape::Tape flangerTape;
    std::unique_ptr<Effect> reverb;
    EffectParams reverbParams {};
};
} // namespace

const char* multiFxKindName (int kind) noexcept
{
    static const char* const names[] { "Delay", "Reverb", "LP Filter", "HP Filter", "Flanger", "Phaser", "Panner", "Vox" };
    return names[std::clamp (kind, 0, 7)];
}
int multiFxKind (float v) noexcept { return std::clamp ((int) (v * 7.0f), 0, 7); }
const char* multiFxAxisName (int kind, bool yAxis) noexcept
{
    static const char* const xs[] { "Feedback", "Decay", "Frequency", "Frequency", "Frequency", "Frequency", "Panning", "Vowel" };
    static const char* const ys[] { "Filter", "Filter", "Resonance", "Resonance", "Feedback", "Feedback", "Distance", "Size" };
    return (yAxis ? ys : xs)[std::clamp (kind, 0, 7)];
}

std::unique_ptr<Effect> makeMultiFx() { return std::make_unique<MultiFx>(); }

std::string multiFxParamText (int param, float v)
{
    char text[32];
    switch (param)
    {
        case 0: return multiFxKindName (multiFxKind (v));
        case 3:
        {
            static const char* const rates[] { "1/8", "1/4", "3/8", "1/2", "3/4", "beat", "2 beats", "bar", "2 bars", "4 bars" };
            return rates[std::clamp ((int) (v * 9.0f), 0, 9)];
        }
        case 5:
        {
            static const char* const shapes[] { "Sine", "Saw", "Square" };
            return shapes[std::clamp ((int) (v * 2.0f), 0, 2)];
        }
        default: std::snprintf (text, sizeof (text), "%d", (int) (v * 100.0f)); return text;
    }
}
} // namespace lynx::pocket
