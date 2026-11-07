#pragma once

// LYNX's own engines that ship as VST3 / CLAP plugins for other hosts (round 180b, docs/PLUGIN_EXPORT.md). Only engines
// that need no FL Studio data files, and only under LYNX's own names. Identity fields must never change once released:
// hosts store them in their projects.

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
    const char* clapFeature;
};

inline constexpr std::array<Entry, 2> catalog {
    Entry { "LYNX Facet EQ", false, EffectKind::facetEq, InstrumentKind::dx10, "com.ka1-s.lynx.facet-eq", "equalizer" },
    // "LYNX FM10" is a PLACEHOLDER name (the user decides the synths' names): internal prototype, not for release
    Entry { "LYNX FM10", true, EffectKind::eq, InstrumentKind::dx10, "com.ka1-s.lynx.fm10", "synthesizer" }
};
} // namespace lynx::plugexport
