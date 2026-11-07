#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/ExportCatalog.h"

namespace lynx::plugexport
{
// A LYNX engine as a juce::AudioProcessor. The audio runs through the same RackRenderer as the app, on a one-channel or
// one-slot Song; the editor is the app's own, built on an engine that plays nothing and exists only while it is open.
// The saved state is that Song as a .lynx project.
juce::AudioProcessor* makeProcessor (const Entry& entry);
} // namespace lynx::plugexport
