#pragma once

// FL Studio's Transporter (Fruity Transporter, a transient looper) as a LYNX mixer effect: the plug-in's own engine,
// a Faust program in FL's shared DSP library, rebuilt in docs/analise/REA_rodada167f-transporter.md (clean-room:
// structure, formulas and behaviour in our own words; no Image-Line code or data in this repository).
//
// A detector splits a side signal (the input, or another insert) into eight bands (Linkwitz-Riley crossovers at about
// 24, 57, 135, 320, 761, 1.8k and 4.3k Hz), follows each band's energy with two four-stage envelopes and marks a
// transient when the band's level jumps by more than 0.5 nepers; an odd number of bands at once fires a trigger (after
// a 20 ms hold). The input is recorded into two 20-second ring buffers; on a trigger loop A (and loop B, with its own
// random draws) restarts: it replays the segment since the previous trigger, or reverses, or plays an octave up or
// down, each by probability (or deterministically from the write position). The loop modes set the segment length:
// Auto (time between triggers), Bouncing (shrinking by a rate), Adaptive (from the trigger density), Tempo (a note
// value, retriggered on the song's beat grid). Loops are de-clicked at restarts, panned (spread or auto-pan) and mixed
// with the delayed dry signal; loop A / B also go out as a two-channel send (left = A, right = B).
//
// Parameters (EffectKind::flTransporter): FL's 22 controls over their range, value = (raw − min) / (max − min),
//   [0] Dry level 0…12800  [1] Loop A level  [2] Loop B level  [3] Reverse probability 0…10000  [4] Octave shift
//   probability  [5] Octave down/up probability  [6] Spread / auto-pan 0…1024  [7] Minimum length A 20…2000 (ms)
//   [8] A/B ratio 0…36  [9] Transient sensitivity 0…1024  [10] Bouncing rate 0…1024  [11] Freeze 0/1  [12] Flush 0/1
//   [13] Loop mode 0…3 (Auto, Bouncing, Adaptive, Tempo)  [14] Bypass 0/1  [15] Update loop A  [16] Update loop B
//   [17] Update loops A/B  [18] Deterministic 0/1  [19] Tempo length 0…20  [20] Tempo trigger 0…21 (21 = off)
//   [21] Ignore probability 0…10000
// then the state's settings: [22] Source 0…2 (left, right, both)  [23] Stereo 0/1  [24] Send to insert 0…500 (0 = none).
// The side-chain input is the slot's sidechain (FL's input track), else the effect's own input.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::transporter
{
constexpr int numControls = 22;
constexpr int numParams = 25;
constexpr int sourceParam = 22, stereoParam = 23, sendParam = 24;
constexpr std::array<int, numParams> rawMin { 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
constexpr std::array<int, numParams> rawMax { 12800, 12800, 12800, 10000, 10000, 10000, 1024, 2000, 36, 1024, 1024, 1, 1, 3, 1, 1, 1, 1, 1, 20, 21,
                                              10000, 2, 1, 500 };

struct State
{
    // FL's "Default" preset
    std::array<int, numControls> raw { 12800, 12800, 12800, 0, 0, 0, 512, 500, 8, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 0 };
    int source = 2, stereo = 1, sendTrack = 0, inputTrack = 0;
    bool flushWhenDisabled = true;
};
// FL's state: i32 version (≤ 8), 22 × i32 controls (version < 8: 20, then [21] = 0), u8 flush-when-disabled, u8
// look-ahead (unused), i32 source, i32 stereo, i32 send track, i32 input track (missing fields keep their defaults)
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
int rawOf (int param, float v01) noexcept;
std::string paramText (int param, float v01);
std::unique_ptr<Effect> make();

// the value the front end sends the engine for control k at raw value v (dB, probabilities, seconds, …)
float engineValue (int k, int raw) noexcept;
// the song position the front end gives the engine: the beat within the bar (0…4), from the host's tick position
float beatOf (double ticks, int ppq) noexcept;

// The engine (FL's "TransientLooperImpl" around the Faust program): parameters by the library's own index (0…21 the
// controls, 22 tempo, 23 look-ahead, 24 source, 25 stereo), a flush request faded out / in by the wrapper.
class Engine
{
public:
    Engine();
    void prepare (float sampleRate);
    void setParam (int k, float v) noexcept;
    void clear() noexcept;   // the DSP's state (not the loop buffers)
    int latency() const noexcept;
    // inL/inR the audio, detL/detR the detector input, beat the bar position per sample; sends may be null
    void process (const float* inL, const float* inR, const float* detL, const float* detR, const float* beat, float* outL, float* outR,
                  float* sendL, float* sendR, int n) noexcept;

    struct Band
    {
        float slow[4] {}, fast[4] {};
        float mag = 0, level = 0;
    };
    struct Svf
    {
        float s1 = 0, s2 = 0;
    };
    struct Coef
    {
        float t = 0, gk = 0, g = 0, g2 = 0, t2 = 0;
    };
    struct Loop // one of the two loopers
    {
        std::vector<float> buffer;
        float ring[1024] {};
        int count = 0, countPrev = 0, wrapCount = 0, wrapCountPrev = 0, bounceCount = 0, bounceCountPrev = 0;
        int bounceFlag = 0, bounceFlagPrev = 0, reverse = 0, reversePrev = 0;
        float freeze = 0, freezePrev = 0;
        float lastStart = 0, lastStartPrev = 0, start = 0, startPrev = 0;
        float bounceLen = 0, bounceLenPrev = 0, length = 0, lengthPrev = 0, phase = 0, phasePrev = 0;
        float rate = 0, ratePrev = 0, read = 0;
        float sinceJump = 0, sinceJumpPrev = 0;
        float env[4] {}, gain = 0;
    };

private:
    bool onset (Band& b, float x, float threshold) const noexcept;
    void compute (int n) noexcept;

    // constants
    float fs = 44100.0f, kBypass = 0, kBypass2 = 0, kSmooth = 0, kSmooth2 = 0, look = 0, loopLen = 0, holdLen = 0, kTrig = 0, kTrig1 = 0,
          kAttack = 0, kRelease = 0, kRatio = 0, minGap = 0, decay = 0, panStep = 0, twoFs = 0;
    Coef c149, c355, c845, c2010, c4782, c11373, c27051;
    // the Faust controls (zones)
    float bypass = 0, lookahead = 0, inputDb = 0, flushZone = 0, loopMode = 0, minLenA = 0.5f, source = 0, sensitivity = 0.5f, trigA = 0,
          trigAB = 0, trigB = 0, freeze = 0, tempoMul = 21, deterministic = 0, ignore = 0, bounceRate = 0, reverseP = 0, octaveP = 0,
          downUpP = 0, loopADb = 0, spread = 0, stereo = 0, ratio = 3, loopBDb = 0;
    // state
    int one = 0, iota = 0, index = 0, hold = 0, peakCount = 0, peakMax = 0;
    unsigned seed = 0;
    float bypassS = 0, gainS = 0, thrS = 0, meter = 0, trigPrev[3] {}, trigS[4] {}, beatPrev = 0, panPhase = 0, panS = 0;
    Svf sv[16];
    Band band[8];
    std::vector<float> dryL, dryR;
    Loop loop[2];
    // the wrapper (TransientLooperImpl)
    float rate = -1, fadeCoef = 0, fade[2] {}, rawMinLen = 0, rawBounce = 0, tempo = 60;
    bool flushRequest = false, bypassFlag = false;
    long long mode = 0, lenIndex = 0, trigIndex = 0;
    std::vector<float> in[5], out[4];
};
} // namespace lynx::transporter
