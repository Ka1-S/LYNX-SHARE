#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// The clip grid of the Clip-launch workflow (round 181a, core/ClipLauncher.h): Playlist tracks as columns, scenes as rows.
// Own design and art. Made only the first time the grid is shown; never in Pattern workflow. It repaints from a timer that
// runs only while it is on screen.
std::unique_ptr<juce::Component> makeClipGrid (AudioEngine& engine);
} // namespace lynx
