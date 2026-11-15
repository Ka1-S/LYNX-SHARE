#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/GeneratorPresets.h"
#include "engine/AudioEngine.h"

// The user's own presets of LYNX's built-in generators (round 190fs, core/GeneratorPresets.h): the files, the name
// prompt (the one the hosted plugins' and the mixer effects' preset menus show) and the loads. The channel window's
// preset box (ui/ChannelSettings.cpp), LYNX's synth page and the Browser use them.
namespace lynx::genpresetui
{
juce::File root();                         // <settings>/Presets/Plugins/Generators (inside the Browser's "Plugin presets")
juce::File folderFor (InstrumentKind kind); // that generator's folder (not created)
bool isPresetFile (const juce::File& file); // a .lynxpreset of a generator (its first line says so)
std::optional<genpresets::Preset> read (const juce::File& file);
// The generator's saved presets, sorted by name (files of other kinds or damaged files left out)
juce::Array<juce::File> userPresets (InstrumentKind kind);

// Writes `channel`'s generator as preset `name` into its folder (an existing preset of that name is replaced).
bool save (const ChannelDef& channel, const juce::String& name, juce::File& written, juce::String& error);
// The preset's sound on rack channel `channel` (one undo step; the project is marked as changed). False + `error`.
bool loadInto (AudioEngine& engine, int channel, const juce::File& file, juce::String& error);
// A new channel with the preset's generator and sound, selected (one undo step). False + `error`.
bool addChannel (AudioEngine& engine, const juce::File& file, juce::String& error);

// "Save preset" › "Name of the preset:" with Save / Cancel; `done` gets the trimmed name (not called on Cancel / empty).
void askName (juce::Component& owner, const juce::String& initial, std::function<void (const juce::String&)> done);
// That box, not shown yet (askName; shots)
std::unique_ptr<juce::AlertWindow> makeNameBox (const juce::String& initial, juce::Component* owner = nullptr);
// Shows `folder` in the file manager (created first)
void reveal (const juce::File& folder);
} // namespace lynx::genpresetui
