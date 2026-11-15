#pragma once

// The user's own presets of LYNX's built-in generators (round 190fs; owner decision O7: LYNX's own file first, FL's
// .fst channel state later). FL's plugin wrapper saves a generator's settings with "Save preset as..." (PluginMenu
// SaveStateMenu, REA rows wrapper_wheel-0072); LYNX's channel window keeps that in its preset box, next to FL's
// presets and LYNX's banks, with the same items as the hosted plugins' and the mixer effects' preset menus: Save
// preset..., "Your presets", Delete preset, Show the presets folder.
//
// A preset holds the generator's whole sound: exactly the instrument fields the .lynx project stores for the channel
// and that loading an FL preset replaces (natives::withPreset): the plugin state, the model parameters, the sample /
// data file path and zones, the controller state, LYNX's synth settings and the key names. The channel's own settings
// (name aside, mixer insert, levels, Misc tab, colour, arpeggiator, envelopes of the Envelope tab) stay as they are.
//
// The file is a small text file, extension .lynxpreset like LYNX's other presets but with its own first line, in
// <settings>/Presets/Plugins/Generators/<generator>/ (the Browser's "Plugin presets" place). Float values are written
// as their exact bits, so a save and a load give back the identical state (and the identical sound).
// Pure C++ (no JUCE): the format is tested in the core tests.

#include <optional>
#include <string>

#include "core/Song.h"

namespace lynx::genpresets
{
constexpr const char* extension = ".lynxpreset";
constexpr const char* header = "LYNX generator preset 1";
// The folder under the plugin presets folder that holds every generator's own folder
constexpr const char* generatorsFolder = "Generators";

struct Preset
{
    std::string name;   // the preset's name (the channel takes it when the preset loads, as FL's presets do)
    InstrumentKind kind = InstrumentKind::synth;
    ChannelDef sound;   // the instrument fields (soundOf); the channel settings are defaults
};

// The generator's id in the file: the same id the .lynx project writes ("dx10", "threeosc"…); nullopt when unknown.
std::string kindId (InstrumentKind kind);
std::optional<InstrumentKind> kindFromId (const std::string& id);

// The generator's English name: its presets' folder ("Fruity DX10", "3x Osc", "Synth"…)
std::string engineName (InstrumentKind kind);
// The folder name for those presets (the name made safe for every file system)
std::string folderName (InstrumentKind kind);

// Whether a generator's channel window has a preset box, so its user presets can be saved there: the FL engines with a
// preset folder, the engines with a LYNX bank and LYNX's own synth (its "Presets..." box).
bool canSave (InstrumentKind kind);

// The file text of a preset of `channel`'s generator, named `name`.
std::string toText (const std::string& name, const ChannelDef& channel);
// nullopt when the text is not a generator preset (an effect's or a hosted plugin's .lynxpreset, a damaged file).
std::optional<Preset> fromText (const std::string& text);
// Whether a file starting with `start` (its first bytes are enough) is a generator preset
bool looksLikePreset (const std::string& start);

// The instrument fields of a channel alone (on a default channel of the same kind)
ChannelDef soundOf (const ChannelDef& channel);
bool sameSound (const ChannelDef& a, const ChannelDef& b);

// `current` with the preset loaded: the preset's sound, the channel's own settings kept, the channel named after the
// preset. nullopt when the preset is for another generator.
std::optional<ChannelDef> withPreset (const ChannelDef& current, const Preset& preset);
// A new channel of the preset's generator with its sound and name (mixer insert left to the caller)
ChannelDef newChannel (const Preset& preset);

// The file name (without the extension) for a preset name: path separators and reserved characters replaced
std::string fileNameFor (const std::string& name);
} // namespace lynx::genpresets
