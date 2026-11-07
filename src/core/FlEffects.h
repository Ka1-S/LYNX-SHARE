#pragma once

// Saved state (0xD5) of FL's native effects in mixer slots -> parameters of LYNX's built-in
// effects. Formats and formulas in docs/analise/REA_rodada7.md (clean-room).

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Builds the slot from the FL plugin name and the blob. Without a usable blob, returns the similar
// built-in effect with default parameters; nullopt when there is no similar effect.
// `bpm` converts tempo-synced times (e.g. Reeverb 2 predelay) to milliseconds.
std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm = 120.0);

// A file of the user's FL installation by its path relative to %FLStudioFactoryData% (e.g.
// "Plugins\\Fruity\\Effects\\Soundgoodizer\\Data\\Presets\\a.dat"); empty = not available
using FlDataReader = std::function<std::vector<std::uint8_t> (const std::string& relativePath)>;
// The same, with access to the installation's data files: Soundgoodizer becomes the exact engine with its preset
// file (REA_rodada40 §3); without them (or with an unreadable file) the approximation is used.
std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm,
                                        const FlDataReader& data);

// FL's Maximus as the exact engine (EffectKind::maximus) when its state is supported (REA_rodada36/38); nullopt =
// a feature that is not decoded (the caller keeps the approximation)
std::optional<EffectSlot> maximusExact (const std::vector<std::uint8_t>& state);
// Soundgoodizer as the exact engine from its state and the preset file it names (a…d.dat, read by the caller)
std::optional<EffectSlot> soundgoodizerExact (const std::vector<std::uint8_t>& state, const std::vector<std::uint8_t>& presetFile);
// The installation path (relative to %FLStudioFactoryData%) of the preset file a Soundgoodizer state loads
std::string soundgoodizerPresetPath (const std::vector<std::uint8_t>& state);

// FL effect preset (.fst from Plugin presets\Effects): the built-in effect with the preset's settings.
std::optional<EffectSlot> effectFromFlPreset (const std::vector<std::uint8_t>& fileBytes, double bpm = 120.0);
// The same with the installation's files: the plugins whose state only names them (Fruity Convolver's impulse response,
// Distructor's and Hardcore's cabinets, Vocodex's carrier, Soundgoodizer's preset) become their exact engine (round 173d3:
// without them all 261 of FL's Convolver presets came back as a generic reverb, which the preset bar then ignored)
std::optional<EffectSlot> effectFromFlPreset (const std::vector<std::uint8_t>& fileBytes, double bpm, const FlDataReader& data);

// FL plugins whose presets map to this built-in effect (for listing the preset folders).
std::vector<std::string> flPluginsForEffect (EffectKind kind);
// Automation of an FL native effect parameter: our equivalent parameter and the conversion from FL's
// normalized value (0..1 over its parameter range) to ours. nullopt = no equivalent.
struct FlParamMap
{
    int param = 0;
    std::function<float (float)> convert;
};
std::optional<FlParamMap> effectParamFromFl (const std::string& pluginName, int flParam);

// Head of a Patcher branch leaving bus `bus` of a Frequency Splitter or Stereo Shaper
// (REA_rodada17 §1-2). Splitter: bus 0 = sum of the bands; bus k = the band whose destination (word
// 15 + i) equals k, filtered by the crossovers with the combo's slope; with 2 bands the mid is muted.
// Stereo Shaper: 0 = matrix output; the "difference" destination (parameter 6) = input − output.
struct PatcherBranchHead
{
    bool valid = false;
    std::optional<EffectSlot> slot; // effect at the head of the branch (none = direct signal)
    float gain = 1.0f;
    std::string note;               // approximation used (for the report)
};
PatcherBranchHead patcherBranchHead (const std::string& pluginName, const std::vector<std::uint8_t>& state, int bus);

// Fruity Love Philter -> LYNX effects (REA_rodadas 16 §2 and 17 §15): each enabled unit becomes a parametric
// EQ (type, cutoff and Q from FL's formulas at 44.1 kHz) + gain/pan. One unit: `slots` in series
// (and `paramMap` translates FL automation to them); several in parallel (NEXT = 0): a container.
struct LovePhilterConversion
{
    std::vector<EffectSlot> slots;
    // automation of FL parameter k -> (index within `slots`, our parameter, value conversion)
    std::function<std::optional<std::tuple<int, int, std::function<float (float)>>> (int k)> paramMap;
    std::vector<std::string> notes; // what was left out (for the report)
    // live automations: the control output of slot `source` drives parameter `param` of slot `target`
    // (indices within `slots`), through the curve (33 points over 0..1)
    struct LiveLink
    {
        int source = 0, target = 0, param = 0;
        std::vector<float> curve;
    };
    std::vector<LiveLink> links;
};
// The Fruity Limiter's compression section as a LYNX compressor (for the sidechain, which only feeds the
// compressor's detector; REA_rodada22 §1)
std::optional<EffectSlot> fruityLimiterCompressor (const std::vector<std::uint8_t>& state);

// Vocodex as the exact engine (core/Vocodex.h, REA_rodada35) holding FL's raw state: ModIn/CarIn index the track's
// inputs list (0 = own input; CarIn < 0 = internal synth carrier)
struct VocodexConversion
{
    EffectSlot slot;
    int modIn = 0, carIn = 0;
    std::vector<std::string> notes;
};
std::optional<VocodexConversion> vocodexEffect (const std::vector<std::uint8_t>& state);
// The same, with the installation's data files appended to the slot (SG.dat for the SG stage, the internal carrier's
// Sytrus preset; REA_rodada40 §4–5)
std::optional<VocodexConversion> vocodexEffect (const std::vector<std::uint8_t>& state, const FlDataReader& data);
// LYNX's Soundgoodizer approximation (REA_rodada8; preset 0…3 = A…D, amount 0…1000): the compressor followed by
// the presets' −0.2 dB master limiter
std::vector<EffectSlot> soundgoodizerApproximation (int preset, int amount);
// Soundgoodizer when its preset files are not available (no FL installation): the approximation in a container that
// keeps the plugin's state (version, preset 0…3, amount 0…1000), so the slot shows as Soundgoodizer and its page
// (Preset A–D, Amount) can still edit it
EffectSlot soundgoodizerApproxSlot (int preset, int amount);
bool isSoundgoodizerApprox (const EffectSlot& slot);

std::optional<LovePhilterConversion> lovePhilterEffects (const std::vector<std::uint8_t>& state);

// Current value (0..1) of output `output` of a static controller (Fruity X-Y Controller, Control
// Surface; REA_rodada15 §11): a link from it becomes a fixed value on the target. nullopt if unreadable.
std::optional<float> controllerConstantOutput (const std::string& pluginName, const std::vector<std::uint8_t>& state, int output);
} // namespace lynx
