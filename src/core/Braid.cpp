// Braid (round 190ko, core/Braid.h): the DSP — voices, the two engines' oscillators, filters, envelopes, LFOs, the
// modulation matrix, the mix, drive and limiter — and the instrument. Laws measured black-box on Live 12.4's renders of
// Meld (REA_rodada190ko §1; render sets m1 / m2 of /mnt/e/lynx-work/meld190ko); the algorithms are LYNX's own.

#include "core/Braid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/HostSlide.h"
#include "core/Song.h"
#include "core/WavetableSynth.h"

namespace lynx
{
namespace braid
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr float twoPi = 6.28318530717958647692f;
constexpr int controlRate = 16; // samples per modulation step
constexpr int maxVoices = 16;   // 12 poly voices + room for releases / stacks

// --- measured constants (REA_rodada190ko §1; each names the render rows that fix it)
struct Laws
{
    double engineLevel = 0.5;       // an engine at Volume 0 dB, centre: a full-scale wave → this per side [MEASURED m1 o00]
    double pitchModSemis = 48.0;    // Pitch Mod ← 1 [MEASURED m1 q1x]
    double detuneSemis = 1.0;       // Detune ← 1 (and the knob's ±0.5 = ± half a semitone) [MEASURED m1 q1y, p1]
    double volumeModDb = 24.0;      // Volume ← 1: dB [MEASURED m1 q2a]
    double filterModOctaves = 10.0; // Filter Freq ← 1 [MEASURED m1 f00 modenv]
    double keySpan = 60.0;          // the Key source: (key − 60) / this
    double lfoRateOctaves = 8.0;    // an LFO rate row ← 1: × 2^this
    double envTimeOctaves = 8.0;    // an envelope time row ← 1: × 2^this
    double slopeCurve = 7.6;        // envelope slope ±1 (Helix's measured law; Meld's envelopes behave the same [MEASURED m1 e*])
    double spreadCents = 25.0;      // the stacked voices' detune at Spread 1 through the default matrix row (0.5) [MEASURED m1 pF]
    double toneOctaves = 6.0;       // Tone ±1: the low / high pass's travel
    double bDelaySeconds = 1.0;     // B Delay 1 [MEASURED m1 pB]
};
constexpr Laws laws {};

constexpr double syncBars[22] { 8.0, 6.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.3125, 0.25, 0.1875, 1.0 / 6.0, 0.125,
                                1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125, 1.0 / 48.0, 0.015625 };

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float clampBi (float v) noexcept { return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v; }
inline float wrap01 (float p) noexcept { return p - std::floor (p); }
// tan (π·fc) of a normalised frequency, kept for the last value (the oscillators' filters move once a control step)
struct TanCache
{
    float fc = -1.0f, g = 0.0f;
    float operator() (float f) noexcept
    {
        if (f != fc)
            fc = f, g = std::tan (3.14159265f * std::clamp (f, 1.0e-5f, 0.45f));
        return g;
    }
};

// ------------------------------------------------------------------------------------------------ small DSP parts
struct Rng
{
    std::uint32_t s = 1u;
    std::uint32_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() noexcept { return 2.0f * uni() - 1.0f; }
};

inline float blep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
inline float sawBl (float t, float dt) noexcept { return 2.0f * t - 1.0f - blep (t, dt); }
inline float squareBl (float t, float dt, float pw) noexcept
{
    auto v = t < pw ? 1.0f : -1.0f;
    v += blep (t, dt);
    v -= blep (wrap01 (t + 1.0f - pw), dt);
    return v;
}
inline float triangle (float t) noexcept { return 1.0f - 4.0f * std::abs (t - 0.5f); } // (−1 at 0, +1 at ½: a sine's phase −90°)
// a sine from a table (4096 points, linear interpolation: −130 dB of error), t in cycles
struct SineTable
{
    std::array<float, 4097> v {};
    SineTable()
    {
        for (int i = 0; i <= 4096; ++i)
            v[(std::size_t) i] = (float) std::sin (2.0 * pi * i / 4096.0);
    }
};
inline const SineTable& sineTable() noexcept
{
    static const SineTable table;
    return table;
}
inline float sine (float t) noexcept
{
    const auto x = (t - std::floor (t)) * 4096.0f;
    const auto i = std::min ((int) x, 4095);
    const auto f = x - (float) i;
    const auto& v = sineTable().v;
    return v[(std::size_t) i] + (v[(std::size_t) i + 1] - v[(std::size_t) i]) * f;
}

// the basic-shape morph sine → triangle → saw → square (Shape 0 … 1), Tone = pulse width of the square part
inline float shapeMorph (float t, float dt, float shape, float pw) noexcept
{
    const auto x = clamp01 (shape) * 3.0f;
    if (x < 1.0f)
        return sine (t) * (1.0f - x) + triangle (wrap01 (t + 0.25f)) * x;
    if (x < 2.0f)
        return triangle (wrap01 (t + 0.25f)) * (2.0f - x) + sawBl (wrap01 (t + 0.5f), dt) * (x - 1.0f);
    return sawBl (wrap01 (t + 0.5f), dt) * (3.0f - x) + squareBl (t, dt, pw) * (x - 2.0f);
}

// a TPT state-variable filter (Helix's / Tetra's measured family)
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float lg = -1.0f, lk = -1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f; // (the coefficients of the last g / k: they move once a step)
    inline void run (float x, float g, float k, float& lp, float& bp, float& hp) noexcept
    {
        if (g != lg || k != lk)
        {
            lg = g, lk = k;
            a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        }
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2, bp = v1, hp = x - k * v1 - v2;
    }
};

struct OnePole
{
    float z = 0.0f;
    inline float lp (float x, float a) noexcept { return z += a * (x - z); }
};

// ------------------------------------------------------------------------------------------------ envelopes
struct EnvSet
{
    float attack = 0.001f, decay = 0.6f, release = 0.05f, aSlope = 0.0f, dSlope = 0.5f, rSlope = 0.5f;
    float initial = 0.0f, peak = 1.0f, sustain = 1.0f, final = 0.0f;
    int loop = 0; // 0 none, 1 trigger, 2 loop, 3 AD loop
};

enum Stage : std::uint8_t
{
    attackStage,
    decayStage,
    sustainStage,
    releaseStage,
    offStage
};

struct Env
{
    Stage stage = offStage;
    float pos = 0.0f, from = 0.0f, value = 0.0f;
    bool gate = false;
    void start (const EnvSet& e, bool legatoKeep) noexcept
    {
        if (! legatoKeep)
        {
            value = e.initial;
            from = e.initial;
            stage = attackStage;
            pos = 0.0f;
        }
        gate = true;
    }
    void release() noexcept
    {
        gate = false;
        if (stage == offStage || stage == releaseStage)
            return;
        from = value;
        stage = releaseStage;
        pos = 0.0f;
    }
    // one step of dt seconds with the (modulated) times
    void step (const EnvSet& e, float dt, float a, float d, float r, float sus) noexcept
    {
        const auto adv = [&] (float t) { pos += dt / std::max (t, 1.0e-4f); };
        for (int guard = 0; guard < 4; ++guard)
        {
            switch (stage)
            {
                case attackStage:
                    adv (a);
                    if (pos >= 1.0f)
                    {
                        value = from = e.peak, stage = decayStage, pos = 0.0f;
                        continue;
                    }
                    value = from + (e.peak - from) * wt::segment (pos, e.aSlope);
                    return;
                case decayStage:
                    adv (d);
                    if (pos >= 1.0f)
                    {
                        value = sus, pos = 0.0f;
                        if (e.loop == 3 && gate) // AD loop: attack again from the sustain level while held
                        {
                            from = value, stage = attackStage;
                            return;
                        }
                        if (e.loop == 1 || (e.loop == 2 && gate)) // trigger: no sustain; loop: release, then again
                        {
                            from = value, stage = releaseStage;
                            return;
                        }
                        stage = sustainStage;
                        return;
                    }
                    value = from + (sus - from) * wt::segment (pos, e.dSlope);
                    return;
                case sustainStage:
                    value = sus;
                    if (e.loop == 1)
                    {
                        from = value, stage = releaseStage, pos = 0.0f;
                        continue;
                    }
                    return;
                case releaseStage:
                    adv (r);
                    if (pos >= 1.0f)
                    {
                        value = e.final;
                        if (e.loop == 2 && gate)
                        {
                            from = value, stage = attackStage, pos = 0.0f;
                            return;
                        }
                        stage = offStage;
                        return;
                    }
                    value = from + (e.final - from) * wt::segment (pos, e.rSlope);
                    return;
                case offStage: value = e.final; return;
            }
        }
    }
};

// ------------------------------------------------------------------------------------------------ LFOs
float lfo2Wave (int w, float t, float& held, bool wrapped, Rng& rng) noexcept
{
    switch (w)
    {
        case 0: return sine (t);
        case 1: return triangle (wrap01 (t + 0.25f));
        case 2: return 2.0f * t - 1.0f;
        case 3: return 1.0f - 2.0f * t;
        case 4: return t < 0.5f ? 1.0f : -1.0f;
        default:
            if (wrapped)
                held = rng.bi();
            return held;
    }
}

// LFO 1: a generator (with two macros), then two transformers
struct Lfo1State
{
    float phase = 0.0f, held = 0.0f, prevHeld = 0.0f, smooth = 0.0f, walk = 0.0f, target = 0.0f;
    int step = 0;
};

float lfo1Generator (int g, float t, float m1, float m2, Lfo1State& s, bool wrapped, Rng& rng) noexcept
{
    switch (g)
    {
        case 0: return shapeMorph (t, 0.0f, m1, 0.5f - 0.49f * m2); // Basic Shapes: Shape, Pulse width
        case 1:                                                      // Ramp: Curve, Direction
        {
            const auto c = 1.0f + 6.0f * m1;
            const auto up = std::pow (t, c);
            return 2.0f * (m2 < 0.5f ? up : 1.0f - up) - 1.0f;
        }
        case 2: // Wander: a smoothed random walk (Smooth, Range)
            if (wrapped)
            {
                s.prevHeld = s.held;
                s.held = clampBi (s.held * (1.0f - m2 * 0.5f) + rng.bi() * (0.5f + m2));
            }
            {
                const auto k = 0.5f - 0.5f * std::cos ((float) pi * std::min (1.0f, t / std::max (0.05f, m1)));
                return s.prevHeld + (s.held - s.prevHeld) * k;
            }
        case 3: // Alternate: two levels that swap each cycle (Level A, Level B)
            if (wrapped)
                s.step ^= 1;
            return s.step ? 2.0f * m2 - 1.0f : 1.0f - 2.0f * m1;
        case 4: // Euclid: pulses of a Euclidean rhythm (Steps, Pulses)
        {
            const auto steps = 2 + (int) std::lround (m1 * 14.0f), pulses = std::max (1, (int) std::lround (m2 * (float) steps));
            const auto i = std::min ((int) (t * (float) steps), steps - 1);
            const auto on = ((i * pulses) % steps) < pulses;
            const auto f = t * (float) steps - (float) i;
            return on && f < 0.5f ? 1.0f : -1.0f;
        }
        default: // Pulsate: a decaying pulse train (Decay, Count)
        {
            const auto count = 1 + (int) std::lround (m2 * 7.0f);
            const auto f = wrap01 (t * (float) count);
            return 2.0f * std::exp (-f * (2.0f + 30.0f * (1.0f - m1))) - 1.0f;
        }
    }
}

float lfoTransform (int fx, float x, float m, float t, Lfo1State& s, Rng& rng) noexcept
{
    switch (fx)
    {
        case 0: return x;                                                       // off
        case 1: return std::tanh (x * (1.0f + 8.0f * m)) / std::tanh (1.0f + 8.0f * m); // shape (saturate)
        case 2: return x * (1.0f - m);                                          // gain (attenuate)
        case 3: return clampBi (x + 2.0f * m - 1.0f);                           // offset
        case 4: return s.smooth += (x - s.smooth) * (1.0f - 0.995f * m);         // smooth
        case 5:
        {
            const auto steps = 2.0f + std::floor ((1.0f - m) * 14.0f);
            return std::round (x * steps) / steps; // quantize
        }
        case 6: return x >= 0.0f ? std::pow (x, 1.0f + 3.0f * m) : -std::pow (-x, 1.0f / (1.0f + 3.0f * m)); // skew
        case 7:
        {
            const auto steps = 2.0f + std::floor (m * 14.0f);
            return std::floor ((x * 0.5f + 0.5f) * steps) / (steps - 1.0f) * 2.0f - 1.0f; // steps
        }
        case 8: return m > 0.5f ? std::abs (x) * 2.0f - 1.0f : x; // mirror
        case 9:
        {
            auto y = x * (1.0f + 3.0f * m);
            while (y > 1.0f || y < -1.0f)
                y = y > 1.0f ? 2.0f - y : -2.0f - y;
            return y; // fold
        }
        case 10: return wrap01 ((x * (1.0f + 3.0f * m)) * 0.5f + 0.5f) * 2.0f - 1.0f; // wrap
        case 11: return x;                                                            // bipolar (the default's neutral)
        case 12: return x * 0.5f + 0.5f;                                              // unipolar
        case 13: return -x;                                                           // invert
        case 14: return clampBi (x * (1.0f + 4.0f * m));                              // clip
        case 15: return x * std::min (1.0f, t * (1.0f + 15.0f * (1.0f - m)));         // ramp up
        case 16: return x * std::max (0.0f, 1.0f - t * (1.0f + 15.0f * (1.0f - m))); // ramp down
        case 17: return clampBi (x + m * rng.bi() * 0.5f);                            // jitter
        default: return x;                                                            // delay (phase, handled by the caller)
    }
}

// ------------------------------------------------------------------------------------------------ the macro oscillator
struct OscState
{
    float phase = 0.0f, phase2 = 0.0f, phase3 = 0.0f, fb = 0.0f, lp = 0.0f, lp2 = 0.0f;
    std::array<float, 8> swarm {};
    std::array<float, 8> drift {};
    std::array<float, 8> driftTarget {};
    std::array<float, 8> ratio { 1, 1, 1, 1, 1, 1, 1, 1 };
    int swarmTick = 0;
    Svf svf {};
    float env = 0.0f, envPhase = 0.0f, eventTimer = 0.0f, eventFreq = 0.0f, eventAmp = 0.0f, eventPhase = 0.0f, eventDecay = 0.0f;
    int step = 0;
    std::array<float, 2048> loop {}; // Noise Loop's buffer
    TanCache tanA, tanB;
    int loopPos = 0, loopLen = 0;
    float held = 0.0f;
};

// the chord shapes of Chord (semitones of the voices above the root)
constexpr float chordShapes[8][4] { { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 0, 4, 7, 11 },
                                    { 0, 3, 7, 10 }, { 0, 4, 7, 10 }, { 0, 7, 12, 19 }, { 0, 5, 10, 15 } };

// one sample of the oscillator. f: the note's frequency / sample rate (cycles per sample)
float oscSample (int type, OscState& s, float f, float m1, float m2, Rng& rng) noexcept
{
    const auto dt = std::min (f, 0.49f);
    switch (type)
    {
        case 0: // Basic Shapes
        {
            const auto y = shapeMorph (s.phase, dt, m1, 0.5f - 0.45f * m2);
            s.phase = wrap01 (s.phase + f);
            return y;
        }
        case 1: // Dual Basic Shapes: a second copy detuned up to a fifth
        {
            const auto ratio = std::pow (2.0f, m2 * 7.0f / 12.0f) + 0.003f;
            const auto y = 0.5f * (shapeMorph (s.phase, dt, m1, 0.5f) + shapeMorph (s.phase2, dt * ratio, m1, 0.5f));
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f * ratio);
            return y * 1.41f;
        }
        case 2: // Noisy Shapes: phase noise grows with Rough
        {
            const auto n = rng.bi();
            s.lp += 0.2f * (n - s.lp);
            const auto y = shapeMorph (wrap01 (s.phase + m2 * m2 * 0.25f * s.lp), dt, m1, 0.5f);
            s.phase = wrap01 (s.phase + f);
            return y;
        }
        case 3: // Square Sync: a square synced to the note, its own frequency Freq 1 … (+Freq 2 as a second, mixed)
        {
            const auto r1 = std::pow (2.0f, m1 * 4.0f), r2 = std::pow (2.0f, m2 * 4.0f);
            const auto y = 0.5f * (squareBl (s.phase2, std::min (dt * r1, 0.49f), 0.5f) + squareBl (s.phase3, std::min (dt * r2, 0.49f), 0.5f));
            s.phase = s.phase + f;
            s.phase2 = wrap01 (s.phase2 + f * r1);
            s.phase3 = wrap01 (s.phase3 + f * r2);
            if (s.phase >= 1.0f)
            {
                s.phase -= 1.0f;
                s.phase2 = s.phase * r1;
                s.phase3 = s.phase * r2;
            }
            return y;
        }
        case 4: // Square 5th: a square and a square a fifth above
        {
            const auto pw = 0.5f - 0.45f * m2;
            const auto y = squareBl (s.phase, dt, pw) * (1.0f - 0.5f * m1) + 0.5f * m1 * squareBl (s.phase2, std::min (dt * 1.4983f, 0.49f), pw);
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f * 1.4983f);
            return y;
        }
        case 5: // Sub: a sine morphing into a square, a sine an octave below added by Aux
        {
            const auto sq = squareBl (s.phase, dt, 0.5f);
            const auto y = sine (s.phase) * (1.0f - m1) + sq * m1 + m2 * sine (s.phase2);
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f * 0.5f);
            return y / (1.0f + 0.5f * m2);
        }
        case 6:
        case 7:
        case 8:
        case 9: // Swarm: eight detuned waves; Motion moves them, Spacing spreads them towards a chord
        {
            constexpr float chord[8] { 0, 12, 7, 19, -12, 24, 5, 16 };
            if (--s.swarmTick <= 0) // (the waves' ratios move slowly: once every 16 samples)
            {
                s.swarmTick = 16;
                for (int i = 0; i < 8; ++i)
                {
                    s.drift[(std::size_t) i] += (s.driftTarget[(std::size_t) i] - s.drift[(std::size_t) i]) * 0.008f;
                    if (rng.uni() < 0.0032f)
                        s.driftTarget[(std::size_t) i] = rng.bi();
                    const auto cents = ((float) i - 3.5f) * 3.0f + s.drift[(std::size_t) i] * 20.0f * m1;
                    s.ratio[(std::size_t) i] = std::exp2 ((cents * 0.01f + m2 * chord[i]) / 12.0f);
                }
            }
            float y = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                const auto fi = f * s.ratio[(std::size_t) i];
                const auto t = s.swarm[(std::size_t) i];
                const auto di = std::min (fi, 0.49f);
                y += type == 6 ? sine (t) : type == 7 ? triangle (t) : type == 8 ? sawBl (t, di) : squareBl (t, di, 0.5f);
                s.swarm[(std::size_t) i] = wrap01 (t + fi);
            }
            return y * 0.25f;
        }
        case 10: // Harmonic FM: a sine modulated at a whole ratio
        {
            const auto ratio = 1.0f + std::floor (m2 * 7.99f);
            const auto y = sine (s.phase + 6.0f * m1 * sine (s.phase2) / twoPi);
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f * ratio);
            return y;
        }
        case 11: // Fold FM: FM then a wave folder shaped by Shape
        {
            auto x = sine (s.phase + 4.0f * m1 * sine (s.phase2) / twoPi) * (1.0f + 3.0f * m2);
            while (x > 1.0f || x < -1.0f)
                x = x > 1.0f ? 2.0f - x : -2.0f - x;
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f);
            return x;
        }
        case 12: // Squelch: feedback FM through a resonant low pass
        {
            const auto y = sine (s.phase + 3.0f * m1 * s.fb / twoPi);
            s.fb = 0.5f * (s.fb + sine (s.phase + 2.5f * m2 * s.fb / twoPi));
            s.phase = wrap01 (s.phase + f);
            return y;
        }
        case 13: // Simple FM: a sine modulated at a free ratio
        {
            const auto ratio = 0.5f + m2 * 7.5f;
            const auto y = sine (s.phase + 5.0f * m1 * sine (s.phase2) / twoPi);
            s.phase = wrap01 (s.phase + f);
            s.phase2 = wrap01 (s.phase2 + f * ratio);
            return y;
        }
        case 14: // Chip: a pulse stepping through an arpeggio (Tone: its width and octave, Rate: the step rate)
        {
            constexpr float arp[4] { 0.0f, 12.0f, 7.0f, 19.0f };
            s.eventTimer += 1.0f;
            const auto stepLen = 4800.0f / (1.0f + 15.0f * m2);
            if (s.eventTimer >= stepLen)
                s.eventTimer = 0.0f, s.step = (s.step + 1) & 3;
            const auto fi = f * std::exp2 ((m2 > 0.02f ? arp[s.step] : 0.0f) / 12.0f);
            const auto y = squareBl (s.phase, std::min (fi, 0.49f), 0.5f - 0.375f * m1);
            s.phase = wrap01 (s.phase + fi);
            return y;
        }
        case 15: // Shepard's Pi: octave-spaced sines under a moving bell
        {
            const auto rate = (m1 - 0.5f) * 0.0002f;
            s.envPhase = wrap01 (s.envPhase + rate);
            const auto width = 2.0f + 6.0f * m2;
            float y = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                const auto oct = (float) i + s.envPhase - 4.0f;
                const auto amp = std::exp (-(oct * oct) / (width * 0.5f));
                const auto fi = f * std::exp2 (oct);
                if (fi < 0.45f)
                    y += amp * sine (s.swarm[(std::size_t) i]);
                s.swarm[(std::size_t) i] = wrap01 (s.swarm[(std::size_t) i] + std::min (fi, 0.45f));
            }
            return y * 0.6f;
        }
        case 16: // Tarp: a plucked noise burst ringing at the pitch in a delay loop (Decay: the loop's feedback, Tone: its brightness)
        {
            const auto len = std::clamp (1.0f / std::max (f, 1.0e-4f), 2.0f, 2040.0f);
            const auto excite = s.env > 1.0e-4f ? rng.bi() * s.env : 0.0f;
            s.env *= 0.995f;
            auto rp = (float) s.loopPos - len;
            if (rp < 0.0f)
                rp += 2048.0f;
            const auto i0 = (int) rp;
            const auto fr = rp - (float) i0;
            const auto d = s.loop[(std::size_t) (i0 & 2047)] * (1.0f - fr) + s.loop[(std::size_t) ((i0 + 1) & 2047)] * fr;
            s.lp += (0.15f + 0.85f * m2) * (d - s.lp);
            const auto y = excite + s.lp * (0.95f + 0.0499f * m1);
            s.loop[(std::size_t) s.loopPos] = y;
            s.loopPos = (s.loopPos + 1) & 2047;
            return std::clamp (y, -2.0f, 2.0f);
        }
        case 17: // Extratone: a sine whose pitch drops from above each cycle of a fast envelope (Pitch, Env Amount)
        {
            s.envPhase += f * 0.25f;
            if (s.envPhase >= 1.0f)
                s.envPhase -= 1.0f;
            const auto drop = std::exp (-s.envPhase * 8.0f) * m2 * 4.0f;
            const auto fi = f * std::exp2 ((m1 - 0.5f) * 2.0f + drop);
            const auto y = sine (s.phase);
            s.phase = wrap01 (s.phase + std::min (fi, 0.45f));
            return y;
        }
        case 18: // Noise Loop: a short noise fragment looped at the note's period (Rate: a new fragment, Fade: roughness)
        {
            const auto len = std::clamp ((int) (1.0f / std::max (f, 1.0e-4f)), 8, 2047);
            if (s.loopLen != len || rng.uni() < m1 * 0.0005f)
            {
                s.loopLen = len;
                for (int i = 0; i < len; ++i)
                    s.loop[(std::size_t) i] = rng.bi();
            }
            s.loopPos = (s.loopPos + 1) % s.loopLen;
            auto& v = s.loop[(std::size_t) s.loopPos];
            const auto y = v;
            v += (rng.bi() - v) * m2 * 0.05f;
            return y * 0.7f;
        }
        case 19: // Filtered Noise: a band of noise (Freq relative to the note, Width)
        {
            float lp, bp, hp;
            const auto fc = std::min (f * std::exp2 ((m1 - 0.5f) * 8.0f), 0.45f);
            const auto k = 0.05f + 2.0f * m2;
            s.svf.run (rng.bi(), s.tanA (fc), k, lp, bp, hp);
            return bp * 0.5f * std::sqrt (k / (3.14159265f * fc)); // (about the same loudness at any width)
        }
        case 20: // Bitgrunge: a square and its sub-octaves combined bitwise
        {
            const auto fi = f * std::exp2 ((m1 - 0.5f) * 4.0f);
            s.phase = wrap01 (s.phase + std::min (fi, 0.45f));
            const auto bits = (int) (s.phase * 256.0f);
            const auto mult = 1 + (int) (m2 * 6.0f);
            int v = (bits >> 7) & 1;
            for (int i = 1; i <= mult; ++i)
                v ^= (bits >> (7 - i)) & 1;
            return v ? 0.7f : -0.7f;
        }
        case 21: // Crackle: random clicks (Density, Intensity)
        {
            if (rng.uni() < 0.0002f + 0.01f * m1 * m1)
                s.eventAmp = rng.bi() * (0.3f + 0.7f * m2);
            const auto y = s.eventAmp;
            s.eventAmp *= 0.6f;
            float lp, bp, hp;
            s.svf.run (y, s.tanA (0.02f + 0.3f * m2), 0.7f, lp, bp, hp);
            return (hp + bp) * 1.5f;
        }
        case 22: // Rain: droplets ringing near the pitch (Tone) at a rate (Rate)
        {
            if (rng.uni() < 0.0001f + 0.004f * m2)
            {
                s.eventFreq = f * std::exp2 (rng.bi() * (1.0f - m1) * 2.0f);
                s.eventAmp = 0.3f + 0.7f * rng.uni();
                s.eventPhase = 0.0f;
            }
            const auto y = s.eventAmp * sine (s.eventPhase);
            s.eventPhase = wrap01 (s.eventPhase + std::min (s.eventFreq, 0.45f));
            s.eventFreq *= 1.00003f;
            s.eventAmp *= 0.9993f;
            return y + 0.02f * rng.bi();
        }
        case 23: // Bubble: rising chirps (Density, Spread of their sizes)
        {
            if (rng.uni() < 0.0001f + 0.003f * m1)
            {
                s.eventFreq = f * std::exp2 (rng.bi() * m2 * 2.0f);
                s.eventAmp = 0.5f + 0.5f * rng.uni();
                s.eventPhase = 0.0f;
            }
            const auto y = s.eventAmp * sine (s.eventPhase);
            s.eventPhase = wrap01 (s.eventPhase + std::min (s.eventFreq, 0.45f));
            s.eventFreq *= 1.0002f;
            s.eventAmp *= 0.9995f;
            return y;
        }
        default: // Chord: four saws in a chord shape (Shape) and inversion (Inversion)
        {
            const auto& shape = chordShapes[std::clamp ((int) (m1 * 7.99f), 0, 7)];
            const auto inv = (int) (m2 * 3.99f);
            float y = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const auto semis = shape[i] + (i < inv ? 12.0f : 0.0f);
                const auto fi = f * std::exp2 (semis / 12.0f);
                y += sawBl (s.swarm[(std::size_t) i], std::min (fi, 0.49f));
                s.swarm[(std::size_t) i] = wrap01 (s.swarm[(std::size_t) i] + fi);
            }
            return y * 0.4f;
        }
    }
}

// ------------------------------------------------------------------------------------------------ the engine's filter
struct FilterState
{
    std::array<Svf, 2> s {};
    std::array<float, 8> ap {};
    float hold = 0.0f, holdT = 0.0f;
    std::array<float, 4096> comb {};
    int combPos = 0;
    float damp = 0.0f;
    std::array<Svf, 8> modes {};
};

constexpr int modeCount = 8;
// the resonators' mode ratios: a membrane's (Bessel zeros over the first) and a plate's (m² + r n² for the first eight
// pairs at r = 1, moved by Ratio) [MODEL: the textbook shapes; their levels and decays fitted on 190ko m1 f15 / f16]
constexpr float membraneRatios[modeCount] { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f };

struct FilterCoefs
{
    int type = 0;
    float g = 1.0f, k = 1.4f, m1 = 0.0f, m2 = 0.0f, fc = 0.1f, drive = 1.0f;
    float combLen = 100.0f, combFb = 0.0f, combDamp = 0.0f;
    std::array<float, modeCount> modeG {}, modeAmp {};
    float modeK = 0.01f;
    float vowelG1 = 0.1f, vowelG2 = 0.2f;
};

inline float svfMorph (float lp, float bp, float hp, float m) noexcept
{
    const auto x = clamp01 (m) * 3.0f; // L → B → H → N
    const float o[4] { lp, bp, hp, lp + hp };
    const auto i = std::min ((int) x, 2);
    const auto t = x - (float) i;
    return o[i] + (o[i + 1] - o[i]) * t;
}

float filterOne (FilterState& st, float x, const FilterCoefs& c) noexcept
{
    auto& s = st.s;
    auto& ap = st.ap;
    auto& hold = st.hold;
    auto& holdT = st.holdT;
    float lp, bp, hp;
    switch (c.type)
    {
        case 0:
            s[0].run (x, c.g, c.k, lp, bp, hp);
            return svfMorph (lp, bp, hp, c.m2);
        case 1:
        {
            s[0].run (x, c.g, c.k, lp, bp, hp);
            const auto y = svfMorph (lp, bp, hp, c.m2);
            s[1].run (y, c.g, c.k, lp, bp, hp);
            return svfMorph (lp, bp, hp, c.m2);
        }
        case 2: // MS2 low pass: driven, 12 dB
            s[0].run (std::tanh (x * c.drive) / std::max (1.0f, c.drive * 0.5f), c.g, c.k, lp, bp, hp);
            return std::tanh (lp);
        case 3:
            s[0].run (std::tanh (x * c.drive) / std::max (1.0f, c.drive * 0.5f), c.g, c.k, lp, bp, hp);
            return std::tanh (hp);
        case 4: // OSR band pass
            s[0].run (std::tanh (x * c.drive), c.g, c.k, lp, bp, hp);
            return bp * c.k;
        case 5: // LP Crunch: the distortion fed back
            s[0].run (x - 0.3f * c.m2 * std::tanh (s[0].ic2 * 3.0f), c.g, c.k, lp, bp, hp);
            s[1].run (std::tanh (lp * (1.0f + 3.0f * c.m2)), c.g, 1.4f, lp, bp, hp);
            return lp;
        case 6: // LP Switched Res: a resonant low pass whose input is sampled and held (Lofi)
        {
            holdT += 1.0f;
            if (holdT >= 1.0f + 40.0f * c.m2 * c.m2)
                holdT = 0.0f, hold = x;
            s[0].run (hold, c.g, c.k, lp, bp, hp);
            return lp;
        }
        case 7: // Filther: input and output distortion around a low pass
            s[0].run (std::tanh (x * c.drive), c.g, c.k, lp, bp, hp);
            s[1].run (lp, c.g, 1.4f, lp, bp, hp);
            return std::tanh (lp * c.drive) / std::tanh (c.drive);
        case 8: // EQ peak: boost around the cutoff
            s[0].run (x, c.g, c.k, lp, bp, hp);
            return x + bp * c.k * c.m2 * 4.0f;
        case 9: // EQ notch: cut around the cutoff
            s[0].run (x, c.g, c.k, lp, bp, hp);
            return x - bp * c.k * c.m2;
        case 10: // Phaser: four all-pass stages with feedback
        {
            auto y = x + c.m1 * 0.9f * ap[7];
            const auto a = (1.0f - c.g) / (1.0f + c.g);
            for (int i = 0; i < 4; ++i)
            {
                const auto ai = std::clamp (a * (1.0f - c.m2 * 0.15f * (float) i), -0.99f, 0.99f);
                const auto out = -ai * y + ap[(std::size_t) i];
                ap[(std::size_t) i] = y + ai * out;
                y = out;
            }
            ap[7] = y;
            return 0.5f * (x + y);
        }
        case 11: // Redux: bit crush + sample-rate reduction at the cutoff
        {
            holdT += c.fc * 2.0f;
            if (holdT >= 1.0f)
                holdT -= 1.0f, hold = x;
            const auto levels = std::exp2 (16.0f - 14.0f * c.m1);
            const auto q = std::round (hold * levels) / levels;
            return q * (0.5f + 0.5f * c.m2) + x * (0.5f - 0.5f * c.m2);
        }
        case 12: // Vowel: two band passes at a formant pair, moved by Morph (coefficients from the control step)
        {
            float l2, b2, h2;
            s[0].run (x, c.vowelG1, c.k, lp, bp, hp);
            s[1].run (x, c.vowelG2, c.k, l2, b2, h2);
            return (bp + 0.7f * b2) * c.k;
        }
        case 13:
        case 14: // Comb + / Comb −: a delay of one period at the cutoff, fed back with the sign, damped in the loop
        {
            const auto len = std::clamp (c.combLen, 2.0f, 4000.0f);
            auto rp = (float) st.combPos - len;
            if (rp < 0.0f)
                rp += 4096.0f;
            const auto i0 = (int) rp;
            const auto fr = rp - (float) i0;
            const auto d = st.comb[(std::size_t) (i0 & 4095)] * (1.0f - fr) + st.comb[(std::size_t) ((i0 + 1) & 4095)] * fr;
            st.damp += (1.0f - c.combDamp) * (d - st.damp);
            const auto fb = (c.type == 13 ? 1.0f : -1.0f) * c.combFb * st.damp;
            const auto y = x + fb;
            st.comb[(std::size_t) st.combPos] = std::clamp (y, -4.0f, 4.0f);
            st.combPos = (st.combPos + 1) & 4095;
            return y;
        }
        case 15:
        case 16: // Plate / Membrane: eight resonant modes above the cutoff
        {
            float y = 0.0f;
            for (int m = 0; m < modeCount; ++m)
            {
                st.modes[(std::size_t) m].run (x, c.modeG[(std::size_t) m], c.modeK, lp, bp, hp);
                y += bp * c.modeAmp[(std::size_t) m] * c.modeK * 2.0f; // (bp · k: unity at each mode)
            }
            return y;
        }
        default: return x;
    }
}

// ------------------------------------------------------------------------------------------------ voices
struct EngineVoice
{
    OscState osc;
    FilterState flt;
    Env amp, mod;
    Lfo1State l1;
    float l1Phase = 0.0f, l2Phase = 0.0f, l2Held = 0.0f;
    float l1Out = 0.0f, l1FxOut = 0.0f, l2Out = 0.0f;
    OnePole toneLp, toneHp, toneLpR, toneHpR;
    float pitchNow = 0.0f, glideFrom = 0.0f, glidePos = 1.0f;
    // the control step's results
    float f = 0.0f, m1 = 0.0f, m2 = 0.0f, gainL = 0.0f, gainR = 0.0f, toneA = 1.0f, toneMode = 0.0f;
    FilterCoefs fc {};
    bool filterOn = false;
    float delay = 0.0f; // B Delay: seconds still to wait
};

struct Voice
{
    bool active = false, held = false;
    int key = 60;
    float velocity = 1.0f, notePitch = 0.0f, slide = 0.0f, pressure = 0.0f, random = 0.0f, spread = 0.0f;
    std::uint64_t age = 0;
    std::array<EngineVoice, 2> e {};
    int counter = 0; // samples to the next control step
};
} // namespace

int polyVoiceCount (int index) noexcept
{
    static constexpr int v[7] { 2, 3, 4, 6, 8, 12, 16 }; // [DATA 190ko: the menu]
    return v[std::clamp (index, 0, 6)];
}
double lfoSyncBeats (int index) noexcept { return 4.0 * syncBars[std::clamp (index, 0, 21)]; }

struct Engine::Impl
{
    double rate = 48000.0;
    std::array<double, stateSize> p {};
    std::array<Voice, maxVoices> voices {};
    std::uint64_t ages = 0;
    Rng rng;
    float pitchOffset = 0.0f, bend = 0.0f, bendRange = 2.0f, wheel = 0.0f, chanPressure = 0.0f;
    float lastKeyPitch = -1.0f;
    float limiterGain = 1.0f;

    // the matrix's non-zero amounts per target (rebuilt when the values change: the audio thread then sums only those)
    struct Route
    {
        std::int8_t source = 0;
        float amount = 0.0f;
    };
    std::array<std::array<Route, sourceCount>, targetCount> routes {};
    std::array<int, targetCount> routeCount {};
    std::array<float, sourceCount> sources {}; // the voice being stepped: its source values

    Impl()
    {
        p = defaults();
        rebuildRoutes();
    }
    void rebuildRoutes() noexcept
    {
        for (int tg = 0; tg < targetCount; ++tg)
        {
            int n = 0;
            for (int s = 0; s < sourceCount; ++s)
                if (const auto a = (float) p[(std::size_t) matrixIndex (tg, s)]; a != 0.0f && std::isfinite (a))
                    routes[(std::size_t) tg][(std::size_t) n++] = { (std::int8_t) s, a };
            routeCount[(std::size_t) tg] = n;
        }
    }
    double v (int i) const noexcept { return p[(std::size_t) i]; }
    double ev (int k, int i) const noexcept { return p[(std::size_t) (engineBase (k) + i)]; }
    bool on (int i) const noexcept { return p[(std::size_t) i] >= 0.5; }

    EnvSet envOf (int k, bool modEnv) const noexcept
    {
        EnvSet e;
        const auto b = engineBase (k) + (modEnv ? eng::modAttack : eng::ampAttack);
        e.attack = (float) v (b), e.decay = (float) v (b + 1), e.release = (float) v (b + 2);
        if (modEnv)
        {
            e.initial = (float) v (b + 3), e.peak = (float) v (b + 4), e.sustain = (float) v (b + 5), e.final = (float) v (b + 6);
            e.aSlope = (float) v (b + 7), e.dSlope = (float) v (b + 8), e.rSlope = (float) v (b + 9);
            e.loop = (int) std::lround (v (b + 10));
        }
        else
        {
            e.aSlope = (float) v (b + 3), e.dSlope = (float) v (b + 4), e.rSlope = (float) v (b + 5);
            e.sustain = (float) v (b + 6);
            e.loop = (int) std::lround (v (b + 7));
        }
        return e;
    }

    void startVoice (Voice& vo, int key, float vel, float pitch, float slide, float pressure, float spreadPos, bool legatoKeep) noexcept
    {
        vo.active = true;
        vo.held = true;
        vo.key = key;
        vo.velocity = vel;
        vo.notePitch = pitch;
        vo.slide = slide < 0.0f ? 0.0f : slide;
        vo.pressure = pressure;
        vo.age = ++ages;
        vo.counter = 0;
        if (! legatoKeep)
            vo.random = rng.bi();
        vo.spread = spreadPos;
        for (int k = 0; k < 2; ++k)
        {
            auto& e = vo.e[(std::size_t) k];
            const auto amp = envOf (on (linkAmp) ? 0 : k, false), mod = envOf (on (linkAmp) ? 0 : k, true);
            e.amp.start (amp, legatoKeep);
            e.mod.start (mod, legatoKeep);
            if (! legatoKeep)
            {
                if (ev (k, eng::oscReset) >= 0.5)
                    e.osc.phase = e.osc.phase2 = e.osc.phase3 = 0.0f;
                e.osc.env = 1.0f;
                if (ev (k, eng::lfo1Retrig) >= 0.5)
                    e.l1Phase = (float) (ev (k, eng::lfo1Phase) / 360.0);
                if (ev (k, eng::lfo2Retrig) >= 0.5)
                    e.l2Phase = (float) (ev (k, eng::lfo2Phase) / 360.0);
                e.delay = k == 1 ? (float) (v (bDelay) * laws.bDelaySeconds) : 0.0f;
                e.glidePos = 1.0f;
            }
            const auto target = (float) key + pitch;
            if (lastKeyPitch >= 0.0f && ev (k, eng::glideTime) > 0.0)
            {
                e.glideFrom = lastKeyPitch - target;
                e.glidePos = 0.0f;
            }
        }
    }

    int freeVoice() noexcept
    {
        const auto limit = on (monoPoly) ? std::min (maxVoices, polyVoiceCount ((int) std::lround (v (polyVoices))) * stackCount()) : stackCount();
        int count = 0, oldest = -1, oldestReleased = -1;
        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& vo = voices[(std::size_t) i];
            if (! vo.active)
                continue;
            ++count;
            if (oldest < 0 || vo.age < voices[(std::size_t) oldest].age)
                oldest = i;
            if (! vo.held && (oldestReleased < 0 || vo.age < voices[(std::size_t) oldestReleased].age))
                oldestReleased = i;
        }
        if (count >= limit)
            return oldestReleased >= 0 ? oldestReleased : oldest;
        for (int i = 0; i < maxVoices; ++i)
            if (! voices[(std::size_t) i].active)
                return i;
        return oldest >= 0 ? oldest : 0;
    }
    int stackCount() const noexcept { return 1 << std::clamp ((int) std::lround (v (stacked)), 0, 2); }

    void note (int key, float vel, float pitch, float slide, float pressure) noexcept
    {
        if (vel <= 0.0f)
        {
            for (auto& vo : voices)
                if (vo.active && vo.held && vo.key == key)
                {
                    vo.held = false;
                    for (auto& e : vo.e)
                        e.amp.release(), e.mod.release();
                }
            return;
        }
        const auto stacks = stackCount();
        if (! on (monoPoly))
        {
            // mono: every stacked voice takes the new note; legato keeps the envelopes when a note is still held
            bool anyHeld = false;
            for (auto& vo : voices)
                anyHeld = anyHeld || (vo.active && vo.held);
            const auto keep = anyHeld && on (legato);
            int s = 0;
            for (int i = 0; i < maxVoices && s < stacks; ++i)
            {
                auto& vo = voices[(std::size_t) i];
                startVoice (vo, key, vel, pitch, slide, pressure, stackPos (s, stacks), keep && vo.active);
                ++s;
            }
            lastKeyPitch = (float) key + pitch;
            return;
        }
        for (int s = 0; s < stacks; ++s)
            startVoice (voices[(std::size_t) freeVoice()], key, vel, pitch, slide, pressure, stackPos (s, stacks), false);
        lastKeyPitch = (float) key + pitch;
    }
    static float stackPos (int s, int n) noexcept { return n <= 1 ? 0.0f : -1.0f + 2.0f * (float) s / (float) (n - 1); }

    // the matrix's sum for one target (engine k's row t)
    float modSum (const Voice&, int target) const noexcept
    {
        const auto n = routeCount[(std::size_t) target];
        float s = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto& r = routes[(std::size_t) target][(std::size_t) i];
            s += r.amount * sources[(std::size_t) r.source];
        }
        return s;
    }
    bool modded (int target) const noexcept { return routeCount[(std::size_t) target] > 0; }
    void takeSources (const Voice& vo) noexcept
    {
        for (int i = 0; i < sourceCount; ++i)
            sources[(std::size_t) i] = sourceValue (vo, i);
    }
    float sourceValue (const Voice& vo, int i) const noexcept
    {
        const auto& a = vo.e[0];
        const auto& b = vo.e[1];
        switch (i)
        {
            case src::aModEnv: return a.mod.value;
            case src::aLfo1: return a.l1Out;
            case src::aLfo1Fx: return a.l1FxOut;
            case src::aLfo2: return a.l2Out;
            case src::aAmpEnv: return a.amp.value;
            case src::bModEnv: return b.mod.value;
            case src::bLfo1: return b.l1Out;
            case src::bLfo1Fx: return b.l1FxOut;
            case src::bLfo2: return b.l2Out;
            case src::bAmpEnv: return b.amp.value;
            case src::velocity: return vo.velocity;
            case src::key: return ((float) vo.key - 60.0f) / (float) laws.keySpan;
            case src::pitchBend: return bendRange > 0.0f ? clampBi (bend / bendRange) : 0.0f; // (the wheel, ±1)
            case src::notePitchBend: return clampBi (vo.notePitch / 48.0f);
            case src::pressure: return std::max (chanPressure, vo.pressure);
            case src::modWheel: return wheel;
            case src::random: return vo.random;
            case src::slide: return vo.slide;
            case src::spread: return vo.spread * (float) v (spread);
            default: return 0.0f;
        }
    }

    float lfoRateHz (int k, bool second, double tempo, float mod) const noexcept
    {
        const auto syncOn = ev (k, second ? eng::lfo2Sync : eng::lfo1Sync) >= 0.5;
        double hz;
        if (syncOn)
            hz = tempo / 60.0 / lfoSyncBeats ((int) std::lround (ev (k, second ? eng::lfo2Synced : eng::lfo1Synced)));
        else
            hz = ev (k, second ? eng::lfo2Rate : eng::lfo1Rate);
        return (float) (hz * std::exp2 (mod * laws.lfoRateOctaves));
    }

    void control (Voice& vo, double tempo) noexcept
    {
        const auto dt = (float) (controlRate / rate);
        takeSources (vo); // (the modulators' times / rates follow the previous step's sources)
        for (int k = 0; k < 2; ++k)
        {
            auto& e = vo.e[(std::size_t) k];
            const auto t0 = k * tgt::perEngine;
            const auto amp = envOf (on (linkAmp) ? 0 : k, false), modE = envOf (on (linkAmp) ? 0 : k, true);
            const auto tm = [&] (int t) { return modded (t0 + t) ? (float) std::exp2 (modSum (vo, t0 + t) * laws.envTimeOctaves) : 1.0f; };
            if (e.delay > 0.0f)
            {
                e.delay -= dt;
                e.gainL = e.gainR = 0.0f;
                continue;
            }
            e.amp.step (amp, dt, amp.attack * tm (tgt::ampAttack), amp.decay * tm (tgt::ampDecay), amp.release * tm (tgt::ampRelease),
                        clamp01 (amp.sustain + modSum (vo, t0 + tgt::ampSustain)));
            e.mod.step (modE, dt, modE.attack * tm (tgt::modAttack), modE.decay * tm (tgt::modDecay), modE.release * tm (tgt::modRelease),
                        clamp01 (modE.sustain + modSum (vo, t0 + tgt::modSustain)));
            // LFO 2
            {
                const auto hz = lfoRateHz (k, true, tempo, modSum (vo, t0 + tgt::lfo2Rate));
                const auto np = e.l2Phase + hz * dt;
                const auto wrapped = np >= 1.0f;
                e.l2Phase = wrap01 (np);
                e.l2Out = lfo2Wave ((int) std::lround (ev (k, eng::lfo2Wave)), e.l2Phase, e.l2Held, wrapped, rng);
            }
            // LFO 1: generator, then the two transformers
            {
                const auto hz = lfoRateHz (k, false, tempo, modSum (vo, t0 + tgt::lfo1Rate));
                const auto np = e.l1Phase + hz * dt;
                const auto wrapped = np >= 1.0f;
                e.l1Phase = wrap01 (np);
                const auto m1 = clamp01 ((float) ev (k, eng::lfo1Macro1) + modSum (vo, t0 + tgt::lfo1Macro1));
                const auto m2 = clamp01 ((float) ev (k, eng::lfo1Macro2) + modSum (vo, t0 + tgt::lfo1Macro2));
                const auto g = lfo1Generator ((int) std::lround (ev (k, eng::lfo1Gen)), e.l1Phase, m1, m2, e.l1, wrapped, rng);
                e.l1Out = g;
                auto y = lfoTransform ((int) std::lround (ev (k, eng::lfo1Fx1)), g, clamp01 ((float) ev (k, eng::lfo1Fx1Macro) + modSum (vo, t0 + tgt::lfo1Fx1)),
                                       e.l1Phase, e.l1, rng);
                y = lfoTransform ((int) std::lround (ev (k, eng::lfo1Fx2)), y, clamp01 ((float) ev (k, eng::lfo1Fx2Macro) + modSum (vo, t0 + tgt::lfo1Fx2)),
                                  e.l1Phase, e.l1, rng);
                e.l1FxOut = y;
            }
        }
        // the values the engines play with (after every source of this step is known)
        takeSources (vo);
        for (int k = 0; k < 2; ++k)
        {
            auto& e = vo.e[(std::size_t) k];
            if (e.delay > 0.0f)
                continue;
            const auto t0 = k * tgt::perEngine;
            // pitch
            auto semis = (ev (k, eng::keytrack) >= 0.5 ? (float) vo.key - 60.0f : 0.0f) + 60.0f + (float) ev (k, eng::transpose)
                         + 12.0f * (float) ev (k, eng::octaves) + (float) ev (k, eng::degrees) // (no scale: a degree is a semitone)
                         + ((float) ev (k, eng::detune) + modSum (vo, t0 + tgt::detune)) * (float) laws.detuneSemis
                         + modSum (vo, t0 + tgt::pitch) * (float) laws.pitchModSemis + pitchOffset;
            if (ev (k, eng::keytrack) >= 0.5)
                semis += vo.notePitch * 0.0f; // (the note's own pitch is a matrix source, Note PB)
            if (e.glidePos < 1.0f)
            {
                const auto gt = std::max ((float) ev (k, eng::glideTime), 1.0e-3f);
                e.glidePos = std::min (1.0f, e.glidePos + dt / gt);
                auto off = e.glideFrom * (1.0f - e.glidePos);
                if (ev (k, eng::glideMode) >= 0.5)
                    off = std::round (off);
                semis += off;
            }
            const auto hz = 440.0 * std::exp2 ((semis - 69.0) / 12.0);
            e.f = (float) std::min (hz / rate, 0.49);
            e.m1 = clamp01 ((float) ev (k, eng::oscMacro1) + modSum (vo, t0 + tgt::oscMacro1));
            e.m2 = clamp01 ((float) ev (k, eng::oscMacro2) + modSum (vo, t0 + tgt::oscMacro2));
            // filter
            e.filterOn = ev (k, eng::filterOn) >= 0.5;
            if (e.filterOn)
            {
                const auto fhz = std::clamp (ev (k, eng::filterFreq) * std::exp2 (modSum (vo, t0 + tgt::filterFreq) * laws.filterModOctaves), 20.0,
                                             rate * 0.45);
                auto& c = e.fc;
                c.type = (int) std::lround (ev (k, eng::filterType));
                c.m1 = clamp01 ((float) ev (k, eng::filterMacro1) + modSum (vo, t0 + tgt::filterMacro1));
                c.m2 = clamp01 ((float) ev (k, eng::filterMacro2) + modSum (vo, t0 + tgt::filterMacro2));
                c.fc = (float) (fhz / rate);
                c.g = (float) std::tan (pi * std::min (fhz / rate, 0.45));
                c.k = 2.0f - 1.95f * c.m1;          // Q
                c.drive = 1.0f + 7.0f * c.m2 * c.m2; // drive
                if (c.type == 12)
                {
                    static constexpr float f1[5] { 730, 530, 270, 570, 300 }, f2[5] { 1090, 1840, 2290, 840, 870 };
                    const auto pos = clamp01 (c.m2) * 3.99f;
                    const auto i = (int) pos;
                    const auto tt = pos - (float) i;
                    const auto scale = (float) (fhz / 1000.0);
                    const auto a = (f1[i] + (f1[i + 1] - f1[i]) * tt) * scale / (float) rate, bb = (f2[i] + (f2[i + 1] - f2[i]) * tt) * scale / (float) rate;
                    c.vowelG1 = std::tan ((float) pi * std::clamp (a, 1.0e-4f, 0.45f));
                    c.vowelG2 = std::tan ((float) pi * std::clamp (bb, 1.0e-4f, 0.45f));
                }
                else if (c.type == 13 || c.type == 14)
                {
                    c.combLen = (float) (rate / fhz);
                    c.combFb = 0.99f * c.m1;
                    c.combDamp = 0.9f * c.m2;
                }
                else if (c.type == 15 || c.type == 16)
                {
                    const auto ratio = 0.5f + c.m1; // (the plate's side ratio / the membrane's tension spread)
                    const auto t60 = 0.02f + 2.0f * c.m2 * c.m2;
                    for (int m = 0; m < modeCount; ++m)
                    {
                        float r = membraneRatios[m];
                        if (c.type == 15)
                        {
                            const auto mm = (float) (1 + m % 3), nn = (float) (1 + m / 3);
                            r = (mm * mm + ratio * nn * nn) / (1.0f + ratio);
                        }
                        const auto f = std::min ((double) fhz * r / rate, 0.45);
                        c.modeG[(std::size_t) m] = (float) std::tan (pi * f);
                        c.modeAmp[(std::size_t) m] = 1.0f / (1.0f + 0.3f * (float) m);
                    }
                    // the modes' damping: k = 2ζ, with T60 = 6.91 / (ζ ω) → k = 13.8 / (ω T60), at the first mode
                    c.modeK = (float) std::clamp (13.8 / (2.0 * pi * fhz * t60), 0.0005, 1.0);
                }
            }
            // tone, pan, volume, envelope
            const auto tone = clampBi ((float) ev (k, eng::tone) + modSum (vo, t0 + tgt::tone));
            e.toneMode = tone;
            const auto toneHz = tone < 0.0f ? 20000.0 * std::exp2 (tone * laws.toneOctaves * 1.5) : 20.0 * std::exp2 (tone * laws.toneOctaves * 1.5);
            e.toneA = (float) (1.0 - std::exp (-2.0 * pi * std::min (toneHz, rate * 0.45) / rate));
            const auto pan = clampBi ((float) ev (k, eng::pan) + modSum (vo, t0 + tgt::pan));
            const auto db = (float) ev (k, eng::volume) + modSum (vo, t0 + tgt::volume) * (float) laws.volumeModDb;
            const auto g = (ev (k, eng::on) >= 0.5 ? (float) laws.engineLevel : 0.0f) * std::pow (10.0f, db / 20.0f) * e.amp.value;
            // the pan law: −3 dB at the centre's partner side (constant power)
            const auto angle = (pan + 1.0f) * 0.25f * (float) pi;
            e.gainL = g * std::cos (angle) * 1.41421356f;
            e.gainR = g * std::sin (angle) * 1.41421356f;
        }
    }

    void renderVoice (Voice& vo, float* l, float* r, int n, double tempo) noexcept
    {
        for (int i = 0; i < n;)
        {
            if (vo.counter <= 0)
            {
                control (vo, tempo);
                vo.counter = controlRate;
            }
            const auto c = std::min (n - i, vo.counter);
            for (int k = 0; k < 2; ++k)
            {
                auto& e = vo.e[(std::size_t) k];
                if (ev (k, eng::on) < 0.5 || e.delay > 0.0f || (e.gainL == 0.0f && e.gainR == 0.0f && e.amp.stage == offStage))
                    continue;
                const auto type = (int) std::lround (ev (k, eng::oscType));
                const auto stereoFilter = e.filterOn && (e.fc.type == 13 || e.fc.type == 14);
                (void) stereoFilter;
                for (int j = 0; j < c; ++j)
                {
                    auto x = oscSample (type, e.osc, e.f, e.m1, e.m2, rng);
                    if (e.filterOn)
                        x = filterOne (e.flt, x, e.fc);
                    if (e.toneMode < -0.001f)
                        x = e.toneLp.lp (x, e.toneA);
                    else if (e.toneMode > 0.001f)
                        x -= e.toneHp.lp (x, e.toneA);
                    l[i + j] += x * e.gainL;
                    r[i + j] += x * e.gainR;
                }
            }
            vo.counter -= c;
            i += c;
        }
        // the voice ends when both amp envelopes are done
        bool alive = false;
        for (int k = 0; k < 2; ++k)
            alive = alive || (ev (k, eng::on) >= 0.5 && (vo.e[(std::size_t) k].amp.stage != offStage || vo.e[(std::size_t) k].delay > 0.0f));
        if (! alive)
            vo.active = false;
    }

    float tmpL[512] {}, tmpR[512] {};
    void render (float* l, float* r, int n, double tempo) noexcept
    {
        const auto driveAmt = (float) v (drive);
        const auto lim = on (limiter);
        const auto outGain = (float) v (volume);
        for (int pos = 0; pos < n; pos += 512)
        {
            const auto c = std::min (512, n - pos);
            for (auto& vo : voices)
            {
                if (! vo.active)
                    continue;
                std::fill (tmpL, tmpL + c, 0.0f);
                std::fill (tmpR, tmpR + c, 0.0f);
                renderVoice (vo, tmpL, tmpR, c, tempo);
                // per voice: drive, then the limiter
                if (driveAmt > 0.0f)
                {
                    const auto d = 1.0f + 15.0f * driveAmt * driveAmt;
                    for (int i = 0; i < c; ++i)
                    {
                        tmpL[i] = std::tanh (tmpL[i] * d) / std::sqrt (d);
                        tmpR[i] = std::tanh (tmpR[i] * d) / std::sqrt (d);
                    }
                }
                if (lim)
                    for (int i = 0; i < c; ++i)
                    {
                        tmpL[i] = std::tanh (tmpL[i]);
                        tmpR[i] = std::tanh (tmpR[i]);
                    }
                for (int i = 0; i < c; ++i)
                {
                    l[pos + i] += tmpL[i] * outGain;
                    r[pos + i] += tmpR[i] * outGain;
                }
            }
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::prepare (double sampleRate) { impl->rate = sampleRate > 0.0 ? sampleRate : 48000.0; }
double Engine::sampleRate() const noexcept { return impl->rate; }
void Engine::setValues (const double* values) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        if (std::isfinite (values[i]))
            impl->p[(std::size_t) i] = values[i];
    impl->rebuildRoutes();
}
void Engine::setParam (int index, double value) noexcept
{
    if (index >= 0 && index < stateSize && std::isfinite (value))
    {
        impl->p[(std::size_t) index] = value;
        if (index >= matrixBase)
            impl->rebuildRoutes();
    }
}
double Engine::param (int index) const noexcept { return index >= 0 && index < stateSize ? impl->p[(std::size_t) index] : 0.0; }
void Engine::note (int key, float velocity, float pitch, float slide, float pressure) noexcept { impl->note (key, velocity, pitch, slide, pressure); }
void Engine::notePitch (int key, float semitones) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.key == key)
            v.notePitch = semitones;
}
void Engine::noteSlide (int key, float slide) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.key == key)
            v.slide = slide;
}
void Engine::notePressure (int key, float pressure) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.key == key)
            v.pressure = pressure;
}
void Engine::slideTo (float semitones, float samples) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.held)
            for (auto& e : v.e)
            {
                e.glideFrom = (float) v.key + v.notePitch - semitones;
                e.glidePos = 0.0f;
                (void) samples;
            }
}
void Engine::setPitchOffset (float semitones) noexcept { impl->pitchOffset = semitones; }
void Engine::setPitchBend (float semitones, float range) noexcept
{
    impl->bend = semitones;
    impl->bendRange = range;
}
void Engine::setModWheel (float value) noexcept { impl->wheel = clamp01 (value); }
void Engine::setPressure (float value) noexcept { impl->chanPressure = clamp01 (value); }
void Engine::allNotesOff() noexcept
{
    for (auto& v : impl->voices)
        if (v.active)
        {
            v.held = false;
            for (auto& e : v.e)
                e.amp.release(), e.mod.release();
        }
}
void Engine::reset() noexcept
{
    for (auto& v : impl->voices)
        v = Voice {};
    impl->lastKeyPitch = -1.0f;
}
void Engine::render (float* left, float* right, int n, double tempo, double) noexcept { impl->render (left, right, n, tempo > 0.0 ? tempo : 120.0); }
bool Engine::idle() const noexcept
{
    for (const auto& v : impl->voices)
        if (v.active)
            return false;
    return true;
}
int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.active ? 1 : 0;
    return n;
}
void Engine::seed (std::uint32_t s) noexcept { impl->rng.s = s != 0 ? s : 1u; }

std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate, int length, int block,
                                    double bpm)
{
    Engine e;
    e.prepare (sampleRate);
    std::vector<double> s (defaults().begin(), defaults().end());
    for (std::size_t i = 0; i < s.size() && i < state.size(); ++i)
        s[i] = state[i];
    e.setValues (s.data());
    std::vector<float> out ((std::size_t) length * 2, 0.0f);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    for (int pos = 0; pos < length; pos += block)
    {
        const auto n = std::min (block, length - pos);
        for (const auto& nt : notes)
        {
            if (nt.start >= pos && nt.start < pos + n)
                e.note (nt.key, nt.velocity);
            if (nt.release >= pos && nt.release < pos + n)
                e.note (nt.key, 0.0f);
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), n, bpm, (double) pos / sampleRate * bpm / 60.0);
        for (int i = 0; i < n; ++i)
        {
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i];
            out[(std::size_t) (pos + i) * 2 + 1] = r[(std::size_t) i];
        }
    }
    return out;
}
} // namespace braid

// ===================================================================================================== the instrument
namespace
{
class BraidInstrument final : public Instrument
{
public:
    BraidInstrument()
    {
        const auto& d = braid::defaults();
        for (int i = 0; i < braid::stateSize; ++i)
            values[(std::size_t) i].store ((float) d[(std::size_t) i]);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        bendRange.store ((float) std::max (1, channel.misc.pitchRange));
        const auto& d = braid::defaults();
        bool changed = false;
        for (int i = 0; i < braid::stateSize; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : (float) d[(std::size_t) i];
            if (! std::isfinite (v))
                v = (float) d[(std::size_t) i];
            if (values[(std::size_t) i].exchange (v) != v)
                changed = true;
        }
        if (changed)
            version.fetch_add (1);
    }

    void setParameter (int index, float value) noexcept override
    {
        const auto* p = index >= 0 && index < braid::paramCount ? &braidEngineParams()[(std::size_t) index] : nullptr;
        if (p == nullptr || ! std::isfinite (value))
            return;
        const auto raw = p->raw (value);
        values[(std::size_t) index].store (raw);
        engine.setParam (index, raw);
    }

    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void setNextNoteModulation (float modX, float modY) noexcept override { nextModX = modX, nextModY = modY; }
    void setChannelPitchNow (float knob, float rest) noexcept override { live.set (knob, rest); }
    void pitchBend (float semitones) noexcept override { engine.setPitchBend (std::isfinite (semitones) ? semitones : 0.0f, bendRange.load()); }
    void controlChange (int cc, float value) noexcept override
    {
        if (cc == 1)
            engine.setModWheel (value);
    }
    void midiMessage (const std::uint8_t* bytes, int size) noexcept override
    {
        if (size >= 2 && (bytes[0] & 0xf0) == 0xd0)
            engine.setPressure ((float) bytes[1] / 127.0f);
        else if (size >= 3 && (bytes[0] & 0xf0) == 0xb0 && bytes[1] == 1)
            engine.setModWheel ((float) bytes[2] / 127.0f);
    }
    // MPE: the note's own pitch, slide and pressure are matrix sources (Note PB, Slide, Pressure)
    void voiceProperty (int key, int prop, float value) noexcept override
    {
        if (prop == 1)
            engine.notePitch (key, value * 0.01f);
        else if (prop == 3)
            engine.noteSlide (key, value);
        else if (prop == 4)
            engine.notePressure (key, pressureOf (value));
    }
    void noteExpression (int key, int prop, float value, float) noexcept override { voiceProperty (key, prop, value); }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 127.0f), (nextCents - liveRest()) * 0.01f,
                     slideOf (nextModX), pressureOf (nextModY));
        nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    }
    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    bool followsChannelPorta() const noexcept override { return true; }
    void slideTo (int key, float cents, float samples) noexcept override
    {
        take();
        engine.slideTo ((float) std::clamp (key, 0, 127) + (cents - liveRest()) * 0.01f, samples);
    }
    void allNotesOff() noexcept override { engine.allNotesOff(); }
    void panic() noexcept override { engine.reset(); }
    bool idle() const noexcept override { return engine.idle(); }

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
            engine.render (bufL, bufR, c, bpm, -1.0);
            for (int i = 0; i < c; ++i)
            {
                left[pos + i] += bufL[i] * gainLeft;
                right[pos + i] += bufR[i] * gainRight;
            }
        }
    }

private:
    static float pressureOf (float modY) noexcept { return std::clamp (2.0f * (modY - 0.5f), 0.0f, 1.0f); }
    static float slideOf (float modX) noexcept { return modX == 0.5f ? -1.0f : std::clamp (modX, 0.0f, 1.0f); }
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }
    void take() noexcept
    {
        const auto v = version.load();
        if (v == taken)
            return;
        taken = v;
        for (int i = 0; i < braid::stateSize; ++i)
            state[(std::size_t) i] = values[(std::size_t) i].load (std::memory_order_relaxed);
        engine.setValues (state.data());
    }

    static constexpr int chunk = 512;
    braid::Engine engine;
    std::array<std::atomic<float>, braid::stateSize> values {};
    std::array<double, braid::stateSize> state {};
    std::atomic<std::uint32_t> version { 1 };
    std::uint32_t taken = 0;
    std::atomic<float> tempo { 120.0f }, pitchCents { 0.0f }, bendRange { 2.0f };
    LiveChannelPitch live;
    float nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    double rate = 48000.0;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makeBraidInstrument() { return std::make_unique<BraidInstrument>(); }
} // namespace lynx
