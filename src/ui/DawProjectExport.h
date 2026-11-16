#pragma once

// File › Export › "DAWproject (.dawproject)..." (round 190jf, slice B22): the song as a DAWproject (core/DawProject.h)
// for Bitwig Studio, Studio One, Cubase and the other programs that read the open format. The samples are embedded in
// the file (DAWproject's audio/ folder) unless they are too big, then referenced where they are; a report lists what
// had no counterpart in the format.

#include "engine/AudioEngine.h"

namespace lynx
{
void showDawProjectExport (AudioEngine& engine, juce::Component* parent, const juce::File& suggestedFolder, const juce::String& projectName);

// Writes `target` (a .dawproject); false with `error`. `notes` gets what was left out. (Also --export-dawproject.)
bool writeDawProjectFile (const Song& song, PluginHost* host, const juce::File& target, juce::StringArray& notes, juce::String& error);
} // namespace lynx
