#pragma once

// Designed channel pages of the synth group of round 168o (Harmless, Fruity DX10, SimSynth, Ogun / Autogun, MiniSynth,
// GMS, Morphine): FL's arrangement of each plugin's editor in LYNX's own style. Value items name the engine parameters
// of core/ModelParamsSynths.h (EngineParam::name). Positions are FL's window pixels at 100 % (the plugins' editor
// forms: docs/analise/REA_rodada168o.md §2).

#include <vector>

#include "core/PluginLayout.h"

namespace lynx::plugui
{
// nullptr = not one of this group's engines
const Layout* synthGroupLayout (InstrumentKind kind);
const std::vector<InstrumentKind>& synthGroupLayoutKinds();
} // namespace lynx::plugui
