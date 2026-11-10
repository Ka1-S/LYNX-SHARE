#pragma once

// LYNX's own engines that are built as VST3 / CLAP plugins for other hosts (round 180b, docs/PLUGIN_EXPORT.md): the entries
// of plugin/plugins.def with built = 1 (the CLAP) or 2 (the CLAP and a VST3 of its own). Only engines that need no FL Studio
// data files.

#include <array>

#include "core/Song.h"

namespace lynx::plugexport
{
struct Entry
{
    const char* name;     // the plugin's name in the host
    bool instrument;
    EffectKind effect;    // when ! instrument
    InstrumentKind synth; // when instrument
    const char* clapId;
};

#define LYNX_PLUGIN_ENTRY_0(...)
#define LYNX_PLUGIN_ENTRY_1(name, instrument, effect, synth, clapId) Entry { name, instrument != 0, EffectKind::effect, InstrumentKind::synth, clapId },
#define LYNX_PLUGIN_ENTRY_2 LYNX_PLUGIN_ENTRY_1
#define LYNX_PLUGIN(name, instrument, effect, synth, clapId, code, category, built) LYNX_PLUGIN_ENTRY_##built (name, instrument, effect, synth, clapId)
inline constexpr std::array catalog {
#include "plugin/plugins.def"
};
#undef LYNX_PLUGIN
#undef LYNX_PLUGIN_ENTRY_0
#undef LYNX_PLUGIN_ENTRY_1
#undef LYNX_PLUGIN_ENTRY_2
} // namespace lynx::plugexport
