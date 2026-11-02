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

// DrumSynth Live: the key selected in the editor (stored after the 120 patches), Drumaxx: no stored selection (the
// panel remembers it); default part of a channel
int defaultPart (const ChannelDef& def);
} // namespace lynx::drumcontrols
