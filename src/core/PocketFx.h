#pragma once

// The Pocket effects: LYNX's exact engines for FL Studio Mobile's own effect modules (the "(FL Mobile)" role in
// docs/PARITY.md), rebuilt in docs/analise/REA_rodada182c.md "F5 effects, round 188h". Clean-room: FL Studio Mobile's
// laws restated as formulas in our own words; no Image-Line code or data in this repository.
//
// Every parameter is the FL Studio Mobile module's own 0…1 value, in its order without the module's Enable (LYNX
// parameter k = FLM parameter k + 1; Enable is the slot's on switch), so the .flm import copies values as they are and
// a LYNX knob drives what FL Studio Mobile's knob drives. Every engine runs in single precision like FLM's.
//
//   Pocket Distortion (EffectKind::pocketDistortion, FLM module 6): [0] Drive (0.25), [1] BitCrush (0), [2] Gain (1 =
//     0 dB; −24…0 dB), [3] Stereo (0.5; the drive's left / right balance), [4] Kind (0: four curves), [5] Bias (0.5).
//   Pocket Limiter Classic (EffectKind::pocketLimiterClassic, FLM module 5, "Limiter (legacy)"): [0] Threshold (1 =
//     0 dB; −24…0 dB), [1] Release Time (0.7), [2] Output Gain (0.6667 = 0 dB; −24…+12 dB, off below −60 dB).
//   Pocket Limiter (EffectKind::pocketLimiter, FLM module 28): [0] In Gain (0.5 = 0 dB; ±24 dB), [1] Out Gain (same),
//     [2] Release Time (0.2; 0.1…2 s, v⁴ law), [3] Threshold (1 = 0 dB; −24…0 dB). 2 ms look-ahead (reported latency).
//   Pocket Leveller (EffectKind::pocketLeveller, FLM module 20): [0] Gain (0.7379573 = 0 dB; v^0.6 → −60…+12 dB),
//     [1] Pan (0.5; linear balance). A trim, not a compressor: gains glide by 1 % per sample.
//   Pocket Gate (EffectKind::pocketGate, FLM module 27): [0] Threshold (0; 66v − 60 dB), [1] Asymmetry (0; 66v² dB),
//     [2] Attack (0.01; 1000v ms), [3] A. Curve (0), [4] Release (0.25), [5] R. Curve (1), [6] Mix (1; floor and
//     invert), [7] Hold (0.25; v s). 256-sample RMS blocks, 255 samples of look-ahead (reported latency).
//   Pocket Trance Delay (EffectKind::pocketTranceDelay, FLM module 16): [0] Time (0.775: 1/4…1/1 beat in 5 steps),
//     [1] Feedback (0.4), [2] Mix (1), [3] Filter (0.5 = open), [4] Mode (0: Stereo / Ping-Pong / Mirror), [5] Input
//     pan (0.5, Ping-Pong), [6] Dry pan (0; FLM's saved flag: Input pan also pans the dry in Ping-Pong).
//   Pocket Tape Delay (EffectKind::pocketTapeDelay, FLM module 18): [0] Time (0.6; v² s), [1] Feedback (0.6), [2] Async
//     (0.5; shortens one side by up to half), [3] Mix (0.4; the echo is × Mix × Feedback). A variable-speed tape loop.
//   Pocket MultiFX (EffectKind::pocketMultiFx, FLM module 4): [0] FX Kind (0: Delay, Reverb, LP Filter, HP Filter,
//     Flanger, Phaser, Panner, Vox), [1] Axis X (0.5), [2] Axis Y (0.5), [3] LFO Rate (0.61: 1/8 beat … 4 bars in 10
//     steps), [4] LFO Amount (0; squared), [5] LFO Shape (0: Sine / Saw / Square). An X/Y pad over eight effects with
//     a song-locked LFO; a kind change cross-fades (core/PocketMultiFx.cpp).
//   Pocket Waveshaper (EffectKind::pocketWaveshaper, FLM module 41, core/PocketFxC.cpp): [0] Shape (0: Soft, Tanh,
//     Crossover, Fold, Octaver, Rectify, CrossFold), [1] Auto-Gain (0), [2] X (0.5), [3] Y (0.5), [4] Drive (0; 0…40 dB),
//     [5] Post Gain (0.5 = 0 dB; ±18 dB), [6] Stereo Spread (0), [7] Mix (1), [8] High Cut (1 = 20 kHz), [9] Low Cut (0 =
//     20 Hz), [10] Bass (0.5), [11] Mid Freq (0.5 = 1 kHz), [12] Mid Gain (0.5), [13] Treble (0.5), [14] LFO Shape (0),
//     [15] LFO Sync (0), [16] LFO Rate (0.5; 1/8 beat … 4 bars), [17] LFO Rate Free (0; 0.02…9.92 Hz), [18] LFO to X
//     (0), [19] LFO to Y (0). Every control glides (1 − 44.1/fs per sample); the LFO and X / Y move once per song tick.
//   Pocket Compressor (EffectKind::pocketCompressor, FLM module 8): [0] Thresh (0.499; −40…0 dB), [1] Ratio (0; 19v² + 1,
//     20 = ∞), [2] Attack (0.6; 0…50 ms), [3] Release (0.6; 10…500 ms), [4] Level (0.5001), [5] Hold (0; 0…50 ms), [6]
//     Auto gain (1), [7] Input gain (0.5; ±20 dB on the detector only), [8] RMS (0; FLM never turns it on).
//   Pocket Spreader (EffectKind::pocketSpreader, FLM module 44): [0] Width (0; 0…2), [1] Low Bypass (0; 10 Hz…10 kHz),
//     [2] Mono Bass (0), [3] Separation (0.5). FL's Spreader engine (core/SpreaderEngine.h) behind FLM's mapping;
//     reports FLM's latency (the engine's longest delay, 6.35 ms), which FL compensates.

#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::pocket
{
constexpr int distortionParams = 6, limiterClassicParams = 3, limiterParams = 4, levellerParams = 2, gateParams = 8, tranceDelayParams = 7, tapeDelayParams = 4, multiFxParams = 6;
constexpr int waveshaperParams = 20, compressorParams = 9, spreaderParams = 4;

// Distortion's curves: soft knee x/(k|x| + 1), tanh, exponential, hard clip
const char* distortionKindName (int kind) noexcept;
int distortionKind (float v) noexcept; // ⌊3v⌋, 0…3

std::unique_ptr<Effect> makeDistortion();
std::unique_ptr<Effect> makeLimiterClassic();
std::unique_ptr<Effect> makeLimiter();
std::unique_ptr<Effect> makeLeveller();
std::unique_ptr<Effect> makeGate();
std::unique_ptr<Effect> makeTranceDelay();
std::unique_ptr<Effect> makeTapeDelay();
std::unique_ptr<Effect> makeMultiFx(); // core/PocketMultiFx.cpp

std::string distortionParamText (int param, float v);
std::string limiterClassicParamText (int param, float v);
std::string limiterParamText (int param, float v);
std::string levellerParamText (int param, float v);
std::string gateParamText (int param, float v);
std::string tranceDelayParamText (int param, float v);
std::string tapeDelayParamText (int param, float v);
std::string multiFxParamText (int param, float v);
const char* multiFxKindName (int kind) noexcept;            // "Delay", "Reverb", … "Vox"
int multiFxKind (float v) noexcept;                          // ⌊7v⌋
const char* multiFxAxisName (int kind, bool yAxis) noexcept; // the pad's axis labels per kind
const char* tranceTimeName (float v) noexcept; // "1/4" … "1/1" (of a beat)

// core/PocketFxC.cpp (round 189h)
std::unique_ptr<Effect> makeWaveshaper();
std::unique_ptr<Effect> makeCompressor();
std::unique_ptr<Effect> makeSpreader();
std::string waveshaperParamText (int param, float v);
std::string compressorParamText (int param, float v);
std::string spreaderParamText (int param, float v);
std::string compressorLevelText (float level, bool autoGain); // Level's dB: 40v, or 40v − 20 with Auto gain
const char* waveshaperShapeName (int shape) noexcept; // round(6v): "Soft" … "CrossFold"
// a shape's curve at input x once its X / Y (the knobs' 0…1 values) have settled (tests, editors)
float waveshaperCurve (int shape, float x, float xKnob, float yKnob) noexcept;
// the Compressor's curve from Thresh and Ratio: g(env) = ((env − T)·slope + T)/env above T; makeup = 1/t2
struct CompressorTransfer
{
    float thresholdDb = 0.0f, threshold = 1.0f, ratio = 1.0f, slope = 1.0f, makeup = 1.0f;
};
CompressorTransfer compressorTransfer (float thresh, float ratio) noexcept;
} // namespace lynx::pocket
