#pragma once

// FL Studio's Fruity Limiter and Fruity Compressor as LYNX mixer effects: exact engines rebuilt from the plugins'
// behaviour (docs/analise/REA_rodada48.md; clean-room, no Image-Line code). Both keep FL's raw integer controls as
// their LYNX parameters (raw value normalised over the control's range), so the slot stays editable and an imported
// state round-trips exactly.
//
// Fruity Limiter: gain → compressor section (RMS detector, optional sidechain) → limiter (peak window, lookahead,
// release/attack curves) → noise gate → soft saturation or clip at the ceiling.
// Fruity Compressor: DC blocker → peak follower → static curve (hard/medium/vintage/soft knee, "/R" release) →
// attack/release → makeup → fixed 64-frame look-ahead brick-wall at −0.1 dB.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Effect;
}

namespace lynx::fldyn
{
// --- Fruity Limiter -------------------------------------------------------------------------------------------

constexpr int limiterControls = 18;
constexpr int limiterParams = limiterControls + 1; // + the "ceiling exactly" menu option (state byte 0xA8 bit 0)
extern const std::array<int, limiterControls> limiterMin, limiterMax, limiterDefault;

struct LimiterState
{
    int version = 7;
    std::array<int, limiterControls> p = limiterDefault;
    int sidechain = 0;         // side-input index k (0 = own input)
    bool exactCeiling = false; // menu option: limiter threshold = ceiling (else 0.975 × ceiling)
};
// FL's saved state (0xD5): u32 version, then (v ≥ 5) the 18 controls, … i32 side input at 0xA4 (v ≥ 6), u8 option (v ≥ 7)
std::optional<LimiterState> parseLimiter (const std::vector<std::uint8_t>& state);

// --- Fruity Compressor ----------------------------------------------------------------------------------------

constexpr int compressorControls = 6;
constexpr int compressorParams = compressorControls + 1; // + the look-ahead flag (latency report only)
extern const std::array<int, compressorControls> compressorMin, compressorMax, compressorDefault;

struct CompressorState
{
    int version = 3;
    std::array<int, compressorControls> p = compressorDefault;
    bool lookahead = true;
    int sidechain = 0;
};
// u32 version (1…3), 6 × i32 controls, u32 flags (v ≥ 2; bit 0 = look-ahead), i32 side input (v3)
std::optional<CompressorState> parseCompressor (const std::vector<std::uint8_t>& state);

// --- LYNX parameters (0..1) -----------------------------------------------------------------------------------

float normalised (int raw, int min, int max) noexcept;
int raw (float value, int min, int max) noexcept;

// Writes the state into a slot's parameter array (first limiterParams / compressorParams entries)
void limiterToParams (const LimiterState& s, float* params) noexcept;
void compressorToParams (const CompressorState& s, float* params) noexcept;

// Value text in FL's units for the effect editor
const char* limiterParamName (int index) noexcept;
const char* compressorParamName (int index) noexcept;
std::string limiterParamText (int index, float value);
std::string compressorParamText (int index, float value);

// `initial` (may be null): the slot's parameters, so the latency is known after prepare(), before the first block
std::unique_ptr<Effect> makeLimiter (const EffectParams* initial = nullptr);
std::unique_ptr<Effect> makeCompressor (const EffectParams* initial = nullptr);
} // namespace lynx::fldyn
