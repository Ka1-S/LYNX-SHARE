#pragma once

// ARA 2 in the Playlist (round 190kx, engine/AraHost.h): the audio clip menu's "Edit in ARA plugin" submenu (LYNX extras
// column: FL Studio 2026 has no ARA) and the plugin's editor window for a clip.

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

namespace aramenu
{
// The submenu for the audio clip `clipIndex` of the Playlist: the ARA plugins of the plugin list (the one the clip's
// channel is edited in ticked; picking it opens its editor), "Open <plugin> editor", "Stop editing in <plugin>", and
// "Find ARA plugins" (a scan of the VST3 files that may have ARA). Disabled items when this build has no ARA.
juce::PopupMenu forClip (AudioEngine& engine, int clipIndex);

// Opens the ARA editor of the clip's channel showing that clip (false: the channel is not edited in an ARA plugin or the
// plugin is not running).
bool openEditor (AudioEngine& engine, int clipIndex);

// The VST3 files of the plugin list whose binary declares an ARA main factory but whose listing does not say so yet (a
// scan made by a build without ARA): forgotten and scanned again in the background, with a hint. Returns how many.
int findAraPlugins (AudioEngine& engine);
} // namespace aramenu
} // namespace lynx
