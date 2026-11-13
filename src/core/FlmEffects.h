#pragma once

// FL Studio Mobile effect modules → LYNX effect slots, round 188f (slice F5 of the .flm import; the value maps are
// REA_rodada182c.md "F5, round 188f" and FLM_FORMAT.md §7.2). Clean-room: FLM's parameter laws and filter designs were
// read from the plugin's code and are restated here as formulas; no Image-Line code was copied.
//
// Mapped today:
//  - Equalizer (3), Graphic EQ (7), Filter (9), Reverb legacy (12), Reverb 2 (35) and Reverb (32): LYNX's exact Pocket
//    engines since round 190dw (core/PocketFxE.cpp, core/PocketReverb.cpp; FLM's values copied as they are). Until then
//    they were approximated on the Parametric EQ and Reeverb 2; projects saved with those slots keep them.
//  - Autoduck legacy (10): Pocket Autoduck (module 29's engine) with the legacy curve, exact (round 190dw).
//  - Tuned EQ (30): the parametric EQ's cookbook bands (round 190by).
//  - MultiFX (4), Limiter legacy (5), Distortion (6), Trance Delay (16), Tape Delay (18), Leveller (20), Gate (27),
//    Limiter (28) and the other Pocket engines (core/PocketFx.h, rounds 188h–190by), FLM's values copied as they are.
// Every module: Mix → the slot's mix, Post Gain → a utility after it (round 188h, the module wrapper's laws).
// Everything else returns no slots and the importer lists it.

#include <cstdint>
#include <string>
#include <vector>

#include "core/FlmFormat.h"
#include "core/Song.h"

namespace lynx::flmfx
{
// The sample rate the rate-dependent conversions assume (FLM computes its filters at the device's rate; a band's Q
// and the Filter's level offset vary slightly with it)
constexpr double designRate = 48000.0;

struct Mapped
{
    std::vector<EffectSlot> slots; // empty = LYNX has no equivalent
    std::string label;             // what the import report lists ("FL Studio Mobile: Equalizer")
    std::vector<std::string> partial; // what isn't reproduced (each one report line)
};

// `live` = the module has automation lanes: the slot layout must not depend on which bands happen to be flat
// `postGainLive` = the module's Post Gain is automated: the utility that carries it stays even at 0 dB
Mapped map (const flm::Module& module, bool live = false, bool postGainLive = false);

// round 190by: Tuned EQ's laws (module 30)
int tunedEqNote (float v) noexcept;                 // ⌊11v + 0.001⌋: 0 = C … 9 = A … 11 = B
double tunedEqHz (int note, int band) noexcept;     // 110·2^((note − 9)/12 + band), band 0…7
double tunedEqOctaves (float v) noexcept;           // 10v³ + 0.1

// An automation lane on parameter `param` of the module (FLM's index; −2 = the module's Mix, −3 its Post Gain): the LYNX
// targets it moves and, per target, the value for each of `values` (0…1, FLM's). Empty = not automatable.
struct LaneTarget
{
    AutomationTargetKind kind = AutomationTargetKind::effectParam; // effectParam or effectMix
    int slot = 0;  // slot of the module's mapping (Mapped::slots)
    int param = 0; // effectParam: the parameter
    std::vector<float> values;
};
std::vector<LaneTarget> automate (const flm::Module& module, int param, const std::vector<float>& values);

// Laws (exposed for the tests)
double equalizerHz (float v) noexcept;  // 2^(9.25 v + 5)
double filterHz (float v) noexcept;     // 2^(10.3 v + 4)
double filterQ (float v) noexcept;      // 2 v² + 0.8
int rawControl (float v, float lo, float hi) noexcept; // FLM's reverb controls: ⌊lo + v (hi − lo) + 0.5⌋
} // namespace lynx::flmfx
