#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// File › Export › "FL Studio project..." (round 171e, idea by Tookolld): the song as an FL Studio 2026 project, a .flp
// or FL's zipped loop package (.zip with every sample). What only LYNX has is rendered to audio first (option A, the
// default) or replaced by the nearest FL plugin (option B); the export report lists what went over exactly, rendered,
// substituted and left out (engine/FlStudioExport.h).
void showFlStudioExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);

// The panel only (--snapshot); nothing is written.
std::unique_ptr<juce::Component> makeFlStudioExportPanel (AudioEngine& engine);
} // namespace lynx
