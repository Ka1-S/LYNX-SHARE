#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// The CLAP plugin id (e.g. "com.vendor.plugin") of a scanned CLAP plugin: its module is opened and the descriptor whose
// id hashes to the description's uniqueId is read (Export to FL Studio, round 171e: FL's wrapper keys CLAP by its id).
// Empty when the file no longer has it.
std::string clapPluginId (const juce::PluginDescription& description);

// CLAP plugin hosting (JUCE 8 only ships VST3/AU/LV2). Implemented as one more
// juce::AudioPluginFormat: scanning, the plugin list, the plugin window, saved state,
// latency (PDC) and parameter automation all work through the same path as VST3.
// Written from the public CLAP 1.2 headers (MIT license, in third_party/clap).
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

// Adds the LYNX formats (JUCE's VST3 + CLAP + our VST2 host) to a manager.
void addPluginFormats (juce::AudioPluginFormatManager& manager);
} // namespace lynx
