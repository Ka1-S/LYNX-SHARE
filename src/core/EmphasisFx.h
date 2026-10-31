#pragma once

// FL Studio's Emphasis (the mastering limiter of FL 2024+) as an exact LYNX effect, rebuilt in
// docs/analise/REA_rodada167f.md (clean-room: the plug-in's layout, formulas and behaviour in our own words; no
// Image-Line code or data in this repository). The DSP is core/EmphasisEngine.h (the shared library's limiter);
// this file is the front end: FL's saved state, its controls and what it sends to the engine.
//
// Parameters (EffectKind::flEmphasis) are FL's controls over their range, value = (raw − min) / (max − min), so FL
// automation and the LYNX editor drive what FL's ProcessParam receives:
//   [0] Bypass 0/1   [1] Delta 0/1   [2] Envelope order 1…4   [3] True peak 0/1   [4] Channel mode 0 L/R, 1 M/S
//   [5] Mode 0…3 (Versatile / Loud / Transient / Steady)   [6] Input gain 0…10000 (0…+24 dB, linear in dB)
//   [7] Linking 0…10000 (0…100 %)   [8] Emphasis 0…10000 (0…100 %)   [9] Ceiling 0…10000 (FL's volume curve, 10000 = 0 dB)
//   [10] Level matching 0/1   [11] Oversampling 0…4 (off, 2×, 4×, 8×, 16×)   [12] Hardness 0…9
//   [13] Loudness compensation 0/1   [14] Instant recovery 0/1   [15] DC block 0/1 (the panel's three option buttons)
// Host blocks longer than 512 frames are processed as 512-frame engine calls, as FL's mixer calls the plug-in.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::emphasis
{
constexpr int numParams = 16;
constexpr std::array<int, numParams> rawMin { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
constexpr std::array<int, numParams> rawMax { 1, 1, 4, 1, 1, 3, 10000, 10000, 10000, 10000, 1, 4, 9, 1, 1, 1 };
// a new instance (FL's "Default" preset): 1st order, linking and emphasis 100 %, ceiling −0.2 dB, 4×, hardness 7,
// instant recovery and DC block on
constexpr std::array<int, numParams> rawDefault { 0, 0, 1, 0, 0, 0, 0, 10000, 10000, 9912, 0, 2, 7, 0, 1, 1 };
// the engine's parameter index for each control
constexpr std::array<int, numParams> engineIndex { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 25, 26, 27 };

// The plug-in's saved state (record 0xD5): i32 version (≤ 10) · 13 i32 controls · i32 UI page · i32 (v ≥ 7) ·
// u8 loudness compensation (v ≥ 9) · u8 instant recovery, u8 DC block (v ≥ 10); before version 8 the envelope
// order is stored 0-based
struct State
{
    int version = 10;
    std::array<int, numParams> raw = rawDefault;
};
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
int rawValue (int param, float v01) noexcept;

std::string paramText (int param, float v01);
// initial: the slot's controls (may be null), read only before the first block to report the latency
std::unique_ptr<Effect> make (const EffectParams* initial = nullptr);

// What the front end sends to the engine for control k at raw value v (dB for input gain and ceiling, the amount
// for linking and emphasis, the 0-based order for the envelope, the raw switch / choice otherwise)
float engineValue (int k, int raw) noexcept;
} // namespace lynx::emphasis
