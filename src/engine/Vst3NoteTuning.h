#pragma once

// Issue #63: the per-note tuning of a VST3 note on (FL's wrapper sends a note's fine pitch there, core/HostNotePitch.h).
// JUCE's VST3 host builds its note ons from MIDI, which has no tuning; cmake/LynxPatchJuce.cmake fix 18 makes
// MidiEventList ask `lynxNoteTuningHook (lynxNoteTuningContext, channel, key)` for each note on it converts, while LYNX
// has set a context on the thread that runs the plugin's block. These are the same two inline variables the patched
// juce_VST3Common.h declares (keep the declarations identical); without the patch nobody asks and the tuning stays 0.
namespace juce
{
inline float (*lynxNoteTuningHook) (void* context, int channel, int key) = nullptr;
inline thread_local void* lynxNoteTuningContext = nullptr;
} // namespace juce
