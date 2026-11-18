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

// Round 190lj, "Convert to MIDI (polyphonic)": the same with the options dialog's settings (Basic Pitch polyphonic or
// one note at a time, thresholds, shortest note, key range, pitch bends, quantize, the channel that plays them).
void convertClipToMidi (AudioEngine& engine, int clipIndex, const a2m::ConvertOptions& options, std::function<void()> done);
void convertAudioToMidi (AudioEngine& engine, std::vector<float> mono, double sampleRate, const a2m::ConvertOptions& options,
                         const juce::String& name, std::function<void (int notes)> done);

// The options dialog (modal, async): the last settings of this run of LYNX, the song's instrument channels to pick from
// (or a new Synth channel); `gridTicks` = the grid "Quantize" uses. `go` runs with the settings on OK.
void showConvertOptions (AudioEngine& engine, juce::Component* owner, std::int64_t gridTicks, std::function<void (const a2m::ConvertOptions&)> go);

// --polyshot190lj: the clip's conversion with these settings at once, on the calling thread (no window); the new
// pattern's index or -1 (`error` says why when the model could not run).
int convertClipToMidiNow (AudioEngine& engine, int clipIndex, const a2m::ConvertOptions& options, juce::String& error);

// --polyshot190lj: the dialog as it opens (not shown).
std::unique_ptr<juce::Component> makeConvertOptionsForSnapshot (AudioEngine& engine, std::int64_t gridTicks);

// The settings the dialog showed last (this run of LYNX; the channel is not kept between conversions).
a2m::ConvertOptions& lastConvertOptions();

// App shutdown: a conversion still running stops at its next check and is waited for (before the engine goes).
void shutdownAudioToMidi();

// --audio2midi: the notes of a whole file, without a window, as CSV lines "start_s,end_s,key,velocity_0_127".
// Empty, or what went wrong.
juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, a2m::Mode mode, const juce::File& csv);

// --audio2midi with the dialog's settings (round 190lj: "poly" mode with key=value settings); bends as extra CSV lines
// "bend,<note index>,<seconds from the note's start>,<cents>". `timing` gets "resample_s,model_s,notes_s".
juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, const a2m::ConvertOptions& options, const juce::File& csv,
                                   juce::String* timing = nullptr);
} // namespace lynx
