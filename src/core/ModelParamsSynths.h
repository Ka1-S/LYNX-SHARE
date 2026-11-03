#pragma once

// The automatable parameters of the synth group of round 168o (core/ModelParams.h): Harmless, Fruity DX10, SimSynth,
// Ogun / Autogun, MiniSynth, GMS and Morphine. Every table is the plugin's own control set as FL's editor shows it
// (docs/analise/REA_rodada168o.md §2: the editor forms of the plugins' DLLs give each control's parameter number and
// range), indexed by the number the engine keeps the value under:
// - Harmless: ChannelDef::modelParams[i] (p0…p99 raw FL integers, then the 7 combo integers at 100…106);
// - Fruity DX10: the 22 raw integers of the state;
// - SimSynth: FL's 58 host parameters (knobs 0…65535, coarse pitch 0…48, switches, the 4 wave selectors);
// - Ogun / Autogun: the raw integers P0…P63 of the state;
// - MiniSynth, GMS, Morphine: their 0…1 floats inside the state.

#include "core/ModelParams.h"

namespace lynx::modelparams
{
Table harmlessModelTable();
Table dx10ModelTable();
Table simSynthModelTable();
Table ogunModelTable();
Table autogunModelTable();
Table miniSynthModelTable();
Table gmsModelTable();
Table morphineModelTable();
} // namespace lynx::modelparams

namespace lynx
{
const std::vector<EngineParam>& harmlessEngineParams();
const std::vector<EngineParam>& dx10EngineParams();
const std::vector<EngineParam>& simSynthEngineParams();
const std::vector<EngineParam>& ogunEngineParams();
const std::vector<EngineParam>& autogunEngineParams();
const std::vector<EngineParam>& miniSynthEngineParams();
const std::vector<EngineParam>& gmsEngineParams();
const std::vector<EngineParam>& morphineEngineParams();
} // namespace lynx
