#pragma once

// Reading FabFilter plug-in states for LYNX's substitutes (REA_rodada108 §5).
//
// What we know comes only from our own black-box observation of the official trials hosted through the VST3 API:
// set a parameter, save the state, see which bytes moved (tools/fxmeasure/py/dyn108_meas.py `states`). Nothing was
// decompiled. The component state is a small header ("FFBS" + version + count, or "FabF" + version + a preset name +
// count) followed by one 32-bit float per VST3 parameter, in the parameter list's order, in the plug-in's own units
// (dB, ms, Hz as log2, enumeration indices, or the normalized value for a few). Translators below map the readable
// values onto LYNX's own effects; they only run when the header and the parameter count match the layout we observed.
// The original state itself is never touched (the substitute slot keeps it byte for byte).

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/Song.h"

namespace lynx::fabfilter
{
struct State
{
    bool named = false;      // "FabF" layout (preset name in the header) vs "FFBS"
    std::uint32_t version = 0, count = 0;
    std::vector<float> values;
    float at (std::size_t k, float fallback = 0.0f) const noexcept { return k < values.size() ? values[k] : fallback; }
};

// The FabFilter state inside `bytes`: a raw component state, or a JUCE host state ("VC2!" XML with the component as
// base64 in <IComponent>), as LYNX's own VST3 host saves it.
std::optional<State> parseState (const std::vector<std::uint8_t>& bytes);

// Translators: write the readable settings into `slot` (an effect slot of the matching LYNX kind, already at its
// defaults). Return how many LYNX parameters were set (0 = state not recognised; slot unchanged).
using Translator = int (*) (const State& state, EffectSlot& slot);
int fromProC3 (const State& state, EffectSlot& slot);   // -> Crest Compressor
int fromProMB (const State& state, EffectSlot& slot);   // -> Prism Multiband
int fromProG (const State& state, EffectSlot& slot);    // -> Sentry Gate
int fromSaturn2 (const State& state, EffectSlot& slot); // -> Ember Saturator (multiband)
int fromProQ4 (const State& state, EffectSlot& slot);   // -> Facet EQ
int fromProDS (const State& state, EffectSlot& slot);   // -> Hush De-esser (round 148)
int fromProL2 (const State& state, EffectSlot& slot);   // -> Apex Limiter (round tp172b)
Translator translatorNamed (std::string_view name);     // "pro-c3", "pro-mb", "pro-g", "saturn2", "pro-q4", "pro-ds", "pro-l2"; null if unknown

// Their gain encoding (output / wet / band levels): −1 … 1 ↔ −∞ … +36 dB (0.2 per 7.2 dB above −0.6)
double levelDb (float v) noexcept;
} // namespace lynx::fabfilter
