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

#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::pocket
{
constexpr int distortionParams = 6, limiterClassicParams = 3;

// Distortion's curves: soft knee x/(k|x| + 1), tanh, exponential, hard clip
const char* distortionKindName (int kind) noexcept;
int distortionKind (float v) noexcept; // ⌊3v⌋, 0…3

std::unique_ptr<Effect> makeDistortion();
std::unique_ptr<Effect> makeLimiterClassic();

std::string distortionParamText (int param, float v);
std::string limiterClassicParamText (int param, float v);
} // namespace lynx::pocket
