#pragma once

// Presets of LYNX's own engines (REA round 129): every original these engines stand in for (Waves, Valhalla,
// FabFilter, Soundtoys) opens with a preset menu — factory starting points, user presets saved and loaded, ◀ ▶ to step
// through them. LYNX's factory presets are its own settings (own names, written for LYNX's engines; nothing taken from
// a third-party preset library). They are a text table (`builtInTable` in EffectPresets.cpp) whose values are written
// the way the editor shows them ("2.5 s", "40 %", "Concert Hall"…), so they stay readable and survive a change of a
// parameter's normalised law.
//
// User presets are small text files (`toText` / `fromText`): the engine's name, the preset's name and every parameter
// by name as its normalised value; the editor keeps them in <settings>/Presets/<engine>/<name>.lynxpreset.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx::presets
{
struct Preset
{
    std::string name;
    std::string folder;                               // the sub-menu it is listed in ("" = top level)
    std::vector<std::pair<std::string, std::string>> values; // parameter name -> value text ("#0.25" = normalised)
    std::vector<std::uint8_t> state; // engines whose settings live in EffectSlot::state (Gross Beat's slots): the whole state
};

// Engines whose presets carry EffectSlot::state as well as the parameters (round 168m: Gross Beat's 72 slots).
bool savesState (EffectKind kind);

// The factory presets of an engine, in menu order (empty for engines without any).
const std::vector<Preset>& factory (EffectKind kind);

// Every engine with factory presets.
std::vector<EffectKind> kindsWithFactoryPresets();

// The parameters of a preset: the engine's defaults, then the preset's values. `unresolved` (optional) receives the
// "name=value" entries that matched no parameter or no value.
// `current` (optional): the slot's parameters, whose values the engine's "keep" list carries over (LYNX Tune's key and
// scale stay when a preset changes how it corrects).
EffectParams apply (EffectKind kind, const Preset& preset, double bpm = 120.0, std::vector<std::string>* unresolved = nullptr,
                    const EffectParams* current = nullptr);

// The parameters an engine's presets leave as they are (the table's "keep" line).
const std::vector<int>& keptParams (EffectKind kind);

// The normalised value whose displayed text is `text`: an option's exact text (case and spaces ignored), else the
// value whose displayed number is nearest ("2.5 s" → the value shown as "2.50 s"; "6 kHz" = 6000). "#0.25" is a
// normalised value as is. nullopt when nothing matches.
std::optional<float> valueFromText (EffectKind kind, int param, const std::string& text, double bpm = 120.0);

// The index of a parameter of a built-in effect by name (case-insensitive), −1 when it has none.
int paramByName (EffectKind kind, const std::string& name);

// A user preset as text and back. fromText returns nullopt for a file of another engine or no preset at all;
// parameters missing from the file keep the engine's defaults, unknown names are ignored.
// `state` (optional) is written for engines that save it (savesState).
std::string toText (EffectKind kind, const std::string& name, const EffectParams& params, const std::vector<std::uint8_t>* state = nullptr);
struct Loaded
{
    std::string name;
    EffectParams params {};
    bool hasState = false;
    std::vector<std::uint8_t> state;
};
std::optional<Loaded> fromText (EffectKind kind, const std::string& text);

// A preset name made safe as a file name (no path separators or reserved characters, trimmed, at most 64 characters).
std::string fileNameFor (const std::string& presetName);
} // namespace lynx::presets
