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
bool sampleNeedsWavForOtherApps (const juce::File& file);
juce::MemoryBlock decodedWavForOtherApps (const juce::File& file, double& seconds, int& sampleRate, int& channels);

// The panel only (--snapshot); nothing is written.
std::unique_ptr<juce::Component> makeAlsExportPanel (AudioEngine& engine);
} // namespace lynx
