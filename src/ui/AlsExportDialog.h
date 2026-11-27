#pragma once

#include "core/AlsExport.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// File › Export › "Live set (.als)..." (round 175a, N2): the song as a Live set for Live 12 or Live 11, written the way
// Live keeps its own sets: "<name> Project/<name>.als", with the samples copied into "Samples/Imported" next to it
// (or referenced where they are). A report lists what had no counterpart in the set (core/AlsExport.h).
void showAlsExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);

// Writes "<folder>/<name> Project/<name>.als" (samples copied into "Samples/Imported" when `collect`); returns the .als,
// or an empty File with `error` set. `notes` gets what had no counterpart in the set. (Also the --export-als command.)
juce::File writeAlsProject (const Song& song, PluginHost* host, AlsVersion version, bool collect, const juce::File& folder, const juce::String& name,
                            juce::StringArray& notes, juce::String& error);

// Round 190rm: a sample other apps cannot read as it is (FL's WavPack .wv, Ogg Vorbis inside a WAV or named .wav, any
// other type only LYNX decodes; core alsSampleNeedsWav), and the file LYNX plays from it as a 32-bit float WAV in memory
// (empty when it cannot be decoded), with its length, rate and channels. Used by the Live set and DAWproject exports.
// Round 190sa (Q6, the DAWproject's size: Thief 5 -> 237 MB): with `flac` given, the sample goes out as a lossless FLAC
// (16 or 24 bits) when every sample LYNX plays from it is exactly a 16- or 24-bit value under full scale, checked by
// decoding the FLAC back (bit for bit); *flac says which was written. FL's Ogg Vorbis samples decode to values on no
// integer grid (and often over full scale), so they stay 32-bit float WAV; FL's WavPack samples become FLAC.
bool sampleNeedsWavForOtherApps (const juce::File& file);
juce::MemoryBlock decodedWavForOtherApps (const juce::File& file, double& seconds, int& sampleRate, int& channels, bool* flac = nullptr);
// (exposed for the check) the planes as a FLAC of the smallest exact bit depth (16 or 24), verified by decoding it back;
// empty when the samples are not exactly on such a grid, reach full scale, or the round trip differs
juce::MemoryBlock losslessFlacOf (const float* const* planes, int channels, juce::int64 frames, double sampleRate, int* bitsUsed = nullptr);

// The panel only (--snapshot); nothing is written.
std::unique_ptr<juce::Component> makeAlsExportPanel (AudioEngine& engine);
} // namespace lynx
