#include "ui/ExportDialog.h"

#include "core/PatternBlocks.h"
#include "core/MessageThreadOwned.h"
#include "core/PlaylistOps.h"
#include "core/PickerSelection.h"
#include "core/QuickRender.h"
#include "core/StandIns.h"
#include "engine/SessionPack.h"
#include "engine/PluginHost.h"
#include "ui/ChannelSettings.h"
#include "ui/PatcherEditor.h"
#include "ui/PluginWindow.h"
#include "core/Mastering.h"
#include "core/EffectPresets.h"
#include "core/PluginPresets.h"
#include "engine/MidiFileIo.h"
#include "engine/ProjectFile.h"
#include "core/HardwareInsert.h"
#include "platform/Paths.h"
#include "ui/FileChoosers.h"
#include "ui/WindowOwner.h"

#include "engine/Exporter.h"
#include "engine/Text.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/SettingsPages.h"
#include "ui/Theme.h"
#include "ui/VideoTracks.h"

#include <thread>

namespace lynx
{
namespace
{
// the job of one stem / playlist track: made and deleted on the message thread, a Cancel never waits for it (round 169g)
struct JobOnMessageThread : MessageThreadOwned<ExportJob>
{
    explicit JobOnMessageThread (juce::Thread& t)
        : MessageThreadOwned<ExportJob> ([] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); },
                                         [&t] { return t.threadShouldExit(); })
    {
    }
};

// round 174d: "Also write a video with this mix" (remembered for the session)
bool& exportWithVideo()
{
    static bool on = false;
    return on;
}

// The exported mix under the project's video, next to the audio file (in the background: a re-encode takes a while)
void writeVideoWithMix (const Song& song, const exportopts::Options& o, const juce::File& audio)
{
    const auto origin = o.range == exportopts::Range::selection && song.hasLoop() ? song.secondsForTicks (song.loopStart) : 0.0;
    const auto out = videotracks::videoFileFor (song, audio);
    ui::postHint ({ tr ("Writing the video..."), out.getFileName() }, 4000);
    std::thread ([song, origin, audio, out] {
        const auto error = videotracks::muxWithVideo (song, origin, audio, out);
        juce::MessageManager::callAsync ([error, out] {
            if (error.isEmpty())
                ui::postHint ({ tr ("Video written") + ": " + out.getFullPathName(), {} }, 8000);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not write the video"), error);
        });
    }).detach();
}

class ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    // onDone (on the UI thread): if empty, shows the result in a message box
    ExportThread (std::unique_ptr<ExportJob> j, juce::File f, std::function<void (bool)> done = {},
                  const juce::String& title = tr ("Exporting audio..."))
        : juce::ThreadWithProgressWindow (title, true, true),
          job (std::move (j)),
          file (std::move (f)),
          onDone (std::move (done))
    {
    }

    void run() override
    {
        ok = job->run ([this] (double p) {
            setProgress (p);
            return ! threadShouldExit();
        }, error);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (onDone)
        {
            if (! ok && ! userPressedCancel)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not render"), error);

            onDone (ok);
        }
        else if (ok && reveal)
            file.revealToUser(); // "Show the file(s) when complete"
        else if (ok && job->getNotice().isEmpty())
            // FL closes its render window and says nothing (round 170h, FL 2026 driven): no box to click away, the
            // result goes to the hint bar for a few seconds; a box stays for a notice that needs reading
            ui::postHint ({ tr ("Export complete") + ": " + file.getFullPathName()
                                + (job->getInfo().isEmpty() ? juce::String() : "  " + job->getInfo()),
                            tr ("Peak: {0} dBFS", { juce::String (juce::Decibels::gainToDecibels (job->getPeak()), 1) }) },
                          8000);
        else if (ok)
            juce::AlertWindow::showMessageBoxAsync (job->getNotice().isEmpty() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                    tr ("Export complete"),
                                                    file.getFullPathName() + "\n\n"
                                                        + tr ("Peak: {0} dBFS", { juce::String (juce::Decibels::gainToDecibels (job->getPeak()), 1) })
                                                        + (job->getInfo().isEmpty() ? juce::String() : "\n" + job->getInfo())
                                                        + (job->getNotice().isEmpty() ? juce::String() : "\n\n" + job->getNotice()));
        else if (! userPressedCancel)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);

        if (ok && then)
            then();
        // the export engine is torn down on the UI thread (plugins require this)
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    std::unique_ptr<ExportJob> job;
    juce::File file;
    std::function<void (bool)> onDone;
public:
    bool reveal = false;
    std::function<void()> then; // after a successful export (round 174d: the video with the mix)
private:
    juce::String error;
    bool ok = false;
};

// Stems: one file per insert that produces sound (FL's "split mixer tracks") or per channel that plays (each heard
// through its mixer path), plus the full mix when asked. Each render creates its own engine; plugins are created and
// destroyed on the UI thread, so the background thread asks it to do that.
class StemsThread final : public juce::ThreadWithProgressWindow
{
public:
    StemsThread (const AudioEngine& e, exportopts::Options o, juce::File base, const std::vector<int>& onlyTracks = {},
                 std::function<void (const std::vector<juce::File>&)> done = {})
        : juce::ThreadWithProgressWindow (exportopts::exportsSections (e.getSong(), o) && o.stems == exportopts::Stems::none ? tr ("Exporting sections...")
                                                                                                                         : tr ("Exporting stems..."),
                                          true, true),
          afterRender (std::move (done)),
          options (o),
          baseFile (std::move (base)),
          song (e.getSong())
    {
        // round 190ap: "One file per marker section" — each section is rendered as a time selection of the song, alone
        // ("<name> - 02 Verse.wav") or with its stems ("<name> - 02 Verse - 3 Bass.wav")
        if (exportopts::exportsSections (song, options))
        {
            sectioned = true;
            const auto ext = exportopts::extension (options.format);
            const auto sections = exportopts::markerSections (song);
            const auto each = exportopts::sectionOptions (options);
            const auto baseName = baseFile.getFileNameWithoutExtension().toStdString();
            for (std::size_t i = 0; i < sections.size(); ++i)
            {
                const auto name = exportopts::sectionBaseName (baseName, (int) i + 1, (int) sections.size(), sections[i].name);
                const auto file = baseFile.getSiblingFile (juce::String::fromUTF8 (name.c_str()) + ext);
                const auto part = exportopts::makeSectionSong (song, sections[i]);
                if (each.stems == exportopts::Stems::none)
                    items.push_back ({ juce::String::fromUTF8 (sections[i].name.c_str()), file, part, each });
                else
                    addItems (part, each, file, onlyTracks, juce::String::fromUTF8 (sections[i].name.c_str()) + ": ");
            }
            return;
        }
        addItems (song, options, baseFile, onlyTracks, {});
    }

    // the files of one render set: the full mix when asked, then one per channel or insert
    void addItems (const Song& from, const exportopts::Options& o, const juce::File& base, const std::vector<int>& onlyTracks, const juce::String& prefix)
    {
        const auto ext = exportopts::extension (o.format);
        const auto baseName = base.getFileNameWithoutExtension().toStdString();
        auto file = [&] (int number, const std::string& name) {
            return base.getSiblingFile (juce::String::fromUTF8 (exportopts::stemFileName (baseName, number, name, ext).c_str()));
        };

        const auto set = (int) sets.size();
        if (o.tail == exportopts::TailMode::leave) // (cut / wrap: every file is the music's length already)
            sets.push_back ({ from, o });
        const auto first = items.size();
        if (o.stemsIncludeMaster)
            items.push_back ({ prefix + juce::String ("Master"), base, from, o, -1, true });

        if (o.stems == exportopts::Stems::channels || o.stems == exportopts::Stems::channelsDry)
            for (auto c : exportopts::stemChannels (from, o.range))
                items.push_back ({ prefix + juce::String::fromUTF8 (from.channels[(std::size_t) c].name.c_str()), file (c + 1, from.channels[(std::size_t) c].name),
                                   exportopts::makeChannelStemSong (from, c, o.stems == exportopts::Stems::channelsDry), o });
        else
            for (auto t : exportopts::stemTracks (from, onlyTracks))
                items.push_back ({ prefix + juce::String::fromUTF8 (from.mixer[(std::size_t) t].name.c_str()),
                                   ! onlyTracks.empty() && recordToFile (from, t) != juce::File() ? recordToFile (from, t).withFileExtension (ext)
                                                                                                  : file (t, from.mixer[(std::size_t) t].name),
                                   makeBounceSong (from, t), o });
        if (o.tail == exportopts::TailMode::leave)
            for (auto i = first; i < items.size(); ++i)
                items[i].set = set;
    }

    // FL's "Record <insert> to" file of an insert (round 167a): a track render writes there; none = File()
    static juce::File recordToFile (const Song& song, int t)
    {
        if (t <= 0 || t >= (int) song.mixer.size())
            return {};
        const auto path = juce::String::fromUTF8 (song.mixer[(std::size_t) t].recordTo.c_str());
        return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    void run() override
    {
        for (std::size_t i = 0; i < items.size() && ! threadShouldExit(); ++i)
        {
            auto& item = items[i];
            setStatusMessage (item.name);

            // round 190gh (B7): the set's length, from its mix (exported first when asked, else rendered once to find it)
            std::int64_t exact = 0;
            if (item.set >= 0 && ! item.mix)
            {
                auto& set = sets[(std::size_t) item.set];
                if (set.frames < 0)
                    set.frames = probeLength (set);
                exact = juce::jmax<std::int64_t> (0, set.frames);
            }

            JobOnMessageThread slot (*this);
            auto* job = slot.create ([&item, exact] {
                auto j = std::make_unique<ExportJob> (item.song, item.options, item.file);
                j->setExtraData (true);
                j->setExactFrames (exact);
                return j;
            });
            if (job == nullptr)
                break; // (cancelled)

            juce::String error;
            const auto base = (double) i / (double) items.size();
            const auto ok = job->run ([this, base] (double p) {
                setProgress (base + p / (double) items.size());
                return ! threadShouldExit();
            }, error);

            if (ok)
            {
                ++written;
                files.push_back (item.file);
                if (item.mix && item.set >= 0)
                    sets[(std::size_t) item.set].frames = job->getWrittenFrames();
            }

            slot.release();
        }
    }

    // the length of a set's mix (no file kept): a render to a temporary file
    std::int64_t probeLength (const RenderSet& set)
    {
        setStatusMessage (tr ("Measuring the length of the mix..."));
        auto o = set.options;
        o.format = exportopts::Format::wav;
        o.moreFormats = 0;
        o.wavBits = 16;
        const juce::TemporaryFile temp (".wav");
        JobOnMessageThread slot (*this);
        const auto& mix = set.mix;
        auto* job = slot.create ([&mix, &o, &temp] { return std::make_unique<ExportJob> (mix, o, temp.getFile()); });
        if (job == nullptr)
            return 0;
        juce::String error;
        const auto frames = job->run ([this] (double) { return ! threadShouldExit(); }, error) ? job->getWrittenFrames() : 0;
        slot.release();
        return frames;
    }

    void threadComplete (bool cancelled) override
    {
        if (! cancelled && written > 0 && afterRender)
            afterRender (files);
        if (! cancelled && options.showWhenDone && written > 0)
            baseFile.getParentDirectory().startAsProcess();
        else if (! cancelled && written > 0)
            ui::postHint ({ (sectioned ? tr ("Sections exported") : tr ("Stems exported")) + ": "
                                + tr ("{0} file(s) in {1}", { juce::String (written), baseFile.getParentDirectory().getFullPathName() }),
                            {} },
                          8000);
        else if (! cancelled)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, sectioned ? tr ("Sections exported") : tr ("Stems exported"),
                                                    tr ("{0} file(s) in {1}", { juce::String (written), baseFile.getParentDirectory().getFullPathName() }));

        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    struct Item
    {
        juce::String name;
        juce::File file;
        Song song;
        exportopts::Options options; // (a section is rendered as a selection of the song, round 190ap)
        int set = -1;                // round 190gh (B7): the render set (a stems export, or one section's) it belongs to
        bool mix = false;            // the set's full mix ("Master")
    };
    // round 190gh (B7): every file of one stems export has the mix's length, as FL's split render (the tail ends where
    // the mix's tail ends): the set's mix and its options, to find that length when the mix itself is not exported
    struct RenderSet
    {
        Song mix;
        exportopts::Options options;
        std::int64_t frames = -1; // found: the mix's length after the trim
    };
    std::vector<RenderSet> sets;
    bool sectioned = false;
    std::function<void (const std::vector<juce::File>&)> afterRender;
    exportopts::Options options;
    juce::File baseFile;
    Song song;
    std::vector<Item> items;
    std::vector<juce::File> files; // written
    int written = 0;
};

juce::File exportSettingsFile()
{
    return lynx::platform::settingsFile ("export-settings.txt");
}

// round 190ap: the named export presets, and what the last export was made with and where it went
juce::File exportPresetsFile() { return lynx::platform::settingsFile ("export-presets.txt"); }
juce::File exportLastFile() { return lynx::platform::settingsFile ("export-last.txt"); }

// A row of buttons where exactly one is on (the output format).
class Segmented final : public juce::Component
{
public:
    std::function<void (int)> onChange;

    explicit Segmented (const juce::StringArray& names)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (names[i]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8401);
            b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < names.size() - 1 ? juce::Button::ConnectedOnRight : 0));
            b->setColour (juce::TextButton::buttonColourId, theme::colour::bg4);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            b->setColour (juce::TextButton::textColourOnId, theme::colour::onAccent);
            b->onClick = [this, i] {
                if (buttons[i]->getToggleState() && onChange)
                    onChange (i);
            };
            addAndMakeVisible (b);
        }
    }

    void setSelected (int index) { if (auto* b = buttons[index]) b->setToggleState (true, juce::dontSendNotification); }
    int getSelected() const
    {
        for (int i = 0; i < buttons.size(); ++i)
            if (buttons[i]->getToggleState())
                return i;
        return 0;
    }

    void resized() override
    {
        const auto w = (float) getWidth() / (float) juce::jmax (1, buttons.size());
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (juce::roundToInt (w * (float) i), 0, juce::roundToInt (w * (float) (i + 1)) - juce::roundToInt (w * (float) i), getHeight());
    }

private:
    juce::OwnedArray<juce::TextButton> buttons;
};

// FL's render window opened for a render that comes back into the project (REA round 168e: FUN_011dcf60 with flags 0x34 for
// the pattern renders, 0x1d0 for consolidations and the exports per track; the form's FormShow FUN_00d1e180, modes 2 / 3):
// WAV only (the format boxes locked), no mixer-track split, the range fixed (Pattern for the pattern renders), and on
// every opening "Enable insert effects" and "Trim PDC silence" ticked, "Save tempo information" and "Show the file when
// complete" unticked. No file dialog: the button renders and `onRender` gets the choices.
struct ClipRenderMode
{
    bool pattern = false; // the range is the pattern
    Song song;            // what is rendered (for the length line)
    juce::String button;
    std::function<void (const exportopts::Options&)> onRender;
};

juce::File clipRenderSettingsFile()
{
    return lynx::platform::settingsFile ("clip-render-settings.txt");
}

// The choices a render to an audio clip starts from: the last ones of such a render (else the Export window's), with
// what FL's FormShow sets each time (see ClipRenderMode).
exportopts::Options clipRenderOptions (bool pattern)
{
    using namespace exportopts;
    const auto clipFile = clipRenderSettingsFile();
    auto o = fromText ((clipFile.existsAsFile() ? clipFile : lynx::platform::settingsFile ("export-settings.txt")).loadFileAsString().toStdString());
    o = withSingleFormat (o, Format::wav);
    o.stems = Stems::none;
    o.stemsIncludeMaster = false;
    o.loops = 1;
    o.range = pattern ? Range::pattern : Range::selection;
    o.insertEffects = true;
    o.trimPdc = true;
    o.saveTempo = false;
    o.showWhenDone = false;
    return o;
}

// The Export window (FL's "WAV rendering", interpreted; REA_rodada84): what to render on the left, the file format on
// the right, mix options below; the choices are remembered (%APPDATA%\LYNX\export-settings.txt).
class ExportPanel final : public juce::Component
{
public:
    // initialFormat: −1 = the formats of the last export (FL's "Last exported format(s)"), else that one format only
    // onlyTracks (FL's "Render armed / selected tracks to wave file(s)", REA round 165f-mixer): one file per insert of the
    // list, the stem choice locked (and not remembered); the Master in the list = also the full mix
    ExportPanel (AudioEngine& e, juce::File folder, juce::String name, bool remember, int initialFormat = -1, std::vector<int> tracks = {},
                 std::shared_ptr<ClipRenderMode> clipMode = nullptr)
        : engine (e),
          suggestedFolder (std::move (folder)),
          suggestedName (std::move (name)),
          persistent (remember),
          onlyTracks (std::move (tracks)),
          clip (std::move (clipMode))
    {
        using namespace exportopts;
        if (persistent)
            options = fromText (exportSettingsFile().loadFileAsString().toStdString());
        if (clip != nullptr)
        {
            persistent = false; // (the Export window's own choices stay as they are)
            options = clipRenderOptions (clip->pattern);
            initialFormat = -1;
        }
        // round 190ap: the plain Export window gets the presets row and "One file per marker section" (not the renders
        // to an audio clip, a list of mixer tracks or the Master export)
        additions = clip == nullptr && onlyTracks.empty();
        if (! additions)
            options.sections = false;
        if (persistent && additions)
        {
            presets = exportopts::presetsFromText (exportPresetsFile().loadFileAsString().toStdString());
            lastExport = exportopts::lastExportFromText (exportLastFile().loadFileAsString().toStdString());
        }
        rememberedStems = options.stems;
        rememberedStemsMaster = options.stemsIncludeMaster;
        if (! onlyTracks.empty())
        {
            options.stems = Stems::mixerTracks;
            options.stemsIncludeMaster = std::find (onlyTracks.begin(), onlyTracks.end(), 0) != onlyTracks.end();
        }
        if (initialFormat >= 0 && initialFormat <= 3)
            options = withSingleFormat (options, (Format) initialFormat);
        const auto& song = engine.getSong();
        // the range follows what is playing unless a selection was the last choice and still exists
        if (clip == nullptr && (options.range != Range::selection || ! hasSelection (song)))
            options.range = song.mode == PlayMode::song && (! song.playlist.empty() || patternblocks::active (song)) ? Range::song : Range::pattern;

        for (auto* t : { &whatTitle, &formatTitle, &optionsTitle })
            addAndMakeVisible (t);
        whatTitle.setText (tr ("What to render"));
        formatTitle.setText (tr ("Output format"));
        optionsTitle.setText (tr ("Mix"));

        auto combo = [this] (juce::Label& label, const juce::String& text, juce::ComboBox& box, const juce::StringArray& items, const juce::String& tip = {}) {
            settingsui::styleLabel (label, text);
            addAndMakeVisible (label);
            box.addItemList (items, 1);
            box.setTooltip (tip);
            box.onChange = [this] { readControls(); };
            addAndMakeVisible (box);
        };

        combo (rangeLabel, tr ("Range"), rangeBox, { tr ("Song (Playlist)"), tr ("Current pattern"), tr ("Playlist selection") },
               tr ("What is rendered: the arrangement, the current pattern, or the Playlist's time selection"));
        rangeBox.setItemEnabled (3, hasSelection (song));
        juce::StringArray loopItems;
        for (int i = 1; i <= 16; ++i)
            loopItems.add (i == 1 ? tr ("Once") : tr ("{0} times", { juce::String (i) }));
        combo (loopsLabel, tr ("Play"), loopsBox, loopItems, tr ("How many times the range plays in the file"));
        combo (tailLabel, tr ("Tail"), tailBox, { tr ("Cut remainder"), tr ("Leave remainder"), tr ("Wrap remainder") },
               tr ("Cut: stop exactly at the end (loops). Leave: let reverbs and releases ring out (songs). Wrap: mix the tail into the start (seamless loops)"));
        combo (tailLengthLabel, tr ("Longest tail"), tailLengthBox, { "3 s", "6 s", "12 s", "30 s" }, tr ("The tail stops earlier when the sound has faded out"));
        combo (stemsLabel, tr ("Files"), stemsBox, { tr ("One file (the mix)"), tr ("One per mixer insert (stems)"), tr ("One per channel (stems)"), tr ("One per channel, instruments only (no sends or buses)") },
               tr ("Stems: a file for each mixer insert that gets sound, or for each channel that plays"));
        stemsMaster.setButtonText (tr ("Also the full mix"));
        stemsMaster.onClick = [this] { readControls(); };
        addAndMakeVisible (stemsMaster);
        // round 190ap: the song cut at its Playlist markers, a file (or a set of stems) per section
        sectionsToggle.setButtonText (tr ("One file per marker section"));
        sectionsToggle.onClick = [this] { readControls(); };
        addChildComponent (sectionsToggle);
        sectionsToggle.setVisible (additions);
        // round 190ap: named presets of these choices, and the last export again without the file dialog
        settingsui::styleLabel (presetLabel, tr ("Preset"));
        presetBox.setTextWhenNothingSelected (tr ("Choose a preset..."));
        presetBox.setTextWhenNoChoicesAvailable (tr ("No presets yet"));
        presetBox.setTooltip (tr ("Sets every choice of this window as it was when the preset was saved"));
        presetBox.onChange = [this] { applyPreset(); };
        savePresetButton.setTooltip (tr ("Keeps the choices of this window under a name"));
        deletePresetButton.setTooltip (tr ("Removes the chosen preset"));
        savePresetButton.onClick = [this] { savePreset(); };
        deletePresetButton.onClick = [this] { deletePreset(); };
        sameAsLastButton.onClick = [this] { exportSameAsLast(); };
        for (auto* c : std::initializer_list<juce::Component*> { &presetLabel, &presetBox, &savePresetButton, &deletePresetButton, &sameAsLastButton })
        {
            addChildComponent (c);
            c->setVisible (additions);
        }
        fillPresets ({});
        if (! onlyTracks.empty())
        {
            stemsBox.setEnabled (false);
            settingsui::styleLabel (tracksLabel, tr ("Tracks"));
            addAndMakeVisible (tracksLabel);
            juce::StringArray names;
            for (auto t : stemTracks (song, onlyTracks))
                names.add (juce::String (t) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()));
            tracksText.setText (names.joinIntoString (", "), juce::dontSendNotification);
            tracksText.setTooltip (names.joinIntoString ("\n"));
            tracksText.setMinimumHorizontalScale (0.7f);
            tracksText.setFont (theme::font (theme::type::small));
            tracksText.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            addAndMakeVisible (tracksText);
        }

        formatButtons.onChange = [this] (int) { readControls(); };
        addAndMakeVisible (formatButtons);
        // FL ticks several format boxes at once (REA round 139): the other formats written from the same render
        settingsui::styleLabel (alsoLabel, tr ("Also save"));
        addAndMakeVisible (alsoLabel);
        for (int i = 0; i < 4; ++i)
        {
            static const char* names[] = { "WAV", "MP3", "OGG", "FLAC" };
            alsoToggles[(std::size_t) i].setButtonText (names[i]);
            alsoToggles[(std::size_t) i].setTooltip (tr ("Also write the {0} file from the same render (same name, its own extension)", { juce::String (names[i]) }));
            alsoToggles[(std::size_t) i].onClick = [this] { readControls(); };
            addAndMakeVisible (alsoToggles[(std::size_t) i]);
        }
        combo (depthLabel, tr ("Bit depth"), depthBox, { tr ("16 bits (integer)"), tr ("24 bits (integer)"), tr ("32 bits (float)") });
        juce::StringArray mp3Items, oggItems;
        for (auto k : mp3Bitrates())
            mp3Items.add (juce::String (k) + " kbps");
        for (auto k : oggBitrates())
            oggItems.add (juce::String (k) + " kbps");
        combo (bitrateLabel, tr ("Bitrate"), mp3Box, mp3Items, tr ("Constant bitrate (higher: better sound, bigger file)"));
        addAndMakeVisible (oggBox);
        oggBox.addItemList (oggItems, 1);
        oggBox.onChange = [this] { readControls(); };
        combo (compressionLabel, tr ("Compression"), flacLevelBox,
               { tr ("0 (fastest)"), "1", "2", "3", "4", tr ("5 (default)"), "6", "7", tr ("8 (smallest)") },
               tr ("FLAC is lossless at every level; higher levels make smaller files and take longer"));
        juce::StringArray rateItems;
        for (auto r : sampleRates())
            rateItems.add (juce::String ((int) r) + " Hz");
        combo (rateLabel, tr ("Sample rate"), rateBox, rateItems);
        combo (channelsLabel, tr ("Channels"), channelsBox, { tr ("Stereo"), tr ("Mono (both sides mixed)"), tr ("Mono (left only)"), tr ("Mono (right only)") });
        combo (ditherLabel, tr ("Dither"), ditherBox, { tr ("Off"), tr ("TPDF"), tr ("TPDF + noise shaping") },
               tr ("Hides the rounding noise of 16 and 24 bit files (noise shaping moves it where the ear is least sensitive)"));

        for (auto* t : { &normalizeToggle, &masterFxToggle, &insertFxToggle, &trimToggle, &hqToggle, &showToggle, &polyToggle, &warmUpToggle, &realtimeToggle,
                         &tempoToggle, &loopMarkersToggle, &playlistMarkersToggle })
        {
            t->onClick = [this] { readControls(); };
            addAndMakeVisible (t);
        }
        normalizeToggle.setButtonText (tr ("Normalize (peak to 0 dB)"));
        masterFxToggle.setButtonText (tr ("Master effects"));
        trimToggle.setButtonText (tr ("Trim the delay compensation's silence"));
        normalizeToggle.setTooltip (tr ("Raises or lowers the whole file so its loudest peak reaches full scale"));
        masterFxToggle.setTooltip (tr ("Off: the Master insert's effects are bypassed for the export"));
        insertFxToggle.setButtonText (tr ("Insert effects"));
        insertFxToggle.setTooltip (tr ("Off: the effects of the inserts (not the Master's) are bypassed for the export"));
        trimToggle.setTooltip (tr ("Removes the silence plugin latency compensation adds at the start"));
        hqToggle.setButtonText (tr ("HQ for all plugins"));
        showToggle.setButtonText (tr ("Show the file(s) when complete"));
        polyToggle.setButtonText (tr ("Disable maximum polyphony"));
        warmUpToggle.setButtonText (tr ("Plugin warm-up"));
        warmUpToggle.setTooltip (tr ("Before the export starts, third-party plugins process a moment of silence until they have "
                                     "finished loading their settings (bridged plugins, samplers that load in the background), "
                                     "so the file does not start with half-loaded effects"));
        // FL's "Extra data" (REA_rodada167l §3): chunks of the WAV file other programs read
        tempoToggle.setButtonText (tr ("Tempo information"));
        tempoToggle.setTooltip (tr ("WAV: stores the tempo, the length in beats and the meter (an acid chunk), so other programs play the file at the song's tempo"));
        loopMarkersToggle.setButtonText (tr ("Loop markers"));
        loopMarkersToggle.setTooltip (tr ("WAV: marks the music (without the tail) as the file's loop (a smpl chunk), for samplers"));
        playlistMarkersToggle.setButtonText (tr ("Playlist markers"));
        playlistMarkersToggle.setTooltip (tr ("WAV: the Playlist's markers become cue points with their names (cue and labl chunks)"));
        // round 174d (issue #83): the exported mix muxed back under the project's video (the user's ffmpeg)
        videoToggle.setButtonText (tr ("Also write a video with this mix"));
        videoToggle.setTooltip (tr ("Next to the audio file: the first video track's picture with the exported mix as its sound (needs FFmpeg; "
                                    "the picture is copied as it is when it starts with the song)"));
        videoToggle.setToggleState (exportWithVideo(), juce::dontSendNotification);
        videoToggle.onClick = [this] { exportWithVideo() = videoToggle.getToggleState(); };
        addChildComponent (videoToggle);
        videoToggle.setVisible (onlyTracks.empty() && videotracks::hasVideo (engine.getSong()));
        // round 165c: only offered when the song has an active hardware insert (outboard gear can't run faster than real time)
        realtimeToggle.setButtonText (tr ("Render through the hardware in real time"));
        realtimeToggle.setTooltip (tr ("The song plays through the audio device while it is recorded, so the hardware inserts' gear is "
                                       "in the file (the export takes as long as the song). At the device's sample rate, played once, "
                                       "with the playback quality: what you hear"));
        // (not for a list of tracks: the real-time pass records the full mix only)
        realtimeToggle.setVisible (onlyTracks.empty() && hwinsert::countActive (engine.getSong()) > 0);
        polyToggle.setTooltip (tr ("The channels' Max polyphony is lifted for the export, so no note is cut by the voice limit"));
        showToggle.setTooltip (tr ("Opens the file manager on the exported file when the export is done"));
        hqToggle.setTooltip (tr ("Plugins with a separate render quality use it, and HQ switches are turned on for the export. "
                                 "Off: the export uses the playback quality, so it sounds exactly like what you heard"));

        infoLabel.setFont (theme::font (theme::type::small));
        infoLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (infoLabel);
        warningLabel.setFont (theme::font (theme::type::small));
        warningLabel.setColour (juce::Label::textColourId, theme::colour::warning);
        infoLabel.setMinimumHorizontalScale (0.7f); // (round 165c: the real-time line is longer)
        warningLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (warningLabel);

        exportButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        exportButton.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        exportButton.onClick = [this] { chooseFile(); };
        cancelButton.onClick = [this] { close(); };

        if (clip != nullptr)
        {
            // what FL locks for a render to an audio clip: the format (WAV), the range, the split into stems
            rangeBox.setItemEnabled (3, true);
            for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &loopsBox, &stemsBox, &stemsMaster, &formatButtons })
                c->setEnabled (false);
            for (auto& t : alsoToggles)
                t.setEnabled (false);
            realtimeToggle.setVisible (false);
            exportButton.setButtonText (clip->button);
            exportButton.onClick = [this] { renderClip(); };
        }

        writeControls();
        // round 167l: the height the content needs (a fixed 486 left an empty band above "Mix"): the taller column (the
        // right one with FLAC's two rows and, in some languages, "Also save" on its own line) plus the switches
        setSize (620, wantedHeight());
    }

    // --versionsshot (round 190ap): the window's additions in use — two presets, one chosen, sections on, a last export
    void setAdditionsForSnapshot()
    {
        exportopts::Options stems = options, demo = options;
        stems.stems = exportopts::Stems::mixerTracks;
        stems.range = exportopts::Range::song;
        stems.sections = true;
        demo.format = exportopts::Format::mp3;
        presets = exportopts::withPreset (exportopts::withPreset ({}, { "Stems for mixing", stems }), { "MP3 demo", demo });
        lastExport.options = demo;
        lastExport.file = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Exports").getChildFile ("Song.mp3").getFullPathName().toStdString();
        snapshotLast = true;
        options = stems;
        fillPresets ("Stems for mixing");
        writeControls();
    }

    // --snapshot: the window with this format chosen (0 WAV, 1 MP3, 2 OGG, 3 FLAC) and a few options set
    void setFormatForSnapshot (int format)
    {
        options.format = (exportopts::Format) juce::jlimit (0, 3, format);
        if (format == 3)
        {
            options.tail = exportopts::TailMode::wrap;
            options.loops = 2;
            options.stems = exportopts::Stems::mixerTracks;
            options.stemsIncludeMaster = true;
            options.normalize = true;
        }
        writeControls();
    }

    // File › Export › "Master..." (round 168e): the same window, the render mastered (core/Mastering.h) to the target
    // chosen in the row of the stems (no stems: one mastered file)
    void enableMaster()
    {
        masterMode = true;
        beforeMaster = options; // (what the plain export keeps: its stems and normalising)
        options.master = true;
        options.stems = exportopts::Stems::none;
        options.stemsIncludeMaster = false;
        settingsui::styleLabel (masterLabel, tr ("Loudness"));
        addAndMakeVisible (masterLabel);
        for (auto t : mastering::targets())
            masterBox.addItem (tr ("{0} LUFS", { juce::String ((int) t) }), (int) -t);
        masterBox.setTooltip (tr ("The integrated loudness of the mastered file (-14 LUFS: what streaming services play at); "
                                  "the peaks stay under -1 dB true peak"));
        masterBox.onChange = [this] { options.masterLufs = -(double) masterBox.getSelectedId(); refresh(); };
        addAndMakeVisible (masterBox);
        stemsLabel.setVisible (false);
        stemsBox.setVisible (false);
        stemsMaster.setVisible (false);
        realtimeToggle.setVisible (false);
        // (the Master export keeps its own window: no presets row, no sections; round 190ap)
        additions = false;
        options.sections = false;
        for (auto* c : std::initializer_list<juce::Component*> { &sectionsToggle, &presetLabel, &presetBox, &savePresetButton, &deletePresetButton, &sameAsLastButton })
            c->setVisible (false);
        writeControls();
        setSize (620, wantedHeight());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);
        if (additions) // round 190ap: the presets row above the two columns
        {
            auto row = area.removeFromTop (26);
            presetLabel.setBounds (row.removeFromLeft (theme::labelColumnWidth ({ &presetLabel }, 50, 110)));
            sameAsLastButton.setBounds (row.removeFromRight (juce::jlimit (130, 200, sameAsLastButton.getBestWidthForHeight (26))));
            row.removeFromRight (14);
            deletePresetButton.setBounds (row.removeFromRight (72));
            row.removeFromRight (6);
            savePresetButton.setBounds (row.removeFromRight (72));
            row.removeFromRight (8);
            presetBox.setBounds (row);
            area.removeFromTop (presetRowHeight - 26);
        }
        auto buttons = area.removeFromBottom (32);
        cancelButton.setBounds (buttons.removeFromRight (96));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (128));
        infoLabel.setBounds (buttons.removeFromTop (16));
        warningLabel.setBounds (buttons);
        area.removeFromBottom (8);

        // three columns of switches (round 167k: the fourth switch of row 2 squeezed "Show the file(s)..." to two clipped
        // lines); the real-time render switch gets a row of its own when the song has hardware inserts
        auto mix = area.removeFromBottom (mixHeight());
        optionsTitle.setBounds (mix.removeFromTop (26));
        mix.removeFromTop (4);
        const int colA = 200, colB = 196;
        auto mixRow = mix.removeFromTop (24);
        normalizeToggle.setBounds (mixRow.removeFromLeft (colA));
        masterFxToggle.setBounds (mixRow.removeFromLeft (colB));
        trimToggle.setBounds (mixRow);
        mix.removeFromTop (2);
        {
            auto row2 = mix.removeFromTop (24);
            hqToggle.setBounds (row2.removeFromLeft (colA));
            polyToggle.setBounds (row2.removeFromLeft (colB));
            showToggle.setBounds (row2);
        }
        mix.removeFromTop (2);
        {
            auto row3 = mix.removeFromTop (24);
            warmUpToggle.setBounds (row3.removeFromLeft (colA));
            insertFxToggle.setBounds (row3.removeFromLeft (colB));
            videoToggle.setBounds (row3);
        }
        mix.removeFromTop (2);
        {
            auto row4 = mix.removeFromTop (24);
            tempoToggle.setBounds (row4.removeFromLeft (colA));
            loopMarkersToggle.setBounds (row4.removeFromLeft (colB));
            playlistMarkersToggle.setBounds (row4);
        }
        if (realtimeToggle.isVisible())
        {
            mix.removeFromTop (2);
            realtimeToggle.setBounds (mix.removeFromTop (24));
        }
        area.removeFromBottom (6);
        columnsBottom = area.getBottom();

        auto left = area.removeFromLeft ((area.getWidth() - 24) / 2);
        area.removeFromLeft (24);
        auto right = area;
        const int labelWidth = theme::labelColumnWidth ({ &rangeLabel, &loopsLabel, &tailLabel, &tailLengthLabel, &stemsLabel, &depthLabel,
                                                          &bitrateLabel, &compressionLabel, &rateLabel, &channelsLabel, &ditherLabel },
                                                        92, 150);
        constexpr int rowHeight = 26, gap = 7;

        auto row = [&] (juce::Rectangle<int>& column, juce::Label& label, juce::Component& control) {
            auto r = column.removeFromTop (rowHeight);
            label.setBounds (r.removeFromLeft (labelWidth));
            control.setBounds (r);
            column.removeFromTop (gap);
        };

        whatTitle.setBounds (left.removeFromTop (26));
        left.removeFromTop (8);
        row (left, rangeLabel, rangeBox);
        row (left, loopsLabel, loopsBox);
        row (left, tailLabel, tailBox);
        row (left, tailLengthLabel, tailLengthBox);
        if (masterMode)
            row (left, masterLabel, masterBox);
        else
            row (left, stemsLabel, stemsBox);
        stemsMaster.setBounds (left.removeFromTop (22).withTrimmedLeft (labelWidth));
        if (sectionsToggle.isVisible())
        {
            left.removeFromTop (2);
            sectionsToggle.setBounds (left.removeFromTop (22).withTrimmedLeft (labelWidth));
        }
        if (! onlyTracks.empty())
        {
            left.removeFromTop (gap);
            row (left, tracksLabel, tracksText);
        }

        formatTitle.setBounds (right.removeFromTop (26));
        right.removeFromTop (8);
        formatButtons.setBounds (right.removeFromTop (28));
        right.removeFromTop (4);
        {
            // the four format ticks keep the room their names need; a caption too long to sit beside them
            // ("Также сохранить", "Auch speichern") gets its own line above them instead of being cut
            const auto ticks = 4 * 54;
            const auto labelNeeds = theme::labelColumnWidth ({ &alsoLabel }, 0, 400);
            const auto ownLine = labelNeeds > (right.getWidth() - ticks) * 11 / 10; // a slight squeeze is fine
            if (ownLine)
                alsoLabel.setBounds (right.removeFromTop (18));
            auto also = right.removeFromTop (22);
            if (! ownLine) // as before round 165o
                alsoLabel.setBounds (also.removeFromLeft (juce::jmax (juce::jmin (70, also.getWidth() - ticks),
                                                                      juce::jmin (theme::labelColumnWidth ({ &alsoLabel }, 70, 130), also.getWidth() - ticks))));
            const auto w = also.getWidth() / 4;
            for (auto& t : alsoToggles)
                t.setBounds (also.removeFromLeft (w));
        }
        right.removeFromTop (gap);
        // the format's own settings: two rows, the others hidden
        using exportopts::Format;
        const auto f = options.format;
        if (f == Format::wav || f == Format::flac)
            row (right, depthLabel, depthBox);
        if (f == Format::mp3)
            row (right, bitrateLabel, mp3Box);
        if (f == Format::ogg)
            row (right, bitrateLabel, oggBox);
        if (f == Format::flac)
            row (right, compressionLabel, flacLevelBox);
        row (right, rateLabel, rateBox);
        row (right, channelsLabel, channelsBox);
        row (right, ditherLabel, ditherBox);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        // a hairline between the two columns
        const auto x = 16 + (getWidth() - 32 - 24) / 2 + 12;
        g.setColour (theme::colour::border);
        const auto top = 22 + (additions ? presetRowHeight : 0);
        g.fillRect (x, top, 1, juce::jmax (0, columnsBottom - top - 8));
        if (additions) // (and one under the presets row)
            g.fillRect (16, 12 + 26 + (presetRowHeight - 26) / 2, getWidth() - 32, 1);
    }

private:
    int mixHeight() const { return 26 + 4 + 4 * 24 + 3 * 2 + (realtimeToggle.isVisible() ? 26 : 0); }

    // the two columns' heights (as resized() lays them out), the taller one
    int columnsHeight()
    {
        constexpr int rowHeight = 26, gap = 7;
        const auto leftHeight = 26 + 8 + 5 * (rowHeight + gap) + 22 + (sectionsToggle.isVisible() ? 24 : 0) + (onlyTracks.empty() ? 0 : gap + rowHeight + gap);
        const auto columnWidth = (620 - 32 - 24) / 2;
        const auto labelNeeds = theme::labelColumnWidth ({ &alsoLabel }, 0, 400);
        const auto ownLine = labelNeeds > (columnWidth - 4 * 54) * 11 / 10;
        // the most rows a format has: FLAC (bit depth, compression) + rate, channels, dither
        const auto rightHeight = 26 + 8 + 28 + 4 + (ownLine ? 18 : 0) + 22 + gap + 5 * (rowHeight + gap);
        return juce::jmax (leftHeight, rightHeight);
    }
    int columnsBottom = 0;
    static constexpr int presetRowHeight = 26 + 14;
    int wantedHeight() { return 12 + (additions ? presetRowHeight : 0) + columnsHeight() + 6 + mixHeight() + 8 + 32 + 12; }

    // ---- round 190ap: presets and "same as last time" ------------------------------------------------------------
    void fillPresets (const juce::String& select)
    {
        presetBox.clear (juce::dontSendNotification);
        int id = 1, chosen = 0;
        for (const auto& p : presets)
        {
            const auto name = juce::String::fromUTF8 (p.name.c_str());
            presetBox.addItem (name, id);
            if (name == select)
                chosen = id;
            ++id;
        }
        presetBox.setSelectedId (chosen, juce::dontSendNotification);
        updatePresetButtons();
    }

    const exportopts::Preset* chosenPreset() const
    {
        const auto i = presetBox.getSelectedId() - 1;
        return i >= 0 && i < (int) presets.size() ? &presets[(std::size_t) i] : nullptr;
    }

    // a preset's (or the last export's) choices as this window can take them now: the selection only when there is one
    exportopts::Options fitted (exportopts::Options o) const
    {
        o.master = false;
        if (o.range == exportopts::Range::selection && ! exportopts::hasSelection (engine.getSong()))
            o.range = exportopts::Range::song;
        return exportopts::clamped (o);
    }

    void applyPreset()
    {
        if (const auto* p = chosenPreset())
        {
            options = fitted (p->options);
            realtime = false;
            writeControls();
        }
        updatePresetButtons();
    }

    void updatePresetButtons()
    {
        deletePresetButton.setEnabled (chosenPreset() != nullptr);
        const auto target = sameAsLastTarget();
        sameAsLastButton.setEnabled (target != juce::File());
        sameAsLastButton.setTooltip (target != juce::File()
                                         ? tr ("Exports now with the choices of the last export, into the same folder, without asking for the file: {0}", { target.getFullPathName() })
                                         : tr ("Exports again with the choices and the folder of the last export (nothing was exported yet)"));
    }

    void storePresets()
    {
        if (persistent)
            exportPresetsFile().replaceWithText (juce::String::fromUTF8 (exportopts::presetsToText (presets).c_str()));
    }

    void savePreset()
    {
        readControls();
        const auto* chosen = chosenPreset();
        auto* w = new juce::AlertWindow (tr ("Save export preset"), tr ("A name for these export choices (an existing name is replaced)"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", chosen != nullptr ? juce::String::fromUTF8 (chosen->name.c_str()) : juce::String());
        w->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe = juce::Component::SafePointer<ExportPanel> (this)] (int result) {
                                if (result != 1 || safe == nullptr)
                                    return;
                                const auto name = exportopts::cleanPresetName (w->getTextEditorContents ("name").toStdString());
                                if (name.empty())
                                    return;
                                safe->presets = exportopts::withPreset (std::move (safe->presets), { name, safe->options });
                                safe->storePresets();
                                safe->fillPresets (juce::String::fromUTF8 (name.c_str()));
                            }),
                            true);
    }

    void deletePreset()
    {
        if (const auto* p = chosenPreset())
        {
            const auto name = p->name;
            presets = exportopts::withoutPreset (std::move (presets), name);
            storePresets();
            fillPresets ({});
        }
    }

    // where "Same as last time" writes: the last export's folder, this project's name, the last format
    juce::File sameAsLastTarget() const
    {
        const auto path = exportopts::sameAsLastFile (lastExport, suggestedName.toStdString());
        if (path.empty())
            return {};
        const juce::File file (juce::String::fromUTF8 (path.c_str()));
        return snapshotLast || file.getParentDirectory().isDirectory() ? file : juce::File();
    }

    void exportSameAsLast()
    {
        const auto file = sameAsLastTarget();
        if (file == juce::File())
            return;
        options = fitted (lastExport.options);
        realtime = false;
        writeControls();
        presetBox.setSelectedId (0, juce::dontSendNotification);
        // one file that is already there: ask once (the sets of stems and sections write beside it, as they did last time)
        if (file.existsAsFile())
        {
            auto* w = new juce::AlertWindow (tr ("Same as last time"), tr ("{0} already exists. Replace it?", { file.getFullPathName() }), juce::MessageBoxIconType::QuestionIcon);
            w->addButton (tr ("Replace"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([file, safe = juce::Component::SafePointer<ExportPanel> (this)] (int result) {
                                    if (result == 1 && safe != nullptr)
                                    {
                                        safe->rememberChoices();
                                        safe->startExport (file);
                                    }
                                }),
                                true);
            return;
        }
        rememberChoices();
        startExport (file);
    }

    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    static int indexOf (const std::vector<int>& list, int value)
    {
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(std::size_t) i] == value)
                return i;
        return 0;
    }

    void writeControls()
    {
        using namespace exportopts;
        const auto n = juce::dontSendNotification;
        rangeBox.setSelectedItemIndex ((int) options.range, n);
        loopsBox.setSelectedItemIndex (options.loops - 1, n);
        tailBox.setSelectedItemIndex ((int) options.tail, n);
        const double tails[] = { 3.0, 6.0, 12.0, 30.0 };
        int tailIndex = 0;
        for (int i = 0; i < 4; ++i)
            if (std::abs (tails[i] - options.maxTailSeconds) < std::abs (tails[tailIndex] - options.maxTailSeconds))
                tailIndex = i;
        tailLengthBox.setSelectedItemIndex (tailIndex, n);
        stemsBox.setSelectedItemIndex ((int) options.stems, n);
        if (masterMode)
        {
            int best = (int) -mastering::targets().front();
            for (auto t : mastering::targets())
                if (std::abs (t - options.masterLufs) < std::abs (-(double) best - options.masterLufs))
                    best = (int) -t;
            masterBox.setSelectedId (best, n);
        }
        stemsMaster.setToggleState (options.stemsIncludeMaster, n);
        sectionsToggle.setToggleState (options.sections, n);
        formatButtons.setSelected ((int) options.format);
        depthBox.setSelectedItemIndex (options.format == Format::flac ? (options.flacBits == 16 ? 0 : 1) : (options.wavBits == 16 ? 0 : options.wavBits == 24 ? 1 : 2), n);
        mp3Box.setSelectedItemIndex (indexOf (mp3Bitrates(), options.mp3Kbps), n);
        oggBox.setSelectedItemIndex (indexOf (oggBitrates(), options.oggKbps), n);
        flacLevelBox.setSelectedItemIndex (options.flacLevel, n);
        int rate = 0;
        for (int i = 0; i < (int) sampleRates().size(); ++i)
            if (sampleRates()[(std::size_t) i] == options.sampleRate)
                rate = i;
        rateBox.setSelectedItemIndex (rate, n);
        channelsBox.setSelectedItemIndex ((int) options.channels, n);
        ditherBox.setSelectedItemIndex ((int) options.dither, n);
        normalizeToggle.setToggleState (options.normalize, n);
        masterFxToggle.setToggleState (options.masterEffects, n);
        insertFxToggle.setToggleState (options.insertEffects, n);
        trimToggle.setToggleState (options.trimPdc, n);
        hqToggle.setToggleState (options.hqAllPlugins, n);
        warmUpToggle.setToggleState (options.pluginWarmUp, n);
        showToggle.setToggleState (options.showWhenDone, n);
        polyToggle.setToggleState (options.disableMaxPolyphony, n);
        realtimeToggle.setToggleState (realtime, n);
        tempoToggle.setToggleState (options.saveTempo, n);
        loopMarkersToggle.setToggleState (options.loopMarkers, n);
        playlistMarkersToggle.setToggleState (options.playlistMarkers, n);
        for (int i = 0; i < 4; ++i)
            alsoToggles[(std::size_t) i].setToggleState (i == (int) options.format || (options.moreFormats & (1u << i)) != 0, n);
        refresh();
    }

    void readControls()
    {
        using namespace exportopts;
        const auto previousFormat = options.format;
        options.range = (Range) juce::jmax (0, rangeBox.getSelectedItemIndex());
        options.loops = loopsBox.getSelectedItemIndex() + 1;
        options.tail = (TailMode) juce::jmax (0, tailBox.getSelectedItemIndex());
        const double tails[] = { 3.0, 6.0, 12.0, 30.0 };
        options.maxTailSeconds = tails[juce::jlimit (0, 3, tailLengthBox.getSelectedItemIndex())];
        options.stems = (Stems) juce::jmax (0, stemsBox.getSelectedItemIndex());
        options.stemsIncludeMaster = stemsMaster.getToggleState();
        options.sections = additions && sectionsToggle.getToggleState();
        options.format = (Format) formatButtons.getSelected();
        if (options.format == previousFormat)
        {
            const auto depth = depthBox.getSelectedItemIndex();
            if (options.format == Format::flac)
                options.flacBits = depth == 0 ? 16 : 24;
            else
                options.wavBits = depth == 0 ? 16 : depth == 1 ? 24 : 32;
        }
        options.mp3Kbps = mp3Bitrates()[(std::size_t) juce::jlimit (0, (int) mp3Bitrates().size() - 1, mp3Box.getSelectedItemIndex())];
        options.oggKbps = oggBitrates()[(std::size_t) juce::jlimit (0, (int) oggBitrates().size() - 1, oggBox.getSelectedItemIndex())];
        options.flacLevel = juce::jlimit (0, 8, flacLevelBox.getSelectedItemIndex());
        if (! realtime) // (real time shows the device's rate)
            options.sampleRate = sampleRates()[(std::size_t) juce::jlimit (0, (int) sampleRates().size() - 1, rateBox.getSelectedItemIndex())];
        options.channels = (Channels) juce::jmax (0, channelsBox.getSelectedItemIndex());
        options.dither = (DitherMode) juce::jmax (0, ditherBox.getSelectedItemIndex());
        options.normalize = normalizeToggle.getToggleState() && ! masterMode;
        if (masterMode)
        {
            options.master = true;
            options.stems = Stems::none;
        }
        options.trimPdc = trimToggle.getToggleState();
        options.showWhenDone = showToggle.getToggleState();
        options.saveTempo = tempoToggle.getToggleState();
        options.loopMarkers = loopMarkersToggle.getToggleState();
        options.playlistMarkers = playlistMarkersToggle.getToggleState();
        if (! realtime) // (while real time is ticked these show what it uses: the offline choices stay as they were)
        {
            options.masterEffects = masterFxToggle.getToggleState();
            options.insertEffects = insertFxToggle.getToggleState();
            options.hqAllPlugins = hqToggle.getToggleState();
            options.pluginWarmUp = warmUpToggle.getToggleState();
            options.disableMaxPolyphony = polyToggle.getToggleState();
        }
        realtime = realtimeToggle.isVisible() && realtimeToggle.getToggleState();
        options.moreFormats = 0;
        for (int i = 0; i < 4; ++i) // (the previous main format's box was only shown ticked)
            if (i != (int) previousFormat && alsoToggles[(std::size_t) i].getToggleState())
                options.moreFormats |= 1u << i;
        options = clamped (options); // (the main format is not "more")
        // (round 190ap: the preset's name stays only while the choices are still the preset's)
        if (const auto* p = chosenPreset(); p != nullptr && ! (fitted (p->options) == options))
            presetBox.setSelectedId (0, juce::dontSendNotification);

        if (options.format != previousFormat)
            writeControls(); // the bit depth list changes with the format
        else
            refresh();
    }

    void refresh()
    {
        using namespace exportopts;
        // the depth list: FLAC has no float
        depthBox.setItemEnabled (3, options.format == Format::wav);
        tailLengthBox.setEnabled (options.tail != TailMode::cut);
        stemsMaster.setEnabled (options.stems != Stems::none);
        const auto& songNow = clip != nullptr ? clip->song : engine.getSong();
        // round 190ap: split by the Playlist markers (the song range only; each section plays once)
        const auto sectionList = additions ? markerSections (songNow) : std::vector<Section>();
        const auto sectioned = additions && ! realtime && exportsSections (songNow, options);
        sectionsToggle.setEnabled (additions && ! realtime && options.range == Range::song && ! sectionList.empty());
        {
            juce::StringArray names;
            for (const auto& section : sectionList)
                names.add (juce::String::fromUTF8 (section.name.c_str()));
            sectionsToggle.setTooltip (sectionList.empty() ? tr ("The song is cut at its Playlist markers and each section becomes a file named after its marker. This song has no markers yet")
                                                           : tr ("The song is cut at its Playlist markers and each section becomes a file named after its marker; with stems, each section gets its stems. Sections: {0}",
                                                                 { names.joinIntoString (", ") }));
        }
        updatePresetButtons();
        loopsBox.setEnabled (! realtime && clip == nullptr && ! sectioned);
        // a real-time bounce (round 165c): the device's rate, the song or the selection once, the Master's effects, the
        // playback quality, no stems — the controls it doesn't use are off
        for (auto* c : std::initializer_list<juce::Component*> { &rateBox, &stemsBox, &masterFxToggle, &insertFxToggle, &hqToggle, &polyToggle, &warmUpToggle })
            c->setEnabled (! realtime);
        stemsBox.setEnabled (! realtime && onlyTracks.empty() && clip == nullptr); // a list of tracks locks the stem choice
        rangeBox.setItemEnabled (2, ! realtime);
        {
            // the boxes show what the export will use: real time = the Master's effects, the playback quality
            const auto used = realtime ? ExportJob::realtimeOptions (options, engine.getTransport().sampleRate.load()) : options;
            masterFxToggle.setToggleState (used.masterEffects, juce::dontSendNotification);
            insertFxToggle.setToggleState (used.insertEffects, juce::dontSendNotification);
            hqToggle.setToggleState (used.hqAllPlugins, juce::dontSendNotification);
            warmUpToggle.setToggleState (used.pluginWarmUp, juce::dontSendNotification);
            polyToggle.setToggleState (used.disableMaxPolyphony, juce::dontSendNotification);
            for (int i = 0; i < (int) sampleRates().size(); ++i)
                if (sampleRates()[(std::size_t) i] == used.sampleRate)
                    rateBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        stemsMaster.setEnabled (! realtime && options.stems != Stems::none && clip == nullptr);
        normalizeToggle.setEnabled (! masterMode); // (the mastering sets the level)
        const auto ditherable = (options.format == Format::wav && options.wavBits != 32) || options.format == Format::flac;
        ditherBox.setEnabled (ditherable);
        ditherLabel.setEnabled (ditherable);
        const auto f = options.format;
        for (int i = 0; i < 4; ++i)
            alsoToggles[(std::size_t) i].setEnabled (i != (int) f && clip == nullptr);
        // (WAV only: FL writes them into WAV files)
        const auto wav = f == Format::wav || (options.moreFormats & (1u << (int) Format::wav)) != 0;
        for (auto* t : { &tempoToggle, &loopMarkersToggle, &playlistMarkersToggle })
            t->setEnabled (wav);
        depthLabel.setVisible (f == Format::wav || f == Format::flac);
        depthBox.setVisible (f == Format::wav || f == Format::flac);
        bitrateLabel.setVisible (f == Format::mp3 || f == Format::ogg);
        mp3Box.setVisible (f == Format::mp3);
        oggBox.setVisible (f == Format::ogg);
        compressionLabel.setVisible (f == Format::flac);
        flacLevelBox.setVisible (f == Format::flac);

        const auto& song = clip != nullptr ? clip->song : engine.getSong();
        const auto shown = realtime ? ExportJob::realtimeOptions (options, engine.getTransport().sampleRate.load()) : options;
        const auto seconds = exportMusicSeconds (song, shown);
        // FL's line: "Length: 144 bars  Total time: 3'57''  Disk space: 38.76 MB" (REA round 134)
        const auto bars = lengthInBars (seconds, song.bpm, song.beatsPerBar);
        auto info = tr ("Length: {0} bars", { juce::String ((int) std::ceil (bars - 0.01)) }) + "  ·  " // whole bars, as FL
                    + tr ("Total time: {0}", { juce::String (formatDuration (seconds)) });
        if (options.tail == TailMode::leave)
            info << " " << tr ("(+ tail up to {0} s)", { juce::String ((int) options.maxTailSeconds) });
        if (options.format == Format::mp3 && mp3SampleRate (options.sampleRate) != options.sampleRate)
            info << "  ·  " << tr ("MP3 at {0} Hz", { juce::String ((int) mp3SampleRate (options.sampleRate)) });
        if (sectioned)
            info << "  ·  " << tr ("{0} section(s)", { juce::String ((int) sectionList.size()) }) << "  ·  "
                 << tr ("{0} file(s)", { juce::String (fileCount (song, options, onlyTracks)) });
        else if (options.stems == Stems::mixerTracks)
            info << "  ·  " << tr ("{0} file(s)", { juce::String ((int) stemTracks (song, onlyTracks).size() + (options.stemsIncludeMaster ? 1 : 0)) });
        else if (options.stems == Stems::channels || options.stems == Stems::channelsDry)
            info << "  ·  " << tr ("{0} file(s)", { juce::String ((int) stemChannels (song, options.range).size() + (options.stemsIncludeMaster ? 1 : 0)) });
        if (options.stems == Stems::none || realtime)
            info << "  ·  " << tr ("Disk space: {0}", { juce::File::descriptionOfSizeInBytes (estimatedBytes (shown, seconds)) });
        if (realtime)
            info << "  ·  " << tr ("{0} Hz (device)", { juce::String ((int) shown.sampleRate) });
        infoLabel.setText (info, juce::dontSendNotification);

        juce::String warning;
        if (! song.mixer.empty() && song.mixer[0].muted)
            warning = tr ("The Master insert is muted: the file will be silent");
        else if (options.range == Range::selection && ! hasSelection (song))
            warning = tr ("There is no Playlist selection: the whole song is rendered");
        else if (realtime && engine.getDeviceManager().getCurrentAudioDevice() == nullptr)
            warning = tr ("No audio device is open: a real-time export needs one");
        else if (realtime)
            warning = tr ("Real time ({0}): leave the transport alone while it records",
                          { juce::String (formatDuration (seconds)) });
        else if (hwinsert::countActive (song) > 0) // round 164d: outboard gear can't run faster than real time
            warning = tr ("Hardware inserts pass their dry signal in an export (tick \"Render through the hardware in real time\" to keep their return)");
        warningLabel.setText (warning, juce::dontSendNotification);
        resized();
        repaint();
    }

    // a render to an audio clip (ClipRenderMode): no file dialog, the choices are remembered for the next one
    void renderClip()
    {
        readControls();
        clipRenderSettingsFile().replaceWithText (juce::String (exportopts::toText (options)));
        auto mode = clip;
        const auto o = options;
        close(); // deletes this panel: no `this` after this point
        if (mode->onRender)
            mode->onRender (o);
    }

    // the choices kept for the next time the window opens
    void rememberChoices()
    {
        if (persistent)
        {
            auto remembered = options; // (the locked stem choice of a track render is not remembered)
            if (! onlyTracks.empty())
            {
                remembered.stems = rememberedStems;
                remembered.stemsIncludeMaster = rememberedStemsMaster;
            }
            if (masterMode)
            {
                remembered.master = false;
                remembered.stems = beforeMaster.stems;
                remembered.stemsIncludeMaster = beforeMaster.stemsIncludeMaster;
                remembered.normalize = beforeMaster.normalize;
            }
            exportSettingsFile().replaceWithText (juce::String (exportopts::toText (remembered)));
        }
    }

    void chooseFile()
    {
        readControls();
        rememberChoices();

        const auto o = options;
        const auto extension = juce::String (exportopts::extension (o.format));

        // FL renders the armed tracks straight to their "Record to" files, without a file dialog (round 167a [SHOT s33])
        if (! onlyTracks.empty() && ! realtime && o.stems != exportopts::Stems::none && ! o.stemsIncludeMaster)
        {
            const auto& song = engine.getSong();
            const auto list = exportopts::stemTracks (song, onlyTracks);
            const auto all = ! list.empty() && std::all_of (list.begin(), list.end(), [&song] (int t) {
                return t > 0 && t < (int) song.mixer.size() && juce::File::isAbsolutePath (juce::String::fromUTF8 (song.mixer[(std::size_t) t].recordTo.c_str()));
            });
            if (all)
            {
                engine.capturePluginStates();
                const auto base = juce::File (juce::String::fromUTF8 (song.mixer[(std::size_t) list.front()].recordTo.c_str())).withFileExtension (extension);
                base.getParentDirectory().createDirectory();
                auto& engineRef = engine;
                const auto tracks = onlyTracks;
                const auto after = afterRender;
                close(); // deletes this panel: no `this` after this point
                (new StemsThread (engineRef, o, base, tracks, after))->launchThread();
                return;
            }
        }

        auto folder = lastFolder.isDirectory() ? lastFolder
                      : suggestedFolder.isDirectory() ? suggestedFolder
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = lynx::ui::makeFileChooser (tr ("Export audio"), folder.getChildFile (suggestedName + extension), "*" + extension);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, extension] (const juce::FileChooser& fc) {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension (extension))
                                      file = file.withFileExtension (extension);
                                  startExport (file);
                              });
    }

    // Renders to `file` with the window's choices (the file dialog's result, or "Same as last time"). Closes the window.
    void startExport (juce::File file)
    {
        const auto o = options;
        auto& engineRef = engine;
        const auto tracks = onlyTracks;
        const auto after = afterRender;
        const auto inRealTime = realtime;
        lastFolder = file.getParentDirectory();
        // round 190ap: what "Same as last time" repeats (the plain Export window only)
        if (persistent && additions)
            exportLastFile().replaceWithText (juce::String::fromUTF8 (exportopts::lastExportToText ({ o, file.getFullPathName().toStdString() }).c_str()));
        engineRef.capturePluginStates(); // the copy carries the current plugin state

        if (inRealTime) // round 165c: through the device, in real time
        {
            auto job = std::make_unique<ExportJob> (engineRef, o, file);
            job->setExtraData (true);
            close(); // deletes this panel: no `this` after this point
            auto* thread = new ExportThread (std::move (job), file, {}, tr ("Exporting in real time..."));
            thread->reveal = o.showWhenDone;
            thread->launchThread();
            return;
        }
        // (round 190ap: one file per marker section goes the same way as the stems: a list of renders)
        if (o.stems != exportopts::Stems::none || exportopts::exportsSections (engineRef.getSong(), o))
        {
            close();
            (new StemsThread (engineRef, o, file, tracks, after))->launchThread();
            return;
        }

        auto job = std::make_unique<ExportJob> (engineRef.getSong(), o, file);
        job->setExtraData (true);
        const auto withVideo = videoToggle.isVisible() && videoToggle.getToggleState() && o.range != exportopts::Range::pattern;
        close(); // deletes this panel: no `this` after this point
        auto* thread = new ExportThread (std::move (job), file);
        thread->reveal = o.showWhenDone;
        if (withVideo)
            thread->then = [song = engineRef.getSong(), o, file] { writeVideoWithMix (song, o, file); };
        thread->launchThread();
    }

public:
    std::function<void (const std::vector<juce::File>&)> afterRender; // showExportDialogForTracks: once the files are written

private:
    AudioEngine& engine;
    juce::File suggestedFolder;
    juce::String suggestedName;
    bool persistent = true;
    std::vector<int> onlyTracks;
    std::shared_ptr<ClipRenderMode> clip;
    bool masterMode = false;
    exportopts::Options beforeMaster;
    juce::Label masterLabel;
    juce::ComboBox masterBox;
    exportopts::Stems rememberedStems = exportopts::Stems::none;
    bool rememberedStemsMaster = false;
    exportopts::Options options;
    juce::Label tracksLabel, tracksText;

    settingsui::SectionTitle whatTitle, formatTitle, optionsTitle;
    juce::Label rangeLabel, loopsLabel, tailLabel, tailLengthLabel, stemsLabel;
    juce::ComboBox rangeBox, loopsBox, tailBox, tailLengthBox, stemsBox;
    juce::ToggleButton stemsMaster;
    // round 190ap: "One file per marker section", the presets row and "Same as last time"
    bool additions = false;
    juce::ToggleButton sectionsToggle;
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { tr ("Save...") }, deletePresetButton { tr ("Delete") }, sameAsLastButton { tr ("Same as last time") };
    std::vector<exportopts::Preset> presets;
    exportopts::LastExport lastExport;
    bool snapshotLast = false; // --versionsshot: the last export's folder need not exist
    Segmented formatButtons { { "WAV", "MP3", "OGG", "FLAC" } };
    juce::Label depthLabel, bitrateLabel, compressionLabel, rateLabel, channelsLabel, ditherLabel;
    juce::ComboBox depthBox, mp3Box, oggBox, flacLevelBox, rateBox, channelsBox, ditherBox;
    juce::ToggleButton normalizeToggle, masterFxToggle, insertFxToggle, trimToggle, hqToggle, showToggle, polyToggle, warmUpToggle, realtimeToggle;
    juce::ToggleButton tempoToggle, loopMarkersToggle, playlistMarkersToggle;
    juce::ToggleButton videoToggle; // round 174d: also a video with the mix (shown when the project has a video clip)
    bool realtime = false; // "Render through the hardware in real time" (round 165c; not remembered: it takes the song's length)
    juce::Label alsoLabel;
    std::array<juce::ToggleButton, 4> alsoToggles;
    juce::Label infoLabel, warningLabel;
    juce::TextButton exportButton { tr ("Export...") }, cancelButton { tr ("Cancel") };
    std::unique_ptr<juce::FileChooser> chooser;

    static inline juce::File lastFolder;
};
} // namespace

void bounceInsertToAudio (AudioEngine& engine, int track)
{
    const auto& song = engine.getSong();

    if (track <= 0 || track >= (int) song.mixer.size())
        return;

    engine.capturePluginStates();
    const auto name = juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str());
    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Renders");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (name), ".wav");

    ExportSettings settings;
    settings.format = ExportSettings::Format::wav32Float;
    settings.sampleRate = engine.getTransport().sampleRate.load();
    settings.tailSeconds = 4.0;

    // clip length: the song + the tail, at the ending tempo
    std::int64_t musicTicks = 0;
    makeExportSong (song, true, 0, musicTicks);
    const auto length = musicTicks + (std::int64_t) (settings.tailSeconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);

    auto job = std::make_unique<ExportJob> (makeBounceSong (song, track), settings, file);
    auto* engineRef = &engine;
    (new ExportThread (std::move (job), file, [engineRef, track, file, length] (bool ok) {
        if (! ok)
            return;

        engineRef->editSong ([&] (Song& s) { applyBounce (s, track, file.getFullPathName().toStdString(), length); });
    }, tr ("Rendering insert to audio...")))->launchThread();
}

// --- Freeze (round 190ay) ---------------------------------------------------------------------------------------------
namespace
{
struct FreezePlan
{
    Song song;               // the song as it was when the freeze started
    int channel = 0;
    std::uint64_t uid = 0;   // (the channel may have moved by the time the render ends)
    double rate = 44100.0;
    juce::File folder;
    juce::String name;
};

// what stops a freeze before any render, as a sentence for the user; empty = go
juce::String whyNoFreeze (AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    if (const auto* why = freeze::whyNot (song, channel))
        return tr (why);
    std::vector<const PluginRef*> refs;
    standins::pluginsOf (song.channels[(std::size_t) channel], refs);
    for (const auto* r : refs)
        if (! engine.getPluginHost().isInstalled (*r))
            return tr ("The plugin {0} is not installed on this PC: there is nothing to render.", { juce::String::fromUTF8 (r->name.c_str()) });
    return {};
}

FreezePlan planFreeze (AudioEngine& engine, int channel)
{
    engine.capturePluginStates(); // the plugin as it sounds now is what is rendered, and what Unfreeze brings back
    // the channel's windows go: a plugin window holds its plugin, which could then not be unloaded
    if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        PluginWindows::close (plugin->getInstance());
    closeChannelSettings (channel);
    if (PatcherWindows::isChannelOpen (channel))
        PatcherWindows::closeChannel (channel);
    FreezePlan plan;
    plan.song = engine.getSong();
    plan.channel = channel;
    plan.uid = plan.song.channels[(std::size_t) channel].uid;
    plan.rate = engine.getTransport().sampleRate.load();
    plan.folder = engine.getFreezeFolder();
    plan.name = juce::String::fromUTF8 (plan.song.channels[(std::size_t) channel].name.c_str());
    return plan;
}

void deleteStems (const std::vector<StandIn>& stems)
{
    for (const auto& s : stems)
        juce::File (juce::String::fromUTF8 (s.path.c_str())).deleteFile();
}

// the rendered stems onto the channel (found by its uid); the audio of an earlier freeze goes
void finishFreeze (AudioEngine& engine, const FreezePlan& plan, std::vector<StandIn> stems)
{
    std::vector<StandIn> old;
    bool found = false;
    for (const auto& c : engine.getSong().channels)
        if (c.uid == plan.uid && plan.uid != 0)
        {
            old = c.freeze.stems;
            found = true;
        }
    if (! found) // the channel was deleted meanwhile
    {
        deleteStems (stems);
        return;
    }
    const auto source = sessionpack::freezeSource (plan.song, plan.channel, plan.rate);
    engine.editSong ([&] (Song& s) { freeze::apply (s, plan.uid, stems, source); });
    for (const auto& o : old)
        if (std::none_of (stems.begin(), stems.end(), [&o] (const StandIn& n) { return n.path == o.path; }))
            juce::File (juce::String::fromUTF8 (o.path.c_str())).deleteFile();
}

class FreezeThread final : public juce::ThreadWithProgressWindow
{
public:
    FreezeThread (AudioEngine& e, FreezePlan p, std::function<void (bool)> done)
        : juce::ThreadWithProgressWindow (tr ("Freezing {0}...", { p.name }), true, true), engine (e), plan (std::move (p)), onDone (std::move (done))
    {
    }

    void run() override
    {
        ok = sessionpack::renderFreeze (plan.song, plan.channel, plan.rate, plan.folder, stems, [this] (double p) {
            setProgress (p);
            return ! threadShouldExit();
        }, error);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (ok)
        {
            finishFreeze (engine, plan, std::move (stems));
            ui::postHint ({ tr ("{0} is frozen: its audio plays and its instrument is unloaded. Right-click the channel to unfreeze it.", { plan.name }), {} }, 8000);
        }
        else if (! userPressedCancel)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not freeze"), error);
        if (onDone)
            onDone (ok);
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    AudioEngine& engine;
    FreezePlan plan;
    std::function<void (bool)> onDone;
    std::vector<StandIn> stems;
    juce::String error;
    bool ok = false;
};
} // namespace

void freezeChannel (AudioEngine& engine, int channel, std::function<void (bool)> done)
{
    if (const auto refusal = whyNoFreeze (engine, channel); refusal.isNotEmpty())
    {
        // (round 190bj: the title names the channel)
        const auto& song = engine.getSong();
        const auto name = channel >= 0 && channel < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) : juce::String();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                name.isNotEmpty() ? tr ("Can't freeze {0}", { name }) : tr ("Can't freeze this channel"), refusal);
        if (done)
            done (false);
        return;
    }
    auto* thread = new FreezeThread (engine, planFreeze (engine, channel), std::move (done));
    // round 190bj (QA: the progress window's first frame was a blank white box): the window is shown and painted once,
    // here on the message thread, before the render starts (launchThread starts the thread first and shows it after)
    if (auto* window = thread->getAlertWindow())
    {
        window->setVisible (true);
        if (auto* peer = window->getPeer())
        {
            window->repaint();
            peer->performAnyPendingRepaintsNow();
        }
    }
    thread->launchThread();
}

juce::String freezeRefusal (AudioEngine& engine, int channel)
{
    return whyNoFreeze (engine, channel);
}

juce::String freezeChannelNow (AudioEngine& engine, int channel, const std::function<bool (double)>& progress)
{
    if (const auto refusal = whyNoFreeze (engine, channel); refusal.isNotEmpty())
        return refusal;
    const auto plan = planFreeze (engine, channel);
    std::vector<StandIn> stems;
    juce::String error;
    if (! sessionpack::renderFreeze (plan.song, plan.channel, plan.rate, plan.folder, stems, progress, error))
        return error.isEmpty() ? juce::String ("cancelled") : error;
    finishFreeze (engine, plan, std::move (stems));
    return {};
}

void unfreezeChannel (AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size() || ! song.channels[(std::size_t) channel].freeze.on())
        return;
    const auto stems = song.channels[(std::size_t) channel].freeze.stems;
    engine.editSong ([channel] (Song& s) { freeze::clear (s, channel); });
    deleteStems (stems);
}

namespace
{
// FL's default render folder (FUN_011bf050): LYNX renders into Music/LYNX/Renders
juce::File renderFolder()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Renders");
    folder.createDirectory();
    return folder;
}

juce::File uniqueWav (const juce::File& folder, const std::string& name)
{
    return folder.getNonexistentChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())), ".wav", false);
}

double fileSeconds (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    return reader != nullptr && reader->sampleRate > 0.0 ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
}

void warn (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
}

// the FL message of a consolidation that cannot run
void explain (quickrender::Result r)
{
    using quickrender::Result;
    if (r == Result::empty)
        warn (tr ("Track is empty"), tr ("This track doesn't produce audio (or is completely muted). There is nothing to consolidate."));
    else if (r == Result::nothingSelected)
        warn (tr ("Nothing selected"), tr ("There is no unmuted playlist selection to consolidate."));
    else if (r == Result::tempoEvents)
        warn (tr ("Can't consolidate"), tr ("One of the selected pattern clips contain tempo events and cannot be consolidated."));
}

// the render window in clip mode (no file dialog), above the main window
void showClipRenderWindow (AudioEngine& engine, const juce::String& title, std::shared_ptr<ClipRenderMode> mode)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (fitContentToScreen (std::make_unique<ExportPanel> (engine, juce::File(), "LYNX SHARE", false, -1, std::vector<int> {}, std::move (mode))).release());
    options.dialogTitle = title;
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

// one render in the background (progress window); `apply` runs on the message thread with the file's length (s)
void renderInBackground (const Song& render, const exportopts::Options& o, const juce::File& file, const juce::String& title,
                         std::function<void (double)> apply)
{
    auto job = std::make_unique<ExportJob> (render, o, file);
    job->setExtraData (false); // (the file becomes a sample again: no loop / markers in it)
    (new ExportThread (std::move (job), file, [file, apply] (bool ok) {
        if (ok && apply)
            apply (fileSeconds (file));
    }, title))->launchThread();
}

void consolidate (AudioEngine& engine, const quickrender::Plan& plan, const juce::File& folder)
{
    engine.capturePluginStates();
    const auto render = quickrender::makePlanRenderSong (engine.getSong(), plan);
    auto mode = std::make_shared<ClipRenderMode>();
    mode->song = render;
    mode->button = tr ("Render");
    auto* engineRef = &engine;
    mode->onRender = [engineRef, plan, render, folder] (const exportopts::Options& o) {
        const auto file = uniqueWav (folder, plan.name);
        renderInBackground (render, o, file, tr ("Consolidating..."), [engineRef, plan, file] (double seconds) {
            const auto length = quickrender::ticksForSeconds (engineRef->getSong(), plan.start, seconds);
            engineRef->editSong ([&] (Song& s) { quickrender::applyConsolidation (s, plan, file.getFullPathName().toStdString(), length); });
        });
    };
    showClipRenderWindow (engine, tr ("Render to audio clip"), std::move (mode));
}

// File › Export › "All playlist tracks" / "All playlist track groups": one file per track from one set of choices
class TrackExportThread final : public juce::ThreadWithProgressWindow
{
public:
    struct Item
    {
        juce::String name;
        juce::File file;
        Song song;
    };

    TrackExportThread (std::vector<Item> list, exportopts::Options o, int skippedTracks, juce::File folder)
        : juce::ThreadWithProgressWindow (tr ("Exporting playlist tracks..."), true, true),
          items (std::move (list)),
          options (o),
          skipped (skippedTracks),
          where (std::move (folder))
    {
    }

    void run() override
    {
        for (std::size_t i = 0; i < items.size() && ! threadShouldExit(); ++i)
        {
            auto& item = items[i];
            setStatusMessage (item.name);
            JobOnMessageThread slot (*this);
            auto* job = slot.create ([&item, this] {
                auto j = std::make_unique<ExportJob> (item.song, options, item.file);
                j->setExtraData (false);
                return j;
            });
            if (job == nullptr)
                break; // (cancelled)

            juce::String error;
            const auto base = (double) i / (double) items.size();
            if (job->run ([this, base] (double p) {
                    setProgress (base + p / (double) items.size());
                    return ! threadShouldExit();
                }, error))
                ++written;
            else if (! threadShouldExit())
                failed = true;

            slot.release();
        }
    }

    void threadComplete (bool cancelled) override
    {
        // FL's messages (FUN_012fd7a0)
        if (failed)
            warn (tr ("Error"), tr ("Something went wrong while rendering playlist tracks."));
        else if (! cancelled && written > 0 && skipped > 0)
            warn (tr ("Tracks skipped"), tr ("Some tracks were skipped because they didn't have audio or were completely muted."));
        if (! cancelled && written > 0 && options.showWhenDone)
            where.startAsProcess();
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    std::vector<Item> items;
    exportopts::Options options;
    int skipped = 0, written = 0;
    bool failed = false;
    juce::File where;
};
} // namespace

void consolidatePlaylistTrack (AudioEngine& engine, int track, bool fromSongStart)
{
    // FL's "Consolidate this track" (FUN_012ea170 → FUN_012fc180): the render window, then a new track above holding the
    // render, the old track grouped under it and its clips muted; Shift held = a folder is asked for (FL's VK_SHIFT test)
    quickrender::Plan plan;
    const auto r = quickrender::planTrack (engine.getSong(), track, false, fromSongStart ? quickrender::Start::songStart : quickrender::Start::clipStart, plan);
    if (r != quickrender::Result::ok)
    {
        explain (r);
        return;
    }
    if (! juce::ModifierKeys::currentModifiers.isShiftDown())
    {
        consolidate (engine, plan, renderFolder());
        return;
    }
    auto chooser = lynx::ui::makeSharedFileChooser (tr ("Select a folder to consolidate to"), renderFolder());
    auto* engineRef = &engine;
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [engineRef, plan, chooser] (const juce::FileChooser& fc) {
        if (fc.getResult().isDirectory())
            consolidate (*engineRef, plan, fc.getResult());
    });
}

void consolidatePlaylistSelection (AudioEngine& engine, const std::vector<int>& selectedClips, bool fromSongStart)
{
    quickrender::Plan plan;
    const auto r = quickrender::planSelection (engine.getSong(), selectedClips, fromSongStart, plan);
    if (r != quickrender::Result::ok)
    {
        explain (r);
        return;
    }
    consolidate (engine, plan, renderFolder());
}

void renderPatternAsAudioClip (AudioEngine& engine, int pattern, int how)
{
    const auto& song = engine.getSong();
    if (! quickrender::patternRenderable (song, pattern))
    {
        warn (tr ("Warning"), tr ("Pattern is empty, nothing to render."));
        return;
    }
    engine.capturePluginStates();
    const auto render = quickrender::makePatternRenderSong (song, pattern);
    const auto name = quickrender::patternName (song, pattern);
    auto* engineRef = &engine;
    const bool replace = how == 2;
    auto go = [engineRef, render, name, pattern, replace] (const exportopts::Options& o) {
        const auto file = uniqueWav (renderFolder(), name);
        renderInBackground (render, o, file, tr ("Rendering pattern..."), [engineRef, file, pattern, replace] (double) {
            engineRef->editSong ([&] (Song& s) { quickrender::applyPatternRender (s, pattern, file.getFullPathName().toStdString(), replace); });
        });
    };
    if (how == 1) // FL's "Quick render as audio clip": the last choices, no window (flag 1)
    {
        go (clipRenderOptions (true));
        return;
    }
    auto mode = std::make_shared<ClipRenderMode>();
    mode->pattern = true;
    mode->song = render;
    mode->button = tr ("Render");
    mode->onRender = go;
    showClipRenderWindow (engine, replace ? tr ("Render and replace") : tr ("Render as audio clip"), std::move (mode));
}

namespace
{
// FL's loop over the picked patterns (PSPatQuickRenderMenuClick FUN_011884b0, REA_rodada169k §2.1)
struct PatternBatch
{
    AudioEngine* engine = nullptr;
    std::vector<int> patterns;
    std::vector<Song> renders;
    std::vector<std::string> names;
    exportopts::Options options;
    bool replace = false;
    int warning = 0;
    std::vector<std::pair<int, juce::String>> done; // pattern, rendered file
};

void finishPatternBatch (const std::shared_ptr<PatternBatch>& b)
{
    if (! b->done.empty())
        b->engine->editSong ([b] (Song& s) {
            for (const auto& [pattern, file] : b->done)
                quickrender::applyPatternRender (s, pattern, file.toStdString(), b->replace);
        });
    if (b->warning == 1)
        warn (tr ("Warning"), tr ("Pattern is empty, nothing to render."));
    else if (b->warning == 2)
        warn (tr ("Warning"), tr ("Empty patterns were ignored."));
}

void runPatternBatch (std::shared_ptr<PatternBatch> b, std::size_t k)
{
    if (k >= b->patterns.size())
    {
        finishPatternBatch (b);
        return;
    }
    const auto file = uniqueWav (renderFolder(), b->names[k]);
    auto job = std::make_unique<ExportJob> (b->renders[k], b->options, file);
    job->setExtraData (false);
    (new ExportThread (std::move (job), file, [b, k, file] (bool ok) {
        if (! ok) // cancelled or failed: FL stops the loop; what was rendered stays
        {
            finishPatternBatch (b);
            return;
        }
        b->done.push_back ({ b->patterns[k], file.getFullPathName() });
        runPatternBatch (b, k + 1);
    }, tr ("Rendering pattern...")))->launchThread();
}
} // namespace

void renderPatternsAsAudioClips (AudioEngine& engine, const std::vector<int>& patterns, int how)
{
    if (patterns.size() == 1)
    {
        renderPatternAsAudioClip (engine, patterns.front(), how);
        return;
    }
    const auto& song = engine.getSong();
    const auto plan = pickersel::planRender (patterns, [&song] (int p) { return quickrender::patternRenderable (song, p); });
    if (plan.render.empty())
    {
        if (const auto w = pickersel::skippedWarning (plan); w != 0)
            warn (tr ("Warning"), w == 1 ? tr ("Pattern is empty, nothing to render.") : tr ("Empty patterns were ignored."));
        return;
    }
    engine.capturePluginStates();
    auto batch = std::make_shared<PatternBatch>();
    batch->engine = &engine;
    batch->patterns = plan.render;
    batch->replace = how == 2;
    batch->warning = pickersel::skippedWarning (plan);
    for (const auto p : plan.render)
    {
        batch->renders.push_back (quickrender::makePatternRenderSong (song, p));
        batch->names.push_back (quickrender::patternName (song, p));
    }
    if (how == 1) // Quick: the last choices, no window
    {
        batch->options = clipRenderOptions (true);
        runPatternBatch (batch, 0);
        return;
    }
    // the window once, for the first pattern; the others follow with the same choices (FL's flag 1 after the first)
    auto mode = std::make_shared<ClipRenderMode>();
    mode->pattern = true;
    mode->song = batch->renders.front();
    mode->button = tr ("Render");
    mode->onRender = [batch] (const exportopts::Options& o) {
        batch->options = o;
        runPatternBatch (batch, 0);
    };
    showClipRenderWindow (engine, batch->replace ? tr ("Render and replace") : tr ("Render as audio clip"), std::move (mode));
}

void exportAllPlaylistTracks (AudioEngine& engine, const juce::String& projectName, quickrender::Start start, bool groups)
{
    // FL (FUN_012fd7a0): first the folder ("Select rendering folder"), then the render window once for every track
    auto chooser = lynx::ui::makeSharedFileChooser (tr ("Select the folder where output should be rendered..."), renderFolder());
    auto* engineRef = &engine;
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [engineRef, projectName, start, groups, chooser] (const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;
        auto& engine = *engineRef;
        engine.capturePluginStates();
        auto song = engine.getSong();
        if (start != quickrender::Start::timeSelection)
            song.loopStart = song.loopEnd = 0; // FL clears the time selection for the other two
        std::vector<TrackExportThread::Item> items;
        int skipped = 0;
        const auto name = projectName.isNotEmpty() ? projectName.toStdString() : std::string ("LYNX SHARE");
        juce::Array<juce::File> taken; // (two tracks of the same name get two files)
        for (auto t : quickrender::exportTracks (song, groups))
        {
            quickrender::Plan plan;
            if (quickrender::planTrack (song, t, groups, start, plan) != quickrender::Result::ok)
            {
                ++skipped;
                continue;
            }
            const auto fileName = quickrender::trackExportName (song, name, t);
            auto file = uniqueWav (folder, fileName);
            for (int k = 2; taken.contains (file); ++k)
                file = uniqueWav (folder, fileName + " (" + std::to_string (k) + ")");
            taken.add (file);
            items.push_back ({ juce::String::fromUTF8 (fileName.c_str()), file, quickrender::makePlanRenderSong (song, plan) });
        }
        if (items.empty())
        {
            warn (tr ("Nothing rendered"), tr ("Nothing was rendered because there are no unmuted clips in any of the selected tracks."));
            return;
        }
        auto mode = std::make_shared<ClipRenderMode>();
        mode->song = items.front().song;
        mode->button = tr ("Render");
        mode->onRender = [items, skipped, folder] (const exportopts::Options& o) mutable {
            (new TrackExportThread (std::move (items), o, skipped, folder))->launchThread();
        };
        showClipRenderWindow (engine, groups ? tr ("Export all playlist track groups") : tr ("Export all playlist tracks"), std::move (mode));
    });
}

int writeProjectBones (const Song& song, const juce::File& dir, juce::StringArray& failed)
{
    int written = 0;
    for (const auto& bone : quickrender::projectBones (song))
    {
        const auto folder = dir.getChildFile (bone.folder);
        folder.createDirectory();
        const auto name = juce::String::fromUTF8 (bone.name.c_str());
        bool ok = false;
        juce::File file;
        if (bone.kind == quickrender::Bone::Kind::mixer)
        {
            file = folder.getChildFile (name + ProjectFile::mixerTrackStateExtension);
            ok = file.replaceWithText (ProjectFile::mixerTrackStateToJson (song.mixer[(std::size_t) bone.track]));
        }
        else if (bone.kind == quickrender::Bone::Kind::effect)
        {
            const auto& slot = song.mixer[(std::size_t) bone.track].effects[(std::size_t) bone.slot];
            file = folder.getChildFile (name + pluginpresets::extension);
            if (slot.kind == EffectKind::plugin)
            {
                pluginpresets::Preset preset;
                preset.name = slot.plugin.name;
                preset.pluginName = slot.plugin.name;
                preset.pluginId = slot.plugin.id;
                juce::MemoryBlock state;
                if (state.fromBase64Encoding (juce::String (slot.plugin.state)))
                    preset.state.assign ((const std::uint8_t*) state.getData(), (const std::uint8_t*) state.getData() + state.getSize());
                ok = ! preset.state.empty() && file.replaceWithText (juce::String::fromUTF8 (pluginpresets::toText (preset).c_str()));
            }
            else
                ok = file.replaceWithText (juce::String::fromUTF8 (presets::toText (slot.kind, bone.name, slot.params).c_str()));
        }
        else
        {
            auto copy = song;
            auto& notes = copy.patterns[(std::size_t) bone.pattern].notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&bone] (const Note& n) { return n.channel != bone.channel; }), notes.end());
            file = folder.getChildFile (name + ".mid");
            juce::String error;
            ok = MidiFileIo::savePattern (copy, bone.pattern, file, error);
        }
        if (ok)
            ++written;
        else
            failed.add (juce::String::fromUTF8 (bone.folder.c_str()) + "/" + name);
    }
    return written;
}

void exportProjectBones (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName)
{
    // FL (FUN_011edb50): "Export project bones", a folder in which the bones go to a sub-folder named after the project
    auto chooser = lynx::ui::makeSharedFileChooser (tr ("Save project bones (channel / effect presets & scores) subdirectory in..."),
                                                        suggestedFolder.isDirectory() ? suggestedFolder : renderFolder());
    auto* engineRef = &engine;
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [engineRef, projectName, chooser] (const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;
        engineRef->capturePluginStates();
        const auto& song = engineRef->getSong();
        const auto base = juce::String::fromUTF8 (quickrender::safeFileName (projectName.toStdString(), "LYNX SHARE").c_str());
        auto dir = folder.getChildFile (base);
        if (! dir.createDirectory())
        {
            warn (tr ("Error"), tr ("Could not create the folder {0}", { dir.getFullPathName() }));
            return;
        }
        juce::StringArray failed;
        const auto written = writeProjectBones (song, dir, failed);
        if (written == 0 && failed.isEmpty())
        {
            warn (tr ("Export project bones"), tr ("The project has no mixer states, effects or notes to export."));
            return;
        }
        if (! failed.isEmpty())
            warn (tr ("Export project bones"), tr ("Some files could not be written:") + "\n" + failed.joinIntoString ("\n"));
        dir.startAsProcess(); // (the folder, like FL's other exports to a folder)
    });
}

std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine, int format)
{
    auto panel = std::make_unique<ExportPanel> (engine, juce::File(), "LYNX SHARE", false);
    panel->setFormatForSnapshot (format);
    return panel;
}

std::unique_ptr<juce::Component> makeExportPanelWithAdditions (AudioEngine& engine)
{
    auto panel = std::make_unique<ExportPanel> (engine, juce::File(), "Song", false, 0);
    panel->setAdditionsForSnapshot();
    return panel;
}

std::unique_ptr<juce::Component> makeExportPanelForTracks (AudioEngine& engine, const std::vector<int>& tracks)
{
    return std::make_unique<ExportPanel> (engine, juce::File(), "LYNX SHARE", false, 0, tracks);
}

void showExportDialogForTracks (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName,
                                const std::vector<int>& tracks, std::function<void (const std::vector<juce::File>&)> afterRender)
{
    juce::DialogWindow::LaunchOptions options;
    auto panel = std::make_unique<ExportPanel> (engine, suggestedFolder, suggestedName.isNotEmpty() ? suggestedName : "LYNX SHARE", true, -1, tracks);
    panel->afterRender = std::move (afterRender);
    options.content.setOwned (fitContentToScreen (std::move (panel)).release());
    options.dialogTitle = tr ("Render mixer tracks to audio files");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

void showMasterDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName)
{
    // FL: "Can't master while recording" (FUN_00edf6a0); the file is "<project> (mastered)" (FUN_00edfda0)
    if (engine.isRecording())
    {
        warn (tr ("Can't master while recording"), tr ("Mastering isn't possible while recording.\nPlease stop recording to master the project."));
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    auto panel = std::make_unique<ExportPanel> (engine, suggestedFolder, (projectName.isNotEmpty() ? projectName : juce::String ("LYNX SHARE")) + " (mastered)", true);
    panel->enableMaster();
    options.content.setOwned (fitContentToScreen (std::move (panel)).release());
    options.dialogTitle = tr ("Master");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName, int format)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (fitContentToScreen (std::make_unique<ExportPanel> (engine, suggestedFolder, suggestedName.isNotEmpty() ? suggestedName : "LYNX SHARE", true, format)).release());
    options.dialogTitle = tr ("Export audio");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace lynx
