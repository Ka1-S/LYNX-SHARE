#include "ui/DawProjectExport.h"

#include <map>
#include <thread>

#include "core/DawProject.h"
#include "engine/Mp3Format.h"
#include "engine/ProjectFile.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

// a sample bigger than this is referenced where it is instead of embedded (DAWproject's external="true")
constexpr juce::int64 maxEmbeddedBytes = (juce::int64) 512 << 20;
} // namespace

namespace
{
using PluginRecords = std::map<std::string, std::optional<ForeignPlugin>>;

std::string recordKey (const PluginRef& ref, bool instrument)
{
    return (instrument ? "i\x1f" : "e\x1f") + ref.id + "\x1f" + ref.state;
}

// (round 190ri) the hosted plugins' records, read from the host on the message thread so the file can be written on a worker
PluginRecords collectPluginRecords (const Song& song, PluginHost& host)
{
    PluginRecords records;
    const auto add = [&] (const PluginRef& ref, bool instrument) {
        if (ref.foreign.format != ForeignPlugin::Format::none || ref.id.empty())
            return;
        const auto key = recordKey (ref, instrument);
        if (records.find (key) == records.end())
            records.emplace (key, host.flWrapperRecord (ref, instrument));
    };
    std::function<void (const std::vector<EffectSlot>&)> chain = [&] (const std::vector<EffectSlot>& slots) {
        for (const auto& slot : slots)
        {
            if (slot.kind == EffectKind::plugin)
                add (slot.plugin, false);
            for (const auto& branch : slot.branches)
                chain (branch.chain);
        }
    };
    for (const auto& channel : song.channels)
        if (channel.instrument == InstrumentKind::plugin)
            add (channel.plugin, true);
    for (const auto& track : song.mixer)
        chain (track.effects);
    for (const auto& track : song.playlistTracks)
        for (const auto& version : track.versions)
            chain (version.chain);
    return records;
}

bool writeWith (const Song& song, std::function<std::optional<ForeignPlugin> (const PluginRef&, bool)> pluginRecord, const juce::File& target,
                juce::StringArray& notes, juce::String& error);
} // namespace

bool writeDawProjectFile (const Song& song, PluginHost* host, const juce::File& target, juce::StringArray& notes, juce::String& error)
{
    std::function<std::optional<ForeignPlugin> (const PluginRef&, bool)> records;
    if (host != nullptr)
        records = [host] (const PluginRef& ref, bool instrument) { return host->flWrapperRecord (ref, instrument); };
    return writeWith (song, std::move (records), target, notes, error);
}

namespace
{
bool writeWith (const Song& song, std::function<std::optional<ForeignPlugin> (const PluginRef&, bool)> pluginRecord, const juce::File& target,
                juce::StringArray& notes, juce::String& error)
{
    juce::AudioFormatManager formats;
    registerAudioFormats (formats);
    std::map<std::string, std::tuple<double, int, int>> seen;

    DawProjectExportOptions options;
    options.application = "LYNX SHARE";
    options.version = JUCE_APPLICATION_VERSION_STRING;
    options.readFile = [] (const std::string& path) {
        const juce::File file (fromUtf8 (path));
        juce::MemoryBlock data;
        if (! file.existsAsFile() || file.getSize() > maxEmbeddedBytes || ! file.loadFileAsData (data))
            return std::vector<std::uint8_t> {};
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
    };
    options.sampleFormat = [&formats, &seen] (const std::string& path, double& seconds, int& sampleRate, int& channels) {
        auto it = seen.find (path);
        if (it == seen.end())
        {
            std::tuple<double, int, int> f { 0.0, 0, 0 };
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (fromUtf8 (path))) }; reader != nullptr && reader->sampleRate > 0)
                f = { (double) reader->lengthInSamples / reader->sampleRate, (int) reader->sampleRate, (int) reader->numChannels };
            it = seen.emplace (path, f).first;
        }
        std::tie (seconds, sampleRate, channels) = it->second;
        return seconds > 0.0 && sampleRate > 0;
    };
    // LYNX's own devices with every field (the project file's JSON; core/DawProject.h's default keeps less)
    options.nativeInstrumentState = [] (const ChannelDef& channel) { return ProjectFile::channelStateBytes (channel); };
    options.nativeEffectState = [] (const EffectSlot& slot) { return ProjectFile::effectStateBytes (slot); };
    // loaded plugins: their records (ids and state) from the host, as the .als and .flp exports do
    options.pluginRecord = std::move (pluginRecord);

    // round 190ri (kainproctor: "Doesn't work"): every failure says so — an exception in the writer (memory, a sample that
    // cannot be read, the zip) was not caught and the window never came
    DawProjectExportResult result;
    try
    {
        result = exportDawProject (song, options);
    }
    catch (const std::exception& e)
    {
        error = tr ("The DAWproject file could not be written") + ": " + juce::String::fromUTF8 (e.what());
        return false;
    }
    catch (...)
    {
        error = tr ("The DAWproject file could not be written");
        return false;
    }
    for (const auto& n : result.notes)
        notes.add (fromUtf8 (n));
    if (result.bytes.empty())
    {
        error = tr ("The DAWproject file could not be written");
        return false;
    }
    if (target.getParentDirectory().hasWriteAccess() == false || ! target.replaceWithData (result.bytes.data(), result.bytes.size()))
    {
        error = tr ("Could not write {0}", { target.getFullPathName() });
        return false;
    }
    return true;
}

// Round 190ri (kainproctor: "Doesn't work"): the file is written on a worker behind a progress window. Every sample is read
// and the whole zip is made in memory: an FL import with a gigabyte of audio froze the window for seconds (7 s for 1 GB
// here) with nothing on screen, and a failure then (out of memory) went uncaught.
class DawProjectThread final : public juce::ThreadWithProgressWindow
{
public:
    DawProjectThread (Song s, PluginRecords r, juce::File t, juce::Component* p)
        : juce::ThreadWithProgressWindow (tr ("Export DAWproject"), true, false), song (std::move (s)), records (std::move (r)), target (std::move (t)), parent (p)
    {
        setStatusMessage (tr ("Writing {0}...", { target.getFileName() }));
    }
    void run() override
    {
        setProgress (-1.0); // (no steps to count: a moving bar)
        ok = writeWith (song, [this] (const PluginRef& ref, bool instrument) -> std::optional<ForeignPlugin> {
                            const auto it = records.find (recordKey (ref, instrument));
                            return it != records.end() ? it->second : std::nullopt;
                        },
                        target, notes, error);
    }
    void threadComplete (bool) override
    {
        report (ok, target, notes, error, parent.getComponent());
        juce::MessageManager::callAsync ([this] { delete this; });
    }
    static void report (bool ok, const juce::File& target, const juce::StringArray& notes, const juce::String& error, juce::Component* parent)
    {
        if (! ok)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Export DAWproject"), error, {}, parent);
            return;
        }
        // the report: what had no counterpart (at most 30 lines here; the count of the rest)
        juce::String text = tr ("{0} saved.", { target.getFileName() });
        if (notes.isEmpty())
            text << "\n\n" << tr ("Everything went over to the DAWproject file.");
        else
        {
            text << "\n\n" << tr ("Not carried over or approximated:");
            for (int i = 0; i < juce::jmin (30, notes.size()); ++i)
                text << "\n- " << notes[i];
            if (notes.size() > 30)
                text << "\n" << tr ("... and {0} more", { juce::String (notes.size() - 30) });
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Export DAWproject"), text, {}, parent);
    }

private:
    Song song;
    PluginRecords records;
    juce::File target;
    juce::Component::SafePointer<juce::Component> parent;
    bool ok = false;
    juce::StringArray notes;
    juce::String error;
};
} // namespace

bool writeDawProjectOnWorkerForTests (AudioEngine& engine, const juce::File& target, juce::StringArray& notes, juce::String& error)
{
    auto records = collectPluginRecords (engine.getSong(), engine.getPluginHost());
    const Song song = engine.getSong();
    bool ok = false;
    std::thread worker ([&] {
        ok = writeWith (song, [&records] (const PluginRef& ref, bool instrument) -> std::optional<ForeignPlugin> {
                            const auto it = records.find (recordKey (ref, instrument));
                            return it != records.end() ? it->second : std::nullopt;
                        },
                        target, notes, error);
    });
    worker.join();
    return ok;
}

void showDawProjectExport (AudioEngine& engine, juce::Component* parent, const juce::File& suggestedFolder, const juce::String& projectName)
{
    const auto stem = juce::File::createLegalFileName (projectName.isNotEmpty() ? projectName : juce::String ("LYNX SHARE"));
    auto chooser = std::shared_ptr<juce::FileChooser> (
        ui::makeFileChooser (tr ("Export DAWproject"), suggestedFolder.getChildFile (stem + ".dawproject"), "*.dawproject").release());
    ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                       [&engine, safe = juce::Component::SafePointer<juce::Component> (parent)] (const juce::FileChooser& c) {
                           auto target = c.getResult();
                           if (target == juce::File())
                               return;
                           if (! target.hasFileExtension ("dawproject"))
                               target = target.withFileExtension ("dawproject");
                           engine.capturePluginStates();
                           // (round 190ri) a copy of the song and the plugins' records on this thread; the file on a worker
                           auto records = collectPluginRecords (engine.getSong(), engine.getPluginHost());
                           (new DawProjectThread (engine.getSong(), std::move (records), target, safe.getComponent()))->launchThread();
                       });
}
} // namespace lynx
