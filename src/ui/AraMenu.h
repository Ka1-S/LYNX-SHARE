#pragma once

// ARA 2 in the Playlist (round 190kx, engine/AraHost.h): the audio clip menu's "Edit in ARA plugin" submenu (LYNX extras
// column: FL Studio 2026 has no ARA) and the plugin's editor window for a clip.

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

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

// Round 190oq (issue #243): ARA on a mixer insert (core/AraRegions.h). Whether slot `slot` of mixer track `track` is that
// track's ARA insert; its editor (the plugin's window showing every audio clip of the channels routed there; a hint when
// none is routed); the slot menu's "Edit the audio clips routed here (ARA)" switch (only for an ARA plugin) and the
// switch itself (one undo step, with a hint); whether a plugin newly put into a slot of `track` starts in ARA mode
// (REAPER / Logic / Studio One: an ARA plugin on a track's insert is in ARA mode): an ARA plugin, a mixer insert (not the
// Master) that audio clip channels are routed to, and no ARA slot there yet.
bool isAraSlot (AudioEngine& engine, int track, int slot);
bool openInsertEditor (AudioEngine& engine, int track);
void addSlotItems (juce::PopupMenu& menu, AudioEngine& engine, int track, int slot);
void setSlotAra (AudioEngine& engine, int track, int slot, bool on);
void hintSlotAra (AudioEngine& engine, int track, int slot, bool on); // (the hint alone)
bool araByDefault (AudioEngine& engine, int track, const PluginRef& plugin);

// The VST3 files of the plugin list whose binary declares an ARA main factory but whose listing does not say so yet (a
// scan made by a build without ARA): forgotten and scanned again in the background, with a hint. Returns how many.
int findAraPlugins (AudioEngine& engine);
} // namespace aramenu
} // namespace lynx
