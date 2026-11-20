#pragma once

// FL Studio's Low Lifter as a LYNX mixer effect: the plugin's own bass enhancer (a Faust program in FL's shared DSP
// library), rebuilt in docs/analise/REA_rodada154.md §5 (clean-room: structure, formulas and behaviour in our own
// words; no Image-Line code or data in this repository).
//
// Per channel: a 24 dB/oct crossover (two TPT state-variable filters) splits off the low band; in multiband mode it is
// split again into four bands around the crossover (½, ¼ of it). Each band, times the pre gain, goes through a
// look-ahead peak limiter (4 hold stages, envelope bounded below at 1, 4 attack / release stages) and the Chebyshev
// enhancer u + a·T₂(u) + a²·T₃(u) normalised by 1 + 2a (a = 10^(slope/20)), then a 20 Hz high-pass. The bands are
// summed, faded in over the first second, optionally diffused by four rotation all-passes, scaled by the low gain and
// the polarity, added to the delayed high band, high-passed at the low cut; the dry signal runs through matching
// all-passes and the same delay; Dry/Wet mixes them.
//
// Parameters (EffectKind::flLowLifter) are FL's controls over their range, value = raw / max:
//   [0] On 0/1  [1] Dry/Wet 0…10000  [2] Low band gain 0…10000 (−120…+12 dB)  [3] Slope 0…10000 (−60…0 dB)
//   [4] Look-ahead 0/1  [5] Invert polarity 0/1  [6] Pre gain 0…10000 (0…18 dB)  [7] Diffuser mix 0…10000
//   [8] Diffuser depth 0…10000 (−0.99…0.99)  [9] High band gain 0…10000 (−120…0 dB)  [10] Crossover 0…10000
//   (20…120 Hz)  [11] Low cut 0…10000 (20…120 Hz)  [12] Multiband 0/1  [13] High band on 0/1  [14] Low band on 0/1
// FL's host skips the engine after a second of silence (the effect then passes its input), as LYNX does.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::lowlifter
{
constexpr int numParams = 15;
// round 190mm (sub-round dyn), hidden (after the named ones; saved while non-zero; only the Waves Renaissance Bass /
// MaxxBass translators set them): the psycho-acoustic bass law (core/BassHarmonics.h; 1 = on) and its values — maxx 1 =
// the MaxxBass role (its input gain), freq log 16 … 1000 Hz, levels as (dB + 72) / 96 (0 = off), orig 0 … 1 linear
enum BassLawParam { bassLaw = numParams, bassMaxx, bassFreq, bassInput, bassOrig, bassHarm, bassOut, bassEnd };
float bassLevelValue (double db) noexcept;
double bassLevelDb (float v) noexcept;
float bassFreqValue (double hz) noexcept;
double bassFreqHz (float v) noexcept;
constexpr std::array<int, numParams> rawMax { 1, 10000, 10000, 10000, 1, 1, 10000, 10000, 10000, 10000, 10000, 10000, 1, 1, 1 };

struct State
{
    std::array<int, numParams> raw { 1, 10000, 4999, 6993, 1, 0, 4998, 0, 5000, 10000, 8920, 0, 0, 1, 1 }; // Default preset
};
// FL's state (0xD5): i32 version (6) + 15 × i32 controls + UI bytes
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
std::string paramText (int param, float v01);
std::unique_ptr<Effect> make (bool lookahead = true);

// the engine value the front end sends for control k at raw value v (dB / Hz / amounts, the switches as their raw)
float engineValue (int k, int raw) noexcept;

class Engine
{
public:
    Engine();
    void init (int sampleRate);
    void setControl (int k, float v) noexcept; // the engine's own units (engineValue)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    struct Svf // TPT state-variable filter state
    {
        float s1 = 0, s2 = 0;
    };
    struct Smooth // TPT one-pole smoother state
    {
        float s = 0;
    };
    struct Harmonics
    {
        std::vector<float> buf = std::vector<float> (2048, 0.0f);
        int count[4] {};
        float hold[4] {}, env[4] {};
        Svf dc;
    };
    struct Channel
    {
        Svf a, b, c, d, e, f, g, h, i, j, lowCut, dryA, dryB, dryC;
        float dry1 = 0;
        std::vector<float> high = std::vector<float> (2048, 0.0f), dryBuf = std::vector<float> (2048, 0.0f);
        Harmonics band[4];
        std::vector<float> ap0 = std::vector<float> (8192, 0.0f), ap1 = std::vector<float> (2048, 0.0f),
                           ap2 = std::vector<float> (2048, 0.0f), ap3 = std::vector<float> (2048, 0.0f);
        float z[4] {};
    };

private:
    float runHarmonics (Harmonics& h, float in, int delay, float holdLen, float kAttack, float a, float b, float norm) noexcept;

    float fs = 44100.0f;
    float kG = 0, kG2 = 0, piFs = 0, quarterPiFs = 0, halfPiFs = 0;
    float g0 = 0, k0 = 0, g0Inv = 0, g0Half = 0, g0Twice = 0, g0Two = 0;
    float holdBase = 0, kRelease = 0, attackBase = 0, kSlope = 0, kSlope2 = 0, kFade = 0, kFade2 = 0;
    float apLen[4] {};
    // controls (the Faust zones)
    float on = 1, dryWet = 1, lowCutHz = 20, highSwitch = 1, highGainDb = 0, multiband = 0, crossoverHz = 60, lookahead = 0,
          invert = 0, lowSwitch = 1, lowGainDb = 0, diffMix = 0, diffDepth = 0, preGainDb = 0, slopeDb = -60;
    Smooth mix, lowCut, hiSw, hiGain, mb, xover, pol, loSw, loGain, dMix, dDepth, preGain, slope, fade;
    int index = 0;
    Channel ch[2];
};
} // namespace lynx::lowlifter
