#pragma once

// Round 190jg (BUILD-LIST 18): sample-accurate automation for hosted plugins. The renderer hands a hosted plugin's
// adapter (PluginInstrument / PluginEffect) the automation's changes inside a chunk with their sample offsets
// (Instrument / Effect::setParameterAt); while the plugin's block runs, the adapter publishes the ones of that block on
// the thread, as plain arrays (parameter index as in getParameters(), offset into the block, value 0..1, offsets
// ascending per parameter). LYNX's CLAP host turns them into CLAP_EVENT_PARAM_VALUE events at those times; JUCE's VST3
// host (cmake/LynxPatchJuce.cmake fix 39, the same four declarations in juce_VST3Common.h: keep them identical) into
// points of the parameter's IParamValueQueue. Without them (another format, the option off) the values of a chunk
// reach the plugin at its start, as before.
namespace juce
{
inline thread_local int lynxTimedParamCount = 0;
inline thread_local const int* lynxTimedParamIndices = nullptr;
inline thread_local const int* lynxTimedParamOffsets = nullptr;
inline thread_local const float* lynxTimedParamValues = nullptr;
} // namespace juce
