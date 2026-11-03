#pragma once

// The editable controls of FL's drum and physical-model generators (round 168p): Fruit Kick, Plucked!, Drumpad,
// Drumaxx, BassDrum, Fruity DrumSynth Live, Sakura and FL Slayer. One table per engine names every control of the
// plugin's FL editor (parameter index = the control's Tag in FL's editor form, ranges = its Min / Max; docs/analise/
// REA_rodada168p.md §1) and says where its value lives in the channel (ChannelDef::modelParams or the plugin's raw
// state). The channel page (ui/ChannelSettings) builds its knobs from these tables, the editor layouts
// (core/PluginLayoutDrums.inc) place them by name, and the tests check both against each other. No JUCE.

#include <optional>
#include <string>
#include <vector>

#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx::drumcontrols
{
struct Control
{
    const char* name;  // the knob's name (layout items refer to it; English, shown as the tooltip)
    int index;         // parameter index in the engine's storage
    float low, high;   // the control's range in stored units
    bool integer;      // whole numbers (switches, menus, raw FL integers)
    bool perPart = false; // Drumaxx: a parameter of the selected pad (index + 44·pad); DrumSynth Live: of the selected key
};

// the controls of an engine (empty for other kinds)
const std::vector<Control>& controls (InstrumentKind kind);
const Control* find (InstrumentKind kind, const std::string& name);

// The part a per-part control edits: Drumaxx's pad (0…15), DrumSynth Live's key (its patch slot is chosen through
// drumSynthKeyToSlot). Ignored for the other engines.
// Value of a control in a channel (stored units), nullopt when the channel's state can't hold it (e.g. an FL state
// LYNX can't read).
std::optional<float> value (const ChannelDef& def, const Control& c, int part = 0);
// Sets it (clamped to the range, rounded for integer controls); false when the state can't hold it.
bool setValue (ChannelDef& def, const Control& c, float v, int part = 0);

// The engine parameter index of a control for a part (−1: no such part), and the raw values by engine index (the
// automation table below uses the same numbers)
int absoluteIndex (InstrumentKind kind, const Control& c, int part);
std::optional<float> rawValue (const ChannelDef& def, int index);
bool setRawValue (ChannelDef& def, int index, float raw);

// Every automatable parameter of the engine, by engine index (sorted): the controls above, Drumaxx's for each of the
// 16 pads ("Pad 3 Pitch"), DrumSynth Live's for each key ("C5 Level"); listed in core/ModelParamList.inc
const std::vector<EngineParam>& params (InstrumentKind kind);

// DrumSynth Live: the key selected in the editor (stored after the 120 patches), Drumaxx: no stored selection (the
// panel remembers it); default part of a channel
int defaultPart (const ChannelDef& def);
} // namespace lynx::drumcontrols

namespace lynx::modelparams
{
Table fruitKickModelTable();
Table pluckedModelTable();
Table drumpadModelTable();
Table drumaxxModelTable();
Table bassDrumModelTable();
Table drumSynthModelTable();
Table sakuraModelTable();
Table slayerModelTable();
} // namespace lynx::modelparams
