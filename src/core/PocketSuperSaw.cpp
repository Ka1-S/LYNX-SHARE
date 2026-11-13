#include "core/PocketSuperSaw.h"
#include "core/MidiControls.h" // round 190dw item 4

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#include "core/HostSlide.h"
#include "core/ModelParams.h"

// Pocket SuperSaw (core/PocketSuperSaw.h). The numbered notes ("§3.2") refer to docs/analise/REA_rodada190by.md.

namespace lynx
{
namespace pocketsaw
{
namespace
{
constexpr int maxVoices = 8, maxOsc = 12, tickSamples = 24, tableCount = 1024, tableSize = 2048;
constexpr float phaseScale = 4292870144.0f; // 2³² − 2²¹: one cycle of the 32-bit phase as FLM scales it
constexpr double twoPi = 6.283185307179586;

inline float clamp01 (float v) noexcept { return v <= 0.0f ? 0.0f : v >= 1.0f ? 1.0f : v; }

// §3.1: table k (0…1023) = the falling saw with k + 1 harmonics, Σ sin(2π·h·i/2048)/h, summed in single precision;
// every table scaled by one factor, 1 / (the largest sample of all tables) (≈ 0.5404).
struct Tables
{
    std::vector<float> data;
    Tables() : data ((std::size_t) tableCount * tableSize)
    {
        std::vector<double> sine ((std::size_t) tableSize);
        for (int i = 0; i < tableSize; ++i)
            sine[(std::size_t) i] = std::sin (twoPi * (double) i / (double) tableSize);
        std::vector<float> acc ((std::size_t) tableSize, 0.0f);
        float peak = 0.0f;
        for (int k = 0; k < tableCount; ++k)
        {
            const int h = k + 1;
            auto* row = data.data() + (std::size_t) k * tableSize;
            for (int i = 0; i < tableSize; ++i)
            {
                acc[(std::size_t) i] += (float) (sine[(std::size_t) ((h * i) & (tableSize - 1))] / (double) h);
                row[i] = acc[(std::size_t) i];
                peak = std::max (peak, std::fabs (row[i]));
            }
        }
        const auto scale = 1.0f / peak;
        for (auto& v : data)
            v *= scale;
    }
    const float* row (int k) const noexcept { return data.data() + (std::size_t) k * tableSize; }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

inline int tableIndex (double f, double fs) noexcept
{
    if (! (f > 0.0))
        return tableCount - 1;
    const auto k = (fs * 0.5) / f;
    return k >= (double) (tableCount - 1) ? tableCount - 1 : k < 1.0 ? 1 : (int) k;
}

inline float pow2 (float x) noexcept { return (float) std::pow (2.0, (double) x); }

struct Lcg
{
    std::uint32_t s = 1;
    // the per-sample dither draw
    std::uint32_t dither() noexcept { return s = s * 0x0bb38435u + 0x3619636bu; }
    // a start phase
    std::uint32_t phase() noexcept { return s = s * 0x77a73631u - 0x0900c56cu; }
    // the random LFO / random arp step: the value is the state before the step
    float unit() noexcept
    {
        const auto v = (float) ((double) s * 2.3283064370807974e-10);
        s = s * 0x4d66b561u + 0x16c0a8e8u;
        return v;
    }
};

enum Stage : std::uint8_t { attack = 0, decay = 1, release = 2 };

struct Envelope
{
    float value = 0.0f;
    std::uint8_t stage = attack;
    // one step of the linear ADSR; true when a release reached 0
    bool step (float attackStep, float decayStep, float sustain, float releaseStep) noexcept
    {
        if (stage == attack)
        {
            value += attackStep;
            if (value >= 1.0f)
                value = 1.0f, stage = decay;
        }
        else if (stage == decay)
        {
            if (sustain < value)
            {
                value -= decayStep;
                if (! (sustain < value))
                    value = sustain;
            }
            else
                value = sustain;
        }
        else
        {
            value -= releaseStep;
            if (value <= 0.0f)
            {
                value = 0.0f;
                return true;
            }
        }
        return false;
    }
};

// what the control tick reads of the module (derived from the parameters when they change, §2)
struct Shared
{
    std::array<float, flmParamCount> P {};
    double fs = 44100.0;
    Lcg rng;
    bool superSync = false;
    int lfoDest = 0, lfoShape = 0, lfoMode = 0, mwDest = 0, bendRange = 2;
    float lfoAmount = 0.0f, lfoFadeStep = 1.0f;
    double lfoBeat = 0.25, lfoInc = 0.0, lfoFree = 0.0;
    float mw = 0.0f, mwAmount = 0.0f, bend = 0.0f, pitchOffset = 0.0f;
    float fAttack = 1.0f, fDecay = 1.0f, fSustain = 1.0f, fRelease = 1.0f;
    float aDecay = 1.0f, aSustain = 1.0f, aRelease = 1.0f;
    float sAttack = 1.0f, sDecay = 1.0f, sSustain = 1.0f, sRelease = 1.0f;
    float mwTerm() const noexcept { return mwAmount * mw; }
};

struct Voice
{
    Shared* m = nullptr;
    int key = -1;
    float velocity = 0.0f, pan = 0.5f;
    float note = 0.0f, target = 0.0f, glide = 0.0f; // the glided note, where it goes, the step per tick
    bool rising = false;                             // the note is above the one it glides from (Sync Kbd Dir.)
    std::uint32_t phase[maxOsc] {}, inc[maxOsc] {};
    const float* table[maxOsc] {};
    const float* subTable[maxOsc] {};
    float gainL[maxOsc] {}, gainR[maxOsc] {};
    int count = 1;
    bool pulse = false, sub = false, stereo = false;
    std::uint32_t pulseOffset = 0;
    float subGain = 0.0f, normaliser = 1.0f;
    float hz = 0.0f, cycles = 0.0f; // the voice's frequency, and per sample
    float yL[4] {}, yR[4] {}, A[4][4] {}, b[4] {};
    Envelope amp, filter, sync;
    float attackStep = 1.0f;
    float lfo = 0.0f, lfoHold = 0.0f, fade = 0.0f;
    double lfoPhase = 0.0;
    int tick = 0;
    std::uint32_t started = 0;
    float gL = -1.0f, gR = -1.0f, targetL = 0.0f, targetR = 0.0f; // (−1: takes the target at the first tick)
    float notePitch = 0.0f;
    // the host's slide notes and channel porta (FL's workflow in LYNX, not FLM's): cents added to the voice's pitch
    HostSlide slide;

    bool active() const noexcept { return velocity != 0.0f; }

    void reset() noexcept
    {
        std::fill (std::begin (phase), std::end (phase), 0u);
        std::fill (std::begin (inc), std::end (inc), 0u);
        std::fill (std::begin (yL), std::end (yL), 0.0f);
        std::fill (std::begin (yR), std::end (yR), 0.0f);
        amp = filter = sync = Envelope();
        fade = 0.0f;
        tick = 0;
        velocity = 0.0f;
        lfoPhase = 0.0;
        gL = gR = -1.0f;
        key = -1;
        slide = HostSlide();
        lfoHold = m->rng.unit(); // (FLM draws with the dither generator here; one generator per instance in LYNX)
    }

    void releaseNow() noexcept { amp.stage = filter.stage = sync.stage = release; }

    // §3.7: a note on this voice. glideFrom < 0: no glide; retrigger = false: a mono legato note
    void start (int k, float vel, float notePan, float glideFrom, bool retrigger, float pitch) noexcept
    {
        const auto& P = m->P;
        const auto silent = velocity == 0.0f;
        if (amp.stage == release)
            retrigger = true;
        velocity = vel;
        if (notePan >= 0.0f)
            pan = notePan;
        key = k;
        target = note = (float) k;
        if (glideFrom >= 0.0f)
            note = glideFrom;
        glide = 0.0f;
        rising = glideFrom < (float) k;
        const auto t = P[flm::slideTime] * 0.5f * P[flm::slideTime] * 0.5f;
        if (t > 0.001f)
        {
            if (P[flm::slideStatic] == 0.0f)
            {
                glide = (float) (24.0 / ((double) t * m->fs));
                if (target < note)
                    glide = -glide;
            }
            else
                glide = (float) (((double) (target - note) / ((double) t * m->fs)) * 24.0);
        }
        const auto retrigPhases = P[flm::retrigPhases] != 0.0f;
        if (silent && ! retrigPhases)
            for (auto& p : phase)
                p = m->rng.phase();
        if (retrigger)
        {
            if (retrigPhases && P[flm::superSync] == 0.0f)
            {
                const auto step = (std::uint32_t) (std::int64_t) (P[flm::phasesOffset] * P[flm::phasesOffset] * 4.2520177e9f);
                std::uint32_t p = 0;
                for (auto& ph : phase)
                    ph = p, p += step;
            }
            if (m->lfoMode == 1)
                lfoPhase = 0.0;
            fade = 0.0f;
            filter = Envelope();
            sync = Envelope();
            amp.stage = attack;
        }
        if (silent && m->lfoMode == 0)
            lfoPhase = m->lfoFree;
        notePitch = pitch;
        // the attack always lasts its time from the level it starts at; a voice still sounding rises no faster
        // than the 2.7 ms declick
        const auto a = envelopeStep (0.9f * P[flm::levelAttack], (float) m->fs);
        const auto declick = envelopeStep (0.01f, (float) m->fs);
        attackStep = (silent || a <= declick) ? (1.0f - amp.value) * a : declick;
    }

    float lfoTo (int dest) const noexcept { return m->lfoDest == dest ? lfo : 0.0f; }
    float wheelTo (int dest) const noexcept { return m->mwDest == dest ? m->mwTerm() : 0.0f; }

    void lfoTick() noexcept
    {
        float shape;
        switch (m->lfoShape)
        {
            case 0: shape = (float) std::sin (lfoPhase * twoPi); break;
            case 1: shape = (float) (1.0 - lfoPhase); break;
            case 2: shape = lfoPhase < 0.5 ? 1.0f : 0.0f; break;
            default: shape = lfoHold; break;
        }
        lfo = shape * m->lfoAmount;
        if (fade < 1.0f)
        {
            fade += m->lfoFadeStep * 24.0f;
            if (fade > 1.0f)
                fade = 1.0f;
        }
        lfo *= fade;
        lfoPhase += m->lfoInc * 24.0;
        if (lfoPhase >= 1.0)
        {
            if (m->lfoMode == 1)
                lfoPhase = 1.0;
            else
            {
                lfoPhase -= 1.0;
                if (m->lfoShape > 2)
                {
                    lfoHold = m->rng.unit();
                    lfo = lfoHold * m->lfoAmount;
                }
            }
        }
    }

    // §3.2 normal mode, part 1: the stack's size, flags, normaliser and stereo spread
    void layoutUnison() noexcept
    {
        const auto& P = m->P;
        auto spread = P[flm::unisonPan];
        if (spread < 0.001f)
            spread = 0.0f;
        pulse = P[flm::pulseMode] > 0.5f;
        subGain = P[flm::subLevel] * P[flm::subLevel];
        sub = subGain >= 0.001f;
        if (! sub)
            subGain = 0.0f;
        count = unisonCount (P[flm::unison]);
        normaliser = voiceNormaliser (count);
        stereo = false;
        if (count > 1 && spread != 0.0f)
        {
            stereo = true;
            for (int u = 0; u < count; ++u)
            {
                const auto x = (float) u / (float) (count - 1);
                const auto s = ((x + x) - 1.0f) * spread;
                gainL[u] = s >= 0.0f ? 1.0f - s : 1.0f;
                gainR[u] = s >= 0.0f ? 1.0f : s + 1.0f;
            }
        }
    }

    // §3.2 normal mode, part 2: increments, tables and the pulse offset
    void tuneUnison() noexcept
    {
        const auto& P = m->P;
        const auto width = clamp01 (P[flm::pulseWidth] + lfoTo (4) + wheelTo (3));
        const auto detuneKnob = P[flm::detune] * P[flm::detune] + lfoTo (5) + wheelTo (4);
        std::fill (std::begin (inc), std::end (inc), 0u);
        for (int u = 0; u < count; ++u)
        {
            const auto ratio = count > 1 ? detuneRatioRaw (u, count, clamp01 (detuneKnob), P[flm::detuneCurve]) : 1.0f;
            inc[u] = (std::uint32_t) (std::int64_t) (ratio * cycles * 0.5f * phaseScale);
            const auto f = ratio * hz;
            table[u] = tables().row (tableIndex ((double) f, m->fs));
            if (pulse)
                pulseOffset = (std::uint32_t) (std::int64_t) (width * 2.1464351e9f);
            if (sub)
                subTable[u] = tables().row (tableIndex ((double) (f * 0.5f), m->fs));
        }
    }

    static float detuneRatioRaw (int u, int n, float d01, float curveKnob) noexcept
    {
        const auto c = curveKnob * 2.0f - 1.0f;
        const auto e = c >= 0.0f ? 1.0f + 4.0f * c : 1.0f + 0.9f * c;
        const auto x = (float) u / (float) (n - 1);
        const auto bent = (float) std::pow ((double) x, (double) e);
        return pow2 (((bent + bent) - 1.0f) * (d01 / 12.0f));
    }

    // §3.2 SuperSync: pairs of saws (+1 at the pulse's start, −1 at its end) laid out again from oscillator 0's phase
    void layoutSync() noexcept
    {
        const auto& P = m->P;
        normaliser = 1.0f;
        pulse = sub = false;
        stereo = true;
        const auto width = clamp01 (P[flm::syncPulseWidth] * 0.5f + lfoTo (4) + wheelTo (3));
        sync.step (m->sAttack, m->sDecay, m->sSustain, m->sRelease);
        auto amount = P[flm::syncEgAmount] * 2.0f - 1.0f;
        amount = amount * amount * amount;
        if (P[flm::syncKbdDir] != 0.0f && ! rising)
            amount = -amount;
        const auto s = clamp01 (amount * sync.value + P[flm::sync] + lfoTo (3) + wheelTo (2));
        const auto spacing = 1.0f / (s * 5.0f + 1.0f);
        const auto base = phase[0];
        const auto step = (std::uint32_t) (std::int64_t) (cycles * phaseScale);
        const auto* t = tables().row (tableIndex ((double) hz, m->fs));
        double at = 0.0;
        count = 0;
        for (int pair = 0; pair < maxOsc / 2; ++pair)
        {
            const int a = 2 * pair, b = a + 1;
            gainL[a] = gainR[a] = 1.0f;
            gainL[b] = gainR[b] = -1.0f;
            const auto end = std::min ((double) (spacing * width) + at, 1.0);
            phase[a] = (std::uint32_t) (std::int64_t) (at * 4294967295.0) + base;
            phase[b] = (std::uint32_t) (std::int64_t) (end * 4294967295.0) + base;
            inc[a] = inc[b] = step;
            table[a] = table[b] = t;
            at += (double) spacing;
            count += 2;
            if (at >= 1.0)
                break;
        }
    }

    // §3.3: the ladder's one-sample transition from the cutoff and resonance now
    void tuneFilter() noexcept
    {
        const auto& P = m->P;
        filter.step (m->fAttack, m->fDecay, m->fSustain, m->fRelease);
        const auto vel = clamp01 (velocity);
        const auto c = clamp01 (P[flm::cutoff] + lfoTo (0) + P[flm::filterVel] * vel + wheelTo (0));
        const auto res = clamp01 (P[flm::resonance] + lfoTo (1) + wheelTo (1));
        const auto fc = (float) cutoffHz (c, P[flm::filterEgAmount], filter.value, P[flm::filterKbd], note);
        // 4096 Euler sub-steps of the four-stage ladder per sample: the sub-step as a matrix, squared 12 times
        const auto g = clamp01 ((float) ((0.000244140625 / m->fs) * (double) (fc * 6.25f)));
        const auto a = 1.0f - g, k = res * -4.0f * g;
        float M[4][4] = { { a, 0.0f, 0.0f, k },
                          { a * g, a, 0.0f, k * g },
                          { a * g * g, a * g, a, k * g * g },
                          { a * g * g * g, a * g * g, a * g, k * g * g * g + a } };
        float v[4] = { g, g * g, g * g * g, g * g * g * g };
        for (int pass = 0; pass < 12; ++pass)
        {
            float M2[4][4], v2[4];
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                    M2[i][j] = M[i][0] * M[0][j] + M[i][1] * M[1][j] + M[i][2] * M[2][j] + M[i][3] * M[3][j];
                v2[i] = M[i][0] * v[0] + M[i][1] * v[1] + M[i][2] * v[2] + M[i][3] * v[3] + v[i];
            }
            std::memcpy (M, M2, sizeof (M));
            std::memcpy (v, v2, sizeof (v));
        }
        std::memcpy (A, M, sizeof (A));
        std::memcpy (b, v, sizeof (b));
    }

    void controlTick() noexcept
    {
        const auto& P = m->P;
        lfoTick();
        if (! m->superSync)
            layoutUnison();
        if (glide != 0.0f && note != target)
        {
            note += glide;
            if ((glide < 0.0f && note < target) || (glide > 0.0f && target < note))
                glide = 0.0f, note = target;
        }
        else
            note = target;
        hz = note >= 1.0f ? pow2 ((note - 69.0f) / 12.0f) * 440.0f : 0.0f;
        const auto semis = ((float) m->bendRange * m->bend + (float) (int) (P[flm::transpose] * 24.0f)) - 12.0f;
        if (slide.moving())
            slide.advance ((float) tickSamples);
        hz = (float) (std::pow (2.0, (double) (semis * 0.083333336f + lfoTo (2))
                                         + (double) (notePitch + m->pitchOffset + slide.cents * 0.01f) * 0.08333333333333333)
                      * (double) hz);
        cycles = (float) ((double) hz / m->fs);
        if (m->superSync)
            layoutSync();
        else
            tuneUnison();
        tuneFilter();
        const auto lv = clamp01 (P[flm::level] + lfoTo (6) + wheelTo (5));
        const auto p = clamp01 ((pan - 0.5f) + P[flm::pan] + lfoTo (7) + wheelTo (6));
        const auto left = std::min ((1.0f - p) + (1.0f - p), 1.0f), right = std::min (p + p, 1.0f);
        const auto g = clamp01 (velocity) * lv * lv * normaliser;
        targetL = g * left;
        targetR = g * right;
        if (gL < 0.0f)
            gL = targetL;
        if (gR < 0.0f)
            gR = targetR;
    }

    // adds n samples; a release that ends resets the voice
    void render (float* left, float* right, int n, double songBeats) noexcept
    {
        if (m->lfoMode == 2 && songBeats >= 0.0)
        {
            const auto p = songBeats / m->lfoBeat;
            lfoPhase = p - (double) (int) p;
        }
        const auto syncRead = m->superSync;
        for (int i = 0; i < n; ++i)
        {
            if (tick == 0)
                controlTick();
            if (++tick == tickSamples)
                tick = 0;
            const auto dither = m->rng.dither() & 0x3fffffu;
            float sumL = 0.0f, sumR = 0.0f;
            for (int u = 0; u < count; ++u)
            {
                const auto p = phase[u];
                const auto at = (syncRead ? p : p * 2u) + dither;
                auto o = table[u][at >> 21];
                if (pulse)
                    o -= table[u][(at + pulseOffset) >> 21];
                if (sub)
                    o += subTable[u][(p + dither) >> 21] * subGain;
                if (stereo)
                {
                    sumR += o * gainR[u];
                    o *= gainL[u];
                }
                sumL += o;
                phase[u] = p + inc[u];
            }
            if (! stereo)
                sumR = sumL;
            runLadder (yL, sumL);
            runLadder (yR, sumR);
            if (amp.stage == attack)
            {
                amp.value += attackStep;
                if (amp.value >= 1.0f)
                    amp.value = 1.0f, amp.stage = decay;
            }
            else if (amp.step (0.0f, m->aDecay, m->aSustain, m->aRelease))
            {
                reset();
                return;
            }
            const auto level = amp.value * amp.value;
            left[i] += level * yL[3] * gL;
            right[i] += yR[3] * level * gR;
            gL += (targetL - gL) * 0.01f;
            gR += (targetR - gR) * 0.01f;
        }
    }

    void runLadder (float* y, float x) const noexcept
    {
        const float y0 = y[0], y1 = y[1], y2 = y[2], y3 = y[3];
        const auto n0 = x * b[0] + A[0][3] * y3 + A[0][2] * y2 + A[0][0] * y0 + A[0][1] * y1;
        y[1] = x * b[1] + y3 * A[1][3] + y2 * A[1][2] + y1 * A[1][1] + A[1][0] * y0;
        y[2] = x * b[2] + A[2][3] * y3 + A[2][2] * y2 + A[2][1] * y1 + A[2][0] * y0;
        y[3] = x * b[3] + A[3][3] * y3 + A[3][2] * y2 + A[3][0] * y0 + A[3][1] * y1;
        y[0] = n0 / (n0 * n0 * 1e-05f + 1.0f); // the first stage alone is softened
    }
};
} // namespace

// --- laws

float envelopeSeconds (float v) noexcept
{
    auto x = pow2 (clamp01 (v));
    for (int i = 0; i < 6; ++i)
        x = pow2 (x - 1.0f);
    return pow2 ((x - 1.0f) * 5.0f) - 1.0f;
}

float envelopeStep (float v, float ticksPerSecond) noexcept
{
    return 1.0f / ((float) (int) (envelopeSeconds (v) * ticksPerSecond) + 1.0f);
}

int unisonCount (float v) noexcept { return std::clamp ((int) (v * 11.0f + 1.0f), 1, maxOsc); }

float detuneRatio (int u, int n, float detuneKnob, float curveKnob) noexcept
{
    return n > 1 ? Voice::detuneRatioRaw (u, n, clamp01 (detuneKnob * detuneKnob), curveKnob) : 1.0f;
}

float voiceNormaliser (int n) noexcept { return (float) std::pow (10.0, -0.05 * (double) n); }

double cutoffHz (float c, float egAmount, float env, float kbd, float note) noexcept
{
    const auto oct = c * 10.3f + 4.0f + (egAmount * 2.0f - 1.0f) * env * 8.0f + (note - 48.0f) * kbd * 0.083333336f;
    return std::min ((double) pow2 (oct), 20000.0);
}

double lfoPeriodSeconds (float v) noexcept
{
    const auto r = 1.0f - clamp01 (v);
    return (double) (r * r * r * 30.0f + 0.033333335f);
}

double lfoBeatLength (float v) noexcept
{
    static const double beats[] = { 0.125, 0.25, 0.75, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 }; // (2 and 3 in FLM's order)
    return beats[std::clamp ((int) (v * 9.0f), 0, 9)];
}

double arpStepBeats (float v) noexcept
{
    static const double beats[] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0 };
    return beats[std::clamp ((int) (v * 5.0f), 0, 5)];
}

double delayBeats (float v) noexcept
{
    static const double beats[] = { 0.25, 0.375, 0.5, 0.75, 1.0 };
    return beats[std::clamp ((int) (v * 4.0f), 0, 4)];
}

int transposeSemitones (float v) noexcept { return (int) (v * 24.0f) - 12; }
int bendRangeSemitones (float v) noexcept { return std::clamp ((int) (v * 11.0f + 1.0f), 1, 12); }
int voiceLimit (float v) noexcept { return std::clamp ((int) (v * 7.0f + 1.0f), 1, maxVoices); }
const float* waveTable (double f, double fs) noexcept { return tables().row (tableIndex (f, fs)); }
int waveTableHarmonics (double f, double fs) noexcept { return tableIndex (f, fs) + 1; }

const std::array<float, flmParamCount>& defaults() noexcept
{
    static const auto d = [] {
        std::array<float, flmParamCount> v {};
        const std::pair<int, float> set[] = { { 0, 1.0f }, { 1, 1.0f }, { 5, 0.2f }, { 7, 0.5f }, { 12, 1.0f }, { 14, 1.0f }, { 15, 1.0f },
                                              { 16, 1.0f }, { 17, 0.5f }, { 19, 1.0f }, { 23, 1.0f }, { 24, 1.0f }, { 25, 1.0f }, { 26, 0.5f },
                                              { 29, 1.0f }, { 30, 1.0f }, { 32, 1.0f }, { 33, 0.5f }, { 36, 0.5f }, { 37, 0.1f }, { 38, 1.0f },
                                              { 44, 0.5f }, { 47, 0.2f }, { 48, 0.5f }, { 51, 0.5f }, { 53, 1.0f }, { 54, 0.775f },
                                              { 55, 0.4f }, { 57, 0.5f }, { 58, 0.5f } };
        for (const auto& [k, x] : set)
            v[(std::size_t) k] = x;
        return v;
    }();
    return d;
}

// --- the module

struct Engine::Impl
{
    Shared m;
    Voice voices[maxVoices];
    bool envDirty = true, lfoDirty = true, monoSwitched = false;
    // notes
    float held[128] {};
    int lastKey = -1;
    std::uint32_t counter = 0;
    // arpeggiator (§3.7)
    int arpMode = 0, arpCount = 0, arpIndex = 0;
    double arpClock = 0.0;
    std::array<std::uint16_t, 1024> arpKey {};
    std::array<float, 1024> arpVelocity {};
    // effects (§3.6)
    bool distortion = false;
    float drive = 1.0f, driveNorm = 1.0f, trim = 1.0f, delayFeedback = 0.0f, delayMix = 0.0f;
    double delayLength = 0.75;
    std::vector<float> lineL, lineR;
    int at[2] = { 0, 0 };
    float lp[2] = { 0.0f, 0.0f };
    int tail = 0;
    float scratchL[512] {}, scratchR[512] {};

    Impl()
    {
        m.P = defaults();
        for (auto& v : voices)
            v.m = &m;
        (void) tables();
        applyAll();
    }

    void applyAll() noexcept
    {
        for (int i = 0; i < flmParamCount; ++i)
            apply (i);
    }

    // the setter's side effects (§2)
    void apply (int id) noexcept
    {
        const auto v = m.P[(std::size_t) id];
        switch (id)
        {
            case flm::superSync: m.superSync = v != 0.0f; break;
            case flm::syncAttack: case flm::syncDecay: case flm::syncSustain: case flm::syncRelease:
            case flm::filterAttack: case flm::filterDecay: case flm::filterSustain: case flm::filterRelease:
            case flm::levelDecay: case flm::levelSustain: case flm::levelRelease:
                envDirty = true;
                break;
            case flm::bendRange: m.bendRange = bendRangeSemitones (v); break;
            case flm::lfoShape: case flm::lfoRate: case flm::lfoBeat: case flm::lfoAmount: case flm::lfoDest: case flm::lfoFade:
                lfoDirty = true;
                break;
            case flm::lfoMode: m.lfoMode = std::clamp ((int) (v + v), 0, 2); break;
            case flm::arpRate: case flm::arpRange: case flm::arpMode:
                arpMode = std::clamp ((int) (m.P[flm::arpMode] * 5.0f), 0, 5);
                buildArp();
                break;
            case flm::mono:
                if (v != 0.0f)
                    monoSwitched = true;
                break;
            case flm::distortion:
                distortion = v > 0.005f;
                drive = (float) std::pow (10.0, (double) (v + v)) - 0.99f;
                driveNorm = distortion ? 1.0f / std::tanh (drive) : 1.0f;
                break;
            case flm::trim: trim = (float) std::pow (10.0, -0.9 * (double) (1.0f - v)); break;
            case flm::delayTime: delayLength = delayBeats (v); break;
            case flm::delayFeedback: delayFeedback = v * 0.99f; break;
            case flm::delayMix: delayMix = v * v; break;
            case flm::mwAmount: m.mwAmount = v + v - 1.0f; lfoDirty = true; break;
            case flm::mwDest: m.mwDest = std::clamp ((int) (v * 8.0f), 0, 8); lfoDirty = true; break;
            default: break;
        }
    }

    void updateEnvelopes() noexcept
    {
        envDirty = false;
        const auto& P = m.P;
        const auto fs = (float) m.fs, ticks = (float) (m.fs * 0.041666666666666664);
        m.fAttack = envelopeStep (P[flm::filterAttack], ticks);
        m.fDecay = envelopeStep (P[flm::filterDecay], ticks);
        m.fRelease = envelopeStep (P[flm::filterRelease], ticks);
        m.fSustain = P[flm::filterSustain];
        m.aDecay = envelopeStep (P[flm::levelDecay], fs);
        m.aRelease = envelopeStep (P[flm::levelRelease] * 0.85f + 0.05f, fs);
        m.aSustain = P[flm::levelSustain];
        m.sAttack = envelopeStep (P[flm::syncAttack], ticks);
        m.sDecay = envelopeStep (P[flm::syncDecay], ticks);
        m.sRelease = envelopeStep (P[flm::syncRelease], ticks);
        m.sSustain = P[flm::syncSustain];
    }

    void updateLfo() noexcept
    {
        lfoDirty = false;
        const auto& P = m.P;
        m.lfoDest = std::clamp ((int) (P[flm::lfoDest] * 7.0f + 0.5f), 0, 7);
        m.lfoShape = std::clamp ((int) (P[flm::lfoShape] * 3.0f + 0.5f), 0, 3);
        m.lfoFadeStep = envelopeStep (P[flm::lfoFade], (float) m.fs);
        auto a = clamp01 (P[flm::lfoAmount] + (m.mwDest == 7 ? m.mwTerm() : 0.0f));
        a = (a + a) - 1.0f;
        m.lfoAmount = a < 0.0f ? -(a * a) : a * a;
        m.lfoBeat = lfoBeatLength (P[flm::lfoBeat]);
    }

    // §3.7: the held keys ascending, repeated an octave up per range step, ordered by the mode
    void buildArp() noexcept
    {
        int keys[128], n = 0;
        float vels[128];
        for (int k = 0; k < 128; ++k)
            if (held[k] != 0.0f)
                keys[n] = k, vels[n] = held[k], ++n;
        if (n == 0 || arpMode == 0)
        {
            arpClock = 0.0;
            arpIndex = 0;
            arpCount = 0;
            for (auto& v : voices)
                if (v.active())
                    v.releaseNow();
            return;
        }
        const auto octaves = std::clamp ((int) (m.P[flm::arpRange] * 3.0f), 0, 3) + 1;
        int up = 0;
        std::uint16_t upKey[512];
        float upVel[512];
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < n; ++i)
                upKey[up] = (std::uint16_t) (keys[i] + 12 * o), upVel[up] = vels[i], ++up;
        int c = 0;
        const auto push = [&] (int i) { arpKey[(std::size_t) c] = upKey[i], arpVelocity[(std::size_t) c] = upVel[i], ++c; };
        if (arpMode == 2)
            for (int i = up - 1; i >= 0; --i)
                push (i);
        else
        {
            for (int i = 0; i < up; ++i)
                push (i);
            if (arpMode == 3)
                for (int i = up - 2; i >= 1; --i)
                    push (i);
            else if (arpMode == 4)
                for (int i = up - 1; i >= 0; --i)
                    push (i);
        }
        arpCount = c;
    }

    void noteOn (int key, float velocity, float pan, float pitch) noexcept
    {
        Voice* voice = &voices[0];
        bool retrigger = false;
        if (m.P[flm::mono] == 0.0f)
        {
            retrigger = true;
            const auto limit = voiceLimit (m.P[flm::voices]);
            voice = nullptr;
            for (int i = 0; i < limit && voice == nullptr; ++i)
                if (! voices[i].active())
                    voice = &voices[i];
            if (voice == nullptr)
            {
                voice = &voices[0];
                for (int i = 1; i < limit; ++i)
                    if (voices[i].started < voice->started)
                        voice = &voices[i];
            }
        }
        auto from = (float) lastKey;
        if (m.P[flm::slideHeld] != 0.0f)
        {
            bool other = false;
            for (int k = 0; k < 128 && ! other; ++k)
                other = held[k] != 0.0f && k != key;
            if (! other)
                from = (float) key;
        }
        voice->started = ++counter;
        voice->start (key, velocity, pan, from, retrigger, pitch);
        // a new note: no slide of an older one; LYNX's channel porta (the rack set it around this note on) glides it in
        voice->slide = HostSlide();
        voice->slide.anchor (NAN);
        lastKey = key;
    }

    // a slide note of the host: every sounding, unreleased voice glides to `key` + `cents` over `samples`, keeping
    // its key (HostSlide.h); `cents` is measured like the `pitch` its note on gave (× 100)
    void slideTo (int key, float cents, float samples) noexcept
    {
        for (auto& v : voices)
            if (v.active() && v.amp.stage != release)
                v.slide.start (100.0f * (float) (key - v.key) + cents - v.notePitch * 100.0f, samples);
    }

    void noteOff (int key) noexcept
    {
        if (m.P[flm::mono] == 0.0f)
        {
            for (auto& v : voices)
                if (v.key == key && v.active())
                    v.releaseNow();
            return;
        }
        auto& v = voices[0];
        if (! v.active())
            return;
        int top = -1;
        for (int k = 0; k < 128; ++k)
            if (held[k] != 0.0f)
                top = k;
        if (top >= 0) // back to the highest key still held, legato
            v.start (top, held[top], -1.0f, (float) v.key, false, v.notePitch);
        else
            v.releaseNow();
    }

    void note (int key, float velocity, float pan, float pitch) noexcept
    {
        key = std::clamp (key, 0, 127);
        held[key] = velocity;
        if (arpMode != 0)
        {
            buildArp();
            return;
        }
        if (velocity == 0.0f)
            noteOff (key);
        else
            noteOn (key, velocity, pan, pitch);
    }

    // §3.6: distortion / trim on the blocks where a voice played, then the delay while it has a tail
    bool effects (float* l, float* r, int n, double tempo, bool played) noexcept
    {
        if (played && (distortion || trim < 1.0f))
        {
            const auto g = trim * driveNorm;
            for (int i = 0; i < n; ++i)
            {
                auto a = l[i], b = r[i];
                if (distortion)
                    a = std::tanh (drive * a), b = std::tanh (drive * b);
                l[i] = a * g;
                r[i] = b * g;
            }
        }
        if (! played && tail < 1)
            return false;
        auto f = m.P[flm::delayFilter] * 2.0f;
        const auto high = f > 1.0f;
        if (high)
        {
            f = (f - 1.0f) * (f - 1.0f);
            f *= f;
        }
        const auto length = std::clamp ((int) (delayLength * m.fs * (60.0 / tempo)), 1, (int) lineL.size());
        float* line[2] = { lineL.data(), lineR.data() };
        float* io[2] = { l, r };
        const auto hold = (int) (m.fs * 4.0);
        for (int i = 0; i < n; ++i)
        {
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                if (at[c] >= length)
                    at[c] = 0;
                const auto x = io[c][i];
                const auto z = line[c][at[c]] + 1e-30f;
                lp[c] += (z - lp[c]) * f;
                const auto y = high ? z - lp[c] : lp[c];
                out[c] = y * delayMix + x;
                line[c][at[c]] = (x + y) * delayFeedback; // (the first echo is already scaled by the feedback)
                ++at[c];
            }
            tail = (std::fabs (out[0]) > 0.001f || std::fabs (out[1]) > 0.001f) ? hold : tail - 1;
            l[i] = out[0];
            r[i] = out[1];
        }
        return true;
    }

    bool renderVoices (float* l, float* r, int n, double songBeats) noexcept
    {
        bool played = false;
        for (auto& v : voices)
            if (v.active())
            {
                played = true;
                v.render (l, r, n, songBeats);
            }
        return played;
    }

    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept
    {
        if (m.P[flm::enable] < 0.5f || n <= 0)
            return;
        if (! (tempo > 0.0))
            tempo = 120.0;
        if (monoSwitched)
        {
            monoSwitched = false;
            if (m.P[flm::mono] != 0.0f)
                for (int i = 1; i < maxVoices; ++i)
                    voices[i].reset();
        }
        if (envDirty)
            updateEnvelopes();
        if (lfoDirty)
            updateLfo();
        const auto period = m.lfoMode == 2 ? (float) ((m.lfoBeat * 60.0) / tempo)
                                           : (float) lfoPeriodSeconds (m.P[flm::lfoRate] + (m.mwDest == 8 ? m.mwTerm() : 0.0f));
        m.lfoInc = 1.0 / ((double) period * m.fs);
        std::fill (scratchL, scratchL + n, 0.0f);
        std::fill (scratchR, scratchR + n, 0.0f);
        bool any = false;
        if (arpMode == 0)
        {
            const auto played = renderVoices (scratchL, scratchR, n, songBeats);
            any = effects (scratchL, scratchR, n, tempo, played) || played;
            // the free-running LFO phase new voices take
            const auto p = (double) n * m.lfoInc + m.lfoFree;
            m.lfoFree = p - (double) (int) p;
        }
        else
        {
            const auto step = arpStepBeats (m.P[flm::arpRate]) * 60.0 * (m.fs / tempo);
            for (int pos = 0; pos < n;)
            {
                if (arpClock >= 0.0)
                {
                    arpClock -= step;
                    for (auto& v : voices)
                        if (v.active())
                            v.releaseNow();
                    if (arpCount != 0)
                    {
                        arpIndex = std::clamp (arpIndex, 0, arpCount - 1);
                        noteOn (arpKey[(std::size_t) arpIndex], arpVelocity[(std::size_t) arpIndex], 0.5f, 0.0f);
                        if (arpMode == 5)
                            arpIndex = std::clamp ((int) ((double) (arpCount - 1) * (double) m.rng.unit() + 0.5), 0, arpCount - 1);
                        else if (++arpIndex >= arpCount)
                            arpIndex = 0;
                    }
                }
                const auto seg = std::min (std::max ((int) -arpClock, 1), n - pos);
                arpClock += (double) seg;
                const auto played = renderVoices (scratchL + pos, scratchR + pos, seg, songBeats);
                any = (effects (scratchL + pos, scratchR + pos, seg, tempo, played) || played) || any;
                pos += seg;
            }
        }
        if (any)
            for (int i = 0; i < n; ++i)
            {
                left[i] += scratchL[i];
                right[i] += scratchR[i];
            }
    }

    void reset() noexcept
    {
        std::fill (lineL.begin(), lineL.end(), 0.0f);
        std::fill (lineR.begin(), lineR.end(), 0.0f);
        at[0] = at[1] = 0;
        lp[0] = lp[1] = 0.0f;
        std::fill (std::begin (held), std::end (held), 0.0f);
        counter = 0;
        for (auto& v : voices)
            v.reset();
        lastKey = -1;
        tail = 0;
        m.lfoFree = 0.0;
        arpClock = 0.0;
        arpIndex = 0;
        arpCount = 0;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) { prepare (44100.0); }
Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    auto& s = *impl;
    s.m.fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto frames = (std::size_t) std::max (1, (int) (s.m.fs + s.m.fs));
    s.lineL.assign (frames, 0.0f);
    s.lineR.assign (frames, 0.0f);
    s.envDirty = s.lfoDirty = true;
    s.reset();
}

double Engine::sampleRate() const noexcept { return impl->m.fs; }

void Engine::setParam (int index, float value) noexcept
{
    if (index < 0 || index >= flmParamCount || ! std::isfinite (value))
        return;
    auto& p = impl->m.P[(std::size_t) index];
    if (p == value)
        return;
    p = value;
    impl->apply (index);
}

float Engine::param (int index) const noexcept { return index >= 0 && index < flmParamCount ? impl->m.P[(std::size_t) index] : 0.0f; }
void Engine::note (int key, float velocity, float pan, float pitch) noexcept { impl->note (key, velocity, pan, pitch); }
void Engine::setPitchOffset (float semitones) noexcept { impl->m.pitchOffset = semitones; }
void Engine::slideTo (int key, float cents, float samples) noexcept { impl->slideTo (key, cents, samples); }

void Engine::setModWheel (float value) noexcept
{
    impl->m.mw = value;
    impl->lfoDirty = true;
}

void Engine::setPitchBend (float value) noexcept { impl->m.bend = value; }

void Engine::allNotesOff() noexcept
{
    auto& s = *impl;
    std::fill (std::begin (s.held), std::end (s.held), 0.0f);
    s.arpCount = 0;
    s.arpIndex = 0;
    s.arpClock = 0.0;
    for (auto& v : s.voices)
        if (v.active())
            v.releaseNow();
}

void Engine::reset() noexcept { impl->reset(); }

void Engine::render (float* left, float* right, int n, double tempo, double songBeats) noexcept
{
    for (int pos = 0; pos < n; pos += 512)
    {
        const auto c = std::min (512, n - pos);
        impl->render (left + pos, right + pos, c, tempo,
                      songBeats >= 0.0 ? songBeats + (double) pos * (tempo > 0.0 ? tempo : 120.0) / (60.0 * impl->m.fs) : songBeats);
    }
}

bool Engine::idle() const noexcept { return activeVoices() == 0 && impl->tail < 1; }

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.active() ? 1 : 0;
    return n;
}

void Engine::seed (std::uint32_t s) noexcept { impl->m.rng.s = s; }

std::vector<float> renderReference (const std::vector<float>& flmParams, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    int block, double bpm, double songPos, std::uint32_t seed)
{
    Engine e;
    e.prepare (sampleRate);
    e.seed (seed);
    for (int i = 0; i < flmParamCount && i < (int) flmParams.size(); ++i)
        e.setParam (i, flmParams[(std::size_t) i]);
    struct Event
    {
        int at, key;
        float velocity, pan;
    };
    std::vector<Event> events;
    for (const auto& n : notes)
    {
        events.push_back ({ n.start, n.key, n.velocity, n.pan });
        if (n.release >= 0)
            events.push_back ({ n.release, n.key, 0.0f, 0.5f });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });
    std::vector<float> l ((std::size_t) length, 0.0f), r ((std::size_t) length, 0.0f);
    std::size_t next = 0;
    block = std::max (1, block);
    for (int pos = 0; pos < length;)
    {
        while (next < events.size() && events[next].at <= pos)
        {
            e.note (events[next].key, events[next].velocity, events[next].pan);
            ++next;
        }
        auto n = std::min (block - pos % block, length - pos);
        if (next < events.size())
            n = std::min (n, events[next].at - pos);
        e.render (l.data() + pos, r.data() + pos, n, bpm, songPos >= 0.0 ? songPos + (double) pos * bpm / (60.0 * sampleRate) : -1.0);
        pos += n;
    }
    std::vector<float> out ((std::size_t) length * 2);
    for (int i = 0; i < length; ++i)
        out[(std::size_t) i * 2] = l[(std::size_t) i], out[(std::size_t) i * 2 + 1] = r[(std::size_t) i];
    return out;
}

// --- parameter list and texts

namespace
{
struct Info
{
    int flm;
    const char* name;
    int steps; // 0 = continuous
};
// FLM's own names (its unnamed parameters 53, 58, 59 named by what they do; 43 is not read by FLM and is left out)
constexpr Info infos[] = {
    { 1, "Osc pulse width", 0 }, { 2, "Osc pulse mode", 2 }, { 3, "Osc sub level", 0 }, { 4, "Osc unison", 12 },
    { 5, "Osc unison detune", 0 }, { 6, "Osc unison pan", 0 }, { 7, "Osc unison detune curve", 0 }, { 8, "Osc re-trig phases", 2 },
    { 9, "Osc phases offset", 0 }, { 10, "SuperSync mode", 2 }, { 11, "Osc sync", 0 }, { 12, "Osc sync pulse width", 0 },
    { 13, "Osc sync attack", 0 }, { 14, "Osc sync decay", 0 }, { 15, "Osc sync sustain", 0 }, { 16, "Osc sync release", 0 },
    { 17, "Osc sync EG amount", 0 }, { 18, "Osc sync kbd dir", 2 }, { 19, "Filter cutoff", 0 }, { 20, "Filter resonance", 0 },
    { 21, "Filter kbd", 0 }, { 22, "Filter EG attack", 0 }, { 23, "Filter EG decay", 0 }, { 24, "Filter EG sustain", 0 },
    { 25, "Filter EG release", 0 }, { 26, "Filter EG amount", 0 }, { 27, "Filter vel", 0 }, { 28, "Level EG attack", 0 },
    { 29, "Level EG decay", 0 }, { 30, "Level EG sustain", 0 }, { 31, "Level EG release", 0 }, { 32, "Level", 0 }, { 33, "Pan", 0 },
    { 34, "Slide held mode", 2 }, { 35, "Slide time", 0 }, { 36, "Transpose", 25 }, { 37, "Pitch bend range", 12 },
    { 38, "Slide time static", 2 }, { 39, "LFO shape", 4 }, { 40, "LFO rate", 0 }, { 41, "LFO mode", 3 }, { 42, "LFO beat", 10 },
    { 44, "LFO amount", 0 }, { 45, "LFO dest", 8 }, { 46, "LFO fade in", 0 }, { 47, "Arp rate", 6 }, { 48, "Arp range", 4 },
    { 49, "Arp mode", 6 }, { 50, "Monophonic mode", 2 }, { 51, "Number of voices", 8 }, { 52, "Distortion", 0 },
    { 53, "Output trim", 0 }, { 54, "Delay time", 5 }, { 55, "Delay feedback", 0 }, { 56, "Delay mix", 0 }, { 57, "Delay filter", 0 },
    { 58, "Mod wheel amount", 0 }, { 59, "Mod wheel dest", 9 },
};

std::string fmt (const char* f, double v)
{
    char b[48];
    std::snprintf (b, sizeof (b), f, v);
    return b;
}

std::string seconds (double s) { return s < 1.0 ? fmt ("%.0f ms", s * 1000.0) : fmt ("%.2f s", s); }
std::string beatText (double b)
{
    if (b >= 4.0)
        return fmt (b == 4.0 ? "%.0f bar" : "%.0f bars", b / 4.0);
    static const std::pair<double, const char*> names[] = { { 0.125, "1/32" }, { 0.25, "1/16" }, { 0.375, "1/16." }, { 0.5, "1/8" },
                                                            { 0.75, "1/8." }, { 1.0, "1/4" }, { 2.0, "1/2" } };
    for (const auto& [x, n] : names)
        if (x == b)
            return n;
    return fmt ("%.3f", b);
}
} // namespace

std::string paramText (int param, float v)
{
    const int id = param + 1;
    const auto percent = [v] { return fmt ("%.0f%%", (double) v * 100.0); };
    const auto onOff = [v] { return std::string (v > 0.5f ? "On" : "Off"); };
    switch (id)
    {
        case flm::pulseMode: case flm::retrigPhases: return onOff();
        case flm::superSync: case flm::syncKbdDir: case flm::slideHeld: case flm::slideStatic: case flm::mono: return v != 0.0f ? "On" : "Off";
        case flm::unison: return std::to_string (unisonCount (v));
        case flm::detune: return fmt ("%.0f cents", (double) clamp01 (v * v) * 100.0);
        case flm::syncAttack: case flm::syncDecay: case flm::syncRelease: case flm::filterAttack: case flm::filterDecay:
        case flm::filterRelease: case flm::levelDecay: case flm::lfoFade:
            return seconds ((double) envelopeSeconds (v));
        case flm::levelAttack: return seconds ((double) envelopeSeconds (0.9f * v));
        case flm::levelRelease: return seconds ((double) envelopeSeconds (0.85f * v + 0.05f));
        case flm::syncEgAmount: case flm::filterEgAmount: case flm::mwAmount: return fmt ("%+.0f%%", ((double) v * 2.0 - 1.0) * 100.0);
        case flm::cutoff: return fmt ("%.0f Hz", cutoffHz (v, 0.5f, 0.0f, 0.0f, 48.0f));
        case flm::pan: return fmt ("%.1f", 200.0 * (double) v - 100.0);
        case flm::slideTime: return seconds ((double) (v * 0.5f * v * 0.5f));
        case flm::transpose: return fmt ("%+.0f st", (double) transposeSemitones (v));
        case flm::bendRange: return fmt ("%.0f st", (double) bendRangeSemitones (v));
        case flm::lfoShape:
        {
            static const char* n[] = { "Sine", "Ramp", "Square", "Random" };
            return n[std::clamp ((int) (v * 3.0f + 0.5f), 0, 3)];
        }
        case flm::lfoRate: return fmt ("%.2f Hz", 1.0 / lfoPeriodSeconds (v));
        case flm::lfoMode:
        {
            static const char* n[] = { "Free", "One shot", "Tempo sync" };
            return n[std::clamp ((int) (v + v), 0, 2)];
        }
        case flm::lfoBeat: return beatText (lfoBeatLength (v));
        case flm::lfoAmount:
        {
            const auto a = (double) v * 2.0 - 1.0;
            return fmt ("%+.0f%%", (a < 0.0 ? -a * a : a * a) * 100.0);
        }
        case flm::lfoDest:
        {
            static const char* n[] = { "Cutoff", "Resonance", "Pitch", "Sync", "Pulse width", "Detune", "Level", "Pan" };
            return n[std::clamp ((int) (v * 7.0f + 0.5f), 0, 7)];
        }
        case flm::arpRate: return beatText (arpStepBeats (v));
        case flm::arpRange: return fmt ("+%.0f oct", (double) std::clamp ((int) (v * 3.0f), 0, 3));
        case flm::arpMode:
        {
            static const char* n[] = { "Off", "Up", "Down", "Up-down", "Up-down (ends twice)", "Random" };
            return n[std::clamp ((int) (v * 5.0f), 0, 5)];
        }
        case flm::voices: return std::to_string (voiceLimit (v));
        case flm::trim: return fmt ("%.1f dB", (double) (int) (-180.0f * (1.0f - v)) * 0.1);
        case flm::delayTime: return beatText (delayBeats (v));
        case flm::delayFilter: return v > 0.5f ? fmt ("HP %.0f%%", ((double) v * 2.0 - 1.0) * 100.0) : v < 0.5f ? fmt ("LP %.0f%%", (1.0 - (double) v * 2.0) * 100.0) : std::string ("Off");
        case flm::mwDest:
        {
            static const char* n[] = { "Cutoff", "Resonance", "Sync", "Pulse width", "Detune", "Level", "Pan", "LFO amount", "LFO rate" };
            return n[std::clamp ((int) (v * 8.0f), 0, 8)];
        }
        default: return percent();
    }
}
} // namespace pocketsaw

// --- the channel's values and the automatable parameter table

std::vector<float> pocketSuperSawParams (const std::vector<float>& flmParams)
{
    if ((int) flmParams.size() < pocketsaw::flmParamCount)
        return {};
    std::vector<float> out (flmParams.begin() + 1, flmParams.begin() + pocketsaw::flmParamCount);
    for (auto& v : out)
        v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f;
    return out;
}

std::vector<float> pocketSuperSawDefaults()
{
    const auto& d = pocketsaw::defaults();
    return std::vector<float> (d.begin() + 1, d.end());
}

const std::vector<EngineParam>& pocketSuperSawEngineParams()
{
    static const auto list = [] {
        std::vector<EngineParam> v;
        for (const auto& i : pocketsaw::infos)
        {
            EngineParam p;
            p.index = i.flm - 1;
            p.name = i.name;
            v.push_back (std::move (p));
        }
        return v;
    }();
    return list;
}

namespace modelparams
{
namespace
{
std::optional<float> getSaw (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::pocketSuperSaw || p.index < 0 || p.index >= pocketsaw::paramCount)
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : pocketsaw::defaults()[(std::size_t) p.index + 1];
}

bool setSaw (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::pocketSuperSaw || p.index < 0 || p.index >= pocketsaw::paramCount)
        return false;
    if ((int) c.modelParams.size() < pocketsaw::paramCount)
    {
        const auto d = pocketSuperSawDefaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) pocketsaw::paramCount);
        std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c.modelParams.begin() + (std::ptrdiff_t) have);
    }
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table pocketSuperSawModelTable() { return { &pocketSuperSawEngineParams(), &getSaw, &setSaw }; }
} // namespace modelparams

// --- the instrument

namespace
{
class PocketSuperSawInstrument final : public Instrument
{
public:
    PocketSuperSawInstrument()
    {
        const auto d = pocketSuperSawDefaults();
        for (int i = 0; i < pocketsaw::paramCount; ++i)
            values[(std::size_t) i].store (d[(std::size_t) i]);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
        lastTicks = -1.0;
        version.fetch_add (1);
    }

    // UI thread: the channel's values; the audio thread takes them at its next call
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        wheelRange.store ((float) std::max (1, channel.misc.pitchRange));
        const auto d = pocketSuperSawDefaults();
        bool changed = false;
        for (int i = 0; i < pocketsaw::paramCount; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : d[(std::size_t) i];
            v = std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : d[(std::size_t) i];
            if (values[(std::size_t) i].exchange (v) != v)
                changed = true;
        }
        if (changed)
            version.fetch_add (1);
    }

    void setParameter (int index, float value) noexcept override
    {
        if (index == midictl::ccBase + 1 && std::isfinite (value)) // round 190dw item 4: an imported .flm mod-wheel lane
        {
            engine.setModWheel (std::clamp (value, 0.0f, 1.0f));
            return;
        }
        if (index < 0 || index >= pocketsaw::paramCount || ! std::isfinite (value))
            return;
        const auto v = std::clamp (value, 0.0f, 1.0f);
        values[(std::size_t) index].store (v);
        engine.setParam (index + 1, v);
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    // the channel's pitch as the rack gives it before every render (PITCH knob; fine tune + main pitch): every voice
    // follows it at its next tick, sounding or new (FL's generators do, round 167a), the arpeggiator's notes too
    void setChannelPitchNow (float knob, float rest) noexcept override { live.set (knob, rest); }
    // the MIDI wheel: the rack gives it in semitones of the channel's pitch range; the module bends by its own Pitch
    // bend range (as FLM's does), so the wheel's throw (−1…1) goes to the engine and every voice follows at its next tick
    void pitchBend (float semitones) noexcept override
    {
        engine.setPitchBend (std::isfinite (semitones) ? semitones / wheelRange.load() : 0.0f);
    }
    // FL's channel Porta / Mono and slide notes (round 168l, issue #14): the voices take the rack's glide at note on
    // and follow a slide note, each keeping its key
    bool followsChannelPorta() const noexcept override { return true; }
    void slideTo (int key, float cents, float samples) noexcept override
    {
        engine.slideTo (std::clamp (key, 0, 127), cents - liveRest(), samples);
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        // FLM's note: the velocity as it is (0 would be a note off), pan 0…1
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 255.0f),
                     std::clamp (0.5f + 0.5f * nextPan, 0.0f, 1.0f), (nextCents - liveRest()) * 0.01f);
        nextPan = 0.0f;
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    void allNotesOff() noexcept override { engine.allNotesOff(); }
    void panic() noexcept override { engine.reset(); }
    void controlChange (int cc, float value) noexcept override
    {
        if (cc == 1)
            engine.setModWheel (std::clamp (value, 0.0f, 1.0f));
    }
    bool idle() const noexcept override { return engine.idle(); }

    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks; // the transport plays when the position moves between blocks
        lastTicks = ticks;
        songBeats = ticks / (double) ppq;
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        take();
        if (n <= 0 || engine.sampleRate() != rate)
            return;
        engine.setPitchOffset ((std::isnan (live.knob) ? pitchCents.load() : live.now()) * 0.01f);
        const auto bpm = (double) tempo.load();
        for (int pos = 0; pos < n; pos += chunk)
        {
            const auto c = std::min (chunk, n - pos);
            std::fill (bufL, bufL + c, 0.0f);
            std::fill (bufR, bufR + c, 0.0f);
            engine.render (bufL, bufR, c, bpm, playing ? songBeats + (double) pos * bpm / (60.0 * rate) : -1.0);
            for (int i = 0; i < c; ++i)
            {
                left[pos + i] += bufL[i] * gainLeft;
                right[pos + i] += bufR[i] * gainRight;
            }
        }
    }

private:
    // the part of a note's cents that is the channel's (fine tune + main pitch): the voices get it live with the knob
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }

    void take() noexcept
    {
        const auto v = version.load();
        if (v == taken)
            return;
        taken = v;
        for (int i = 0; i < pocketsaw::paramCount; ++i)
            engine.setParam (i + 1, values[(std::size_t) i].load());
    }

    static constexpr int chunk = 512;
    pocketsaw::Engine engine;
    std::array<std::atomic<float>, pocketsaw::paramCount> values {};
    std::atomic<std::uint32_t> version { 1 };
    std::uint32_t taken = 0;
    std::atomic<float> tempo { 120.0f }, pitchCents { 0.0f }, wheelRange { 2.0f };
    LiveChannelPitch live; // (audio thread)
    float nextPan = 0.0f, nextCents = 0.0f;
    double rate = 44100.0, lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makePocketSuperSawInstrument() { return std::make_unique<PocketSuperSawInstrument>(); }
} // namespace lynx
