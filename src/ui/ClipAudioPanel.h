#pragma once

// The Clip page's Audio panel (round 190cg): what the Clip-launch workflow shows for an audio clip of the clip grid —
// the clip's gain, pitch (semitones and cents) and reverse and its channel's stretch mode in a column, and beside it
// the audio editor with its warp markers, segment tempo and ÷2 / ×2. The layout follows the reference's Clip View
// (/mnt/e/lynx-work/warp190cg/HANDOFF.md cites the shot); every value is the Song's own (Clip::gain / pitch / reversed,
// SamplerSettings), so the Playlist and the Channel settings show the same data.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// nullptr when (track, scene) holds no audio clip with a channel.
std::unique_ptr<juce::Component> makeClipAudioPanel (AudioEngine& engine, int track, int scene);
} // namespace lynx
