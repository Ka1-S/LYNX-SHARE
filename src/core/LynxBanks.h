#pragma once

// LYNX's own preset banks for the FL engines it reproduces (round 168n). With FL Studio installed, a channel's
// ◀ ▶ arrows and preset list show FL's factory presets (core/PresetCycle.h) and then these; without FL (macOS, Linux,
// Windows without FL) these banks are the presets, and an engine that plays without FL's data files starts from its
// bank's first preset. The presets are designed for LYNX (clean-room: never converted from FL's preset files).
//
// Each engine's bank lives in its own .cpp file (e.g. core/ToxicBank.cpp) as a factory function in namespace
// lynx::banks, and is listed with ONE line in core/LynxBankList.inc:
//
//     LYNX_BANK (poizone, poizoneBank)   // → std::vector<lynx::banks::Preset> lynx::banks::poizoneBank();
//
// (an explicit list, not static registrars: the core is a static library, whose unreferenced objects the linker
// drops). Registrar exists for the tests.
//
// `apply` receives a channel of that engine (instrument already set; its pluginState / modelParams / samplePath as
// the channel had them) and writes the preset's sound into it. It must not touch the channel's own settings (name,
// volume, pan, mixer insert, Misc tab): withPreset keeps those.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::banks
{
struct Preset
{
    std::string name;                            // "Acid Squelch" (English, shown as is: preset names are not translated)
    std::string category;                        // the folder in the menus: "Bass", "Lead", "Pad", "Pluck", "Keys", "FX", "Init"...
    std::vector<std::string> tags;               // free words for searching ("analog", "fm", "dark"...)
    std::function<void (ChannelDef&)> apply;     // writes the sound into a channel of the engine
};

using Factory = std::vector<Preset> (*)();

// Registers (or, with nullptr, removes) the bank of an engine at run time; replaces the listed one (tests)
struct Registrar
{
    Registrar (InstrumentKind kind, Factory factory);
};

// The bank of an engine (empty = none); built once, in the factory's order
const std::vector<Preset>& bankOf (InstrumentKind kind);
bool hasBank (InstrumentKind kind);

// The engines that have a bank
std::vector<InstrumentKind> kindsWithBanks();

// `current` with preset `index` of its engine's bank: the instrument's sound from the preset, the channel's own
// settings kept, the channel named after the preset (as FL's arrows name it). nullopt for a bad index.
std::optional<ChannelDef> withPreset (const ChannelDef& current, int index);

// A new channel of `kind` from its bank's first preset (nullopt = no bank). Named after the engine (`engineName`).
std::optional<ChannelDef> defaultChannel (InstrumentKind kind, const std::string& engineName);

// The pseudo path of a bank preset in the arrows' list ("lynxbank:<index>") and back (−1 = not a bank path)
std::string pathOf (int index);
int indexOfPath (const std::string& path);
// The relative path shown in the menus: "LYNX/<category>/<name>"
std::string relativeOf (const Preset& preset);
} // namespace lynx::banks
