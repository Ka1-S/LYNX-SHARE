#pragma once

// Auto-pan's "Rhythm" voicing (round 136, vox136): a step-pattern auto-panner whose laws were measured black-box on the
// official Soundtoys PanMan trial (hosted through the VST3 API, fed steady tones and her takes, outputs measured;
// nothing decompiled or copied), so that her projects that used it pan the same through LYNX
// (docs/analise/REA_rodada136.md §2).
//
// Measured structure [EXEC]: a pattern of N step values per period (2 steps: left / right; 8 steps: a stepped ramp;
// round tp172d, the trial's default shape: a continuous ramp from right to left whose return to the right takes
// 45 ms + smoothing × a quarter of the period, fitted on its 0.25 / 0.63 Hz pan trajectories within 1°);
// each step glides linearly from the previous value over `smoothing` of its length (at least ~1.1 ms); the pan
// angle is 45° + offset / 2 + width / 4 · value, clipped to 0 … 90°; equal-power balance gains (left · cos, right ·
// sin: a balance, the channels are not summed). The period is free (Hz, from the start of the song) or a number of
// beats locked to the song position. Analog mode: a low-frequency shelf and an asymmetric soft saturation on each
// input. The oversampler's passband colour is a short causal FIR (the original's 45-sample latency is compensated by
// the host, so this one has none).

#include <array>

namespace lynx::rhythmpan
{
constexpr int maxSteps = 8;

struct Settings
{
    bool sync = false;         // false: rateHz; true: beats per period, locked to the song position
    double rateHz = 1.0, beats = 1.0;
    double width = 180.0;      // degrees (0 … 210)
    double offset = 0.0;       // degrees (−105 … 105)
    double smoothing = 0.0;    // 0 … 1 of a step
    int pattern = 0;           // 0: left / right (2 steps), 1: 8-step ramp, 2: ramp (round tp172d: PanMan's default shape)
    double amount = 1.0;       // the pattern's values scaled by this
    double phase = 0.0;        // start phase (cycles) at song position 0
    int analog = 1;            // 0 off, 1 the analog input stage (soft saturation), 2 the "hot" stage (a fast compressor)
    double outputDb = 0.0;
    // round 150 (parity150a): the Tweak page — an envelope follower on the input moves the rate (octaves), the offset
    // and the width (degrees) by its amount (0 below the threshold, 1 at threshold + range) [EXEC, REA_rodada150a §6]
    double rateMod = 0.0;          // −3 … +3 octaves
    double offsetMod = 0.0;        // −180 … +180 degrees
    double widthMod = 0.0;         // −180 … +180 degrees
    double envThresholdDb = -20.0; // peak dBFS of a sine where the modulation starts (the trial's default, measured)
    double envRangeDb = 11.0;      // dB above it where the modulation is full (measured)
    double feel = 0.0, groove = 0.0; // ±0.25 of a step: the whole pattern / the odd steps late (+) or early (−) [model]
    bool beyond = false;           // angles past the speakers fold back with the far side inverted (measured); off = clipped
    // round 152 (parity152): PanMan's audio-triggered sources and its Random source [MANUAL docs/specs/panman.md; laws
    // model]: each time the input (through the trigger filter) rises past the threshold — or the manual trigger is
    // pressed — a counter advances; every `divider` triggers the pan moves: Ping-pong to the other side, Random step to
    // a random position, gliding over `smoothing` × 250 ms. Random steps: the clock's steps take random positions.
    int trigger = 0;               // 0: the clock's pattern, 1: ping-pong, 2: random step
    double trigThresholdDb = -20.0;
    bool trigAudio = true;         // false: the threshold is fully up (manual trigger only)
    int divider = 1;               // 1 … 12 triggers per move
    int trigFilter = 0;            // 0 off, 1 high pass, 2 low pass (the detector only)
    double trigFreqHz = 1000.0;
    float manual = 0.0f;           // the manual trigger: each change of this value is one trigger
    bool randomSteps = false;
    bool operator== (const Settings&) const = default;
};

// the parameter laws (normalised 0 … 1 values)
double rateHz (float v) noexcept;   // 0.01 … 10 Hz (log), the original's Rate law
double beats (float v) noexcept;    // 1/16 … 64 beats (log2)
float beatsValue (double beats) noexcept;
float rateValue (double hz) noexcept;

class Engine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void set (const Settings&) noexcept;
    // `beatPos` = song position in beats at the block's first sample; `bpm` the tempo
    void process (float* left, float* right, int n, double beatPos, double bpm) noexcept;
    float currentPan() const noexcept { return current; } // 0 (left) … 1 (right), for a display / control output
    float envelopeAmount() const noexcept { return (float) envAmount; } // round 150: the Tweak follower, 0 … 1

private:
    float analogStage (float x, int c) noexcept;

    double sr = 48000.0;
    Settings s;
    std::array<double, maxSteps> values {};
    int steps = 2;
    double outGain = 1.0;
    // analog LF shelf (first order) and colour FIR, per channel
    double shB0 = 1.0, shB1 = 0.0, shA1 = 0.0;
    std::array<double, 2> shZ {};
    double rmsA = 0.0, attA = 0.0, rms = 0.0, gr = 0.0;
    std::array<std::array<float, 32>, 2> hist {};
    int histPos = 0;
    float current = 0.5f;
    // round 150: the Tweak envelope follower (mean square 1 ms, then attack 21 ms / release 290 ms on the peak-equivalent
    // level) and the phase it adds when it moves the rate
    double envMs = 0.0, env = 0.0, envAmount = 0.0, extraPhase = 0.0;
    double msA = 0.0, envAtt = 0.0, envRel = 0.0;
    // round 152: the trigger detector (its filter, a peak follower with a re-arm level) and the moves it makes
    double tfB0 = 1.0, tfB1 = 0.0, tfB2 = 0.0, tfA1 = 0.0, tfA2 = 0.0, tfZ1 = 0.0, tfZ2 = 0.0;
    double trigEnv = 0.0, trigRel = 0.0;
    bool armed = true;
    int trigCount = 0;
    double trigFrom = 0.0, trigTo = 0.0, trigAge = 1e12, trigValue = 0.0;
    float lastManual = 0.0f;
    bool manualSeen = false;
    unsigned long long rng = 0x9E3779B97F4A7C15ull;
};
} // namespace lynx::rhythmpan
