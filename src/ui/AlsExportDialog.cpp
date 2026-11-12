#include "ui/AlsExportDialog.h"

#include <map>

#include "core/AlsExport.h"
#include "engine/Mp3Format.h"
#include "engine/SampleLibrary.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

// The report after a write: where the set went and what had no counterpart in it
class AlsReportPanel final : public juce::Component
{
public:
    AlsReportPanel (const juce::File& target, const juce::String& text)
        : file (target)
    {
        title.setText (tr ("{0} saved.", { target.getFileName() }), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::body, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);
        body.setMultiLine (true);
        body.setReadOnly (true);
        body.setScrollbarsShown (true);
        body.setCaretVisible (false);
        body.setFont (theme::font (theme::type::small));
        body.setText (text.isEmpty() ? tr ("Everything went over to the Live set.") : text, false);
        addAndMakeVisible (body);
        show.onClick = [this] { file.revealToUser(); };
        close.onClick = [this] {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
        };
        addAndMakeVisible (show);
        addAndMakeVisible (close);
        setSize (560, 360);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (28);
        close.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        show.setBounds (buttons.removeFromRight (140));
        r.removeFromBottom (8);
        r.removeFromTop (4);
        body.setBounds (r);
    }

private:
    juce::File file;
    juce::Label title;
    juce::TextEditor body;
    juce::TextButton show { tr ("Show in folder") }, close { tr ("Close") };
};

void showAlsReport (const juce::File& target, const juce::String& text)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export Live set");
    options.content.setOwned (fitContentToScreen (std::make_unique<AlsReportPanel> (target, text)).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace

namespace
{
// A sample Live cannot read as it is (core alsSampleNeedsWav): FL's WavPack (.wv), Ogg Vorbis inside a WAV file, a raw
// Ogg Vorbis stream named .wav (FL ships many), or any other file type LYNX decodes that Live doesn't
bool needsWavForLive (const juce::File& file)
{
    juce::MemoryBlock head;
    if (juce::FileInputStream in (file); in.openedOk())
        in.readIntoMemoryBlock (head, 4096);
    return alsSampleNeedsWav (file.getFileExtension().toStdString(), static_cast<const std::uint8_t*> (head.getData()), head.getSize());
}

// Decodes `source` the way LYNX plays it and writes it as a 32-bit float WAV
bool writeWavForLive (const juce::File& source, const juce::File& target)
{
    SampleLibrary library;
    const auto data = library.load (source.getFullPathName());
    if (data == nullptr || data->frames() <= 0 || data->sampleRate <= 0.0)
        return false;
    const auto channels = data->isStereo() ? 2 : 1;
    std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), data->sampleRate, (unsigned int) channels, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns it now
    const float* planes[2] { data->leftData(), channels > 1 ? data->rightData() : nullptr };
    return writer->writeFromFloatArrays (planes, channels, (int) data->frames());
}
} // namespace

// Writes "<folder>/<name> Project/<name>.als"; the samples are copied into "Samples/Imported" when `collect` is set.
// Returns the .als written, or an empty File with `error` set.
juce::File writeAlsProject (const Song& song, PluginHost* host, AlsVersion version, bool collect, const juce::File& folder, const juce::String& name,
                            juce::StringArray& notes, juce::String& error)
{
    const auto stem = juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("LYNX SHARE"));
    // Live keeps (and saves) a set only inside a "<name> Project" folder (ALS_FORMAT §9)
    const auto project = folder.getFileName().endsWithIgnoreCase (" Project") ? folder : folder.getChildFile (stem + " Project");
    if (! project.createDirectory())
    {
        error = tr ("Could not create the folder {0}", { project.getFullPathName() });
        return {};
    }
    const auto samples = project.getChildFile ("Samples").getChildFile ("Imported");

    juce::AudioFormatManager formats;
    registerAudioFormats (formats);
    std::map<std::string, std::string> copied; // source path -> path in the project
    std::map<std::string, double> lengths;

    AlsExportOptions options;
    options.version = version;
    options.creator = "LYNX SHARE";
    options.sampleSeconds = [&] (const std::string& path) {
        if (auto it = lengths.find (path); it != lengths.end())
            return it->second;
        double seconds = 0.0;
        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (fromUtf8 (path))) }; reader != nullptr && reader->sampleRate > 0)
            seconds = (double) reader->lengthInSamples / reader->sampleRate;
        return lengths[path] = seconds;
    };
    std::map<std::string, std::pair<double, double>> formatsSeen; // (round 190ec: a Drum Rack pad's Simpler region)
    options.sampleFormat = [&] (const std::string& path, double& frames, double& rate) {
        auto it = formatsSeen.find (path);
        if (it == formatsSeen.end())
        {
            std::pair<double, double> f { 0.0, 0.0 };
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (fromUtf8 (path))) }; reader != nullptr && reader->sampleRate > 0)
                f = { (double) reader->lengthInSamples, reader->sampleRate };
            it = formatsSeen.emplace (path, f).first;
        }
        frames = it->second.first;
        rate = it->second.second;
        return frames > 0.0 && rate > 0.0;
    };
    options.samplePath = [&] (const std::string& path, bool& relative) -> std::string {
        relative = false;
        const juce::File source (fromUtf8 (path));
        // a sample Live cannot read is converted even when the set refers to the samples where they are
        if (! source.existsAsFile() || (! collect && ! needsWavForLive (source)))
            return path;
        if (auto it = copied.find (path); it != copied.end())
        {
            relative = true;
            return it->second;
        }
        samples.createDirectory();
        // Live reads WAV (PCM / float), AIFF, FLAC, Ogg and MP3 only: FL's WavPack (.wv) and Ogg-in-WAV samples are
        // written as plain WAV (Live 12.4: "Some of the media files could not be loaded", "Unknown unsupported format
        // 26447 for WAV files", round 175a)
        if (needsWavForLive (source))
        {
            auto target = samples.getChildFile (source.getFileNameWithoutExtension() + ".wav");
            if (target.existsAsFile())
                target = samples.getNonexistentChildFile (source.getFileNameWithoutExtension(), ".wav", false);
            if (! writeWavForLive (source, target))
            {
                notes.add (tr ("Could not copy {0}; the set refers to it where it is", { source.getFullPathName() }));
                return path;
            }
            relative = true;
            return copied[path] = target.getRelativePathFrom (project).replaceCharacter ('\\', '/').toStdString();
        }
        auto target = samples.getChildFile (source.getFileName());
        if (target.existsAsFile() && target.getSize() != source.getSize())
            target = samples.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), false);
        if (! target.existsAsFile() && ! source.copyFileTo (target))
        {
            notes.add (tr ("Could not copy {0}; the set refers to it where it is", { source.getFullPathName() }));
            return path;
        }
        relative = true;
        return copied[path] = target.getRelativePathFrom (project).replaceCharacter ('\\', '/').toStdString();
    };

    // loaded plugins: their records (ids and state) from the host, as the FL Studio export does
    if (host != nullptr)
        options.pluginRecord = [host] (const PluginRef& ref, bool instrument) { return host->flWrapperRecord (ref, instrument); };

    const auto result = exportAls (song, options);
    for (const auto& n : result.notes)
        notes.add (fromUtf8 (n));
    if (result.bytes.empty())
    {
        error = tr ("The Live set could not be written");
        return {};
    }
    const auto als = project.getChildFile (stem + ".als");
    if (! als.replaceWithData (result.bytes.data(), result.bytes.size()))
    {
        error = tr ("Could not write {0}", { als.getFullPathName() });
        return {};
    }
    return als;
}

namespace
{
class AlsExportPanel final : public juce::Component
{
public:
    AlsExportPanel (AudioEngine& e, juce::File folder, juce::String name, bool live)
        : engine (e), suggestedFolder (std::move (folder)), projectName (std::move (name)), canExport (live)
    {
        auto heading = [this] (juce::Label& l, const juce::String& text) {
            l.setText (text, juce::dontSendNotification);
            l.setFont (theme::font (theme::type::body, true));
            l.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            addAndMakeVisible (l);
        };
        heading (versionHeading, tr ("Save for"));
        heading (samplesHeading, tr ("Samples"));
        for (auto* b : { &live12Button, &live11Button })
        {
            b->setRadioGroupId (17501);
            addAndMakeVisible (b);
        }
        for (auto* b : { &collectButton, &referenceButton })
        {
            b->setRadioGroupId (17502);
            addAndMakeVisible (b);
        }
        (lastLive11 ? live11Button : live12Button).setToggleState (true, juce::dontSendNotification);
        (lastReference ? referenceButton : collectButton).setToggleState (true, juce::dontSendNotification);
        plan.setFont (theme::font (theme::type::small));
        plan.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        plan.setJustificationType (juce::Justification::topLeft);
        plan.setText (tr ("The set is saved as \"<name> Project/<name>.als\". Tracks, clips, notes, mixer levels, sends, "
                          "VST plugins with their state, markers and tempo go over; a report lists the rest."),
                      juce::dontSendNotification);
        addAndMakeVisible (plan);
        exportButton.onClick = [this] { chooseFolder(); };
        cancelButton.onClick = [this] { closeWindow(); };
        exportButton.setEnabled (canExport);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        setSize (520, 330);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        versionHeading.setBounds (r.removeFromTop (22));
        live12Button.setBounds (r.removeFromTop (24));
        live11Button.setBounds (r.removeFromTop (24));
        r.removeFromTop (10);
        samplesHeading.setBounds (r.removeFromTop (22));
        collectButton.setBounds (r.removeFromTop (24));
        referenceButton.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (28);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (120));
        r.removeFromTop (8);
        r.removeFromBottom (8);
        plan.setBounds (r);
    }

private:
    void chooseFolder()
    {
        lastLive11 = live11Button.getToggleState();
        lastReference = referenceButton.getToggleState();
        const auto start = lastFolder.isDirectory() ? lastFolder : suggestedFolder;
        chooser = lynx::ui::makeFileChooser (tr ("Choose the folder for the Live set"), start, {});
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<AlsExportPanel> (this)] (const juce::FileChooser& c) {
                                  const auto folder = c.getResult();
                                  if (safe == nullptr || ! folder.isDirectory())
                                      return;
                                  lastFolder = folder;
                                  auto& engine = safe->engine;
                                  const auto name = safe->projectName;
                                  safe->closeWindow();
                                  engine.capturePluginStates();
                                  juce::StringArray notes;
                                  juce::String error;
                                  const auto als = writeAlsProject (engine.getSong(), &engine.getPluginHost(), lastLive11 ? AlsVersion::live11 : AlsVersion::live12, ! lastReference,
                                                                    folder, name, notes, error);
                                  if (als == juce::File())
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);
                                  else
                                      showAlsReport (als, notes.joinIntoString ("\n"));
                              });
    }

    void closeWindow()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    AudioEngine& engine;
    juce::File suggestedFolder;
    juce::String projectName;
    bool canExport;
    juce::Label versionHeading, samplesHeading, plan;
    juce::ToggleButton live12Button { "Live 12" }, live11Button { tr ("Live 11 (also opens in Live 12)") },
        collectButton { tr ("Copy them into the project folder") }, referenceButton { tr ("Refer to them where they are") };
    juce::TextButton exportButton { tr ("Export...") }, cancelButton { tr ("Cancel") };
    std::unique_ptr<juce::FileChooser> chooser;

    static inline juce::File lastFolder;
    static inline bool lastLive11 = false, lastReference = false;
};
} // namespace

void showAlsExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export Live set");
    options.content.setOwned (fitContentToScreen (std::make_unique<AlsExportPanel> (engine, suggestedFolder, projectName.isNotEmpty() ? projectName : "LYNX SHARE", true)).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

std::unique_ptr<juce::Component> makeAlsExportPanel (AudioEngine& engine)
{
    return std::make_unique<AlsExportPanel> (engine, juce::File(), "LYNX SHARE", false);
}
} // namespace lynx
