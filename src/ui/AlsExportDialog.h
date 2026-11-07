#pragma once

#include "core/AlsExport.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// File › Export › "Live set (.als)..." (round 175a, N2): the song as a Live set for Live 12 or Live 11, written the way
// Live keeps its own sets: "<name> Project/<name>.als", with the samples copied into "Samples/Imported" next to it
// (or referenced where they are). A report lists what had no counterpart in the set (core/AlsExport.h).
void showAlsExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);

// Writes "<folder>/<name> Project/<name>.als" (samples copied into "Samples/Imported" when `collect`); returns the .als,
// or an empty File with `error` set. `notes` gets what had no counterpart in the set. (Also the --export-als command.)
juce::File writeAlsProject (const Song& song, PluginHost* host, AlsVersion version, bool collect, const juce::File& folder, const juce::String& name,
                            juce::StringArray& notes, juce::String& error);

// The panel only (--snapshot); nothing is written.
std::unique_ptr<juce::Component> makeAlsExportPanel (AudioEngine& engine);
} // namespace lynx
