// Tetra: LYNX's own 4-operator FM synthesizer (core/FmSynth.h). Laws: docs/analise/REA_rodada190hs.md.

#include "core/FmSynth.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>

#include "core/HostSlide.h"
#if (defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64)) && ! defined(LYNX_FM_FORCE_SCALAR)
 #include "core/Simd.h"
 #define LYNX_FM_SIMD 1
// the four phases phase + {0, 1, 2, 3}·inc (32-bit wrap)
inline __m128i phaseRamp4 (std::uint32_t inc) noexcept
{
    return _mm_setr_epi32 (0, (int) inc, (int) (inc * 2u), (int) (inc * 3u));
}
#else
 #define LYNX_FM_SIMD 0
#endif
#include "core/MidiControls.h"
#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx
{
namespace fm
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double floorLevel = 0.00031622799; // Operator's −70 dB floor (its "off" level)
constexpr int controlRate = 16;              // samples per modulation step (envelopes, LFO, pitch)

// --- measured constants (190hs §2; each names the render bank rows that fix it)
struct Laws
{
    double outputGain = 1.0;        // a 0 dB sine carrier = Volume (190hs w00: −12.00 dBFS peak at Volume −12 dB, Vel 0 %)
    double pmRadians = 12.3;        // phase modulation per unit of modulator output (190hs i0*: I ≈ 12.3 × level ≈ 4π)
    double keyDbPerOctave = 6.0;    // KeyScale 100 %: level change per octave from C3
    double lfoDepth = 2.0;          // LFO Amount 1, Amount A 100 %: the frequency moves ± 2 × itself (linear) [190hs l00, l4, l5]
    double penvSemitones = 1.0;     // pitch envelope level unit (Operator stores semitones) [190hs p0, p1]
    double spreadCents = 10.2;      // Spread 100 %: the two voices ± 10.2 cents, hard left / right [190hs x4, x5]
    double envTimeOctaves = 3.0;    // global Time ±100 %: envelope times × 2^(±3 · …/100) [190hs e90, e91: ×2.8 / ×0.33 at ±50]
    double timeVelOctaves = 0.7;    // Time < Vel 100 %: × 2^(0.7 (1 − velocity)) [190hs e8: ×1.27 at velocity 64]
};
constexpr Laws laws {};

// --- wave tables -----------------------------------------------------------------------------------------------------
// table layout: [wave][mip][waveTableSize + 1] (one guard sample); mip m holds the partials up to 1024 >> m, so the
// table an oscillator reads (chosen from its frequency) has no partial above Nyquist
constexpr int mipCount = 11; // highest partial 1024, 512 … 1
struct Tables
{
    std::vector<float> data; // waveCount × mipCount × (size + 1)
    float* at (int wave, int mip) noexcept { return data.data() + ((std::size_t) wave * mipCount + (std::size_t) mip) * (waveTableSize + 1); }
    const float* at (int wave, int mip) const noexcept
    {
        return data.data() + ((std::size_t) wave * mipCount + (std::size_t) mip) * (waveTableSize + 1);
    }
};

// the additive waves [MEASURED 190hs w03…w19: the ideal waves' Fourier amplitudes, not peak-normalised]: Saw n = the
// first n partials of a saw (2/π)/h; Square n = its first n odd partials (4/π)/h; Triangle = odd partials (8/π²)/h²
// alternating; the "D" saw / square = every partial below Nyquist (the same amplitudes)
struct Additive
{
    int kind = 0;     // 0 none, 1 saw, 2 square, 3 triangle
    int partials = 0; // highest partial number
};
Additive additiveOf (int wave) noexcept
{
    static constexpr int counts[7] { 3, 4, 6, 8, 16, 32, 64 };
    if (wave >= 3 && wave <= 9)
        return { 1, counts[wave - 3] };
    if (wave == 10)
        return { 1, 1023 };
    if (wave >= 11 && wave <= 17)
        return { 2, 2 * counts[wave - 11] - 1 };
    if (wave == 18)
        return { 2, 1023 };
    if (wave == 19)
        return { 3, 1023 };
    return {};
}

void additive (float* t, const std::vector<float>& sine, Additive a, int maxPartial)
{
    std::vector<double> acc ((std::size_t) waveTableSize, 0.0);
    for (int h = 1; h <= std::min (a.partials, maxPartial); ++h)
    {
        double amp = 0.0;
        if (a.kind == 1)
            amp = -2.0 / pi / h; // (a rising ramp [MEASURED 190hs o3 / i4: a saw modulator raises the carrier's frequency])
        else if (a.kind == 2)
            amp = (h % 2) == 1 ? 4.0 / pi / h : 0.0;
        else if (a.kind == 3)
            amp = (h % 2) == 1 ? ((h / 2) % 2 == 0 ? 1.0 : -1.0) * 8.0 / (pi * pi) / ((double) h * h) : 0.0;
        if (amp == 0.0)
            continue;
        for (int i = 0; i < waveTableSize; ++i)
            acc[(std::size_t) i] += amp * sine[(std::size_t) ((h * i) % waveTableSize)];
    }
    for (int i = 0; i < waveTableSize; ++i)
        t[i] = (float) acc[(std::size_t) i];
    t[waveTableSize] = t[0];
}

Tables& tables()
{
    static Tables tab = [] {
        Tables x;
        x.data.assign ((std::size_t) waveCount * mipCount * (waveTableSize + 1), 0.0f);
        std::vector<float> sine ((std::size_t) waveTableSize);
        for (int i = 0; i < waveTableSize; ++i)
            sine[(std::size_t) i] = (float) std::sin (2.0 * pi * i / waveTableSize);
        // the looped noise [MEASURED 190hs w20: partials of the note's pitch with random levels, falling off above the
        // 12th]: 16 random partials, peak-normalised
        std::vector<float> loop ((std::size_t) waveTableSize + 1, 0.0f);
        {
            std::uint32_t r = 0x9e3779b9u;
            auto next = [&r] { r = r * 1664525u + 1013904223u; return (double) (r >> 8) / 16777216.0; };
            std::vector<double> acc ((std::size_t) waveTableSize, 0.0);
            for (int h = 1; h <= 16; ++h)
            {
                const auto amp = (0.3 + next()) * (h > 12 ? std::pow (0.5, h - 12) : 1.0);
                const auto ph = (int) (next() * waveTableSize);
                for (int i = 0; i < waveTableSize; ++i)
                    acc[(std::size_t) i] += amp * sine[(std::size_t) ((h * i + ph) % waveTableSize)];
            }
            double peak = 0.0;
            for (auto v : acc)
                peak = std::max (peak, std::abs (v));
            for (int i = 0; i < waveTableSize; ++i)
                loop[(std::size_t) i] = (float) (acc[(std::size_t) i] / peak);
            loop[waveTableSize] = loop[0];
        }
        for (int w = 0; w < waveCount; ++w)
            for (int m = 0; m < mipCount; ++m)
            {
                auto* t = x.at (w, m);
                if (const auto a = additiveOf (w); a.kind != 0)
                {
                    additive (t, sine, a, 1024 >> m);
                    continue;
                }
                if (w == 20)
                {
                    std::copy (loop.begin(), loop.end(), t);
                    continue;
                }
                for (int i = 0; i <= waveTableSize; ++i)
                {
                    const auto s = (double) sine[(std::size_t) (i % waveTableSize)];
                    float v = (float) s;
                    if (w == 1) // 4 bit: 16 steps
                        v = (float) (std::round (s * 7.5 + 0.5) - 0.5) / 7.5f;
                    else if (w == 2) // 8 bit: 256 steps
                        v = (float) (std::round (s * 127.5 + 0.5) - 0.5) / 127.5f;
                    t[i] = v;
                }
            }
        return x;
    }();
    return tab;
}

inline float readTable (const float* t, std::uint32_t phase) noexcept
{
    const auto i = phase >> 21; // 2048 entries
    const auto f = (float) (phase & 0x1fffffu) * (1.0f / 2097152.0f);
    return t[i] + (t[i + 1] - t[i]) * f;
}

int mipFor (double cyclesPerSample) noexcept
{
    // highest harmonic h allowed below Nyquist: h · f < 0.5
    const auto allowed = cyclesPerSample > 0.0 ? 0.5 / cyclesPerSample : 1.0e9;
    int m = 0;
    while (m < mipCount - 1 && (double) (1024 >> m) > allowed)
        ++m;
    return m;
}
} // namespace

// --- routing ---------------------------------------------------------------------------------------------------------
const Routing& routing (int algorithm) noexcept
{
    // 190ei shots (each algorithm drawn by Live): k ← j means j modulates k; A = 0 … D = 3
    static const std::array<Routing, algorithmCount> table = [] {
        std::array<Routing, algorithmCount> r {};
        auto feed = [&] (int a, int from, int to) { r[(std::size_t) a].from[(std::size_t) to] |= (std::uint8_t) (1u << from); };
        auto out = [&] (int a, std::initializer_list<int> ks) {
            for (auto k : ks)
                r[(std::size_t) a].carriers |= (std::uint8_t) (1u << k);
        };
        feed (0, 3, 2), feed (0, 2, 1), feed (0, 1, 0), out (0, { 0 });
        feed (1, 2, 1), feed (1, 3, 1), feed (1, 1, 0), out (1, { 0 });
        feed (2, 2, 1), feed (2, 1, 0), feed (2, 3, 0), out (2, { 0 });
        feed (3, 3, 1), feed (3, 3, 2), feed (3, 1, 0), feed (3, 2, 0), out (3, { 0 });
        feed (4, 3, 2), feed (4, 2, 1), feed (4, 2, 0), out (4, { 0, 1 });
        feed (5, 3, 2), feed (5, 2, 1), out (5, { 0, 1 });
        feed (6, 1, 0), feed (6, 2, 0), feed (6, 3, 0), out (6, { 0 });
        feed (7, 1, 0), feed (7, 3, 2), out (7, { 0, 2 });
        feed (8, 3, 0), feed (8, 3, 1), feed (8, 3, 2), out (8, { 0, 1, 2 });
        feed (9, 3, 2), out (9, { 0, 1, 2 });
        out (10, { 0, 1, 2, 3 });
        return r;
    }();
    return table[(std::size_t) std::clamp (algorithm, 0, algorithmCount - 1)];
}

// --- laws ------------------------------------------------------------------------------------------------------------
double ratio (int coarse, double fine) noexcept
{
    const auto c = coarse <= 0 ? 0.5 : (double) std::min (coarse, 48);
    return c * (1.0 + std::clamp (fine, 0.0, 1000.0) / 1000.0);
}

double fixedHz (double freq, int multi) noexcept
{
    // the range switch: × 10^(multi − 4) [MEASURED 190hs t22…t25: Freq 100 at 2 / 3 / 4 / 5 = 1 / 10 / 100 / 1000 Hz]
    return std::clamp (freq * std::pow (10.0, (double) std::clamp (multi, 0, 8) - 4.0), 0.001, 60000.0);
}

int voiceLimit (double index) noexcept
{
    // Live's voice chooser [the Simpler data of round 182b: index 0 … 14; 190hs y*]
    static constexpr int counts[] { 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 32 };
    return counts[(std::size_t) std::clamp ((int) std::lround (index), 0, (int) std::size (counts) - 1)];
}

double levelDb (double linear) noexcept { return linear <= floorLevel * 1.0001 ? -INFINITY : 20.0 * std::log10 (linear); }

double lfoHz (int range, double rate) noexcept
{
    // [MEASURED 190hs l1000…l1127, l2, l20: low 0.02 Hz at 0, 0.80 at 64, 30.8 Hz at 127, a decade per 40 steps; high = low × 412]
    const auto low = 0.0196 * std::pow (10.0, 0.02515 * std::clamp (rate, 0.0, 127.0));
    return range == 1 ? low * 412.0 : low;
}

namespace
{
// the note values of the synced LFO and of the Beat / Sync loop modes, in beats [MEASURED 190hs: LFO sync 0 / 3 / 7 =
// 1/12, 1/4, 1 beat; Repeat 0 / 3 / 6 / 9 = 1/12, 1/4, 2/3, 2 beats; the steps between and above ASSUMED on Live's
// usual 1/48 … note list]
constexpr double noteBeats[] { 1.0 / 12.0, 1.0 / 8.0, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0, 4.0, 8.0, 16.0, 32.0,
                               64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0, 4096.0, 8192.0 };
} // namespace

double lfoSyncBeats (int index) noexcept { return noteBeats[(std::size_t) std::clamp (index, 0, (int) std::size (noteBeats) - 1)]; }
double repeatBeats (int index) noexcept { return noteBeats[(std::size_t) std::clamp (index, 0, (int) std::size (noteBeats) - 1)]; }

float segment (float x, float slope) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    // slope 0: a straight line in level [MEASURED 190hs e00, e10, e20]; slope 1: an exponential approach that is −90 dB
    // from its end at the segment's end [MEASURED e02, e11, 190ei release]; every other value (−1 … −0.25, 0.25 … 0.9
    // rendered) holds the start level and jumps at the segment's end [MEASURED 190hs e01, e1-1, e10.5, e20.5, a*, d*, r*]
    if (std::abs (slope) < 1.0e-4f)
        return x;
    if (std::abs (slope - 1.0f) < 1.0e-4f)
    {
        constexpr float k = 10.36f;
        return (1.0f - std::exp (-k * x)) / (1.0f - std::exp (-k));
    }
    return x >= 1.0f ? 1.0f : 0.0f;
}

double cutoffHz (double freq, double env, double keyOffset, double velocity, const double* p) noexcept
{
    // the base frequency, then envelope (semitones × amount), key tracking (% of the key distance from C3) and velocity
    auto st = 12.0 * std::log2 (std::clamp (freq, 20.0, 20000.0) / 440.0);
    st += env * p[g::filterEnvAmount - g::filterOn];
    // key: an octave per octave at 100 % around key 70; velocity: 0.074 octave per step around velocity 70 at 100 %
    // [MEASURED 190hs k36…k84, u32…u127, f7]
    st += (keyOffset + 60.0 - 70.0) * p[g::filterKey - g::filterOn] / 100.0;
    st += (velocity * 127.0 - 69.6) * 0.0742 * 12.0 * p[g::filterVel - g::filterOn] / 100.0;
    return std::clamp (440.0 * std::pow (2.0, st / 12.0), 20.0, 22000.0);
}

// --- defaults --------------------------------------------------------------------------------------------------------
namespace
{
void setEnv (std::array<double, stateSize>& d, int base, double a, double dc, double s, double r)
{
    d[(std::size_t) (base + env::attack)] = a;
    d[(std::size_t) (base + env::initial)] = floorLevel;
    d[(std::size_t) (base + env::attackSlope)] = 0.0;
    d[(std::size_t) (base + env::decay)] = dc;
    d[(std::size_t) (base + env::peak)] = 1.0;
    d[(std::size_t) (base + env::decaySlope)] = 1.0;
    d[(std::size_t) (base + env::sustain)] = s;
    d[(std::size_t) (base + env::release)] = r;
    d[(std::size_t) (base + env::end)] = floorLevel;
    d[(std::size_t) (base + env::releaseSlope)] = 1.0;
    d[(std::size_t) (base + env::loopMode)] = 0.0;
    d[(std::size_t) (base + env::loopTime)] = 100.0;
    d[(std::size_t) (base + env::repeat)] = 3.0;
    d[(std::size_t) (base + env::timeVel)] = 0.0;
}
} // namespace

const std::array<double, stateSize>& defaults() noexcept
{
    static const auto d = [] {
        std::array<double, stateSize> v {};
        for (int k = 0; k < opCount; ++k)
        {
            const auto b = opBase (k);
            v[(std::size_t) (b + op::on)] = k == 0 ? 1.0 : 0.0;
            v[(std::size_t) (b + op::level)] = k == 0 ? 1.0 : floorLevel;
            v[(std::size_t) (b + op::coarse)] = 1.0;
            v[(std::size_t) (b + op::fixedFreq)] = 100.0;
            v[(std::size_t) (b + op::fixedMulti)] = 4.0;
            v[(std::size_t) (b + op::retrig)] = 1.0;
            v[(std::size_t) (b + op::velScale)] = 30.0;
            v[(std::size_t) (b + op::lfoOn)] = 1.0;
            setEnv (v, b + op::envBase, 0.1, 600.0, 1.0, 50.0);
            v[(std::size_t) (harmonicsBase + k * harmonics)] = 1.0; // the user wave: a sine
        }
        v[(std::size_t) g::algorithm] = 0.0;
        v[(std::size_t) g::volume] = 0.251188643; // −12 dB
        v[(std::size_t) g::tone] = 0.7;
        v[(std::size_t) g::voices] = 5.0; // 6 voices
        v[(std::size_t) g::retrigger] = 1.0;
        v[(std::size_t) g::glideTime] = 50.0;
        v[(std::size_t) g::lfoRate] = 64.0;
        v[(std::size_t) g::lfoSynced] = 3.0;
        v[(std::size_t) g::lfoRetrig] = 1.0;
        v[(std::size_t) g::lfoAmount] = 0.25;
        v[(std::size_t) g::lfoAmountA] = 100.0;
        setEnv (v, g::lfoEnv, 0.1, 600.0, 1.0, 50.0);
        v[(std::size_t) g::penvAmountA] = 100.0;
        v[(std::size_t) g::penvAmount] = 1.0;
        setEnv (v, g::penv, 0.1, 600.0, 0.0, 50.0);
        v[(std::size_t) (g::penv + env::initial)] = 0.0;
        v[(std::size_t) (g::penv + env::peak)] = 0.0;
        v[(std::size_t) (g::penv + env::end)] = 0.0;
        v[(std::size_t) g::filterType] = 0.0;
        v[(std::size_t) g::filterSlope] = 1.0;
        v[(std::size_t) g::filterFreq] = 4000.0;
        v[(std::size_t) g::filterVel] = 0.0;
        setEnv (v, g::filterEnv, 0.1, 600.0, 0.0, 50.0);
        v[(std::size_t) (g::filterEnv + env::initial)] = 0.0;
        v[(std::size_t) (g::filterEnv + env::end)] = 0.0;
        v[(std::size_t) g::shaperMix] = 100.0;
        return v;
    }();
    return d;
}

float waveValue (int wave, double phase) noexcept
{
    auto& t = tables();
    const auto p = (std::uint32_t) (std::fmod (phase - std::floor (phase), 1.0) * 4294967296.0);
    if (wave == 21)
        return 0.0f;
    return readTable (t.at (std::clamp (wave, 0, waveCount - 2), 0), p);
}

void buildUserWave (const float* partials, float* table) noexcept
{
    std::array<double, waveTableSize> acc {};
    for (int h = 1; h <= harmonics; ++h)
    {
        const auto a = (double) std::clamp (partials[h - 1], 0.0f, 1.0f);
        if (a <= 0.0)
            continue;
        for (int i = 0; i < waveTableSize; ++i)
            acc[(std::size_t) i] += a * std::sin (2.0 * pi * h * i / waveTableSize);
    }
    double peak = 0.0;
    for (auto v : acc)
        peak = std::max (peak, std::abs (v));
    for (int i = 0; i < waveTableSize; ++i)
        table[i] = peak > 0.0 ? (float) (acc[(std::size_t) i] / peak) : 0.0f;
    table[waveTableSize] = table[0];
}
} // namespace fm
} // namespace lynx

// =====================================================================================================================
// the engine
namespace lynx
{
namespace fm
{
namespace
{
struct EnvParams
{
    float attack = 0.1f, initial = 0.0f, attackSlope = 0.0f, decay = 600.0f, peak = 1.0f, decaySlope = 1.0f, sustain = 1.0f,
          release = 50.0f, end = 0.0f, releaseSlope = 1.0f, loopTime = 100.0f, timeVel = 0.0f;
    int loopMode = 0, repeat = 3;
    bool amplitude = true; // levels are gains (the floor is silence); false: plain values (pitch envelope in semitones)

    void read (const double* p, int base, bool amp) noexcept
    {
        attack = (float) std::max (0.0, p[base + env::attack]);
        initial = (float) p[base + env::initial];
        attackSlope = (float) std::clamp (p[base + env::attackSlope], -1.0, 1.0);
        decay = (float) std::max (0.0, p[base + env::decay]);
        peak = (float) p[base + env::peak];
        decaySlope = (float) std::clamp (p[base + env::decaySlope], -1.0, 1.0);
        sustain = (float) p[base + env::sustain];
        release = (float) std::max (0.0, p[base + env::release]);
        end = (float) p[base + env::end];
        releaseSlope = (float) std::clamp (p[base + env::releaseSlope], -1.0, 1.0);
        loopMode = std::clamp ((int) std::lround (p[base + env::loopMode]), 0, 4);
        loopTime = (float) std::max (0.0, p[base + env::loopTime]);
        repeat = (int) std::lround (p[base + env::repeat]);
        timeVel = (float) p[base + env::timeVel];
        amplitude = amp;
        if (amp)
        {
            // Operator's floor (−70 dB) is silence
            auto snap = [] (float v) { return v <= (float) floorLevel * 1.0001f ? 0.0f : v; };
            initial = snap (initial), peak = snap (peak), sustain = snap (sustain), end = snap (end);
        }
    }
};

enum Stage : std::uint8_t { attackStage, decayStage, sustainStage, releaseStage, offStage };

struct Env
{
    Stage stage = offStage;
    float pos = 0.0f;   // ms into the stage
    float from = 0.0f;  // the level the stage started at
    float value = 0.0f;
    float since = 0.0f; // ms since the last (re)start (Beat / Loop modes)
    bool triggered = false;

    void start (const EnvParams& e) noexcept
    {
        stage = attackStage;
        pos = 0.0f;
        since = 0.0f;
        from = e.initial;
        value = e.initial;
    }
    void gateOff (const EnvParams& e) noexcept
    {
        if (stage == offStage || stage == releaseStage || e.loopMode == 4) // Trigger: the note off is ignored
            return;
        stage = releaseStage;
        pos = 0.0f;
        from = value;
    }
};

// one envelope step of dt ms (time already scaled)
inline float shaped (const EnvParams&, float from, float to, float x, float slope) noexcept
{
    return from + (to - from) * segment (x, slope);
}

void stepEnv (Env& v, const EnvParams& e, float dt, float timeScale, double songBeats, double beatsPerMs) noexcept
{
    if (v.stage == offStage)
        return;
    v.since += dt;
    v.pos += dt;
    const auto T = [&] (float ms) { return std::max (ms * timeScale, 0.0f); };
    // the loop modes restart the envelope (from its present level)
    if (e.loopMode == 2 || e.loopMode == 3)
    {
        if (v.stage != releaseStage)
        {
            bool restart = false;
            const auto period = (float) (repeatBeats (e.repeat) / std::max (beatsPerMs, 1.0e-9));
            if (e.loopMode == 2)
                restart = v.since >= period;
            else if (songBeats >= 0.0)
            {
                const auto rb = repeatBeats (e.repeat);
                const auto before = std::floor ((songBeats - beatsPerMs * dt) / rb), now = std::floor (songBeats / rb);
                restart = now != before && v.since > dt;
            }
            if (restart)
            {
                v.stage = attackStage;
                v.pos = 0.0f;
                v.since = 0.0f;
                v.from = v.value;
            }
        }
    }
    for (int guard = 0; guard < 4; ++guard)
    {
        switch (v.stage)
        {
            case attackStage:
            {
                const auto t = T (e.attack);
                if (v.pos < t)
                {
                    v.value = shaped (e, v.from, e.peak, v.pos / t, e.attackSlope);
                    return;
                }
                v.pos -= t;
                v.stage = decayStage;
                v.from = e.peak;
                v.value = e.peak;
                continue;
            }
            case decayStage:
            {
                const auto t = T (e.decay);
                if (v.pos < t)
                {
                    v.value = shaped (e, v.from, e.sustain, v.pos / t, e.decaySlope);
                    return;
                }
                v.pos -= t;
                v.value = e.sustain;
                if (e.loopMode == 4) // Trigger: straight on to the release
                {
                    v.stage = releaseStage;
                    v.from = e.sustain;
                    continue;
                }
                v.stage = sustainStage;
                continue;
            }
            case sustainStage:
                if (e.loopMode == 1) // Loop: from the sustain straight down to silence over Loop Time, then again [MEASURED 190hs e40 / e41]
                {
                    const auto t = T (e.loopTime);
                    if (v.pos < t)
                    {
                        v.value = e.sustain * (1.0f - v.pos / t);
                        return;
                    }
                    v.stage = attackStage;
                    v.pos -= t;
                    v.since = 0.0f;
                    v.from = v.value = 0.0f;
                    continue;
                }
                v.value = e.sustain;
                return;
            case releaseStage:
            {
                const auto t = T (e.release);
                if (v.pos < t)
                {
                    v.value = shaped (e, v.from, e.end, v.pos / t, e.releaseSlope);
                    return;
                }
                // an amplitude envelope's end is the voice's end, whatever its End level [MEASURED 190hs e32: End 25 %,
                // silent after the release]; the other envelopes hold their End level
                v.value = e.amplitude ? 0.0f : e.end;
                v.stage = e.amplitude ? offStage : releaseStage;
                if (v.stage == releaseStage)
                    v.pos = t;
                return;
            }
            case offStage: return;
        }
    }
}

// a TPT state-variable filter (two of them make 24 dB/oct)
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    inline void process (float x, float g, float k, float a1, float a2, float a3, float& lp, float& bp, float& hp) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
        (void) g;
    }
};

struct OpState
{
    std::uint32_t phase = 0, inc = 0;
    float amp = 0.0f, ampStep = 0.0f; // output level, ramped over the control step
    float y1 = 0.0f, y2 = 0.0f;       // the last outputs (feedback)
    float modScale = 1.0f;            // Tone's index factor at its frequency (as a modulator)
    const float* table = nullptr;
};

struct Voice
{
    bool active = false, gate = false;
    int key = 60;
    float velocity = 1.0f, pitch = 0.0f; // the note's own offset (semitones)
    std::uint64_t age = 0;
    std::array<OpState, opCount> ops {};
    std::array<Env, opCount> env {};
    Env penv, fenv, lenv;
    double lfoPhase = 0.0;
    float lfoHeld = 0.0f, lfoNext = 0.0f;
    float glideSt = 0.0f, glideStep = 0.0f; // semitones still to slide (towards 0) and per ms
    HostSlide slide;                        // FL's channel porta and slide notes (round 190ix, core/HostSlide.h): cents
    float pan = 0.0f, detune = 0.0f;        // spread: this voice's side and detune (cents)
    int pair = -1;                          // the other voice of a spread note
    std::uint32_t noise = 1;
    std::array<Svf, 2> svf {};
    int counter = 0;                         // samples left in the control step
    std::array<float, opCount> scale {};     // each operator's velocity × key level scaling (set at the note's start)
    std::array<float, opCount + 3> timeScale {}; // each envelope's time factor (global Time, velocity)
    float cutoff = 1000.0f;
    float fk = 2.0f, fg = 0.1f, fa1 = 1.0f, fa2 = 0.0f, fa3 = 0.0f; // the filter's coefficients (per modulation step)
};

constexpr int maxVoices = 64;
} // namespace

struct Engine::Impl
{
    double rate = 48000.0;
    std::array<double, stateSize> p {};
    std::array<std::vector<float>, opCount> user {}; // the user waves (built by setState)
    std::array<Voice, maxVoices> voices {};
    std::array<EnvParams, opCount> opEnv {};
    EnvParams penvP, fenvP, lenvP;
    std::uint64_t clock = 0;
    float pitchOffset = 0.0f, bend = 0.0f;
    float lastNote = -1.0f;
    std::uint32_t rng = 0x12345678u;
    double lfoFree = 0.0; // a free-running LFO phase (LFO retrigger off)
    float filterDrive = 1.0f, shaperDrive = 1.0f, shaperTrim = 1.0f, shaperMix = 1.0f, volume = 0.25f, panL = 1.0f, panR = 1.0f, toneHz = 1.0e6f;
    int voiceCap = 6;

    Impl()
    {
        for (auto& u : user)
            u.assign (waveTableSize + 1, 0.0f);
        p = defaults();
        derive();
    }

    float rnd() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) ((double) (rng >> 8) / 8388608.0 - 1.0);
    }

    void derive() noexcept
    {
        for (int k = 0; k < opCount; ++k)
            opEnv[(std::size_t) k].read (p.data(), opBase (k) + op::envBase, true);
        penvP.read (p.data(), g::penv, false);
        fenvP.read (p.data(), g::filterEnv, false);
        lenvP.read (p.data(), g::lfoEnv, false);
        volume = (float) (std::clamp (p[g::volume], 0.0, 1.99526) * laws.outputGain);
        const auto pan = std::clamp (p[g::pan], -1.0, 1.0);
        // [the pan law: 190hs x6] constant power, centre = the measured level
        panL = (float) (std::cos ((pan + 1.0) * pi / 4.0) * std::sqrt (2.0));
        panR = (float) (std::sin ((pan + 1.0) * pi / 4.0) * std::sqrt (2.0));
        shaperDrive = (float) std::pow (10.0, std::clamp (p[g::shaperDrive], -20.0, 36.0) / 20.0);
        filterDrive = (float) std::pow (10.0, std::clamp (p[g::filterDrive], 0.0, 24.0) / 20.0);
        shaperTrim = (float) std::pow (10.0, -std::clamp (p[g::shaperDrive], -20.0, 36.0) / 40.0);
        shaperMix = (float) std::clamp (p[g::shaperMix] / 100.0, 0.0, 1.0);
        voiceCap = voiceLimit (p[g::voices]);
        // Tone: how much a modulator's index falls with its frequency f, × 1 / (1 + (f / F)^1.25) with
        // log10 F = 1.74 + 1.90 Tone + 2.21 Tone² [MEASURED 190hs x2 / x3, i0*, i1, i2, o*: a 0 dB modulator gives index 4π
        // at Tone 1; 0.98 / 0.966 / 0.93 × at 262 / 523 / 785 Hz at Tone 0.635; 0.57 / 0.20 × at 262 / 1046 Hz at 0.3;
        // 0.44 / 0.11 × at 65 / 262 Hz at 0]; the heard sound itself is not filtered (x0 = x1)
        const auto tone = std::clamp (p[g::tone], 0.0, 1.0);
        toneHz = (float) std::pow (10.0, 1.74 + 1.90 * tone + 2.21 * tone * tone);
    }

    void rebuildUser (int k) noexcept
    {
        std::array<float, harmonics> parts {};
        for (int h = 0; h < harmonics; ++h)
            parts[(std::size_t) h] = (float) p[(std::size_t) (harmonicsBase + k * harmonics + h)];
        buildUserWave (parts.data(), user[(std::size_t) k].data());
    }

    int wave (int k) const noexcept { return std::clamp ((int) std::lround (p[(std::size_t) (opBase (k) + op::wave)]), 0, waveCount - 1); }

    bool opOn (int k) const noexcept
    {
        const auto b = (std::size_t) opBase (k);
        return p[b + op::on] > 0.5 && p[b + op::level] > floorLevel * 1.0001;
    }

    // --- notes
    Voice* freeVoice() noexcept
    {
        int sounding = 0;
        for (auto& v : voices)
            sounding += v.active && v.pair < 0 ? 1 : 0;
        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.active)
                return &v;
        // steal: the oldest released voice, else the oldest
        for (auto& v : voices)
            if (best == nullptr || (! v.gate && best->gate) || (v.gate == best->gate && v.age < best->age))
                best = &v;
        (void) sounding;
        return best;
    }

    void stealOverCap() noexcept
    {
        // notes beyond the voice count: the oldest note ends (a quick fade: its envelopes jump to release)
        int notes = 0;
        for (auto& v : voices)
            notes += v.active && v.gate ? 1 : 0;
        while (notes > voiceCap)
        {
            Voice* oldest = nullptr;
            for (auto& v : voices)
                if (v.active && v.gate && (oldest == nullptr || v.age < oldest->age))
                    oldest = &v;
            if (oldest == nullptr)
                break;
            kill (*oldest);
            --notes;
        }
    }

    void kill (Voice& v) noexcept
    {
        const auto pair = v.pair;
        v.active = false;
        v.gate = false;
        if (pair >= 0 && pair < maxVoices && voices[(std::size_t) pair].active)
        {
            voices[(std::size_t) pair].active = false;
            voices[(std::size_t) pair].gate = false;
        }
    }

    void startVoice (Voice& v, int key, float velocity, float pitch, float pan, float detune) noexcept
    {
        const bool wasActive = v.active;
        v.active = true;
        v.gate = true;
        v.key = key;
        v.velocity = velocity;
        v.pitch = pitch;
        v.age = ++clock;
        v.pan = pan;
        v.detune = detune;
        v.counter = 0;
        for (int k = 0; k < opCount; ++k)
        {
            auto& o = v.ops[(std::size_t) k];
            const auto b = (std::size_t) opBase (k);
            if (p[b + op::retrig] > 0.5 || ! wasActive)
                o.phase = (std::uint32_t) (std::fmod (std::clamp (p[b + op::phase], 0.0, 100.0) / 100.0 + 0.5, 1.0) * 4294967295.0);
            // (% of a cycle [MEASURED 190hs t4 / t5]; every wave starts half a cycle in [MEASURED i1: a ratio-2 modulator's
            // sidebands fit only with that start, ratios 1 and 3 do not show it])
            o.y1 = o.y2 = 0.0f;
            o.amp = wasActive ? o.amp : 0.0f;
            o.ampStep = 0.0f;
            auto& e = v.env[(std::size_t) k];
            const auto keep = wasActive ? e.value : 0.0f;
            e.start (opEnv[(std::size_t) k]);
            if (wasActive && opEnv[(std::size_t) k].initial <= 0.0f) // a retriggered voice starts from where it is
                e.from = e.value = keep;
        }
        noteScaling (v);
        v.penv.start (penvP);
        v.fenv.start (fenvP);
        v.lenv.start (lenvP);
        v.lfoPhase = p[g::lfoRetrig] > 0.5 ? 0.0 : lfoFree;
        v.lfoHeld = rnd();
        v.lfoNext = rnd();
        if (! wasActive)
        {
            for (auto& s : v.svf)
                s = {};
        }
        // FL's channel porta (round 190ix): the rack starts the note at the previous note's pitch (its cents hold it) and
        // the voice glides back to its own pitch (HostSlide::pendingGlide); a slide note moves it later (slideTo). The
        // channel's live pitch moves every voice through pitchOffset, so the slide does not track it (NaN reference).
        v.slide = {};
        v.slide.anchor (NAN);
        // glide from the last note [190hs x8 / x9]
        v.glideSt = 0.0f;
        v.glideStep = 0.0f;
        const auto st = (float) key + pitch;
        // glide only into a note played while another is held [MEASURED 190hs x8 / x9]
        bool held = false;
        for (const auto& x : voices)
            held = held || (&x != &v && x.active && x.gate);
        if (p[g::glideOn] > 0.5 && held && lastNote >= 0.0f && p[g::glideTime] > 0.0)
        {
            v.glideSt = lastNote - st;
            v.glideStep = std::abs (v.glideSt) / (float) std::max (p[g::glideTime], 1.0);
        }
    }

    // what the note keeps for its whole life: level scaling by velocity / key, envelope times by Time / velocity
    void noteScaling (Voice& v) noexcept
    {
        for (int k = 0; k < opCount; ++k)
        {
            const auto b = (std::size_t) opBase (k);
            // velocity: ((1 − s) + s · velocity / 64)² for Vel s [MEASURED 190hs v1001…v1127 at 100 %: (v / 64)², −72 dB at 1,
            // +12 dB at 127; v5 50 % at 32: −4.95 dB; 190ei 30 % at 127: +4.56 dB]
            const auto sv = std::clamp (p[b + op::velScale] / 100.0, -1.0, 1.0);
            const auto vg = std::max (0.0, (1.0 - sv) + sv * (double) v.velocity * 127.0 / 64.0);
            const auto keyDb = p[b + op::keyScale] / 100.0 * laws.keyDbPerOctave * ((double) v.key - 60.0) / 12.0;
            v.scale[(std::size_t) k] = (float) (vg * vg * std::pow (10.0, keyDb / 20.0));
        }
        const auto timeGlobal = std::pow (2.0, laws.envTimeOctaves * std::clamp (p[g::envTime], -100.0, 100.0) / 100.0);
        const auto velTime = [&] (const EnvParams& e) { return (float) (timeGlobal * std::pow (2.0, laws.timeVelOctaves * e.timeVel / 100.0 * (1.0 - (double) v.velocity))); };
        for (int k = 0; k < opCount; ++k)
            v.timeScale[(std::size_t) k] = velTime (opEnv[(std::size_t) k]);
        v.timeScale[opCount] = velTime (penvP);
        v.timeScale[opCount + 1] = velTime (fenvP);
        v.timeScale[opCount + 2] = velTime (lenvP);
    }

    void noteOn (int key, float velocity, float pitch) noexcept
    {
        // a key that sounds again: retrigger its voice (global Retrigger) or start another one
        Voice* v = nullptr;
        if (p[g::retrigger] > 0.5)
            for (auto& x : voices)
                if (x.active && x.key == key && x.pair <= (int) (&x - voices.data()))
                {
                    v = &x;
                    break;
                }
        const auto spread = std::clamp (p[g::spread], 0.0, 100.0) / 100.0;
        if (v == nullptr)
            v = freeVoice();
        if (v != nullptr && v->pair >= 0 && voices[(std::size_t) v->pair].active && v->pair != (int) (v - voices.data()))
            voices[(std::size_t) v->pair].active = false;
        v->pair = -1;
        if (spread > 0.0)
        {
            const auto d = (float) (laws.spreadCents * spread);
            startVoice (*v, key, velocity, pitch, -1.0f, -d);
            auto* w = freeVoice();
            if (w != nullptr && w != v)
            {
                w->pair = (int) (v - voices.data());
                startVoice (*w, key, velocity, pitch, 1.0f, d);
                w->age = v->age;
                v->pair = (int) (w - voices.data());
                // (v's own pair index points at w; w points back at v)
            }
        }
        else
            startVoice (*v, key, velocity, pitch, 0.0f, 0.0f);
        lastNote = (float) key + pitch;
        stealOverCap();
    }

    // FL's slide note / a Mono channel's next note: the held voices glide to `toSt` (semitones, the frame of key + pitch)
    void slideTo (float toSt, float samples) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.gate)
                v.slide.start ((toSt - ((float) v.key + v.pitch)) * 100.0f, samples);
    }

    void noteOff (int key) noexcept
    {
        for (auto& v : voices)
            if (v.active && v.gate && v.key == key)
            {
                v.gate = false;
                for (int k = 0; k < opCount; ++k)
                    v.env[(std::size_t) k].gateOff (opEnv[(std::size_t) k]);
                v.penv.gateOff (penvP);
                v.fenv.gateOff (fenvP);
                v.lenv.gateOff (lenvP);
            }
    }

    // --- the control step: envelopes, LFO, pitches and levels for the next `n` samples
    void control (Voice& v, int n, double tempo, double songBeats) noexcept
    {
        const auto dt = (float) (1000.0 * n / rate);
        const auto beatsPerMs = tempo / 60000.0;
        // envelope time scale: global Time [190hs e90 / e91] and velocity per envelope (noteScaling)
        for (int k = 0; k < opCount; ++k)
            stepEnv (v.env[(std::size_t) k], opEnv[(std::size_t) k], dt, v.timeScale[(std::size_t) k], songBeats, beatsPerMs);
        stepEnv (v.penv, penvP, dt, v.timeScale[opCount], songBeats, beatsPerMs);
        stepEnv (v.fenv, fenvP, dt, v.timeScale[opCount + 1], songBeats, beatsPerMs);
        stepEnv (v.lenv, lenvP, dt, v.timeScale[opCount + 2], songBeats, beatsPerMs);

        // LFO
        float lfo = 0.0f;
        if (p[g::lfoOn] > 0.5)
        {
            const auto range = (int) std::lround (p[g::lfoRange]);
            const auto hz = range == 2 ? tempo / 60.0 / lfoSyncBeats ((int) std::lround (p[g::lfoSynced])) : lfoHz (range, p[g::lfoRate]);
            const auto before = v.lfoPhase;
            v.lfoPhase += hz * n / rate;
            if (std::floor (v.lfoPhase) != std::floor (before))
            {
                v.lfoHeld = v.lfoNext;
                v.lfoNext = rnd();
            }
            const auto ph = (float) (v.lfoPhase - std::floor (v.lfoPhase));
            float s = 0.0f;
            switch ((int) std::lround (p[g::lfoType]))
            {
                case 0: s = std::sin (2.0f * (float) pi * ph); break;
                case 1: s = ph < 0.5f ? 1.0f : -1.0f; break;
                case 2: s = 1.0f - 4.0f * std::abs (ph - 0.5f) ; break; // (from its bottom [MEASURED 190hs l02])
                case 3: s = 2.0f * ph - 1.0f; break;
                case 4: s = 1.0f - 2.0f * ph; break;
                case 5: s = v.lfoHeld; break;
                default: s = v.lfoHeld + (v.lfoNext - v.lfoHeld) * ph; break; // noise: random values joined smoothly (approximate, 190hs l06)
            }
            const auto envGain = v.lenv.stage == offStage ? lenvP.sustain : v.lenv.value;
            // the Amount knob acts squared [MEASURED 190hs l4: Amount 0.25 = 1/16 of the depth at 1]
            const auto amount = (float) std::clamp (p[g::lfoAmount], 0.0, 1.0);
            lfo = s * amount * amount * envGain;
        }
        const auto lfoA = lfo * (float) (p[g::lfoAmountA] / 100.0);

        // pitch: the note, glide, transpose, the channel's pitch and the wheel
        if (v.glideSt != 0.0f)
        {
            const auto stepSt = v.glideStep * dt;
            v.glideSt = std::abs (v.glideSt) <= stepSt ? 0.0f : v.glideSt - std::copysign (stepSt, v.glideSt);
        }
        const auto slideSt = v.slide.left > 0.0f || v.slide.cents != 0.0f ? v.slide.advance ((float) n) * 0.01f : 0.0f;
        const auto noteSt = (float) v.key + v.pitch + v.glideSt + slideSt + (float) p[g::transpose] + pitchOffset + bend + v.detune * 0.01f;
        const auto noteHz = 440.0 * std::pow (2.0, (noteSt - 69.0f) / 12.0);
        const auto penv = p[g::penvOn] > 0.5 ? v.penv.value * (float) laws.penvSemitones * (float) p[g::penvAmount] * (float) (p[g::penvAmountA] / 100.0) : 0.0f;

        const auto& tab = tables();
        for (int k = 0; k < opCount; ++k)
        {
            auto& o = v.ops[(std::size_t) k];
            const auto b = (std::size_t) opBase (k);
            const bool on = opOn (k);
            double hz = p[b + op::fixed] > 0.5 ? fixedHz (p[b + op::fixedFreq], (int) std::lround (p[b + op::fixedMulti]))
                                               : noteHz * ratio ((int) std::lround (p[b + op::coarse]), p[b + op::fine]);
            if (p[b + op::pitchEnvOn] > 0.5 && penv != 0.0f)
                hz *= std::pow (2.0, penv / 12.0);
            // the LFO moves the frequency linearly, through zero (the wave then runs backwards) [MEASURED 190hs l00:
            // 261.6 Hz swings −261 … 785 Hz at Amount 1]
            if (p[b + op::lfoOn] > 0.5)
                hz *= 1.0 + laws.lfoDepth * lfoA;
            const auto cps = std::clamp (hz / rate, -0.5, 0.5);
            o.inc = (std::uint32_t) (std::int32_t) (cps * 4294967295.0);
            o.modScale = (float) (1.0 / (1.0 + std::pow (std::abs (hz) / (double) toneHz, 1.25)));
            const auto w = wave (k);
            o.table = w == waveUser ? user[(std::size_t) k].data() : tab.at (w == 21 ? 0 : w, mipFor (std::abs (cps)));
            // the level: envelope × level × velocity and key scaling [190hs v*]
            float target = 0.0f;
            if (on)
                target = v.env[(std::size_t) k].value * (float) p[b + op::level] * v.scale[(std::size_t) k];
            o.ampStep = (target - o.amp) / (float) n;
        }
        // filter coefficients
        if (p[g::filterOn] > 0.5)
        {
            double fp[13];
            for (int i = 0; i < 13; ++i)
                fp[i] = p[(std::size_t) (g::filterOn + i)];
            auto hz = cutoffHz (p[g::filterFreq], v.fenv.value, (double) v.key - 60.0, v.velocity, fp);
            if (p[g::lfoFilter] > 0.5)
                hz *= std::pow (2.0, lfoA * 48.0 / 12.0);
            v.cutoff = (float) std::clamp (hz, 20.0, rate * 0.45);
            const auto type = std::clamp ((int) std::lround (p[g::filterType]), 0, filterTypeCount - 1);
            const bool steep = p[g::filterSlope] > 0.5;
            const auto circuit = std::clamp ((int) std::lround (type <= 1 ? p[g::filterCircuit] : p[g::filterCircuitBp]), 0, circuitCount - 1);
            static constexpr float resScale[circuitCount] { 1.0f, 0.85f, 1.08f, 0.97f, 0.88f };
            const auto r = std::clamp ((float) p[g::filterRes] * resScale[circuit], 0.0f, 1.0f);
            const auto kmin = steep ? 0.267f : 0.067f;
            v.fk = kmin + (2.0f - kmin) * std::pow (1.0f - r, 2.1f);
            const auto fhz = circuit > 0 ? v.cutoff * 0.88f : v.cutoff;
            v.fg = std::tan ((float) pi * std::min (fhz, (float) rate * 0.45f) / (float) rate);
            v.fa1 = 1.0f / (1.0f + v.fg * (v.fg + v.fk));
            v.fa2 = v.fg * v.fa1;
            v.fa3 = v.fg * v.fa2;
        }
    }

    // One control step of a voice's operators (n ≤ controlRate samples) into `sum`: D, C, B, A in turn, each over the
    // whole step (a modulator always has a higher index than what it feeds, so its step is ready before). Operators
    // without feedback or noise run 4 samples at a time (SSE: phases, phase modulation, table interpolation, level ramp).
    void blockOf (Voice& v, const Routing& r, int n, float* sum) noexcept
    {
        constexpr float pmCycles = (float) (laws.pmRadians / (2.0 * pi));
        alignas (16) float out[opCount][controlRate] {};
        alignas (16) float pm[controlRate];
        std::fill (sum, sum + n, 0.0f);
        for (int k = opCount - 1; k >= 0; --k)
        {
            auto& o = v.ops[(std::size_t) k];
            auto* y = out[k];
            if (o.amp == 0.0f && o.ampStep == 0.0f)
            {
                o.phase += o.inc * (std::uint32_t) n;
                continue;
            }
            std::fill (pm, pm + n, 0.0f);
            const auto from = r.from[(std::size_t) k];
            for (int j = k + 1; j < opCount; ++j)
                if ((from >> j) & 1u)
                    for (int i = 0; i < n; ++i)
                        pm[i] += out[j][i];
            // feedback acts only on an operator nothing modulates in the algorithm [MEASURED 190hs b010…b100: A of algorithm
            // 1 alone and B of algorithm 1 sound the same at any Feedback; switched-off modulators still count]
            // (the self modulation's depth in radians: 2.4 tanh (2 · Feedback) [MEASURED 190hs b1010 … b1050: 0.43 / 1.23 /
            // 1.88 rad at 10 / 25 / 50 %])
            const auto fb = from != 0 ? 0.0f
                                      : 2.4f * std::tanh (0.02f * (float) p[(std::size_t) (opBase (k) + op::feedback)]) / (float) laws.pmRadians;
            const bool noise = wave (k) == 21;
#if LYNX_FM_SIMD
            if (fb <= 0.0f && ! noise && n == controlRate)
            {
                const auto* t = o.table;
                const __m128i step4 = _mm_set1_epi32 ((int) (o.inc * 4u));
                __m128i ph = _mm_add_epi32 (_mm_set1_epi32 ((int) o.phase), phaseRamp4 (o.inc));
                __m128 amp = _mm_add_ps (_mm_set1_ps (o.amp), _mm_mul_ps (_mm_set1_ps (o.ampStep), _mm_setr_ps (1.0f, 2.0f, 3.0f, 4.0f)));
                const __m128 ampStep4 = _mm_set1_ps (o.ampStep * 4.0f);
                const __m128 cyc = _mm_set1_ps (pmCycles), full = _mm_set1_ps (4294967040.0f), lim = _mm_set1_ps (64.0f);
                const __m128 fracScale = _mm_set1_ps (1.0f / 2097152.0f);
                const __m128i fracMask = _mm_set1_epi32 (0x1fffff);
                for (int i = 0; i < n; i += 4)
                {
                    // the phase offset of the modulation, in whole turns dropped (−½ … ½ of a turn)
                    auto c = _mm_mul_ps (_mm_min_ps (_mm_max_ps (_mm_load_ps (pm + i), _mm_sub_ps (_mm_setzero_ps(), lim)), lim), cyc);
                    c = _mm_sub_ps (c, _mm_cvtepi32_ps (_mm_cvtps_epi32 (c)));
                    const auto at = _mm_add_epi32 (ph, _mm_cvttps_epi32 (_mm_mul_ps (c, full)));
                    const auto idx = _mm_srli_epi32 (at, 21);
                    const auto frac = _mm_mul_ps (_mm_cvtepi32_ps (_mm_and_si128 (at, fracMask)), fracScale);
                    alignas (16) std::int32_t ix[4];
                    _mm_store_si128 ((__m128i*) ix, idx);
                    const auto a0 = _mm_setr_ps (t[ix[0]], t[ix[1]], t[ix[2]], t[ix[3]]);
                    const auto a1 = _mm_setr_ps (t[ix[0] + 1], t[ix[1] + 1], t[ix[2] + 1], t[ix[3] + 1]);
                    const auto sv = _mm_add_ps (a0, _mm_mul_ps (_mm_sub_ps (a1, a0), frac));
                    _mm_store_ps (y + i, _mm_mul_ps (sv, amp));
                    ph = _mm_add_epi32 (ph, step4);
                    amp = _mm_add_ps (amp, ampStep4);
                }
                o.phase += o.inc * (std::uint32_t) n;
                o.amp += o.ampStep * (float) n;
                o.y1 = o.y2 = 0.0f;
            }
            else
#endif
            {
                for (int i = 0; i < n; ++i)
                {
                    o.amp += o.ampStep;
                    auto m = pm[i];
                    if (fb > 0.0f)
                        m += (o.y1 + o.y2) * 0.5f * fb;
                    m = std::clamp (m, -64.0f, 64.0f);
                    float sv;
                    if (noise)
                    {
                        v.noise = v.noise * 1664525u + 1013904223u;
                        sv = (float) ((double) (v.noise >> 8) / 8388608.0 - 1.0);
                    }
                    else
                        sv = readTable (o.table, o.phase + (std::uint32_t) (std::int64_t) (m * pmCycles * 4294967296.0f)); // (whole turns drop)
                    o.phase += o.inc;
                    // (the feedback takes the operator's output after its level and envelope [MEASURED 190hs b3 / b4: D at
                    // −12 dB with Feedback 50 % changes its chain only a little])
                    o.y2 = o.y1;
                    o.y1 = sv * o.amp;
                    y[i] = o.y1;
                }
            }
            if ((r.carriers >> k) & 1u)
                for (int i = 0; i < n; ++i)
                    sum[i] += y[i];
            // what this operator sends into the others' phase, scaled by Tone's law of its frequency
            if (k > 0 && o.modScale != 1.0f)
                for (int i = 0; i < n; ++i)
                    y[i] *= o.modScale;
        }
    }

    float filterSample (Voice& v, float x) noexcept
    {
        const auto type = std::clamp ((int) std::lround (p[g::filterType]), 0, filterTypeCount - 1);
        const bool steep = p[g::filterSlope] > 0.5;
        const auto circuit = std::clamp ((int) std::lround (type <= 1 ? p[g::filterCircuit] : p[g::filterCircuitBp]), 0, circuitCount - 1);
        // resonance → damping [MEASURED 190hs f0x on white noise: 24 dB/oct peaks +6.9 / +22.9 dB at Res 0.5 / 1, 12 dB/oct
        // +23.5 dB at Res 1; Res above 1 = 1]; the driven circuits resonate a little less or more and sit lower [f31…f35,
        // approximate: peaks −3.1 / +1.6 / −0.8 / −2.6 dB against Clean, at 0.88 × the frequency]
        // (coefficients: once per modulation step, in control())
        const auto k = v.fk, gg = v.fg, a1 = v.fa1, a2 = v.fa2, a3 = v.fa3;
        // Drive: a gain into the filter [MEASURED f4: +12 dB louder at Drive 12]; the driven circuits saturate softly
        // (the Clean circuit ignores Drive [MEASURED 190hs c0])
        if (circuit > 0)
            x = 3.0f * std::tanh (x * filterDrive * (1.0f / 3.0f)); // (gentle: f4's +12 dB stayed nearly linear on noise)
        auto stage = [&] (Svf& s, float in) {
            float lp, bp, hp;
            s.process (in, gg, k, a1, a2, a3, lp, bp, hp);
            switch (type)
            {
                case 0: return lp;
                case 1: return hp;
                case 2: return bp; // (−6 dB at the frequency at Res 0 [MEASURED f12])
                case 3: return lp + hp;
                default:
                {
                    // morph: low → band → high → notch → low [MEASURED f20.25 … f20.75]
                    const auto m = (float) std::clamp (p[g::filterMorph], 0.0, 1.0) * 4.0f;
                    const float outs[5] { lp, bp, hp, lp + hp, lp };
                    const auto i = std::min ((int) m, 3);
                    const auto f = m - (float) i;
                    return outs[i] + (outs[i + 1] - outs[i]) * f;
                }
            }
        };
        auto y = stage (v.svf[0], x);
        if (steep)
            y = stage (v.svf[1], y);
        return y;
    }

    // the shaper, on the voice before the volume, its output trimmed by half the drive in dB [MEASURED 190hs h1… h4:
    // Soft = tanh (1.19 g x), Hard = a clip of 4.4 g x, Sine = sin (π/2 g x), g = the drive's gain; 4 Bit approximate]
    float shape (float x) const noexcept
    {
        const auto type = (int) std::lround (p[g::shaperType]);
        if (type <= 0)
            return x;
        const auto d = x * shaperDrive;
        float y = d;
        switch (type)
        {
            case 1: y = std::tanh (1.185f * d); break;
            case 2: y = std::clamp (4.42f * d, -1.0f, 1.0f); break;
            case 3: y = std::sin (d * (float) pi * 0.5f); break;
            default: y = std::round (std::sin (d * (float) pi * 0.5f) * 7.5f) / 7.5f; break;
        }
        return x + (y * shaperTrim - x) * shaperMix;
    }

    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept
    {
        const auto& r = routing ((int) std::lround (p[g::algorithm]));
        const bool filter = p[g::filterOn] > 0.5;
        const bool shaper = filter && p[g::shaperType] > 0.5;
        const auto lfoHz0 = p[g::lfoOn] > 0.5 && p[g::lfoRetrig] < 0.5
                                ? ((int) std::lround (p[g::lfoRange]) == 2 ? tempo / 60.0 / lfoSyncBeats ((int) std::lround (p[g::lfoSynced]))
                                                                           : lfoHz ((int) std::lround (p[g::lfoRange]), p[g::lfoRate]))
                                : 0.0;
        lfoFree += lfoHz0 * n / rate;
        lfoFree -= std::floor (lfoFree);
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            int pos = 0;
            while (pos < n)
            {
                if (v.counter <= 0)
                {
                    const auto beat = songBeats >= 0.0 ? songBeats + tempo / 60.0 * pos / rate : -1.0;
                    control (v, controlRate, tempo, beat);
                    v.counter = controlRate;
                }
                const auto c = std::min (v.counter, n - pos);
                // the side of a spread voice: −1 left … 1 right
                const auto gl = volume * panL * (v.pan > 0.0f ? 1.0f - v.pan : 1.0f);
                const auto gr = volume * panR * (v.pan < 0.0f ? 1.0f + v.pan : 1.0f);
                alignas (16) float block[controlRate];
                blockOf (v, r, c, block);
                for (int i = 0; i < c; ++i)
                {
                    auto x = block[i];
                    if (filter)
                        x = filterSample (v, x);
                    if (shaper) // (the shaper belongs to the filter section: off with it [MEASURED 190hs s1…s5])
                        x = shape (x);
                    left[pos + i] += x * gl;
                    right[pos + i] += x * gr;
                }
                pos += c;
                v.counter -= c;
            }
            // a voice ends when every heard envelope is over
            bool sounding = false;
            for (int k = 0; k < opCount; ++k)
                if (((r.carriers >> k) & 1u) && opOn (k) && v.env[(std::size_t) k].stage != offStage)
                    sounding = true;
            if (! sounding)
                v.active = false;
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    tables();
    impl->rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    impl->derive();
    reset();
}

double Engine::sampleRate() const noexcept { return impl->rate; }

void Engine::setState (const double* values) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        impl->p[(std::size_t) i] = std::isfinite (values[i]) ? values[i] : defaults()[(std::size_t) i];
    for (int k = 0; k < opCount; ++k)
        impl->rebuildUser (k);
    impl->derive();
}

void Engine::setValues (const double* values) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        impl->p[(std::size_t) i] = std::isfinite (values[i]) ? values[i] : defaults()[(std::size_t) i];
    impl->derive();
}

void Engine::setUserTable (int k, const float* table) noexcept
{
    if (k >= 0 && k < opCount && table != nullptr)
        std::copy (table, table + waveTableSize + 1, impl->user[(std::size_t) k].begin());
}

void Engine::setParam (int index, double value) noexcept
{
    if (index < 0 || index >= paramCount || ! std::isfinite (value))
        return;
    impl->p[(std::size_t) index] = value;
    impl->derive();
}

double Engine::param (int index) const noexcept { return index >= 0 && index < stateSize ? impl->p[(std::size_t) index] : 0.0; }

void Engine::note (int key, float velocity, float pitch) noexcept
{
    if (velocity > 0.0f)
        impl->noteOn (std::clamp (key, 0, 127), std::clamp (velocity, 0.0f, 1.0f), pitch);
    else
        impl->noteOff (key);
}

void Engine::slideTo (float semitones, float samples) noexcept
{
    if (std::isfinite (semitones) && std::isfinite (samples))
        impl->slideTo (semitones, std::max (samples, 1.0f));
}

void Engine::setPitchOffset (float semitones) noexcept { impl->pitchOffset = std::isfinite (semitones) ? semitones : 0.0f; }
void Engine::setPitchBend (float semitones) noexcept { impl->bend = std::isfinite (semitones) ? semitones : 0.0f; }

void Engine::allNotesOff() noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate)
            impl->noteOff (v.key);
}

void Engine::reset() noexcept
{
    for (auto& v : impl->voices)
        v = Voice {};
    impl->lastNote = -1.0f;
}

void Engine::render (float* left, float* right, int n, double tempo, double songBeats) noexcept
{
    if (n > 0)
        impl->render (left, right, n, tempo > 0.0 ? tempo : 120.0, songBeats);
}

bool Engine::idle() const noexcept { return activeVoices() == 0; }

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : impl->voices)
        n += v.active ? 1 : 0;
    return n;
}

void Engine::seed (std::uint32_t s) noexcept { impl->rng = s != 0 ? s : 1u; }

std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    int block, double bpm)
{
    Engine e;
    e.prepare (sampleRate);
    std::vector<double> s (defaults().begin(), defaults().end());
    for (std::size_t i = 0; i < state.size() && i < s.size(); ++i)
        s[i] = state[i];
    e.setState (s.data());
    std::vector<float> out ((std::size_t) length * 2, 0.0f);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    struct Event
    {
        int at, key;
        float vel;
    };
    std::vector<Event> events;
    for (const auto& nt : notes)
    {
        events.push_back ({ nt.start, nt.key, nt.velocity });
        if (nt.release >= 0)
            events.push_back ({ nt.release, nt.key, 0.0f });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });
    std::size_t next = 0;
    for (int pos = 0; pos < length;)
    {
        while (next < events.size() && events[next].at <= pos)
        {
            e.note (events[next].key, events[next].vel);
            ++next;
        }
        auto n = std::min (block, length - pos);
        if (next < events.size())
            n = std::min (n, events[next].at - pos);
        n = std::max (n, 1);
        std::fill (l.begin(), l.begin() + n, 0.0f);
        std::fill (r.begin(), r.begin() + n, 0.0f);
        e.render (l.data(), r.data(), n, bpm, pos * bpm / 60.0 / sampleRate);
        for (int i = 0; i < n; ++i)
        {
            out[(std::size_t) (2 * (pos + i))] = l[(std::size_t) i];
            out[(std::size_t) (2 * (pos + i) + 1)] = r[(std::size_t) i];
        }
        pos += n;
    }
    return out;
}
} // namespace fm
} // namespace lynx

// =====================================================================================================================
// parameters, texts, the channel's values and the instrument
namespace lynx
{
namespace fm
{
namespace
{
struct Spec
{
    std::string name;
    float low, high;
    bool stepped;
};

const char* const opNames[opCount] { "A", "B", "C", "D" };

void envSpecs (std::vector<Spec>& s, const std::string& who, bool pitch)
{
    const float lo = pitch ? -48.0f : 0.0f, hi = pitch ? 48.0f : 1.0f;
    s.push_back ({ who + " Attack", 0.0f, 20000.0f, false });
    s.push_back ({ who + " Initial", lo, hi, false });
    s.push_back ({ who + " Attack Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Decay", 0.0f, 20000.0f, false });
    s.push_back ({ who + " Peak", lo, hi, false });
    s.push_back ({ who + " Decay Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Sustain", lo, hi, false });
    s.push_back ({ who + " Release", 0.0f, 20000.0f, false });
    s.push_back ({ who + " End", lo, hi, false });
    s.push_back ({ who + " Release Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Loop Mode", 0.0f, 4.0f, true });
    s.push_back ({ who + " Loop Time", 0.0f, 20000.0f, false });
    s.push_back ({ who + " Repeat", 0.0f, 21.0f, true });
    s.push_back ({ who + " Time < Vel", -100.0f, 100.0f, false });
}

const std::vector<Spec>& specs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        for (int k = 0; k < opCount; ++k)
        {
            const std::string o = opNames[k];
            s.push_back ({ o + " On", 0.0f, 1.0f, true });
            s.push_back ({ o + " Level", (float) floorLevel, 1.0f, false });
            s.push_back ({ o + " Wave", 0.0f, (float) (waveCount - 1), true });
            s.push_back ({ o + " Coarse", 0.0f, 48.0f, true });
            s.push_back ({ o + " Fine", 0.0f, 1000.0f, false });
            s.push_back ({ o + " Fixed", 0.0f, 1.0f, true });
            s.push_back ({ o + " Fixed Freq", 1.0f, 1000.0f, false });
            s.push_back ({ o + " Fixed Range", 0.0f, 6.0f, true });
            s.push_back ({ o + " Feedback", 0.0f, 100.0f, false });
            s.push_back ({ o + " Phase", 0.0f, 100.0f, false });
            s.push_back ({ o + " Retrigger", 0.0f, 1.0f, true });
            s.push_back ({ o + " Level < Vel", -100.0f, 100.0f, false });
            s.push_back ({ o + " Level < Key", -100.0f, 100.0f, false });
            s.push_back ({ o + " LFO On", 0.0f, 1.0f, true });
            s.push_back ({ o + " Pitch Env On", 0.0f, 1.0f, true });
            envSpecs (s, o + " Env", false);
        }
        s.push_back ({ "Algorithm", 0.0f, (float) (algorithmCount - 1), true });
        s.push_back ({ "Volume", 0.0f, 1.99526f, false });
        s.push_back ({ "Pan", -1.0f, 1.0f, false });
        s.push_back ({ "Transpose", -48.0f, 48.0f, true });
        s.push_back ({ "Tone", 0.0f, 1.0f, false });
        s.push_back ({ "Spread", 0.0f, 100.0f, false });
        s.push_back ({ "Voices", 0.0f, 14.0f, true });
        s.push_back ({ "Retrigger", 0.0f, 1.0f, true });
        s.push_back ({ "Glide", 0.0f, 1.0f, true });
        s.push_back ({ "Glide Time", 0.0f, 10000.0f, false });
        s.push_back ({ "Time", -100.0f, 100.0f, false });
        s.push_back ({ "LFO On", 0.0f, 1.0f, true });
        s.push_back ({ "LFO Wave", 0.0f, 6.0f, true });
        s.push_back ({ "LFO Range", 0.0f, 2.0f, true });
        s.push_back ({ "LFO Rate", 0.0f, 127.0f, false });
        s.push_back ({ "LFO Sync Rate", 0.0f, 13.0f, true });
        s.push_back ({ "LFO Retrigger", 0.0f, 1.0f, true });
        s.push_back ({ "LFO Amount", 0.0f, 1.0f, false });
        s.push_back ({ "LFO Amount A", 0.0f, 100.0f, false });
        s.push_back ({ "LFO Filter On", 0.0f, 1.0f, true });
        envSpecs (s, "LFO Env", false);
        s.push_back ({ "Pitch Env On", 0.0f, 1.0f, true });
        s.push_back ({ "Pitch Env Amount A", 0.0f, 100.0f, false });
        s.push_back ({ "Pitch Env Amount", -1.0f, 1.0f, false });
        envSpecs (s, "Pitch Env", true);
        s.push_back ({ "Filter On", 0.0f, 1.0f, true });
        s.push_back ({ "Filter Type", 0.0f, (float) (filterTypeCount - 1), true });
        s.push_back ({ "Filter Slope", 0.0f, 1.0f, true });
        s.push_back ({ "Filter Circuit", 0.0f, 4.0f, true });
        s.push_back ({ "Filter Circuit BP", 0.0f, 2.0f, true });
        s.push_back ({ "Filter Freq", 20.0f, 20000.0f, false });
        s.push_back ({ "Filter Res", 0.0f, 1.25f, false });
        s.push_back ({ "Filter Morph", 0.0f, 1.0f, false });
        s.push_back ({ "Filter Drive", 0.0f, 24.0f, false });
        s.push_back ({ "Filter Freq < Vel", 0.0f, 100.0f, false });
        s.push_back ({ "Filter Freq < Key", 0.0f, 100.0f, false });
        s.push_back ({ "Filter Env Amount", -72.0f, 72.0f, false });
        envSpecs (s, "Filter Env", false);
        s.push_back ({ "Shaper", 0.0f, 4.0f, true });
        s.push_back ({ "Shaper Drive", -20.0f, 36.0f, false });
        s.push_back ({ "Shaper Dry/Wet", 0.0f, 100.0f, false });
        return s;
    }();
    return list;
}

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof (b), f, v);
    return b;
}

std::string timeText (double ms) { return ms < 1000.0 ? fmt (ms < 10.0 ? "%.1f ms" : "%.0f ms", ms) : fmt ("%.2f s", ms / 1000.0); }
std::string dbText (double lin) { return lin <= floorLevel * 1.0001 ? std::string ("-inf dB") : fmt ("%.1f dB", 20.0 * std::log10 (lin)); }
} // namespace

std::string paramName (int index) { return index >= 0 && index < paramCount ? specs()[(std::size_t) index].name : std::string(); }

std::string paramText (int index, double v)
{
    static const char* waves[waveCount] { "Sine", "Sine 4 Bit", "Sine 8 Bit", "Saw 3", "Saw 4", "Saw 6", "Saw 8", "Saw 16", "Saw 32",
                                          "Saw 64", "Saw D", "Square 3", "Square 4", "Square 6", "Square 8", "Square 16", "Square 32",
                                          "Square 64", "Square D", "Triangle", "Noise Loop", "Noise White", "User" };
    static const char* loops[] { "None", "Loop", "Beat", "Sync", "Trigger" };
    static const char* lfoWaves[] { "Sine", "Square", "Triangle", "Saw Up", "Saw Down", "S&H", "Noise" };
    static const char* lfoRanges[] { "Low", "High", "Sync" };
    static const char* types[] { "Low-pass", "High-pass", "Band-pass", "Notch", "Morph" };
    static const char* circuits[] { "Clean", "OSR", "MS2", "SMP", "PRD" };
    static const char* shapers[] { "Off", "Soft", "Hard", "Sine", "4 Bit" };
    static const char* syncs[] { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/64T", "1/128" };
    const auto i = (int) std::lround (v);
    const auto onOff = [&] { return std::string (v > 0.5 ? "On" : "Off"); };
    if (index < globalBase)
    {
        const auto r = index % op::stride;
        if (r >= op::envBase)
        {
            const auto e = r - op::envBase;
            if (e == env::attack || e == env::decay || e == env::release || e == env::loopTime)
                return timeText (v);
            if (e == env::initial || e == env::peak || e == env::sustain || e == env::end)
                return dbText (v);
            if (e == env::loopMode)
                return loops[std::clamp (i, 0, 4)];
            if (e == env::repeat)
                return fmt ("%.3g beats", repeatBeats (i));
            if (e == env::timeVel)
                return fmt ("%.0f %%", v);
            return fmt ("%.2f", v);
        }
        switch (r)
        {
            case op::on: case op::fixed: case op::retrig: case op::lfoOn: case op::pitchEnvOn: return onOff();
            case op::level: return dbText (v);
            case op::wave: return waves[std::clamp (i, 0, waveCount - 1)];
            case op::coarse: return i <= 0 ? std::string ("0.5") : std::to_string (i);
            case op::fine: return fmt ("%.0f", v);
            case op::fixedFreq: return fmt ("%.1f Hz", v);
            case op::fixedMulti: return fmt ("x%g", fixedHz (1.0, i));
            case op::phase: return fmt ("%.0f %%", v);
            default: return fmt ("%.0f %%", v);
        }
    }
    const auto envText = [&] (int base) -> std::optional<std::string> {
        if (index < base || index >= base + env::count)
            return std::nullopt;
        const auto e = index - base;
        const bool pitch = base == g::penv;
        if (e == env::attack || e == env::decay || e == env::release || e == env::loopTime)
            return timeText (v);
        if (e == env::initial || e == env::peak || e == env::sustain || e == env::end)
            return pitch ? fmt ("%+.1f st", v) : fmt ("%.0f %%", v * 100.0);
        if (e == env::loopMode)
            return std::string (loops[std::clamp (i, 0, 4)]);
        if (e == env::repeat)
            return fmt ("%.3g beats", repeatBeats (i));
        if (e == env::timeVel)
            return fmt ("%.0f %%", v);
        return fmt ("%.2f", v);
    };
    for (const auto base : { g::lfoEnv, g::penv, g::filterEnv })
        if (auto t = envText (base))
            return *t;
    switch (index)
    {
        case g::algorithm: return std::to_string (i + 1);
        case g::volume: return dbText (v);
        case g::pan: return std::abs (v) < 0.005 ? std::string ("C") : fmt (v < 0.0 ? "%.0fL" : "%.0fR", std::abs (v) * 50.0);
        case g::transpose: return fmt ("%+.0f st", v);
        case g::tone: case g::lfoAmount: return fmt ("%.0f %%", v * 100.0);
        case g::voices: return std::to_string (voiceLimit (v));
        case g::retrigger: case g::glideOn: case g::lfoOn: case g::lfoRetrig: case g::lfoFilter: case g::penvOn: case g::filterOn: return onOff();
        case g::glideTime: return timeText (v);
        case g::lfoType: return lfoWaves[std::clamp (i, 0, 6)];
        case g::lfoRange: return lfoRanges[std::clamp (i, 0, 2)];
        case g::lfoRate: return fmt ("%.2f Hz", lfoHz ((int) std::lround (0.0), v));
        case g::lfoSynced: return syncs[std::clamp (i, 0, 13)];
        case g::penvAmount: return fmt ("%.0f %%", v * 100.0);
        case g::filterType: return types[std::clamp (i, 0, 4)];
        case g::filterSlope: return v > 0.5 ? "24 dB" : "12 dB";
        case g::filterCircuit: case g::filterCircuitBp: return circuits[std::clamp (i, 0, 4)];
        case g::filterFreq: return v < 1000.0 ? fmt ("%.0f Hz", v) : fmt ("%.2f kHz", v / 1000.0);
        case g::filterRes: case g::filterMorph: return fmt ("%.2f", v);
        case g::filterDrive: case g::shaperDrive: return fmt ("%.1f dB", v);
        case g::filterEnvAmount: return fmt ("%+.0f st", v);
        case g::shaperType: return shapers[std::clamp (i, 0, 4)];
        default: return fmt ("%.0f %%", v);
    }
}
} // namespace fm

// --- the channel's values

std::vector<float> tetraDefaults()
{
    const auto& d = fm::defaults();
    return std::vector<float> (d.begin(), d.end());
}

std::vector<double> tetraState (const ChannelDef& channel)
{
    const auto& d = fm::defaults();
    std::vector<double> s (d.begin(), d.end());
    for (std::size_t i = 0; i < s.size() && i < channel.modelParams.size(); ++i)
        if (std::isfinite (channel.modelParams[i]))
            s[i] = channel.modelParams[i];
    return s;
}

const std::vector<EngineParam>& tetraEngineParams()
{
    static const auto list = [] {
        std::vector<EngineParam> v;
        const auto& s = fm::specs();
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            EngineParam p;
            p.index = (int) i;
            p.name = s[i].name;
            p.low = s[i].low;
            p.high = s[i].high;
            p.stepped = s[i].stepped;
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
std::optional<float> getTetra (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::tetra || p.index < 0 || p.index >= fm::paramCount)
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : (float) fm::defaults()[(std::size_t) p.index];
}

bool setTetra (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::tetra || p.index < 0 || p.index >= fm::paramCount)
        return false;
    if ((int) c.modelParams.size() < fm::stateSize)
    {
        const auto d = tetraDefaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) fm::stateSize);
        std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c.modelParams.begin() + (std::ptrdiff_t) have);
    }
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table tetraModelTable() { return { &tetraEngineParams(), &getTetra, &setTetra }; }
} // namespace modelparams

// --- the instrument

namespace
{
class TetraInstrument final : public Instrument
{
public:
    TetraInstrument()
    {
        const auto& d = fm::defaults();
        for (int i = 0; i < fm::stateSize; ++i)
            values[(std::size_t) i].store ((float) d[(std::size_t) i]);
        for (int k = 0; k < fm::opCount; ++k)
            buildTable (k);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
        lastTicks = -1.0;
        taken = 0;
        tablesTaken = 0;
    }

    // UI thread: the channel's values (the user waves are built here, never on the audio thread)
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        const auto& d = fm::defaults();
        bool changed = false, harmonicsChanged = false;
        for (int i = 0; i < fm::stateSize; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : (float) d[(std::size_t) i];
            if (! std::isfinite (v))
                v = (float) d[(std::size_t) i];
            if (values[(std::size_t) i].exchange (v) != v)
            {
                changed = true;
                harmonicsChanged = harmonicsChanged || i >= fm::harmonicsBase;
            }
        }
        if (harmonicsChanged)
        {
            for (int k = 0; k < fm::opCount; ++k)
                buildTable (k);
            tableVersion.fetch_add (1);
        }
        if (changed)
            version.fetch_add (1);
    }

    void setParameter (int index, float value) noexcept override
    {
        const auto* p = index >= 0 && index < fm::paramCount ? &tetraEngineParams()[(std::size_t) index] : nullptr;
        if (p == nullptr || ! std::isfinite (value))
            return;
        const auto raw = p->raw (value);
        values[(std::size_t) index].store (raw);
        engine.setParam (index, raw);
    }

    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void setChannelPitchNow (float knob, float rest) noexcept override { live.set (knob, rest); }
    void pitchBend (float semitones) noexcept override { engine.setPitchBend (std::isfinite (semitones) ? semitones : 0.0f); }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 127.0f), (nextCents - liveRest()) * 0.01f);
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    // FL's channel porta / Mono and slide notes (round 190ix): the voices take HostSlide::pendingGlide at note on and
    // glide to a slide note's pitch (`cents`: the note's cents, the fine tune and main pitch, as noteOn's)
    bool followsChannelPorta() const noexcept override { return true; }
    void slideTo (int key, float cents, float samples) noexcept override
    {
        take();
        engine.slideTo ((float) std::clamp (key, 0, 127) + (cents - liveRest()) * 0.01f, samples);
    }
    void allNotesOff() noexcept override { engine.allNotesOff(); }
    void panic() noexcept override { engine.reset(); }
    bool idle() const noexcept override { return engine.idle(); }

    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks;
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
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }

    void buildTable (int k) noexcept
    {
        std::array<float, fm::harmonics> parts {};
        for (int h = 0; h < fm::harmonics; ++h)
            parts[(std::size_t) h] = values[(std::size_t) (fm::harmonicsBase + k * fm::harmonics + h)].load();
        std::array<float, fm::waveTableSize + 1> t {};
        fm::buildUserWave (parts.data(), t.data());
        for (int i = 0; i <= fm::waveTableSize; ++i)
            tables[(std::size_t) k][(std::size_t) i].store (t[(std::size_t) i]);
    }

    void take() noexcept
    {
        if (const auto tv = tableVersion.load(); tv != tablesTaken)
        {
            tablesTaken = tv;
            std::array<float, fm::waveTableSize + 1> t {};
            for (int k = 0; k < fm::opCount; ++k)
            {
                for (int i = 0; i <= fm::waveTableSize; ++i)
                    t[(std::size_t) i] = tables[(std::size_t) k][(std::size_t) i].load (std::memory_order_relaxed);
                engine.setUserTable (k, t.data());
            }
        }
        const auto v = version.load();
        if (v == taken)
            return;
        taken = v;
        for (int i = 0; i < fm::stateSize; ++i)
            state[(std::size_t) i] = values[(std::size_t) i].load (std::memory_order_relaxed);
        engine.setValues (state.data());
    }

    static constexpr int chunk = 512;
    fm::Engine engine;
    std::array<std::atomic<float>, fm::stateSize> values {};
    std::array<std::array<std::atomic<float>, fm::waveTableSize + 1>, fm::opCount> tables {};
    std::array<double, fm::stateSize> state {};
    std::atomic<std::uint32_t> version { 1 }, tableVersion { 1 };
    std::uint32_t taken = 0, tablesTaken = 0;
    std::atomic<float> tempo { 120.0f }, pitchCents { 0.0f };
    LiveChannelPitch live;
    float nextCents = 0.0f;
    double rate = 48000.0, lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makeTetraInstrument() { return std::make_unique<TetraInstrument>(); }
} // namespace lynx
