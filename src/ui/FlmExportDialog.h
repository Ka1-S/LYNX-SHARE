#pragma once

// File › "Save a copy as FL Studio Mobile project (.flm)..." (round 190og, FLM coverage app-0003): the song as an FL Studio
// Mobile project (core/FlmExport.h) with its samples in "<name> Samples" beside it. The window lists what the copy
// loses before it is saved, the way "Save a copy for an older LYNX" does; the open project and its file are untouched.

#include "core/FlmExport.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// The window (its list filled from `result`): "Save copy..." = 1, "Cancel" = 0. Also used by --flmexportshot.
juce::AlertWindow* buildFlmExportWindow (const FlmExportResult& result, std::shared_ptr<juce::TextEditor> list);

// The list's text: "May sound different there:" / "Left out (the sound stays):" / "Nothing is lost..."
juce::String flmExportLossText (const FlmExportResult& result);

// Writes `target` (.flm) and copies the samples beside it; false with `error`. `result` gets what was written and lost.
// (Also LYNX --export-flm.)
bool writeFlmCopy (const Song& song, const juce::File& target, FlmExportResult& result, juce::String& error);

// FL Studio Mobile's factory content on this computer (engine/AudioEngine.cpp): samples there stay "@R@" references
std::vector<std::string> flmFactoryRoots();

void showFlmExportDialog (AudioEngine& engine, juce::Component* parent, const juce::File& suggestedFolder, const juce::String& projectName);
} // namespace lynx
