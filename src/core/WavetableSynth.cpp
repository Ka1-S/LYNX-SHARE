// Helix, LYNX's own wavetable synth (round 190ja, core/WavetableSynth.h). Evidence tags: [DATA] read in Live's saved
// sets / presets (the parameter set, ranges and defaults), [MEASURED] black-box Live 12.4 renders of the 190ja render
// sets (docs/analise/REA_rodada190ja.md, the rows named), [MODEL] LYNX's own choice where no measurement decides.

#include "core/WavetableSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "core/HostSlide.h"
#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx
{
namespace wt
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int controlRate = 16; // samples per modulation step (envelopes, LFOs, the matrix)

// --- measured constants (REA_rodada190ja §2; each names the render rows that fix it)
struct Laws
{
    double centreGain = 0.70710678; // an oscillator at Gain 1, pan centre: each side −3 dB [MEASURED a0, a1: a sine peaks −9.03 dBFS at Volume −6 dB]
    double oscPitchSemis = 24.0;    // Osc Pitch ← 1 [MEASURED m1 osc1pitch: 0.25 × velocity 127 = +6 st]
    double pitchSemis = 48.0;       // Pitch ← 1 [MEASURED m1 pitch: 0.25 → +12 st; Live's MIDI tab shows Pitch ← PB 1/24 as "2"]
    double noteSpan = 48.0;         // the Note source: (key − 60) / 48 [MEASURED m2 pitch note0.25: 3 st per octave, C1 … C8]
    double keyTrackOctaves = 4.0;   // Filter Freq ← Note 1: one octave per octave [MEASURED m2 f1 note1: C1 … C6]
    double filterOctaves = 10.0;    // Filter Freq ← 1 (other sources): the knob's whole range, 20 Hz … 20.48 kHz [MEASURED n4, approx.]
    double fmIndex = 10.0;          // FM Amt 1: index 10 rad, a sine at the ratio moving the read phase [MEASURED x1: exact on Amt 0.25 / 0.5 / 1]
    double fmOctaves = 2.0;         // FM Pitch ±1: the modulator 2 octaves up / down [MEASURED x1: ratios 0.25 / 0.5 / 1 / 2 / 4]
    double syncSpan = 3.0;          // Sync: the synced wave at 1 + 3 × Sync times the note [MEASURED x2: 1.75 / 2.5 / 4, exact]
    double foldSpan = 7.0;          // Fold: the read phase folded at 1 + 7 × Fold times its speed, mirrored at half the cycle
                                    // [MEASURED x3 fold0.25 / 0.5 / 1: exact]
    double warpCurve = 32.0;        // Warp w: the phase x / (x + c (1 − x)), c = 1 / (1 + 32 w²) (w < 0: mirrored) [MEASURED x3: exact]
    double unisonCents = 50.0;      // Classic unison, Amount 1: the outer voices ± cents [MEASURED u*]
    double lfoRateOctaves = 11.55;  // an LFO rate row ← 1: × 2^this (the 0.01 … 30 Hz knob) [MODEL]
    double slopeCurve = 7.6;        // envelope slope ±1: the exponential's steepness [MEASURED e0 / e1 / e2: slopes ±0.5 / ±1]
    double subLevel = 1.0;          // the sub at Gain 1 = an oscillator at Gain 1 [MEASURED s1]
};
constexpr Laws laws {};

// Live's synced LFO rates, index → bars per cycle [MEASURED l2: index 6 = 1 bar, 11 = 5/16, 15 = 1/8 at 120 BPM; the rest
// follows the menu's order, MODEL]
constexpr double syncBars[22] { 8.0, 6.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.3125, 0.25, 0.1875, 1.0 / 6.0, 0.125,
                                1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125, 1.0 / 48.0, 0.015625 };

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float clampBi (float v) noexcept { return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v; }

} // namespace

double filterHz (double normalised) noexcept { return 20.0 * std::pow (2.0, 10.0 * std::clamp (normalised, 0.0, 1.0)); }
double filterNorm (double hz) noexcept { return std::log2 (std::clamp (hz, 20.0, 20480.0) / 20.0) / 10.0; }
double lfoSyncBeats (int index) noexcept { return 4.0 * syncBars[std::clamp (index, 0, 21)]; }
int polyVoiceCount (int index) noexcept { return std::clamp (index, 0, 6) + 2; } // [DATA: 0…6, default 6 = 8 voices]
double fmRatio (double fmPitch) noexcept { return std::pow (2.0, laws.fmOctaves * std::clamp (fmPitch, -1.0, 1.0)); }
double syncRatio (double sync) noexcept { return 1.0 + laws.syncSpan * std::clamp (sync, 0.0, 1.0); }

double unisonCents (int mode, double amount) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    switch (mode)
    {
        case unisonNone: return 0.0;
        case unisonPositionSpread: return laws.unisonCents * 0.1 * amount;
        default: return laws.unisonCents * amount;
    }
}

float segment (float x, float slope) noexcept
{
    x = std::clamp (x, 0.0f, 1.0f);
    if (std::abs (slope) < 1.0e-4f)
        return x;
    // slope > 0: fast at first (an exponential approach); < 0: slow at first [MEASURED e1*, e2*]
    const auto k = (float) laws.slopeCurve * std::abs (slope);
    const auto e = 1.0f - std::exp (-k);
    if (slope > 0.0f)
        return (1.0f - std::exp (-k * x)) / e;
    return 1.0f - (1.0f - std::exp (-k * (1.0f - x))) / e;
}

const std::array<double, stateSize>& defaults() noexcept
{
    // Live's default Wavetable [DATA: Core Library Defaults/Instruments/Wavetable.adv]
    static const auto d = [] {
        std::array<double, stateSize> s {};
        for (int k = 0; k < 2; ++k)
        {
            const auto b = oscBase (k);
            s[(std::size_t) (b + osc::on)] = k == 0 ? 1.0 : 0.0;
            s[(std::size_t) (b + osc::effectMode)] = k == 0 ? effectModern : effectNone;
            s[(std::size_t) (b + osc::gain)] = 1.0;
        }
        s[(std::size_t) subGain] = 0.5011875033;
        s[(std::size_t) subTranspose] = 1.0;
        for (int k = 0; k < 2; ++k)
        {
            const auto b = filterBase (k);
            s[(std::size_t) (b + flt::on)] = k == 0 ? 1.0 : 0.0;
            s[(std::size_t) (b + flt::type)] = k == 0 ? 0.0 : 1.0;
            s[(std::size_t) (b + flt::freq)] = k == 0 ? 20479.998 : 20.000309;
        }
        for (int k = 0; k < 3; ++k)
        {
            const auto b = envBase (k);
            s[(std::size_t) (b + env::attack)] = 0.001;
            s[(std::size_t) (b + env::decay)] = 0.6;
            s[(std::size_t) (b + env::release)] = 0.6;
            s[(std::size_t) (b + env::decaySlope)] = 0.5;
            s[(std::size_t) (b + env::releaseSlope)] = 0.5;
            s[(std::size_t) (b + env::sustain)] = k == 0 ? 0.5011875629 : 0.5;
            if (k > 0)
                s[(std::size_t) (b + env::peak)] = 1.0;
        }
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            s[(std::size_t) (b + lfo::retrigger)] = 1.0;
            s[(std::size_t) (b + lfo::amount)] = 1.0;
            s[(std::size_t) (b + lfo::rate)] = 1.0;
            s[(std::size_t) (b + lfo::syncedRate)] = 15.0;
        }
        s[(std::size_t) modAmount] = 1.0;
        s[(std::size_t) unisonVoices] = 3.0;
        s[(std::size_t) unisonAmount] = 0.3;
        s[(std::size_t) volume] = 0.3548134267;
        s[(std::size_t) monoPoly] = 1.0;
        s[(std::size_t) polyVoices] = 6.0;
        s[(std::size_t) tableBase] = 0.0;
        s[(std::size_t) tableBase + 1] = 0.0;
        s[(std::size_t) matrixIndex (tgt::oscPos, src::modWheel)] = 1.0;
        s[(std::size_t) matrixIndex (tgt::oscPos, src::slide)] = 0.33;
        s[(std::size_t) matrixIndex (tgt::oscFx1, src::pressure)] = 0.07;
        s[(std::size_t) matrixIndex (tgt::amp, src::velocity)] = 0.5;
        s[(std::size_t) matrixIndex (tgt::pitch, src::pitchBend)] = 1.0 / 24.0;
        s[(std::size_t) matrixIndex (tgt::pitch, src::notePitchBend)] = 1.0;
        return s;
    }();
    return d;
}

// =====================================================================================================================
namespace
{
struct EnvParams
{
    float attack = 0.001f, decay = 0.6f, release = 0.6f, aSlope = 0.0f, dSlope = 0.5f, rSlope = 0.5f;
    float initial = 0.0f, peak = 1.0f, sustain = 0.5f, final = 0.0f;
    int loop = 0; // 0 none, 1 trigger, 2 loop
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
    float pos = 0.0f;   // 0…1 through the stage
    float from = 0.0f;  // the level the stage started at
    float value = 0.0f;
    bool looping = false; // Loop mode: this release goes back to the attack while the key is held
    void start (const EnvParams& e, bool fromNow) noexcept
    {
        from = fromNow ? value : e.initial;
        if (! fromNow)
            value = e.initial;
        stage = attackStage;
        pos = 0.0f;
        looping = false;
    }
    void release() noexcept
    {
        if (stage == releaseStage)
            looping = false;
        if (stage == offStage || stage == releaseStage)
            return;
        from = value;
        stage = releaseStage;
        pos = 0.0f;
    }
};

// one step of `dt` seconds; `gate`: the key is held; `scale` multiplies every time
void stepEnv (Env& v, const EnvParams& e, float dt, float scale, bool gate, const float* times) noexcept
{
    auto advance = [&] (float t) {
        t = std::max (t * scale, 1.0e-4f);
        v.pos += dt / t;
    };
    for (int guard = 0; guard < 4; ++guard)
    {
        switch (v.stage)
        {
            case attackStage:
                advance (times[0]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.peak, v.from = e.peak, v.stage = decayStage, v.pos = 0.0f;
                    continue;
                }
                v.value = v.from + (e.peak - v.from) * segment (v.pos, e.aSlope);
                return;
            case decayStage:
                advance (times[1]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.sustain;
                    v.pos = 0.0f;
                    // Trigger: the sustain is skipped, the release follows at once (the key's release is ignored); Loop: the
                    // same while the key is held, then the attack again from the final value [MEASURED e3, e4]
                    if (e.loop == 1 || (e.loop == 2 && gate))
                    {
                        v.from = v.value, v.stage = releaseStage, v.looping = e.loop == 2;
                        return;
                    }
                    v.stage = sustainStage;
                    return;
                }
                v.value = v.from + (e.sustain - v.from) * segment (v.pos, e.dSlope);
                return;
            case sustainStage:
                v.value = e.sustain;
                return;
            case releaseStage:
                advance (times[2]);
                if (v.pos >= 1.0f)
                {
                    v.value = e.final;
                    if (v.looping && gate)
                    {
                        v.from = e.final, v.stage = attackStage, v.pos = 0.0f;
                        return;
                    }
                    v.stage = offStage;
                    return;
                }
                v.value = v.from + (e.final - v.from) * segment (v.pos, e.rSlope);
                return;
            case offStage: return;
        }
    }
}

// a TPT state-variable filter stage (two make 24 dB/oct); Live's filter family, laws as Tetra's (round 190hs) [MEASURED
// 190hs f0…f5: Operator's filter is the same circuit set; 190ja f* re-checked on Wavetable]
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    inline void process (float x, float k, float a1, float a2, float a3, float& lp, float& bp, float& hp) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

struct FilterCoefs
{
    bool on = false, steep = false;
    int type = 0, circuit = 0;
    float k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, morph = 0.0f, drive = 1.0f;
};

struct FilterState
{
    std::array<Svf, 2> l {}, r {};
};

inline float filterStage (Svf& s, float x, const FilterCoefs& c) noexcept
{
    float lp, bp, hp;
    s.process (x, c.k, c.a1, c.a2, c.a3, lp, bp, hp);
    switch (c.type)
    {
        case 0: return lp;
        case 1: return hp;
        case 2: return bp;
        case 3: return lp + hp;
        default:
        {
            const auto m = c.morph * 4.0f; // low → band → high → notch → low
            const float outs[5] { lp, bp, hp, lp + hp, lp };
            const auto i = std::min ((int) m, 3);
            const auto f = m - (float) i;
            return outs[i] + (outs[i + 1] - outs[i]) * f;
        }
    }
}

inline float filterSample (std::array<Svf, 2>& s, float x, const FilterCoefs& c) noexcept
{
    if (c.circuit > 0)
        x = 2.0f * std::tanh (x * c.drive * 0.5f); // (the driven circuits saturate softly; Clean ignores Drive [190hs c0]; the ceiling
                                                   // fitted on the 135 presets of 190ja W3: ±3 left LYNX +1.3 dB louder, ±1 −1.5 dB)
    auto y = filterStage (s[0], x, c);
    if (c.steep)
        y = filterStage (s[1], y, c);
    return y;
}

struct UnisonVoice
{
    std::uint32_t phase = 0, fmPhase = 0;
    double syncPhase = 0.0;  // the synced wave's phase (turns)
    float held = 0.0f;       // sync: the sample held one step for the polyBLEP correction
    float drift = 0.0f, driftTarget = 0.0f; // Shimmer / Noise: the voice's moving detune (cents)
    float randomCents = 0.0f;                // Random Note: the note's own detune
};

struct OscState
{
    std::array<UnisonVoice, maxUnison> u {};
    float pos = 0.0f, posStep = 0.0f;   // position, ramped over the control step
    float gainL = 0.0f, gainR = 0.0f;   // (per unison voice: below)
};

struct Voice
{
    bool active = false, gate = false;
    int key = 60;
    float velocity = 1.0f, pitch = 0.0f;       // the note's own offset (semitones, MPE per-note bend included)
    float slide = 0.0f, pressure = 0.0f;       // MPE
    float random = 0.0f;                       // the Random source (−1…1, drawn at the note's start)
    std::uint64_t age = 0;
    std::array<Env, 3> env {};
    std::array<double, 2> lfoPhase {};
    std::array<float, 2> lfoHeld {}, lfoAge {};
    float glideFrom = 0.0f, glidePos = 1.0f;   // semitones below / above the note at the start, 0…1 through the glide
    HostSlide host;                            // FL's channel porta and slide notes (rollup 0.1.30, core/HostSlide.h): cents
    std::array<OscState, 2> osc {};
    std::uint32_t subPhase = 0;
    FilterState f1, f2;
    std::array<FilterCoefs, 2> fc {};
    int counter = 0;
    std::uint32_t noise = 1;
    // the step's values
    float amp = 0.0f, ampStep = 0.0f;
    std::array<std::array<float, maxUnison>, 2> uGainL {}, uGainR {};
    std::array<std::array<std::uint32_t, maxUnison>, 2> uInc {};
    std::array<std::array<float, maxUnison>, 2> uPosOffset {};
    std::array<int, 2> mip {};
    std::array<float, 2> fx1 {}, fx2 {}, gain {};
    std::array<int, 2> mode {};
    float subGain = 0.0f, subTone = 0.0f;
    std::uint32_t subInc = 0;
};

constexpr int maxVoices = 32;
} // namespace

struct Engine::Impl
{
    double rate = 48000.0;
    std::array<double, stateSize> p {};
    std::array<const Table*, 2> tables { nullptr, nullptr };
    std::array<Voice, maxVoices> voices {};
    std::array<EnvParams, 3> envP {};
    std::uint64_t clock = 0;
    float pitchOffset = 0.0f, bend = 0.0f, bendSource = 0.0f, modWheel = 0.0f, chanPressure = 0.0f;
    float lastNote = -1.0f;
    std::uint32_t rng = 0x2545f491u;
    std::array<double, 2> lfoFree {};
    // the matrix's cells that are not 0 (rebuilt with the values)
    struct Cell
    {
        std::uint8_t target, source;
        float amount;
    };
    std::array<Cell, targetCount * sourceCount> cells {};
    int cellCount = 0;
    float bufL[controlRate] {}, bufR[controlRate] {};

    Impl()
    {
        const auto& d = defaults();
        std::copy (d.begin(), d.end(), p.begin());
        refresh();
    }

    int ip (int i) const noexcept { return (int) std::lround (p[(std::size_t) i]); }
    float fp (int i) const noexcept { return (float) p[(std::size_t) i]; }

    void refresh() noexcept
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto b = envBase (k);
            auto& e = envP[(std::size_t) k];
            e.attack = fp (b + env::attack), e.decay = fp (b + env::decay), e.release = fp (b + env::release);
            e.aSlope = fp (b + env::attackSlope), e.dSlope = fp (b + env::decaySlope), e.rSlope = fp (b + env::releaseSlope);
            e.sustain = fp (b + env::sustain);
            e.loop = std::clamp (ip (b + env::loopMode), 0, 2);
            if (k == 0)
                e.initial = 0.0f, e.peak = 1.0f, e.final = 0.0f;
            else
                e.initial = fp (b + env::initial), e.peak = fp (b + env::peak), e.final = fp (b + env::final);
        }
        cellCount = 0;
        for (int t = 0; t < targetCount; ++t)
            for (int s = 0; s < sourceCount; ++s)
                if (const auto a = (float) p[(std::size_t) matrixIndex (t, s)]; a != 0.0f && std::isfinite (a))
                    cells[(std::size_t) cellCount++] = { (std::uint8_t) t, (std::uint8_t) s, a };
    }

    void takeTables() noexcept
    {
        for (int k = 0; k < 2; ++k)
            if (const auto* t = tableIfBuilt ((int) std::lround (p[(std::size_t) tableBase + (std::size_t) k])))
                tables[(std::size_t) k] = t;
    }

    std::uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng;
    }
    float bipolarRandom() noexcept { return (float) ((double) (nextRandom() >> 8) / 8388608.0 - 1.0); }

    void startVoice (Voice& v, int key, float velocity, float pitch, float slide, float pressure, bool legato) noexcept
    {
        const auto glideTime = p[(std::size_t) glide];
        const auto target = (float) key + pitch;
        // Glide: Mono only, a straight slide in semitones over the glide time [MEASURED g1, g2: no glide in Poly]
        if (glideTime > 0.0005 && lastNote >= 0.0f && ip (monoPoly) == 0)
        {
            v.glideFrom = lastNote - target + (legato ? v.glideFrom * (1.0f - v.glidePos) : 0.0f);
            v.glidePos = 0.0f;
        }
        else
            v.glideFrom = 0.0f, v.glidePos = 1.0f;
        lastNote = target;
        v.key = key;
        v.pitch = pitch;
        v.velocity = velocity;
        v.slide = slide < 0.0f ? 0.0f : slide;
        v.pressure = pressure;
        v.gate = true;
        v.age = ++clock;
        // FL's channel porta (rollup 0.1.30, as round 190ix's Tetra): the rack starts the note `fromCents` from its own
        // pitch (the note's cents hold it) and the voice glides back to its own pitch (HostSlide::pendingGlide); a slide
        // note moves it later (slideTo). The channel's live pitch moves every voice through pitchOffset, so the slide does
        // not track it (NaN reference).
        v.host = {};
        v.host.anchor (NAN);
        if (legato && v.active)
            return; // (mono legato: the envelopes go on)
        v.active = true;
        v.random = bipolarRandom();
        v.noise = nextRandom() | 1u;
        for (int k = 0; k < 3; ++k)
            v.env[(std::size_t) k].start (envP[(std::size_t) k], false);
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            v.lfoPhase[(std::size_t) k] = p[(std::size_t) (b + lfo::retrigger)] > 0.5 ? p[(std::size_t) (b + lfo::phase)] / 360.0 : lfoFree[(std::size_t) k];
            v.lfoHeld[(std::size_t) k] = bipolarRandom();
            v.lfoAge[(std::size_t) k] = 0.0f;
        }
        const auto mode = ip (unisonMode);
        for (auto& o : v.osc)
        {
            for (int u = 0; u < maxUnison; ++u)
            {
                auto& uv = o.u[(std::size_t) u];
                // free phases: random per voice (Phase Sync and a lone voice start at 0) [MEASURED u4]
                uv.phase = mode == unisonPhaseSync || mode == unisonNone ? 0u : nextRandom();
                uv.fmPhase = 0u;
                uv.syncPhase = 0.0;
                uv.held = 0.0f;
                uv.drift = uv.driftTarget = 0.0f;
                uv.randomCents = bipolarRandom();
            }
            o.pos = -1.0f;
        }
        v.subPhase = 0u;
        v.f1 = {}, v.f2 = {};
        v.counter = 0;
        v.amp = 0.0f;
    }

    void note (int key, float velocity, float pitch, float slide, float pressure) noexcept
    {
        takeTables();
        if (velocity <= 0.0f)
        {
            for (auto& v : voices)
                if (v.active && v.gate && v.key == key)
                {
                    v.gate = false;
                    for (int k = 0; k < 3; ++k)
                        if (envP[(std::size_t) k].loop != 1) // (Trigger plays through)
                            v.env[(std::size_t) k].release();
                }
            return;
        }
        if (ip (monoPoly) == 0) // Mono: one voice, legato while a key is held
        {
            auto& v = voices[0];
            const bool legato = v.active && v.gate;
            for (std::size_t i = 1; i < voices.size(); ++i)
                voices[i].active = false;
            startVoice (v, key, velocity, pitch, slide, pressure, legato);
            return;
        }
        const auto cap = polyVoiceCount (ip (polyVoices));
        Voice* slot = nullptr;
        int playing = 0;
        for (auto& v : voices)
            playing += v.active ? 1 : 0;
        for (auto& v : voices)
            if (! v.active)
            {
                slot = &v;
                break;
            }
        if (slot == nullptr || playing >= cap)
        {
            // steal: the oldest released voice, else the oldest
            Voice* best = nullptr;
            for (auto& v : voices)
                if (v.active && (best == nullptr || (v.gate == best->gate ? v.age < best->age : ! v.gate)))
                    best = &v;
            slot = best;
        }
        if (slot != nullptr)
            startVoice (*slot, key, velocity, pitch, slide, pressure, false);
    }

    // --- one control step of a voice (n samples)
    void control (Voice& v, int n, double tempo, double songBeats) noexcept
    {
        const auto dt = (float) (n / rate);
        // sources
        float s[sourceCount] {};
        s[src::amp] = v.env[0].value;
        s[src::env2] = v.env[1].value;
        s[src::env3] = v.env[2].value;
        s[src::velocity] = 2.0f * v.velocity - 1.0f; // (bipolar around velocity 64 [MEASURED m1 pitch vel / m3 pos vel])
        s[src::note] = (float) ((v.key - 60) / laws.noteSpan);
        s[src::pitchBend] = bendSource;
        s[src::pressure] = std::max (v.pressure, chanPressure);
        s[src::modWheel] = modWheel;
        s[src::random] = v.random;
        s[src::notePitchBend] = v.pitch / 48.0f;
        s[src::slide] = v.slide;
        // the matrix: a row's sum of amount × source in the row's own unit (each row's law below); the internal sources
        // (envelopes, LFOs) scale with Global Mod Amount, the MIDI ones do not [MEASURED m8: Amount 50 % leaves Pitch ←
        // Velocity whole]. The level rows (Amp, the oscillators' and the sub's Gain) multiply instead: a unipolar view of
        // the source pulls the level down from full as it falls [MEASURED m4, m5, m6, m7, m10, d1]
        float mod[targetCount] {};
        float level[targetCount] {};
        std::fill (std::begin (level), std::end (level), 1.0f);
        auto internalScale = std::clamp (fp (modAmount), 0.0f, 2.0f);
        auto applyCells = [&] (bool lfoRowsOnly) {
            for (int i = 0; i < cellCount; ++i)
            {
                const auto& c = cells[(std::size_t) i];
                const bool lfoRow = c.target >= tgt::lfo1 && c.target < tgt::lfo2 + 3;
                if (lfoRow != lfoRowsOnly)
                    continue;
                // Pitch ← Pitch Bend is the channel's wheel in LYNX (its range is the channel's) [MODEL]
                if (c.target == tgt::pitch && c.source == src::pitchBend)
                    continue;
                const auto a = c.amount * (c.source <= src::lfo2 ? internalScale : 1.0f);
                const auto t = c.target;
                if (t == tgt::amp || t == tgt::subGain || t == tgt::oscGain || t == tgt::oscGain + tgt::oscStride)
                {
                    const bool bipolar = c.source == src::lfo1 || c.source == src::lfo2 || c.source == src::velocity || c.source == src::note
                                         || c.source == src::pitchBend || c.source == src::random || c.source == src::notePitchBend;
                    const auto u = bipolar ? 0.5f * (s[c.source] + 1.0f) : s[c.source];
                    level[t] *= std::max (0.0f, a >= 0.0f ? 1.0f - a * (1.0f - u) : 1.0f + a * u);
                    continue;
                }
                // Filter Freq ← Note is key tracking: 1 = an octave per octave [MEASURED m2 f1 note1]
                const bool keyTrack = c.source == src::note && (t == tgt::filterFreq || t == tgt::filterFreq + tgt::filterStride);
                mod[t] += a * s[c.source] * (keyTrack ? (float) (laws.keyTrackOctaves / laws.filterOctaves) : 1.0f);
            }
        };
        applyCells (true);
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            const auto row = k == 0 ? tgt::lfo1 : tgt::lfo2;
            const auto hz = p[(std::size_t) (b + lfo::sync)] > 0.5 ? tempo / 60.0 / lfoSyncBeats (ip (b + lfo::syncedRate))
                                                                   : std::clamp (p[(std::size_t) (b + lfo::rate)], 0.01, 30.0);
            const auto rateNow = hz * std::pow (2.0, laws.lfoRateOctaves * (double) mod[row + 2]);
            auto& ph = v.lfoPhase[(std::size_t) k];
            const auto before = ph;
            ph += rateNow * dt;
            if (std::floor (ph) != std::floor (before))
                v.lfoHeld[(std::size_t) k] = bipolarRandom();
            ph -= std::floor (ph);
            const auto x = (float) ph;
            float y;
            switch (std::clamp (ip (b + lfo::type), 0, 4))
            {
                case 0: y = std::sin (2.0f * (float) pi * x); break;
                case 1: y = x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f; break;
                case 2: // (falls from 0 at the start, jumps up at half the cycle [MEASURED l0 type2])
                {
                    const auto q = x + 0.5f;
                    y = 1.0f - 2.0f * (q - std::floor (q));
                    break;
                }
                case 3: y = x < 0.5f ? 1.0f : -1.0f; break;
                default: y = v.lfoHeld[(std::size_t) k]; break;
            }
            // Shaping ("Morph" in Live's LFO panel) bends the wave towards its peaks (> 0) or its middle (< 0) [MODEL, l3 approx.]
            const auto shaping = clampBi (fp (b + lfo::shaping) + 2.0f * mod[row + 1]);
            if (shaping != 0.0f)
                y = std::copysign (std::pow (std::abs (y), std::exp2 (-2.0f * shaping)), y);
            v.lfoAge[(std::size_t) k] += dt;
            const auto attack = fp (b + lfo::attack);
            const auto fade = attack > 0.0f ? clamp01 (v.lfoAge[(std::size_t) k] / attack) : 1.0f; // (a straight fade in [MEASURED l4])
            s[k == 0 ? src::lfo1 : src::lfo2] = y * clamp01 (fp (b + lfo::amount) + mod[row]) * fade;
        }
        applyCells (false);
        if (mod[tgt::globalMod] != 0.0f) // (Global Mod Amount modulated: the internal sources' rows again at the new scale)
        {
            const auto g = std::clamp (internalScale + 2.0f * mod[tgt::globalMod], 0.0f, 2.0f);
            if (internalScale > 0.0f)
                for (int i = 0; i < cellCount; ++i)
                    if (const auto& c = cells[(std::size_t) i]; c.source <= src::lfo2 && c.target != tgt::amp && c.target != tgt::subGain
                                                                && c.target != tgt::oscGain && c.target != tgt::oscGain + tgt::oscStride)
                        mod[c.target] += c.amount * s[c.source] * (g - internalScale);
        }
        // envelopes
        // Time: every envelope time × 10^Time [MEASURED e9: ×3.16 / ×0.32 at ±50 %]
        const auto timeAll = (float) std::pow (10.0, std::clamp ((double) fp (timeScale) + 2.0 * mod[tgt::time], -1.0, 1.0));
        for (int k = 0; k < 3; ++k)
        {
            const auto& e = envP[(std::size_t) k];
            const int row = k == 0 ? tgt::ampAttack : k == 1 ? tgt::env2 : tgt::env3;
            float times[3] { e.attack, e.decay, e.release };
            for (int j = 0; j < 3; ++j)
                if (mod[row + j] != 0.0f)
                {
                    // an Attack / Decay row moves the time knob, whose travel is the cube root of the time over 20 s; a
                    // Release row adds its share of the 20 s straight [MEASURED W4 t1 … t4: Amp Decay / Attack ← Velocity
                    // or Env 2 0.25 makes 0.1 s ≥ 1.4 s, at velocity 64 unchanged, −0.25 makes it 0; t5: Amp Release ←
                    // Note 0.25 makes 0.2 s last ≥ 2 s two octaves up]
                    if (j == 2)
                        times[j] = std::clamp (times[j] + 20.0f * mod[row + j], 0.0f, 20.0f);
                    else
                    {
                        const auto x = std::cbrt (std::clamp (times[j], 0.0f, 20.0f) / 20.0f) + mod[row + j];
                        times[j] = 20.0f * std::pow (std::clamp (x, 0.0f, 1.0f), 3.0f);
                    }
                }
            EnvParams ep = e;
            if (k == 0)
                ep.sustain = clamp01 (e.sustain + mod[tgt::ampSustain]);
            else
            {
                ep.initial = clamp01 (e.initial + mod[row + 3]);
                ep.peak = clamp01 (e.peak + mod[row + 4]);
                ep.sustain = clamp01 (e.sustain + mod[row + 5]);
                ep.final = clamp01 (e.final + mod[row + 6]);
            }
            stepEnv (v.env[(std::size_t) k], ep, dt, timeAll, v.gate, times);
        }
        const auto ampTarget = v.env[0].value * level[tgt::amp];
        v.ampStep = (ampTarget - v.amp) / (float) n;
        // pitch: the note, its own offset (through Pitch ← Note PB), glide, the wheel, the Pitch row
        if (v.glidePos < 1.0f)
        {
            // (the Glide row adds its share of the knob's 20 s, straight [MEASURED W4 t7: ← Velocity 0.5 at 127 = 10.1 s])
            const auto g = std::max (1.0e-4, std::clamp (p[(std::size_t) glide] + 20.0 * mod[tgt::glide], 0.0, 20.0));
            v.glidePos = std::min (1.0f, v.glidePos + (float) (dt / g));
        }
        const auto glideNow = v.glideFrom * (1.0f - v.glidePos);
        const auto hostSt = v.host.left > 0.0f || v.host.cents != 0.0f ? v.host.advance ((float) n) * 0.01f : 0.0f; // (FL's porta / slides)
        auto notePitch = (double) v.key + fp (transpose) + glideNow + hostSt + pitchOffset + bend + laws.pitchSemis * mod[tgt::pitch];
        if (p[(std::size_t) matrixIndex (tgt::pitch, src::notePitchBend)] == 0.0)
            notePitch += 0.0; // (the note's own pitch is a matrix source; without its cell it does not move the pitch, as in Live)
        // unison
        const auto uMode = ip (unisonMode);
        const auto uCount = uMode == unisonNone ? 1 : std::clamp (ip (unisonVoices), 2, maxUnison);
        const auto uAmount = clamp01 (fp (unisonAmount) + mod[tgt::unison]);
        const auto uCents = (float) unisonCents (uMode, uAmount);
        for (int k = 0; k < 2; ++k)
        {
            const auto b = oscBase (k);
            const auto row = k * tgt::oscStride;
            auto& o = v.osc[(std::size_t) k];
            v.gain[(std::size_t) k] = p[(std::size_t) (b + osc::on)] > 0.5 ? clamp01 (fp (b + osc::gain)) * level[row + tgt::oscGain] : 0.0f;
            const auto pos = clamp01 (fp (b + osc::position) + mod[row + tgt::oscPos]);
            if (o.pos < 0.0f)
                o.pos = pos;
            o.posStep = (pos - o.pos) / (float) n;
            v.mode[(std::size_t) k] = std::clamp (ip (b + osc::effectMode), 0, 3);
            v.fx1[(std::size_t) k] = clampBi (fp (b + osc::effect1) + mod[row + tgt::oscFx1]); // [MEASURED W4 p2: PW ← Velocity 0.5 at 127 is not silent]
            v.fx2[(std::size_t) k] = clamp01 (fp (b + osc::effect2) + mod[row + tgt::oscFx2]);
            const auto pan = clampBi (fp (b + osc::pan) + 2.0f * mod[row + tgt::oscPan]); // [MEASURED m11: Pan ← LFO 1 reaches a side at 0.5]
            const auto semis = notePitch + fp (b + osc::transpose) + fp (b + osc::detune) + laws.oscPitchSemis * mod[row + tgt::oscPitch];
            const auto hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
            // the mip: no partial above Nyquist at the highest frequency the effect reaches
            auto stretch = 1.0;
            const auto mode = v.mode[(std::size_t) k];
            if (mode == effectFm)
                stretch = 1.0 + laws.fmIndex * v.fx2[(std::size_t) k] * fmRatio (v.fx1[(std::size_t) k]);
            else if (mode == effectClassic) // (the squeezed cycle runs 1 / width faster; sync: the ratio)
                stretch = syncRatio (v.fx2[(std::size_t) k]) / std::max (1.0 - std::abs ((double) v.fx1[(std::size_t) k]), 0.02);
            else if (mode == effectModern)
            {
                // warp: the curve's steepest point is 1 + 32 w² times the speed, but only briefly: its geometric middle; fold
                // runs the phase 1 + 7 × Fold times faster
                const auto w = (double) v.fx1[(std::size_t) k];
                stretch = std::sqrt (1.0 + laws.warpCurve * w * w) * (v.fx2[(std::size_t) k] > 1.0e-4f ? 1.0 + laws.foldSpan * v.fx2[(std::size_t) k] : 1.0);
            }
            const auto top = hz * (uCount > 1 ? std::pow (2.0, uCents / 1200.0) : 1.0) * stretch;
            auto m = 0;
            while (m < mipCount - 1 && mipPartials (m) * top > rate * 0.5)
                ++m;
            v.mip[(std::size_t) k] = m;
            // each unison voice: detune, side, position offset
            for (int u = 0; u < uCount; ++u)
            {
                auto& uv = o.u[(std::size_t) u];
                const auto spread = uCount > 1 ? 2.0f * (float) u / (float) (uCount - 1) - 1.0f : 0.0f; // −1 … 1
                auto cents = 0.0f, side = 0.0f, posOff = 0.0f;
                // Live's unison: the voices alternate hard left / right (the two sides do not correlate), the detune spreads
                // evenly to ± 50 cents × Amount (Classic, Phase Sync); Shimmer wanders slowly over ± 180 cents × Amount, Noise
                // fast over ± 70; Position Spread detunes a little and spreads the position; Random Note gives every note its
                // own detunes [MEASURED u1 … u6: peaks of the spectrum, L / R correlation 0, level = one voice]
                switch (uMode)
                {
                    case unisonClassic:
                    case unisonPhaseSync: cents = uCents * spread; break;
                    case unisonShimmer:
                    case unisonNoise:
                    {
                        const auto depth = uCents * (uMode == unisonShimmer ? 3.6f : 1.4f);
                        const auto speed = uMode == unisonShimmer ? 4.0f : 150.0f;
                        if ((nextRandom() & (uMode == unisonShimmer ? 255u : 3u)) == 0u)
                            uv.driftTarget = bipolarRandom() * depth;
                        uv.drift += (uv.driftTarget - uv.drift) * std::min (1.0f, speed * dt);
                        cents = uv.drift;
                        break;
                    }
                    case unisonPositionSpread: cents = uCents * spread, posOff = 0.5f * uAmount * spread; break;
                    case unisonRandomNote: cents = uCents * 0.4f * uv.randomCents; break;
                    default: break;
                }
                if (uCount > 1)
                    side = (u & 1) ? 1.0f : -1.0f;
                v.uPosOffset[(std::size_t) k][(std::size_t) u] = posOff;
                const auto f = hz * std::pow (2.0, cents / 1200.0) / rate;
                v.uInc[(std::size_t) k][(std::size_t) u] = (std::uint32_t) (std::int64_t) (std::clamp (f, 0.0, 0.5) * 4294967296.0);
                // Live's pan: −3 dB each side in the middle; towards a side the near one rises to +3 dB as √|pan| and the far one
                // falls as √(1 − |pan|) [MEASURED a3, a4, m11]; unison voices sit hard on their side, 1 / √voices each
                const auto pp = clampBi (pan + side);
                const auto c = uCount > 1 ? 0.5f / std::sqrt ((float) uCount) : (float) laws.centreGain;
                const auto near = c * (1.0f + std::sqrt (std::abs (pp))), far = c * std::sqrt (1.0f - std::abs (pp));
                v.uGainL[(std::size_t) k][(std::size_t) u] = pp > 0.0f ? far : near;
                v.uGainR[(std::size_t) k][(std::size_t) u] = pp > 0.0f ? near : far;
            }
        }
        // the sub: an octave or two below oscillator 1's note [MEASURED s*]
        v.subGain = p[(std::size_t) subOn] > 0.5 ? clamp01 (fp (subGain)) * level[tgt::subGain] * (float) (laws.subLevel * laws.centreGain) : 0.0f;
        v.subTone = clamp01 (fp (subTone) + mod[tgt::subTone]);
        {
            const auto semis = notePitch - 12.0 * std::clamp (ip (subTranspose), 0, 2);
            const auto f = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0) / rate;
            v.subInc = (std::uint32_t) (std::int64_t) (std::clamp (f, 0.0, 0.5) * 4294967296.0);
        }
        // filters
        for (int k = 0; k < 2; ++k)
        {
            const auto b = filterBase (k);
            auto& c = v.fc[(std::size_t) k];
            c.on = p[(std::size_t) (b + flt::on)] > 0.5;
            if (! c.on)
                continue;
            const auto row = tgt::filterFreq + k * tgt::filterStride;
            c.type = std::clamp (ip (b + flt::type), 0, 4);
            c.steep = p[(std::size_t) (b + flt::slope)] > 0.5;
            c.circuit = c.type <= 1 ? std::clamp (ip (b + flt::circuitLpHp), 0, 4) : std::clamp (ip (b + flt::circuitBpNoMo), 0, 1);
            const auto hz = filterHz (filterNorm (p[(std::size_t) (b + flt::freq)]) + mod[row]);
            const auto res = std::clamp (fp (b + flt::res) + 1.25f * mod[row + 1], 0.0f, 1.25f);
            c.drive = std::pow (10.0f, std::clamp (fp (b + flt::drive) + 24.0f * mod[row + 2], 0.0f, 24.0f) / 20.0f);
            c.morph = clamp01 (fp (b + flt::morph) + mod[row + 3]);
            // resonance → damping [MEASURED 190hs f0x: 24 dB/oct peaks +6.9 / +22.9 dB at Res 0.5 / 1; above 1 = 1]
            static constexpr float resScale[5] { 1.0f, 0.85f, 1.08f, 0.97f, 0.88f };
            const auto r = std::clamp (res * resScale[c.circuit], 0.0f, 1.0f);
            const auto kmin = c.steep ? 0.267f : 0.067f;
            c.k = kmin + (2.0f - kmin) * std::pow (1.0f - r, 2.1f);
            const auto fhz = (float) std::min (c.circuit > 0 ? hz * 0.88 : hz, rate * 0.45);
            const auto g = std::tan ((float) pi * fhz / (float) rate);
            c.a1 = 1.0f / (1.0f + g * (g + c.k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
        }
        (void) songBeats;
    }

    // one oscillator's step into out L / R (adds)
    void oscStep (Voice& v, int k, int n, float* outL, float* outR) noexcept
    {
        const auto* tb = tables[(std::size_t) k];
        const auto g = v.gain[(std::size_t) k];
        auto& o = v.osc[(std::size_t) k];
        if (tb == nullptr || g <= 0.0f)
        {
            for (auto& u : o.u)
                u.phase += v.uInc[(std::size_t) k][0] * (std::uint32_t) n;
            o.pos += o.posStep * (float) n;
            return;
        }
        const auto m = v.mip[(std::size_t) k];
        const auto len = mipLength (m);
        const auto& frames = tb->frames[(std::size_t) m]; // (a mip's frames; equal ones shared, WavetableTables.cpp)
        const auto shift = len == 4096 ? 20 : len == 2048 ? 21 : len == 1024 ? 22 : 23; // (lengths 512 … 4096)
        const auto fracScale = 1.0f / (float) (1u << shift);
        const auto fracMask = (1u << shift) - 1u;
        const auto uMode = ip (unisonMode);
        const auto uCount = uMode == unisonNone ? 1 : std::clamp (ip (unisonVoices), 2, maxUnison);
        const auto mode = v.mode[(std::size_t) k];
        const auto fx1 = v.fx1[(std::size_t) k], fx2 = v.fx2[(std::size_t) k];
        // effect constants
        const auto fmIdx = (float) (laws.fmIndex * fx2 / (2.0 * pi));       // turns of phase per unit of the modulator
        const auto fmR = fmRatio (fx1);
        const auto width = std::max (1.0f - std::abs (fx1), 1.0e-4f);     // Classic PW: the part of the cycle the wave takes
        const bool pwLate = fx1 < 0.0f;                                     // (PW < 0: the gap comes first [MEASURED x2 pw-0.5])
        const auto sync = syncRatio (fx2);
        const bool syncing = mode == effectClassic && fx2 > 1.0e-4f;
        const auto warpC = fx1 == 0.0f ? 1.0f : std::pow (1.0f + (float) laws.warpCurve * fx1 * fx1, fx1 > 0.0f ? -1.0f : 1.0f);
        const auto foldG = 1.0f + (float) laws.foldSpan * fx2;
        const bool folding = mode == effectModern && fx2 > 1.0e-4f;
        for (int u = 0; u < uCount; ++u)
        {
            auto& uv = o.u[(std::size_t) u];
            const auto inc = v.uInc[(std::size_t) k][(std::size_t) u];
            const auto gl = v.uGainL[(std::size_t) k][(std::size_t) u] * g, gr = v.uGainR[(std::size_t) k][(std::size_t) u] * g;
            auto pos = o.pos + v.uPosOffset[(std::size_t) k][(std::size_t) u];
            const auto fmInc = (std::uint32_t) (std::int64_t) ((double) inc * fmR);
            const auto dt = (float) inc / 4294967296.0f;
            // the frame pair at the position (found once when the position holds still over the step)
            const float* a = nullptr;
            const float* b = nullptr;
            float w = 0.0f;
            const auto findFrames = [&] (float at) noexcept {
                const auto fpos = clamp01 (at) * (float) (frameCount - 1);
                const auto f0 = std::min ((int) fpos, frameCount - 2);
                w = fpos - (float) f0;
                a = frames[(std::size_t) f0];
                b = frames[(std::size_t) f0 + 1];
            };
            const bool moving = o.posStep != 0.0f;
            findFrames (pos);
            for (int i = 0; i < n; ++i)
            {
                if (moving)
                    findFrames (pos);
                auto read = [&] (std::uint32_t ph) noexcept {
                    const auto idx = ph >> shift;
                    const auto fr = (float) (ph & fracMask) * fracScale;
                    const auto va = a[idx] + (a[idx + 1] - a[idx]) * fr;
                    const auto vb = b[idx] + (b[idx + 1] - b[idx]) * fr;
                    return va + (vb - va) * w;
                };
                // Classic's PW: the whole cycle squeezed into `width`, the rest holds the cycle's end [MEASURED x2 pw0.5 / pw1 / pw-0.5]
                auto pwPhase = [&] (float px) noexcept {
                    const auto q = pwLate ? std::max (0.0f, px - (1.0f - width)) / width : std::min (px / width, 1.0f);
                    return (std::uint32_t) (std::int64_t) (std::min (q, 0.9999999f) * 4294967296.0f);
                };
                float y;
                switch (mode)
                {
                    case effectFm:
                    {
                        const auto mphase = (float) uv.fmPhase * (1.0f / 4294967296.0f);
                        auto off = fmIdx * std::sin (2.0f * (float) pi * mphase); // (turns; whole turns drop)
                        off -= std::floor (off);
                        y = read (uv.phase + (std::uint32_t) (std::int64_t) (off * 4294967296.0));
                        uv.fmPhase += fmInc;
                        break;
                    }
                    case effectClassic:
                    {
                        if (syncing)
                        {
                            // the synced wave runs `sync` times faster and restarts with the oscillator; the jump at the
                            // restart is smoothed by a two-sample polyBLEP (this sample now, the next one through `held`)
                            const auto x = (float) uv.phase * (1.0f / 4294967296.0f);
                            const auto sp = uv.syncPhase;
                            y = read (pwPhase ((float) (sp - std::floor (sp)))) + uv.held;
                            uv.held = 0.0f;
                            if (x + dt >= 1.0f) // the oscillator restarts before the next sample
                            {
                                const auto tau = std::clamp ((1.0f - x) / std::max (dt, 1.0e-9f), 0.0f, 1.0f);
                                const auto end = sp + sync * (1.0 - (double) x);
                                const auto jump = read (0u) - read (pwPhase ((float) (end - std::floor (end))));
                                y += jump * (1.0f - tau) * (1.0f - tau) * 0.5f;
                                uv.held = -jump * tau * tau * 0.5f;
                                uv.syncPhase = sync * std::max (0.0, (double) x + dt - 1.0);
                            }
                            else
                                uv.syncPhase = sp + sync * dt;
                        }
                        else
                            y = read (pwPhase ((float) uv.phase * (1.0f / 4294967296.0f)));
                        break;
                    }
                    case effectModern:
                    {
                        // Warp bends the read phase (a bilinear curve), Fold runs it 1 + 7 × Fold times faster, reflecting at
                        // the cycle's ends, mirrored about the cycle's middle [MEASURED x3]
                        auto px = (float) uv.phase * (1.0f / 4294967296.0f);
                        if (warpC != 1.0f)
                            px = px / (px + warpC * (1.0f - px));
                        if (folding)
                        {
                            // (below 1/7 the plain fold, which the mirrored one equals at 1/7; Live's small folds are close to
                            // it, 6 … 9 dB apart on W4 x3 fold 0.02 … 0.1; exact from 0.15 up)
                            auto f = foldG < 2.0f ? foldG * px : foldG * std::min (px, 1.0f - px);
                            f -= 2.0f * std::floor (f * 0.5f);
                            px = f <= 1.0f ? f : 2.0f - f;
                        }
                        y = read ((std::uint32_t) (std::int64_t) (std::min (px, 0.9999999f) * 4294967296.0f));
                        break;
                    }
                    default: y = read (uv.phase); break;
                }
                uv.phase += inc;
                outL[i] += y * gl;
                outR[i] += y * gr;
                pos += o.posStep;
            }
        }
        o.pos += o.posStep * (float) n;
    }

    void subStep (Voice& v, int n, float* outL, float* outR) noexcept
    {
        if (v.subGain <= 0.0f)
        {
            v.subPhase += v.subInc * (std::uint32_t) n;
            return;
        }
        // Tone: the sine crossfades into a 40 % pulse, all of it by Tone 0.4 [MEASURED s2, W4 s2: harmonics 2 / 3 / 4 at −10.5 /
        // −13.6 / −12.4 dB and no 5th from 0.4 up]; the pulse's edges are smoothed (polyBLEP)
        const auto mix = std::min (1.0f, v.subTone / 0.4f);
        const auto dt = (float) v.subInc * (1.0f / 4294967296.0f);
        constexpr float duty = 0.4f;
        auto blep = [dt] (float t) noexcept {
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
        };
        for (int i = 0; i < n; ++i)
        {
            const auto ph = (float) v.subPhase * (1.0f / 4294967296.0f);
            auto y = std::sin (2.0f * (float) pi * ph);
            if (mix > 0.0f)
            {
                auto pulse = (ph < duty ? 1.0f : -1.0f) - (2.0f * duty - 1.0f);
                pulse += blep (ph);
                auto q = ph - duty;
                q -= std::floor (q);
                pulse -= blep (q);
                y += (pulse * 0.92f - y) * mix;
            }
            y *= v.subGain;
            outL[i] += y;
            outR[i] += y;
            v.subPhase += v.subInc;
        }
    }

    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept
    {
        for (int k = 0; k < 2; ++k)
        {
            const auto b = lfoBase (k);
            const auto hz = p[(std::size_t) (b + lfo::sync)] > 0.5 ? tempo / 60.0 / lfoSyncBeats (ip (b + lfo::syncedRate))
                                                                   : std::clamp (p[(std::size_t) (b + lfo::rate)], 0.01, 30.0);
            lfoFree[(std::size_t) k] += hz * n / rate;
            lfoFree[(std::size_t) k] -= std::floor (lfoFree[(std::size_t) k]);
        }
        const auto route = std::clamp (ip (routing), 0, 2);
        const auto vol = (float) std::clamp (p[(std::size_t) volume], 0.0, 1.0); // (linear [MEASURED a1])
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
                float o1L[controlRate] {}, o1R[controlRate] {}, o2L[controlRate] {}, o2R[controlRate] {};
                oscStep (v, 0, c, o1L, o1R);
                if (route == routeSplit)
                    oscStep (v, 1, c, o2L, o2R);
                else
                    oscStep (v, 1, c, o1L, o1R);
                subStep (v, c, o1L, o1R);
                const auto& c1 = v.fc[0];
                const auto& c2 = v.fc[1];
                for (int i = 0; i < c; ++i)
                {
                    float l, r;
                    if (route == routeSerial)
                    {
                        l = o1L[i], r = o1R[i];
                        if (c1.on)
                            l = filterSample (v.f1.l, l, c1), r = filterSample (v.f1.r, r, c1);
                        if (c2.on)
                            l = filterSample (v.f2.l, l, c2), r = filterSample (v.f2.r, r, c2);
                    }
                    else if (route == routeParallel)
                    {
                        const auto xl = o1L[i], xr = o1R[i];
                        if (c1.on && c2.on)
                            l = filterSample (v.f1.l, xl, c1) + filterSample (v.f2.l, xl, c2), r = filterSample (v.f1.r, xr, c1) + filterSample (v.f2.r, xr, c2);
                        else if (c1.on)
                            l = filterSample (v.f1.l, xl, c1), r = filterSample (v.f1.r, xr, c1);
                        else if (c2.on)
                            l = filterSample (v.f2.l, xl, c2), r = filterSample (v.f2.r, xr, c2);
                        else
                            l = xl, r = xr;
                    }
                    else
                    {
                        // Split: oscillator 1 and the sub through filter 1, oscillator 2 through filter 2 [MEASURED r2*]
                        l = c1.on ? filterSample (v.f1.l, o1L[i], c1) : o1L[i];
                        r = c1.on ? filterSample (v.f1.r, o1R[i], c1) : o1R[i];
                        l += c2.on ? filterSample (v.f2.l, o2L[i], c2) : o2L[i];
                        r += c2.on ? filterSample (v.f2.r, o2R[i], c2) : o2R[i];
                    }
                    v.amp += v.ampStep;
                    left[pos + i] += l * v.amp * vol;
                    right[pos + i] += r * v.amp * vol;
                }
                pos += c;
                v.counter -= c;
            }
            if (v.env[0].stage == offStage)
                v.active = false;
        }
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    impl->rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

double Engine::sampleRate() const noexcept { return impl->rate; }

void Engine::setState (const double* values)
{
    for (int k = 0; k < 2; ++k)
        (void) table ((int) std::lround (values[tableBase + k])); // (builds it here, off the audio thread)
    setValues (values);
}

void Engine::setValues (const double* values) noexcept
{
    for (int i = 0; i < stateSize; ++i)
        impl->p[(std::size_t) i] = std::isfinite (values[i]) ? values[i] : defaults()[(std::size_t) i];
    impl->refresh();
    impl->takeTables();
}

void Engine::setParam (int index, double value) noexcept
{
    if (index < 0 || index >= paramCount || ! std::isfinite (value))
        return;
    impl->p[(std::size_t) index] = value;
    if (index >= ampEnv && index < lfoBase (0))
        impl->refresh();
}

double Engine::param (int index) const noexcept { return index >= 0 && index < stateSize ? impl->p[(std::size_t) index] : 0.0; }

void Engine::note (int key, float velocity, float pitch, float slide, float pressure) noexcept
{
    impl->note (key, std::clamp (velocity, 0.0f, 1.0f), std::isfinite (pitch) ? pitch : 0.0f, slide, std::clamp (pressure, 0.0f, 1.0f));
}

void Engine::slideTo (float semitones, float samples) noexcept
{
    if (! std::isfinite (semitones) || ! std::isfinite (samples))
        return;
    for (auto& v : impl->voices)
        if (v.active && v.gate)
            v.host.start ((semitones - (float) v.key) * 100.0f, std::max (samples, 1.0f));
}

void Engine::notePitch (int key, float semitones) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.pitch = std::isfinite (semitones) ? semitones : 0.0f;
}
void Engine::noteSlide (int key, float slide) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.slide = clamp01 (slide);
}
void Engine::notePressure (int key, float pressure) noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate && v.key == key)
            v.pressure = clamp01 (pressure);
}
void Engine::setPitchOffset (float semitones) noexcept { impl->pitchOffset = std::isfinite (semitones) ? semitones : 0.0f; }
void Engine::setPitchBend (float semitones, float range) noexcept
{
    impl->bend = std::isfinite (semitones) ? semitones : 0.0f;
    impl->bendSource = clampBi (range > 0.0f ? impl->bend / range : 0.0f);
}
void Engine::setModWheel (float value) noexcept { impl->modWheel = clamp01 (value); }
void Engine::setPressure (float value) noexcept { impl->chanPressure = clamp01 (value); }

void Engine::allNotesOff() noexcept
{
    for (auto& v : impl->voices)
        if (v.active && v.gate)
            impl->note (v.key, 0.0f, 0.0f, -1.0f, 0.0f);
}

void Engine::reset() noexcept
{
    for (auto& v : impl->voices)
        v.active = false;
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
    for (std::size_t i = 0; i < s.size() && i < state.size(); ++i)
        s[i] = state[i];
    e.setState (s.data());
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
} // namespace wt
} // namespace lynx

// =====================================================================================================================
// parameters, texts, the channel's values and the instrument
namespace lynx
{
namespace wt
{
namespace
{
struct Spec
{
    std::string name;
    float low, high;
    bool stepped;
};

void envSpecs (std::vector<Spec>& s, const std::string& who, bool values)
{
    s.push_back ({ who + " Attack", 0.0f, 20.0f, false });
    s.push_back ({ who + " Decay", 0.0015f, 20.0f, false });
    s.push_back ({ who + " Release", 0.0015f, 20.0f, false });
    s.push_back ({ who + " Attack Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Decay Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Release Slope", -1.0f, 1.0f, false });
    s.push_back ({ who + " Sustain", 0.0f, 1.0f, false });
    s.push_back ({ who + " Loop Mode", 0.0f, 2.0f, true });
    if (values)
    {
        s.push_back ({ who + " Initial", 0.0f, 1.0f, false });
        s.push_back ({ who + " Peak", 0.0f, 1.0f, false });
        s.push_back ({ who + " Final", 0.0f, 1.0f, false });
    }
}

const std::vector<Spec>& specs()
{
    static const auto list = [] {
        std::vector<Spec> s;
        for (int k = 0; k < 2; ++k)
        {
            const auto o = "Osc " + std::to_string (k + 1);
            s.push_back ({ o + " On", 0.0f, 1.0f, true });
            s.push_back ({ o + " Transpose", -24.0f, 24.0f, true });
            s.push_back ({ o + " Detune", -0.5f, 0.5f, false });
            s.push_back ({ o + " Pos", 0.0f, 1.0f, false });
            s.push_back ({ o + " Effect Mode", 0.0f, 3.0f, true });
            s.push_back ({ o + " Effect 1", -1.0f, 1.0f, false });
            s.push_back ({ o + " Effect 2", 0.0f, 1.0f, false });
            s.push_back ({ o + " Pan", -1.0f, 1.0f, false });
            s.push_back ({ o + " Gain", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Sub On", 0.0f, 1.0f, true });
        s.push_back ({ "Sub Tone", 0.0f, 1.0f, false });
        s.push_back ({ "Sub Gain", 0.0f, 1.0f, false });
        s.push_back ({ "Sub Octave", 0.0f, 2.0f, true });
        for (int k = 0; k < 2; ++k)
        {
            const auto f = "Filter " + std::to_string (k + 1);
            s.push_back ({ f + " On", 0.0f, 1.0f, true });
            s.push_back ({ f + " Type", 0.0f, 4.0f, true });
            s.push_back ({ f + " Circuit", 0.0f, 4.0f, true });
            s.push_back ({ f + " Circuit BP", 0.0f, 1.0f, true });
            s.push_back ({ f + " Slope", 0.0f, 1.0f, true });
            s.push_back ({ f + " Freq", 20.0f, 20480.0f, false });
            s.push_back ({ f + " Res", 0.0f, 1.25f, false });
            s.push_back ({ f + " Drive", 0.0f, 24.0f, false });
            s.push_back ({ f + " Morph", 0.0f, 1.0f, false });
        }
        s.push_back ({ "Filter Routing", 0.0f, 2.0f, true });
        envSpecs (s, "Amp", false);
        envSpecs (s, "Env 2", true);
        envSpecs (s, "Env 3", true);
        for (int k = 0; k < 2; ++k)
        {
            const auto l = "LFO " + std::to_string (k + 1);
            s.push_back ({ l + " Retrigger", 0.0f, 1.0f, true });
            s.push_back ({ l + " Shape", 0.0f, 4.0f, true });
            s.push_back ({ l + " Amount", 0.0f, 1.0f, false });
            s.push_back ({ l + " Shaping", -1.0f, 1.0f, false });
            s.push_back ({ l + " Phase", 0.0f, 360.0f, false });
            s.push_back ({ l + " Sync", 0.0f, 1.0f, true });
            s.push_back ({ l + " Rate", 0.01f, 30.0f, false });
            s.push_back ({ l + " Sync Rate", 0.0f, 21.0f, true });
            s.push_back ({ l + " Attack", 0.0f, 20.0f, false });
        }
        s.push_back ({ "Time", -1.0f, 1.0f, false });
        s.push_back ({ "Global Mod Amount", 0.0f, 2.0f, false });
        s.push_back ({ "Unison Mode", 0.0f, 6.0f, true });
        s.push_back ({ "Unison Voices", 2.0f, 8.0f, true });
        s.push_back ({ "Unison Amount", 0.0f, 1.0f, false });
        s.push_back ({ "Transpose", -48.0f, 48.0f, true });
        s.push_back ({ "Glide", 0.0f, 20.0f, false });
        s.push_back ({ "Volume", 0.0f, 1.0f, false });
        s.push_back ({ "Poly", 0.0f, 1.0f, true });
        s.push_back ({ "Voices", 0.0f, 6.0f, true });
        s.push_back ({ "HiQ", 0.0f, 1.0f, true });
        return s;
    }();
    return list;
}

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}
std::string timeText (double s) { return s < 1.0 ? fmt ("%.1f ms", s * 1000.0) : fmt ("%.2f s", s); }
std::string dbText (double lin) { return lin <= 1.0e-5 ? std::string ("-inf dB") : fmt ("%.1f dB", 20.0 * std::log10 (lin)); }
std::string pctText (double v) { return fmt ("%.0f %%", v * 100.0); }
} // namespace

std::string paramName (int index) { return index >= 0 && index < paramCount ? specs()[(std::size_t) index].name : std::string(); }

std::string sourceName (int source)
{
    static const char* names[sourceCount] { "Amp", "Env 2", "Env 3", "LFO 1", "LFO 2", "Velocity", "Note", "Pitch Bend", "Pressure",
                                            "Mod Wheel", "Random", "Note PB", "Slide" };
    return source >= 0 && source < sourceCount ? names[source] : std::string();
}

std::string targetName (int target, int effectMode1, int effectMode2)
{
    if (target < 0 || target >= targetCount)
        return {};
    if (target < tgt::subTone)
    {
        const auto k = target / tgt::oscStride, r = target % tgt::oscStride;
        const auto o = "Osc " + std::to_string (k + 1) + " ";
        const auto mode = k == 0 ? effectMode1 : effectMode2;
        static const char* fx[4][2] { { "FX 1", "FX 2" }, { "FM Pitch", "FM Amt" }, { "PW", "Sync" }, { "Warp", "Fold" } };
        static const char* rows[] { "Pitch", "Pos", "", "", "Pan", "Gain" };
        if (r == tgt::oscFx1 || r == tgt::oscFx2)
            return o + fx[std::clamp (mode, 0, 3)][r - tgt::oscFx1];
        return o + rows[r];
    }
    static const char* rest[] { "Sub Tone", "Sub Gain", "Filter 1 Freq", "Filter 1 Res", "Filter 1 Drive", "Filter 1 Morph", "Filter 2 Freq",
                                "Filter 2 Res", "Filter 2 Drive", "Filter 2 Morph", "Amp Attack", "Amp Decay", "Amp Release", "Amp Sustain",
                                "Env 2 Attack", "Env 2 Decay", "Env 2 Release", "Env 2 Initial", "Env 2 Peak", "Env 2 Sustain", "Env 2 Final",
                                "Env 3 Attack", "Env 3 Decay", "Env 3 Release", "Env 3 Initial", "Env 3 Peak", "Env 3 Sustain", "Env 3 Final",
                                "LFO 1 Amount", "LFO 1 Shaping", "LFO 1 Rate", "LFO 2 Amount", "LFO 2 Shaping", "LFO 2 Rate", "Time",
                                "Global Mod Amount", "Unison Amount", "Amp", "Pitch", "Glide" };
    return rest[target - tgt::subTone];
}

std::string paramText (int index, double v)
{
    static const char* modes[] { "None", "FM", "Classic", "Modern" };
    static const char* types[] { "Lowpass", "Highpass", "Bandpass", "Notch", "Morph" };
    static const char* circuits[] { "Clean", "OSR", "MS2", "SMP", "PRD" };
    static const char* loops[] { "None", "Trigger", "Loop" };
    static const char* shapes[] { "Sine", "Triangle", "Saw", "Rectangle", "Random" };
    static const char* routes[] { "Serial", "Parallel", "Split" };
    static const char* unison[] { "None", "Classic", "Shimmer", "Noise", "Phase Sync", "Position Spread", "Random Note" };
    static const char* syncs[] { "8 bars", "6 bars", "4 bars", "3 bars", "2 bars", "6/4", "1 bar", "3/4", "1/2", "3/8", "1/3", "1/4", "3/16",
                                 "1/6", "1/8", "3/32", "1/12", "1/16", "3/64", "1/24", "1/32", "1/64" };
    const auto i = (int) std::lround (v);
    const auto onOff = [&] { return std::string (v > 0.5 ? "On" : "Off"); };
    if (index < subOn)
    {
        switch (index % osc::stride)
        {
            case osc::on: return onOff();
            case osc::transpose: return fmt ("%+.0f st", v);
            case osc::detune: return fmt ("%+.0f ct", v * 100.0);
            case osc::position: return pctText (v);
            case osc::effectMode: return modes[std::clamp (i, 0, 3)];
            case osc::effect1: case osc::effect2: return pctText (v);
            case osc::pan: return std::abs (v) < 0.005 ? std::string ("C") : fmt (v < 0.0 ? "%.0fL" : "%.0fR", std::abs (v) * 50.0);
            default: return dbText (v);
        }
    }
    if (index == subOn)
        return onOff();
    if (index == subTone)
        return pctText (v);
    if (index == subGain)
        return dbText (v);
    if (index == subTranspose)
        return fmt ("%.0f oct", -v);
    if (index < routing)
    {
        switch ((index - filterBase (0)) % flt::stride)
        {
            case flt::on: return onOff();
            case flt::type: return types[std::clamp (i, 0, 4)];
            case flt::circuitLpHp: return circuits[std::clamp (i, 0, 4)];
            case flt::circuitBpNoMo: return circuits[std::clamp (i, 0, 1)];
            case flt::slope: return v > 0.5 ? "24 dB" : "12 dB";
            case flt::freq: return v < 1000.0 ? fmt ("%.0f Hz", v) : fmt ("%.2f kHz", v / 1000.0);
            case flt::res: return pctText (v);
            case flt::drive: return fmt ("%.1f dB", v);
            default: return pctText (v);
        }
    }
    if (index == routing)
        return routes[std::clamp (i, 0, 2)];
    if (index < lfoBase (0))
    {
        const auto k = index < envBase (1) ? 0 : index < envBase (2) ? 1 : 2;
        const auto e = index - envBase (k);
        if (e == env::attack || e == env::decay || e == env::release)
            return timeText (v);
        if (e == env::loopMode)
            return loops[std::clamp (i, 0, 2)];
        if (e == env::sustain && k == 0)
            return dbText (v);
        if (e >= env::sustain)
            return pctText (v);
        return pctText (v);
    }
    if (index < timeScale)
    {
        const auto e = (index - lfoBase (0)) % lfo::stride;
        switch (e)
        {
            case lfo::retrigger: case lfo::sync: return onOff();
            case lfo::type: return shapes[std::clamp (i, 0, 4)];
            case lfo::phase: return fmt ("%.0f deg", v);
            case lfo::rate: return fmt (v < 10.0 ? "%.2f Hz" : "%.1f Hz", v);
            case lfo::syncedRate: return syncs[std::clamp (i, 0, 21)];
            case lfo::attack: return timeText (v);
            default: return pctText (v);
        }
    }
    switch (index)
    {
        case unisonMode: return unison[std::clamp (i, 0, 6)];
        case unisonVoices: return std::to_string (std::clamp (i, 2, 8));
        case transpose: return fmt ("%+.0f st", v);
        case glide: return timeText (v);
        case volume: return dbText (v);
        case monoPoly: return v > 0.5 ? "Poly" : "Mono";
        case polyVoices: return std::to_string (polyVoiceCount (i));
        case hiQ: return onOff();
        default: return pctText (v);
    }
}
} // namespace wt

// --- the channel's values

std::vector<float> helixDefaults()
{
    const auto& d = wt::defaults();
    return std::vector<float> (d.begin(), d.end());
}

std::vector<double> helixState (const ChannelDef& channel)
{
    const auto& d = wt::defaults();
    std::vector<double> s (d.begin(), d.end());
    for (std::size_t i = 0; i < s.size() && i < channel.modelParams.size(); ++i)
        if (std::isfinite (channel.modelParams[i]))
            s[i] = channel.modelParams[i];
    return s;
}

const std::vector<EngineParam>& helixEngineParams()
{
    static const auto list = [] {
        std::vector<EngineParam> v;
        const auto& s = wt::specs();
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
std::optional<float> getHelix (const ChannelDef& c, const EngineParam& p)
{
    if (c.instrument != InstrumentKind::helix || p.index < 0 || p.index >= wt::paramCount)
        return std::nullopt;
    return p.index < (int) c.modelParams.size() ? c.modelParams[(std::size_t) p.index] : (float) wt::defaults()[(std::size_t) p.index];
}

bool setHelix (ChannelDef& c, const EngineParam& p, float raw)
{
    if (c.instrument != InstrumentKind::helix || p.index < 0 || p.index >= wt::paramCount)
        return false;
    if ((int) c.modelParams.size() < wt::stateSize)
    {
        const auto d = helixDefaults();
        const auto have = c.modelParams.size();
        c.modelParams.resize ((std::size_t) wt::stateSize);
        std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), c.modelParams.begin() + (std::ptrdiff_t) have);
    }
    c.modelParams[(std::size_t) p.index] = raw;
    return true;
}
} // namespace

Table helixModelTable() { return { &helixEngineParams(), &getHelix, &setHelix }; }
} // namespace modelparams

// --- the instrument

namespace
{
class HelixInstrument final : public Instrument
{
public:
    HelixInstrument()
    {
        const auto& d = wt::defaults();
        for (int i = 0; i < wt::stateSize; ++i)
            values[(std::size_t) i].store ((float) d[(std::size_t) i]);
        (void) wt::table (0);
    }

    void prepare (double sampleRate) noexcept override
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
        lastTicks = -1.0;
        taken = 0;
    }

    // UI thread: the channel's values (the tables they name are built here, never on the audio thread)
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        tempo.store (bpm > 0.0 ? (float) bpm : 120.0f);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        bendRange.store ((float) std::max (1, channel.misc.pitchRange));
        const auto& d = wt::defaults();
        bool changed = false;
        for (int i = 0; i < wt::stateSize; ++i)
        {
            auto v = i < (int) channel.modelParams.size() ? channel.modelParams[(std::size_t) i] : (float) d[(std::size_t) i];
            if (! std::isfinite (v))
                v = (float) d[(std::size_t) i];
            if (i == wt::tableBase || i == wt::tableBase + 1)
                (void) wt::table ((int) std::lround (v));
            if (values[(std::size_t) i].exchange (v) != v)
                changed = true;
        }
        if (changed)
            version.fetch_add (1);
    }

    void setParameter (int index, float value) noexcept override
    {
        const auto* p = index >= 0 && index < wt::paramCount ? &helixEngineParams()[(std::size_t) index] : nullptr;
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
        if (size >= 2 && (bytes[0] & 0xf0) == 0xd0) // channel pressure
            engine.setPressure ((float) bytes[1] / 127.0f);
        else if (size >= 3 && (bytes[0] & 0xf0) == 0xb0 && bytes[1] == 1)
            engine.setModWheel ((float) bytes[2] / 127.0f);
    }

    // MPE (issue #108): a sounding note's own pitch, timbre (Live's Slide) and pressure, as matrix sources
    void voiceProperty (int key, int prop, float value) noexcept override
    {
        if (prop == 1)
            engine.notePitch (key, value * 0.01f);
        else if (prop == 3)
            engine.noteSlide (key, value);
        else if (prop == 4)
            engine.notePressure (key, pressureOf (value));
    }
    void noteExpression (int key, int prop, float value, float) noexcept override
    {
        // (the channel's tuning is the engine's own pitch offset: only the note's cents move the voice)
        voiceProperty (key, prop, value);
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        engine.note (std::clamp (key, 0, 127), std::max (std::clamp (velocity, 0.0f, 1.0f), 1.0f / 127.0f), (nextCents - liveRest()) * 0.01f,
                     slideOf (nextModX), pressureOf (nextModY));
        nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    }

    void noteOff (int key) noexcept override { engine.note (std::clamp (key, 0, 127), 0.0f); }
    // FL's channel porta / Mono and slide notes (rollup 0.1.30, as round 190ix's Tetra): the voices take
    // HostSlide::pendingGlide at note on and glide to a slide note's pitch (`cents`: the note's cents, as noteOn's)
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
    // Mod Y → pressure: 0.5 (FL's neutral, no pressure) … 1 [190hy: Mod Y = 0.5 + pressure / 2]
    static float pressureOf (float modY) noexcept { return std::clamp (2.0f * (modY - 0.5f), 0.0f, 1.0f); }
    // Mod X → Live's Slide (CC 74 / 127); FL's neutral 0.5 is "no slide data" [MEASURED d0: Live's notes without MPE
    // data play the default preset's Pos ← Slide as Slide 0]
    static float slideOf (float modX) noexcept { return modX == 0.5f ? -1.0f : std::clamp (modX, 0.0f, 1.0f); }
    float liveRest() const noexcept { return std::isnan (live.rest) ? 0.0f : live.rest; }

    void take() noexcept
    {
        const auto v = version.load();
        if (v == taken)
            return;
        taken = v;
        for (int i = 0; i < wt::stateSize; ++i)
            state[(std::size_t) i] = values[(std::size_t) i].load (std::memory_order_relaxed);
        engine.setValues (state.data());
    }

    static constexpr int chunk = 512;
    wt::Engine engine;
    std::array<std::atomic<float>, wt::stateSize> values {};
    std::array<double, wt::stateSize> state {};
    std::atomic<std::uint32_t> version { 1 };
    std::uint32_t taken = 0;
    std::atomic<float> tempo { 120.0f }, pitchCents { 0.0f }, bendRange { 2.0f };
    LiveChannelPitch live;
    float nextCents = 0.0f, nextModX = 0.5f, nextModY = 0.5f;
    double rate = 48000.0, lastTicks = -1.0, songBeats = 0.0;
    bool playing = false;
    float bufL[chunk] {}, bufR[chunk] {};
};
} // namespace

std::unique_ptr<Instrument> makeHelixInstrument() { return std::make_unique<HelixInstrument>(); }
} // namespace lynx
