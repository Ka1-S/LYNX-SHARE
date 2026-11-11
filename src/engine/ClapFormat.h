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

    ~ClapPluginFormat() override;

    // A CLAP instance deleted on another thread has its plugin deactivated and destroyed by the message thread (CLAP's
    // threading rules, issue #46); the deleting thread waits a moment for that and then goes on. Teardowns the message
    // thread has not run yet wait in a queue: it runs them when it is free again, and this runs them now. Call it on
    // the message thread when the application closes, after the threads that may still hold plugins have ended (from
    // any other thread it does nothing).
    static void finishPendingTeardowns();
    static int pendingTeardowns();  // how many wait for the message thread now
    static int deferredTeardowns(); // how many the deleting thread ever went on without (diagnostics, tests)
    // how long the deleting thread waits for the message thread (default 10 s; the tests shorten it)
    static void setTeardownWaitMs (int ms);

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

// Adds the LYNX formats (JUCE's VST3 + CLAP + our VST2 host) to a manager.
void addPluginFormats (juce::AudioPluginFormatManager& manager);

// Issue #108 (MPE, core/Mpe.h): from now on a CLAP plugin that takes CLAP notes gets the pitch bend, CC 74 and channel
// pressure of MIDI channels 2 … 16 as note expressions (tuning over `bendRange` semitones, brightness, pressure) for
// the notes on that channel; `bendRange` 0 = as before (raw MIDI events). False = not a CLAP plugin of ours, or one
// that takes MIDI notes (it gets the MPE messages themselves). Call before the plugin processes.
bool setClapNoteExpressions (juce::AudioPluginInstance& plugin, float bendRange);
} // namespace lynx
