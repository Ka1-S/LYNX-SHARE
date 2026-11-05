#include "ui/FlStudioExportDialog.h"

#include "core/MessageThreadOwned.h"
#include "engine/Exporter.h"
#include "engine/FlStudioExport.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
juce::String trText (const juce::String& s) { return tr (s); }

// a render's job: made and deleted on the message thread (plugins), a Cancel never waits for it (ExportDialog.cpp)
struct RenderJob : MessageThreadOwned<ExportJob>
{
    explicit RenderJob (juce::Thread& t)
        : MessageThreadOwned<ExportJob> ([] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); },
                                         [&t] { return t.threadShouldExit(); })
    {
    }
};

// the report: what went over exactly, rendered, substituted and left out
class ReportPanel final : public juce::Component
{
public:
    ReportPanel (const juce::File& target, const juce::String& text)
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
        body.setText (text.isEmpty() ? tr ("Everything went over as FL Studio's own data.") : text, false);
        addAndMakeVisible (body);
        show.onClick = [this] { file.revealToUser(); };
        close.onClick = [this] {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
        };
        addAndMakeVisible (show);
        addAndMakeVisible (close);
        setSize (620, 440);
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

void showReport (const juce::File& target, const flstudio::Written& written)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export to FL Studio");
    options.content.setOwned (fitContentToScreen (std::make_unique<ReportPanel> (target, flstudio::reportText (written, trText))).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

class FlExportThread final : public juce::ThreadWithProgressWindow
{
public:
    FlExportThread (AudioEngine& engine, flpexport::LynxOnly mode, juce::File t, bool z)
        : juce::ThreadWithProgressWindow (tr ("Exporting to FL Studio..."), true, true),
          song (engine.getSong()),
          options (flstudio::optionsFor (&engine.getPluginHost(), mode, song)),
          target (std::move (t)),
          zip (z),
          sampleRate (engine.getTransport().sampleRate.load())
    {
        // option A's renders: next to the .flp (FL opens them from there); for the package a temporary folder (they go
        // into the .zip)
        renderDir = zip ? juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("LYNX FL export", {}, false)
                        : target.getSiblingFile (target.getFileNameWithoutExtension() + " audio");
    }

    void run() override
    {
        auto render = [this] (const Song& s, const ExportSettings& settings, const juce::File& wav, const std::function<bool (double)>& progress,
                              juce::String& err) {
            RenderJob slot (*this);
            auto* job = slot.create ([&] { return std::make_unique<ExportJob> (s, settings, wav); });
            if (job == nullptr)
                return false;
            const auto ok = job->run (progress, err);
            slot.release();
            return ok;
        };
        ok = flstudio::exportProject (song, options, target, zip, renderDir, sampleRate, render,
                                      [this] (double p, const juce::String& what) {
                                          setProgress (p);
                                          setStatusMessage (what);
                                          return ! threadShouldExit();
                                      },
                                      written, error);
    }

    void threadComplete (bool cancelled) override
    {
        if (zip)
            renderDir.deleteRecursively();
        if (ok)
            showReport (target, written);
        else if (! cancelled)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    Song song;
    flpexport::Options options;
    juce::File target, renderDir;
    bool zip;
    double sampleRate;
    flstudio::Written written;
    juce::String error;
    bool ok = false;
};

class FlExportPanel final : public juce::Component
{
public:
    FlExportPanel (AudioEngine& e, juce::File folder, juce::String name, bool live)
        : engine (e), suggestedFolder (std::move (folder)), projectName (std::move (name)), canExport (live)
    {
        // (the plugin lookups once: option A's plan)
        renders = flpexport::planRenders (engine.getSong(), flstudio::optionsFor (&engine.getPluginHost(), flpexport::LynxOnly::renderAudio, engine.getSong()));
        auto heading = [this] (juce::Label& l, const juce::String& text) {
            l.setText (text, juce::dontSendNotification);
            l.setFont (theme::font (theme::type::body, true));
            l.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            addAndMakeVisible (l);
        };
        heading (formatHeading, tr ("Save as"));
        heading (lynxHeading, tr ("What FL Studio doesn't have"));
        for (auto* b : { &flpButton, &zipButton })
        {
            b->setRadioGroupId (17101);
            b->onClick = [this] { update(); };
            addAndMakeVisible (b);
        }
        for (auto* b : { &renderButton, &substituteButton })
        {
            b->setRadioGroupId (17102);
            b->onClick = [this] { update(); };
            addAndMakeVisible (b);
        }
        (lastZip ? zipButton : flpButton).setToggleState (true, juce::dontSendNotification);
        (lastSubstitute ? substituteButton : renderButton).setToggleState (true, juce::dontSendNotification);
        plan.setFont (theme::font (theme::type::small));
        plan.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        plan.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (plan);
        exportButton.onClick = [this] { chooseFile(); };
        cancelButton.onClick = [this] { closeWindow(); };
        exportButton.setEnabled (canExport);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        update();
        setSize (520, 330);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        formatHeading.setBounds (r.removeFromTop (22));
        flpButton.setBounds (r.removeFromTop (24));
        zipButton.setBounds (r.removeFromTop (24));
        r.removeFromTop (10);
        lynxHeading.setBounds (r.removeFromTop (22));
        renderButton.setBounds (r.removeFromTop (24));
        substituteButton.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (28);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (120));
        r.removeFromTop (8);
        r.removeFromBottom (8);
        plan.setBounds (r);
    }

private:
    flpexport::LynxOnly mode() const { return substituteButton.getToggleState() ? flpexport::LynxOnly::substitute : flpexport::LynxOnly::renderAudio; }

    void update()
    {
        lastZip = zipButton.getToggleState();
        lastSubstitute = substituteButton.getToggleState();
        juce::String text;
        if (mode() == flpexport::LynxOnly::renderAudio)
            text = renders.empty() ? tr ("Nothing needs rendering: everything goes over as FL Studio's own data.")
                                   : tr ("{0} channel(s) and {1} mixer insert(s) will be rendered to audio first.",
                                         { juce::String ((int) renders.channels.size()), juce::String ((int) renders.inserts.size()) });
        else
            text = tr ("LYNX-only effects become the nearest FL plugin where there is one; LYNX-only instruments are left out.");
        text << "\n" << tr ("A report lists what went over exactly, what was rendered, replaced or left out.");
        plan.setText (text, juce::dontSendNotification);
    }

    void chooseFile()
    {
        const auto zip = zipButton.getToggleState();
        const auto ext = zip ? juce::String (".zip") : juce::String (".flp");
        const auto start = (lastFolder.isDirectory() ? lastFolder : suggestedFolder).getChildFile (juce::File::createLegalFileName (projectName) + ext);
        chooser = std::make_unique<juce::FileChooser> (tr ("Export to FL Studio"), start, "*" + ext);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<FlExportPanel> (this), zip, ext] (const juce::FileChooser& c) {
                                  auto file = c.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  file = file.withFileExtension (ext);
                                  lastFolder = file.getParentDirectory();
                                  auto& engine = safe->engine;
                                  const auto mode = safe->mode();
                                  safe->closeWindow();
                                  engine.capturePluginStates();
                                  (new FlExportThread (engine, mode, file, zip))->launchThread();
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
    flpexport::RenderPlan renders;
    juce::Label formatHeading, lynxHeading, plan;
    juce::ToggleButton flpButton { tr ("FL Studio project (.flp)") },
        zipButton { tr ("Zipped loop package (.zip, with every sample)") },
        renderButton { tr ("Render them to audio (sounds the same)") },
        substituteButton { tr ("Use the nearest FL plugin (stays editable, may sound different)") };
    juce::TextButton exportButton { tr ("Export...") }, cancelButton { tr ("Cancel") };
    std::unique_ptr<juce::FileChooser> chooser;

    static inline juce::File lastFolder;
    static inline bool lastZip = false, lastSubstitute = false;
};
} // namespace

void showFlStudioExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr ("Export to FL Studio");
    options.content.setOwned (fitContentToScreen (std::make_unique<FlExportPanel> (engine, suggestedFolder, projectName.isNotEmpty() ? projectName : "LYNX SHARE", true)).release());
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

std::unique_ptr<juce::Component> makeFlStudioExportPanel (AudioEngine& engine)
{
    return std::make_unique<FlExportPanel> (engine, juce::File(), "LYNX SHARE", false);
}
} // namespace lynx
