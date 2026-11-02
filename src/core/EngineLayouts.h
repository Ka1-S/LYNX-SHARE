#pragma once

// Designed channel pages of FL engines that kept the plain knob grid until round 168q (group D of
// docs/analise/REA_rodada168n.md §1.3): Kepler, Kepler Exo, Transistor Bass, Sawer, BeepMap, Fruity Granulizer and the
// Wasp / Wasp XT placeholder. FL's arrangement and control set (read from the plugins' own editor forms, REA round 168q)
// drawn in LYNX's style; value items name the engine's parameters (core/ModelParams.h tables), values are raw.

#include "core/PluginLayout.h"

namespace lynx::plugui
{
Layout keplerLayout();
Layout keplerExoLayout();
Layout transistorBassLayout();
Layout sawerLayout();
Layout beepMapLayout();
Layout granulizerLayout();
Layout waspLayout();
} // namespace lynx::plugui
