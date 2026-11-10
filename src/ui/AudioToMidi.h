#pragma once

#include <functional>
#include <vector>

#include "core/AudioToMidi.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// "Convert to MIDI" (round a2m189b; Ableton Live's Convert Melody / Harmony / Drums to New MIDI Track): the analysis runs
// on a low-priority worker with a progress window and Cancel (melody and harmony through Basic Pitch on ONNX Runtime,
// drums with LYNX's own detector); nothing of it runs on the audio thread.

// The Playlist audio clip menu: what the clip plays becomes a new pattern on new channels, on a new track right under
// the clip and aligned with it (one undo step). `done` runs on the message thread after it was added.
void convertClipToMidi (AudioEngine& engine, int clipIndex, a2m::Mode mode, std::function<void()> done);

// The audio editor: `mono` (at `sampleRate`) becomes a new pattern named after `name`, which is selected (one undo step).
// `done` gets the number of notes (0: none found or cancelled) on the message thread.
void convertAudioToMidi (AudioEngine& engine, std::vector<float> mono, double sampleRate, a2m::Mode mode, const juce::String& name,
                         std::function<void (int notes)> done);

// App shutdown: a conversion still running stops at its next check and is waited for (before the engine goes).
void shutdownAudioToMidi();

// --audio2midi: the notes of a whole file, without a window, as CSV lines "start_s,end_s,key,velocity_0_127".
// Empty, or what went wrong.
juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, a2m::Mode mode, const juce::File& csv);
} // namespace lynx
