#include "ui/DawProjectExport.h"

#include <map>

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

bool writeDawProjectFile (const Song& song, PluginHost* host, const juce::File& target, juce::StringArray& notes, juce::String& error)
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
    if (! target.replaceWithData (result.bytes.data(), result.bytes.size()))
    {
        error = tr ("Could not write {0}", { target.getFullPathName() });
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
