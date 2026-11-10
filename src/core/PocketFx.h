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
//   Pocket Autoduck (EffectKind::pocketAutoduck, FLM module 29): [0] Ducking (0.501; depth √v), [1] Attack (0.1; 0.5v +
//     0.001 beat), [2] Curve (0.501), [3] Time (1; of the beat), [4] Shift (0.5; ±½ beat), [5] Hold (0; of Time), [6] Shape
//     (0: Accel, Accel 2, Decel, Decel 2, S-Curve, S-Curve 2). A gain curve on every song beat, 0.01 one-pole per sample
//     (core/PocketFxA.cpp).
//   Pocket Chorus (EffectKind::pocketChorus, FLM module 11): [0] Rate (0.4; 4v² + 0.2 Hz), [1] Depth (0.6), [2] Stereo
//     (1), [3] Mix (0.6; v², added to the dry). Two taps into one 10 ms ring of L + R.
//   Pocket Spacer (EffectKind::pocketSpacer, FLM module 34): [0] L/R Delay (0.5; ±300 ms, cubic), [1] Pan (0.5),
//     [2] Separation (0.5), [3] L/R Inversion (0), [4] M/S Delay (0.5; ±100 ms, fifth power), [5] M/S Inversion (0),
//     [6] Stereo Mode (0: Standard, Output M/S, Input M/S).
//   Pocket Stereoizer (EffectKind::pocketStereoizer, FLM module 15): [0] Strength (0.7), [1] Timbre (0.52; Haas delay
//     0.5…50 ms), [2] Focus (0.75; low-pass below ½, high-pass above), [3] L/R Delay (0.5; ±25 ms), [4] Pan (0.5).

#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::pocket
{
constexpr int distortionParams = 6, limiterClassicParams = 3, limiterParams = 4, levellerParams = 2, gateParams = 8, tranceDelayParams = 7, tapeDelayParams = 4, multiFxParams = 6;
constexpr int autoduckParams = 7, chorusParams = 4, spacerParams = 7, stereoizerParams = 5;

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
std::unique_ptr<Effect> makeAutoduck();   // core/PocketFxA.cpp
std::unique_ptr<Effect> makeChorus();     // core/PocketFxA.cpp
std::unique_ptr<Effect> makeSpacer();     // core/PocketFxA.cpp
std::unique_ptr<Effect> makeStereoizer(); // core/PocketFxA.cpp

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
std::string autoduckParamText (int param, float v, double bpm);
std::string autoduckHoldText (float hold, float time); // Hold reads as a share of the beat (× Time)
const char* autoduckShapeName (int shape) noexcept;     // "Accel" … "S-Curve 2"
float autoduckEnvelope (const EffectParams& p, float phase) noexcept; // the duck curve (0…1) at a beat phase
std::string chorusParamText (int param, float v);
std::string spacerParamText (int param, float v);
std::string stereoizerParamText (int param, float v);
} // namespace lynx::pocket
