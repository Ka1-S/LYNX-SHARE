#pragma once

// FL Studio's Fruity Multiband Compressor as a LYNX mixer effect: an exact engine rebuilt from the plugin's behaviour
// (docs/analise/REA_rodada56.md; clean-room, no Image-Line code). The slot keeps FL's 28 raw integer controls as its
// LYNX parameters (raw value normalised over the control's range), so it stays editable and an imported state
// round-trips exactly.
//
// Three bands, each split from the input on its own (high: high-pass; mid: low-pass + high-pass; low: low-pass), by
// 4th-order IIR filters or, in linear-phase mode, 105-tap Kaiser-windowed FIR filters (52 frames of latency); each
// band has a peak compressor and a make-up gain; the sum goes through an output gain and an optional soft limiter.

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

namespace lynx::flmb
{
constexpr int controls = 28;           // FL's controls 0…27 (the state also holds two UI-only words, not used here)
constexpr int params = controls;
constexpr int linearPhaseLatency = 52; // frames, FIR mode (control 2 on)
extern const std::array<int, controls> controlMin, controlMax, controlDefault;

struct State
{
    int version = 0;
    std::array<int, controls> c = controlDefault;
};
// FL's saved state (0xD5): i32 version (≤ 0), then the 28 controls as i32 (+ two UI words)
std::optional<State> parse (const std::vector<std::uint8_t>& state);

float normalised (int raw, int min, int max) noexcept;
int raw (float value, int min, int max) noexcept;

// Writes the state into a slot's parameter array (first `params` entries)
void toParams (const State& s, float* params) noexcept;

// Name and value text in FL's units for the effect editor
const char* paramName (int index) noexcept;
std::string paramText (int index, float value);

// `params` (may be null): the slot's parameters, so the linear-phase latency is known before the first block (#113)
std::unique_ptr<Effect> make (const EffectParams* params = nullptr);
// The same, with FL's raw state kept in the slot (EffectSlot::state): parameters still at their imported value keep
// FL's exact integer controls even after the project file has rounded them to 4 decimals
std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& flState, const EffectParams* params = nullptr);
} // namespace lynx::flmb
