#include "ui/DawProjectExport.h"
#include "engine/SafeWrite.h"

#include <map>

#include "core/DawProject.h"
#include "engine/Mp3Format.h"
#include "engine/ProjectFile.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/AlsExportDialog.h" // (round 190rm: samples other apps cannot read, as WAV)

namespace lynx
{
namespace
{
juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

// a sample bigger than this is referenced where it is instead of embedded (DAWproject's external="true")
constexpr juce::int64 maxEmbeddedBytes = (juce::int64) 512 << 20;
} // namespace

bool writeDawProjectFile (const Song& song, PluginHost* host, const juce::File& target, juce::StringArray& notes, juce::String& error)
{
    juce::AudioFormatManager formats;
    registerAudioFormats (formats);
    std::map<std::string, std::tuple<double, int, int>> seen;

    DawProjectExportOptions options;
    options.application = "LYNX SHARE";
    options.version = JUCE_APPLICATION_VERSION_STRING;
    // (round 190rm) a sample other apps cannot read (FL's Ogg-in-WAV and WavPack samples, a raw Ogg named .wav...) goes in
    // as the 32-bit float WAV LYNX plays from it, under "<name>.wav": Bitwig, Studio One and Cubase read PCM / float WAV,
    // AIFF, FLAC, MP3 and Ogg files, not those. Before, it went in as it was, with 0 Hz / 0 channels / 0 s on its Audio
    // element when JUCE's readers could not open it either (401 of Thief's 1,351 clips), and stretched ones unstretched.
    struct Decoded
    {
        bool needs = false;
        juce::MemoryBlock wav;
        double seconds = 0.0;
        int rate = 0, channels = 0;
    };
    std::map<std::string, Decoded> decoded;
    const auto decodedFor = [&decoded] (const std::string& path) -> Decoded& {
        auto it = decoded.find (path);
        if (it == decoded.end())
        {
            Decoded d;
            const juce::File file (fromUtf8 (path));
            d.needs = file.existsAsFile() && sampleNeedsWavForOtherApps (file);
            if (d.needs)
                d.wav = decodedWavForOtherApps (file, d.seconds, d.rate, d.channels);
            it = decoded.emplace (path, std::move (d)).first;
        }
        return it->second;
    };
    options.readFile = [decodedFor] (const std::string& path) {
        const juce::File file (fromUtf8 (path));
        if (auto& d = decodedFor (path); d.needs)
        {
            if (d.wav.getSize() == 0 || (juce::int64) d.wav.getSize() > maxEmbeddedBytes)
                return std::vector<std::uint8_t> {};
            const auto* bytes = static_cast<const std::uint8_t*> (d.wav.getData());
            std::vector<std::uint8_t> out (bytes, bytes + d.wav.getSize());
            d.wav.reset(); // (embedded once: the export keeps the entry's path)
            return out;
        }
        juce::MemoryBlock data;
        if (! file.existsAsFile() || file.getSize() > maxEmbeddedBytes || ! file.loadFileAsData (data))
            return std::vector<std::uint8_t> {};
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
    };
    options.embeddedName = [decodedFor] (const std::string& path) {
        const juce::File file (fromUtf8 (path));
        return (decodedFor (path).needs ? file.getFileNameWithoutExtension() + ".wav" : file.getFileName()).toStdString();
    };
    options.sampleFormat = [&formats, &seen, decodedFor] (const std::string& path, double& seconds, int& sampleRate, int& channels) {
        auto it = seen.find (path);
        if (it == seen.end())
        {
            std::tuple<double, int, int> f { 0.0, 0, 0 };
            if (const auto& d = decodedFor (path); d.needs && d.rate > 0)
                f = { d.seconds, d.rate, d.channels };
            else if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (fromUtf8 (path))) }; reader != nullptr && reader->sampleRate > 0)
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
    if (host != nullptr)
        options.pluginRecord = [host] (const PluginRef& ref, bool instrument) { return host->flWrapperRecord (ref, instrument); };

    const auto result = exportDawProject (song, options);
    for (const auto& n : result.notes)
        notes.add (fromUtf8 (n));
    if (result.bytes.empty())
    {
        error = tr ("The DAWproject file could not be written");
        return false;
    }
    // (round 190rm: the checked writer: a full disk left a cut .dawproject that "exported" before)
    if (juce::String why; ! safewrite::writeData (target, result.bytes.data(), result.bytes.size(), why))
    {
        error = tr ("Not written: {0}. {1} was not changed.", { tr (why), target.getFileName() });
        return false;
    }
    return true;
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
                           juce::StringArray notes;
                           juce::String error;
                           if (! writeDawProjectFile (engine.getSong(), &engine.getPluginHost(), target, notes, error))
                           {
                               juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Export DAWproject"), error,
                                                                       {}, safe.getComponent());
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
                           juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Export DAWproject"), text, {},
                                                                   safe.getComponent());
                       });
}
} // namespace lynx
