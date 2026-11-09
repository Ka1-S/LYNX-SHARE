#pragma once

// FL Studio Mobile effect modules → LYNX effect slots, round 188f (slice F5 of the .flm import; the value maps are
// REA_rodada182c.md "F5, round 188f" and FLM_FORMAT.md §7.2). Clean-room: FLM's parameter laws and filter designs were
// read from the plugin's code and are restated here as formulas; no Image-Line code was copied.
//
// Mapped today:
//  - Equalizer (3): 4 bands (low shelf / peak / high shelf, gain, frequency, bandwidth in octaves) → LYNX's 7-band
//    parametric EQ with cookbook biquads (the same shapes; FLM computes them at twice the rate);
//  - Graphic EQ (7): 8 fixed bands (80 Hz low shelf … 12 kHz high shelf, 1 octave) → the parametric EQ (two slots when
//    more than seven bands are not flat);
//  - Filter (9): low-pass / band-pass / high-pass, 24 / 48 / 96 dB per octave (2 / 4 / 8 equal biquads) → the
//    parametric EQ's single-biquad cut and band-pass bands, FLM's level offsets on the EQ's output;
//  - Autoduck legacy (10): the beat-synced duck curve → LYNX's rhythm gain (12 points), the ducking depth on the slot's
//    mix;
//  - Reverb legacy (12) and Reverb 2 (35): FLM's copy of the plate reverb that FL's Fruity Reeverb 2 uses → LYNX's
//    exact Reeverb 2 engine, FLM's integer controls converted into FL's.
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
