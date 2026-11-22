#pragma once

// ARA 2 hosting (round 190kx; core/AraLink.h has the what and why, core/AraRegions.h the model mapping).
//
// One AraHost per engine, made the first time a song has an audio clip channel edited in an ARA plugin (an engine that
// never sees one never makes it: no plugin, no document, no CPU). It keeps, per ARA plugin, one ARA document controller
// (JUCE's ARAHostDocumentController over Celemony's ARA SDK, third_party/ara) and the document's model objects, and per
// ARA channel one instance of the plugin bound to that document with the playback renderer, editor renderer and editor
// view roles. That instance plays the channel's regions in the rack (instrumentFor: the channel's instrument) and shows
// its editor (pluginOf). Message thread only, except the instruments' render().
//
// Without the ARA SDK (JUCE_PLUGINHOST_ARA 0) the class exists and does nothing: available() is false.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/AraLink.h"

namespace lynx
{
class Instrument;
class PluginHost;
struct ChannelDef;
struct SampleData;
struct Song;

class AraHost
{
public:
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;
    AraHost (PluginHost& plugins, SampleProvider samples);
    ~AraHost();

    // Whether this build hosts ARA (it was compiled with the ARA SDK)
    static bool available() noexcept;
    // The ARA plugins of the plugin list (VST3 with an ARA main factory, as the scan found them), by name
    static std::vector<juce::PluginDescription> araPlugins (PluginHost& plugins);
    static bool isAra (const juce::PluginDescription& d) noexcept;
    // Whether `instrument` is an ARA channel's instrument (made by instrumentFor)
    static bool isAraInstrument (const Instrument* instrument) noexcept;

    // Brings the documents in line with `song` (its ARA channels, their clips, the tempo): creates what is new, updates
    // what moved, removes what went. The first time a document is made for a plugin, the song's archive of it
    // (Song::araDocuments) is given back to the plugin. Message thread, before the rack is built from `song`.
    void sync (const Song& song, double sampleRate);
    // A new project: every document and instance goes (the instruments still playing keep theirs until the rack drops
    // them). Message thread.
    void reset();

    // The instrument of an ARA channel (the rack's factory; after sync). nullptr: the plugin is missing or failed (the
    // rack then plays the channel's clips as usual).
    std::shared_ptr<Instrument> instrumentFor (const ChannelDef& channel, double sampleRate);
    // The plugin instance of an ARA channel (its editor; nullptr: none). Message thread.
    juce::AudioPluginInstance* pluginOf (std::uint64_t channelUid);
    // The owner that keeps that instance alive (for a plugin window)
    std::shared_ptr<void> ownerOf (std::uint64_t channelUid);
    // Tells the plugin's editor which regions to show: the clips `clipUids` of the channel (empty: all of the channel's)
    void showInEditor (std::uint64_t channelUid, const std::vector<std::uint64_t>& clipUids = {});
    // Round 190oq (issue #243, an ARA insert): the editor of channel `editorChannel`'s instance shows every region of the
    // channels `channelUids` (one document: the plugin lists each channel as a track)
    void showChannelsInEditor (std::uint64_t editorChannel, const std::vector<std::uint64_t>& channelUids);

    // Every document as the plugin archives it now (for saving). Message thread.
    std::vector<AraDocumentArchive> archives();
    // Whether a plugin is still analysing audio (an export waits for it)
    bool analysing();
    // How many documents / bound instances exist now (tests, diagnostics)
    int documentCount() const;
    int instanceCount() const;
    // The plugin's own transport (Melodyne's play / stop / position, ARA's playback controller): called on the message
    // thread with 0 = start, 1 = stop, 2 = move to `seconds` on the timeline
    std::function<void (int request, double seconds)> onPlaybackRequest;
    // The last problem (a plugin that failed to load or has no ARA), for the UI; empty = none
    juce::String lastError() const { return error; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::String error;
};
// LYNX --aratest (engine/AraTest.cpp, round 190kx): the ARA hosting end to end with `pluginFile`; "OK" or "ERROR ..."
juce::String araSelfTest (const juce::File& pluginFile, const juce::File& audio, const juce::File& out, const juce::File& project,
                          const std::function<void (const juce::String&)>& log);
} // namespace lynx
