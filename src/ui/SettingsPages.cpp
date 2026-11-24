#include "ui/AutoLookEditor.h"
#include "ui/RemoteControl.h"
#include "core/EscClose.h"
#include "ui/NumBox.h"
#include "ui/DemoMode.h" // round 190pl
#include "ui/SettingsPages.h"
#include "ui/LabelFont.h"
#include "core/TextSize.h"
#include "ui/RemoteControlSettings.h"
#include "platform/Paths.h"
#include "platform/FileTypes.h"
#include "platform/Package.h"

#include "core/RenderAhead.h"
#include "core/FlSettings.h"
#include "core/LayoutStyle.h"
#include "core/LynxExtras.h"
#include "core/Templates.h" // round 190oo (CS O-2: newProjectDefaults)
#include "ui/PolyEditor.h"
#include "ui/WorkflowExtras.h" // round 190iu
#include "core/PlaylistOps.h"
#include "core/ToolbarLayout.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "platform/LinuxRealtime.h"
#include "ui/AutomationUi.h"
#include "ui/DetailView.h"
#include "ui/Dialogs.h"
#include "ui/MapMode.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/NewProjectDialog.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/PluginSkin.h"
#include "ui/SimpleLook.h" // round 190oo (O-4)
#include "ui/Updater.h"

#include <algorithm>
#include <array>

namespace lynx
{
using namespace settingsui;

juce::File autosaveFolder()
{
    return lynx::platform::settingsFile ("autosave");
}

namespace
{
constexpr int labelWidth = 190;
constexpr int rowHeight = 26;
constexpr int rowGap = 5; // round 163: tighter (was 8)
constexpr int sectionHeight = 24;

void styleCombo (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::outlineColourId, theme::colour::borderStrong);
}

void styleToggle (juce::ToggleButton& t, const juce::String& text)
{
    t.setButtonText (text);
}

// Lays out rows top to bottom inside a page of a given width.
struct Rows
{
    juce::Rectangle<int> area;
    int y = 0;

    explicit Rows (int width) : area (20, 14, width - 40, 100000), y (14) {}

    // Settings › General › Text size (issue #189): above 100 % the rows and notes grow with the text, so a two-line note
    // or a control's text is not cut (the page scrolls)
    static int h (int height) { return juce::roundToInt ((float) height * juce::jmax (1.0f, (float) theme::textSize() / 100.0f)); }

    void section (juce::Component& title, bool first = false)
    {
        if (! first)
            y += 10;
        title.setBounds (area.getX(), y, area.getWidth(), h (sectionHeight));
        y += h (sectionHeight) + rowGap;
    }

    // label + control (control width 0 = to the right edge)
    // round 165o: a caption wider than the label column (a long translation) gets its own line above the control
    juce::Rectangle<int> row (juce::Label& label, int controlWidth = 260, int height = rowHeight)
    {
        height = h (height);
        if (theme::labelColumnWidth ({ &label }, 0, 2000) > labelWidth + labelWidth / 10) // a slight squeeze is fine
        {
            label.setBounds (area.getX(), y, area.getWidth(), h (18));
            y += h (18);
        }
        else
            label.setBounds (area.getX(), y, labelWidth, height);
        auto r = juce::Rectangle<int> (area.getX() + labelWidth, y, controlWidth > 0 ? controlWidth : area.getWidth() - labelWidth, height);
        y += height + rowGap;
        return r;
    }

    juce::Rectangle<int> full (int height = rowHeight)
    {
        height = h (height);
        auto r = juce::Rectangle<int> (area.getX(), y, area.getWidth(), height);
        y += height + rowGap;
        return r;
    }

    juce::Rectangle<int> indented (int height = rowHeight)
    {
        height = h (height);
        auto r = juce::Rectangle<int> (area.getX() + labelWidth, y, area.getWidth() - labelWidth, height);
        y += height + rowGap;
        return r;
    }

    // a note under a control: as tall as its text needs at the width it gets (wrapped), at least `minHeight` (issue #189:
    // a larger Text size wraps a note into more lines than the fixed heights had room for)
    // (round 190oo WG-32: `indent` false: the full page width, as full() places it)
    juce::Rectangle<int> indentedNote (juce::Label& label, int minHeight, bool indent = true)
    {
        const auto left = indent ? labelWidth : 0;
        const auto width = area.getWidth() - left;
        const auto border = label.getBorderSize();
        juce::GlyphArrangement text;
        text.addJustifiedText (label.getFont(), label.getText(), 0.0f, 0.0f, (float) juce::jmax (40, width - border.getLeftAndRight()),
                               juce::Justification::topLeft);
        const auto needed = (int) std::ceil (text.getBoundingBox (0, -1, true).getHeight()) + border.getTopAndBottom() + 2;
        // (the standard size keeps its layout to the pixel, unless the text needs another line: round 190oj WG-3, the
        // German Animation-rate note was squeezed to ~7 px and cut in its fixed 32 px)
        if (theme::textSize() <= 100 && needed <= h (minHeight) + 6)
            return indent ? indented (minHeight) : full (minHeight);
        auto r = juce::Rectangle<int> (area.getX() + left, y, width, juce::jmax (h (minHeight), needed));
        y += r.getHeight() + rowGap;
        return r;
    }

    int bottom() const { return y + 14; }

    // round 190bj (QA: Settings › MIDI's game-controller line was drawn squeezed sideways): a note label as tall as its
    // text needs at the page's width (never less than `minHeight`), wrapped, never squeezed
    // (`indent`: under the controls, past the label column, as indented() places it)
    juce::Rectangle<int> note (juce::Label& label, int minHeight, int lift = 3, bool indent = false)
    {
        label.setMinimumHorizontalScale (1.0f);
        const auto border = label.getBorderSize();
        juce::AttributedString text;
        text.append (label.getText(), label.getFont());
        text.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout (text, (float) juce::jmax (1, area.getWidth() - (indent ? labelWidth : 0) - border.getLeftAndRight()));
        const auto height = juce::jmax (h (minHeight), (int) std::ceil (layout.getHeight()) + border.getTopAndBottom() + 1);
        const auto left = indent ? labelWidth : 0;
        auto r = juce::Rectangle<int> (area.getX() + left, y, area.getWidth() - left, height); // (measured: not scaled again)
        y += height + rowGap;
        return r.translated (0, -lift);
    }
};

// ================================================================================================================
// General
// ================================================================================================================
class GeneralPage final : public ScrollingPage, private juce::Timer
{
public:
    GeneralPage (AudioEngine& e, SettingsHooks h, bool keep) : engine (e), hooks (std::move (h)), persistent (keep)
    {
        if (hooks.getGeneral)
            options = hooks.getGeneral();
        else
            options.language = i18n::current(); // --snapshot: the language of the run

        for (auto* c : std::initializer_list<juce::Component*> { &workflowTitle, &layoutStyleLabel, &layoutStyleBox, &layoutStyleNote, &alsSessionLabel, &alsSessionBox, &languageTitle, &displayTitle, &startupTitle, &editingTitle, &backupTitle,
                                                                 &pluginsTitle, &languageLabel, &languageBox, &languageNote, &scaleLabel, &scaleBox, &menuLabel, &menuBox,
                                                                 &wavesLabel, &wavesBox, &stereoWaves, &noteNamesLabel, &noteNamesBox, &blackNotesLabel, &blackNotesBox, &everyKeyNames, &rackColumn, &rackStepColour, &pluginStyleLabel, &pluginStyleBox, &artMotion, &reducedEffects, &uiAnimations, &animationLabel, &animationBox, &animationNote, &gpuRendering, &editorsInMain, &templateLabel, &templateBox, &templateNote, &startupProjectLabel, &startupProjectBox, &splash, &startupSound, &scanAtStart, &supportNoteBox, &menuEntry,
                                                                 &typingPiano, &keepEditors, &rememberSeek, &clipsLoop, &autoClipsAtPlayback, &firstChannel, &monoPaint, &altMeterScale, &autoSelectLinked, &noTypingPriority, &hoverValues, &escCloses, &editInLabel, &editInBox, &undoLabel,
                                                                 &undoBox, &alternateUndo, &hintsTitle, &manageMessages, &messagesNote, &autosaveLabel, &autosaveBox, &keepLabel, &keepBox, &openBackups, &backupQuiet, &longestWaitLabel, &longestWaitBox, &unusedTakes, &detachNew,
                                                                 &sandboxLabel, &sandboxBox, &sandboxNote, &substitutes, &substitutesNote, &sampleAccurate, &limitEditors, &managePlugins, &manageShortcuts, &shortcutsNote,
                                                                 &reportsTitle, &crashLabel, &crashBox, &crashNote, &updatesTitle, &checkUpdates, &checkNow, &updatesNote,
                                                                 &previousLabel, &goBack, &autoNameChannels, &restoreAfterSolo, &readSampleTempo, &historyNewestFirst, &projectLayout, &historyOnUndo, &undoKnobTweaks, &dataSizeLabel, &dataSizeBox, &clickHold, &bothButtons })
            add (*c);

        setupFontRow();
        styleLabel (layoutStyleLabel, tr ("Layout style"));
        layoutStyleBox.setComponentID ("layoutStyle"); // (--simpleshot finds it)
        layoutStyleBox.addItem (tr ("Pattern workflow (default)"), layoutstyle::comboId (layoutstyle::Style::pattern));
        layoutStyleBox.addItem (tr ("Clip-launch workflow"), layoutstyle::comboId (layoutstyle::Style::clipLaunch));
        layoutStyleBox.addItem (tr ("Simple workflow"), layoutstyle::comboId (layoutstyle::Style::simple));
        layoutStyleBox.setSelectedId (layoutstyle::comboId ((layoutstyle::Style) juce::jlimit (0, layoutstyle::count - 1, options.layoutStyle)), juce::dontSendNotification);
        layoutStyleBox.onChange = [this] {
            options.layoutStyle = (int) layoutstyle::fromComboId (layoutStyleBox.getSelectedId());
            if (hooks.setGeneral)
                hooks.setGeneral (options);
        };
        styleNote (layoutStyleNote, tr ("Three workflows in one program. Switch any time in View › Layout style: the project stays the same, only the windows change.") + "\n"
                                        + tr ("Pattern workflow: works the way FL Studio does, with the Channel Rack, Piano Roll, Playlist and Mixer.") + "\n"
                                        + tr ("Clip-launch workflow: clips and scenes launched from a grid, in the style of Ableton Live, with a Session mixer and the clip and device views; patterns and the Channel Rack stay one click away.") + "\n"
                                        + tr ("Simple workflow: BandLab's approachable one-window style mixed with FL Studio Mobile's way of working, with tracks and a timeline, loops, pads and quick recording; FL Studio Mobile projects (.flm) open too."));
        styleLabel (alsSessionLabel, tr ("Session clips of Live sets"));
        alsSessionBox.addItem (tr ("Clip grid (default)"), 1);
        alsSessionBox.addItem (tr ("Playlist, after the arrangement"), 2);
        alsSessionBox.setSelectedId (options.alsSessionOnPlaylist ? 2 : 1, juce::dontSendNotification);
        alsSessionBox.onChange = [this] { change ([&] { options.alsSessionOnPlaylist = alsSessionBox.getSelectedId() == 2; }); };
        add (remoteTitle);
        add (remoteControl);
        add (remoteStatus);
        toggle (remoteControl, tr ("Enable local remote control"), options.remoteControl,
                [this] (bool on) { options.remoteControl = on; });
        remoteControl.setEnabled (hooks.setGeneral != nullptr);
        remoteControl.setTooltip (tr ("Allows authenticated local applications to read and edit this project."));
        styleNote (remoteStatus, tr (remoteui::status()));
        startTimerHz (2);

        styleLabel (languageLabel, tr ("Language"));
        styleNote (languageNote, tr ("A new language is applied when LYNX SHARE restarts."));
        int id = 1;
        for (const auto& l : i18n::languages())
        {
            languageBox.addItem (juce::String::fromUTF8 (l.nativeName), id);
            if (options.language == l.id)
                languageBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
        languageBox.onChange = [this] {
            const auto index = languageBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (index, (int) i18n::languages().size()))
                change ([&] { options.language = i18n::languages()[(std::size_t) index].id; });
            updateTranslationLink();
            resized();
        };
        // the translations added in 0.1.20: an invitation to improve them (GitHub issue form)
        translationLink.setFont (juce::FontOptions (13.0f), false, juce::Justification::centredLeft);
        translationLink.setColour (juce::HyperlinkButton::textColourId, theme::colour::accent);
        add (translationLink);
        updateTranslationLink();

        styleLabel (scaleLabel, tr ("Interface size"));
        for (auto percent : toolbarlayout::uiScales)
            scaleBox.addItem (juce::String (percent) + "%", percent);
        scaleBox.setSelectedId (options.uiScale, juce::dontSendNotification);
        scaleBox.onChange = [this] { change ([&] { options.uiScale = scaleBox.getSelectedId(); }); };

        styleLabel (menuLabel, tr ("Menu density"));
        menuBox.addItem (tr ("Compact"), 1);
        menuBox.addItem (tr ("Comfortable"), 2);
        menuBox.setSelectedId (options.compactMenus ? 1 : 2, juce::dontSendNotification);
        menuBox.onChange = [this] { change ([&] { options.compactMenus = menuBox.getSelectedId() == 1; }); };

        styleLabel (wavesLabel, tr ("Waveforms"));
        wavesBox.addItem (tr ("Clean (the clip colour)"), 1);
        wavesBox.addItem (tr ("Spectral colours (warm = bass, cool = treble)"), 2);
        wavesBox.setSelectedId (options.waveSpectral ? 2 : 1, juce::dontSendNotification);
        wavesBox.onChange = [this] { change ([&] { options.waveSpectral = wavesBox.getSelectedId() == 2; }); };
        toggle (stereoWaves, tr ("Stereo (left and right channels apart)"), options.waveStereo, [this] (bool on) { options.waveStereo = on; });
        styleLabel (noteNamesLabel, tr ("Note names"));
        noteNamesBox.addItem (tr ("English (C, D, E)"), 1);
        noteNamesBox.addItem (tr ("Germanic (H for B)"), 2);
        noteNamesBox.addItem (tr ("Solfège (do, re, mi)"), 3);
        noteNamesBox.setSelectedId (juce::jlimit (0, 2, options.noteNames) + 1, juce::dontSendNotification);
        noteNamesBox.onChange = [this] { change ([&] { options.noteNames = noteNamesBox.getSelectedId() - 1; }); };
        styleLabel (blackNotesLabel, tr ("Black notes"));
        blackNotesBox.addItem (tr ("Sharps (#)"), 1);
        blackNotesBox.addItem (tr ("Flats (b)"), 2);
        blackNotesBox.setSelectedId (options.noteFlats ? 2 : 1, juce::dontSendNotification);
        blackNotesBox.onChange = [this] { change ([&] { options.noteFlats = blackNotesBox.getSelectedId() == 2; }); };
        toggle (everyKeyNames, tr ("Note names on every Piano roll key"), options.noteNamesEveryKey, [this] (bool on) { options.noteNamesEveryKey = on; });
        toggle (rackColumn, tr ("Playhead column in the Channel Rack"), options.rackPlayheadColumn, [this] (bool on) { options.rackPlayheadColumn = on; });
        toggle (rackStepColour, tr ("Steps use channel colour"), options.rackStepsChannelColour, [this] (bool on) { options.rackStepsChannelColour = on; });
        // (FL's "Show complete piano roll preview" is in the Channel rack menu's Options, as in FL: round 190cj)
        styleLabel (pluginStyleLabel, tr ("Plugin style"));
        pluginStyleBox.addItem (tr ("Automatic (each plugin its own style)"), 1);
        for (int i = 0; i < skin::styleCount; ++i)
            pluginStyleBox.addItem (tr ("{0} for every plugin", { tr (skin::styleName ((skin::Style) i)) }), i + 2);
        pluginStyleBox.setSelectedId (juce::jlimit (-1, skin::styleCount - 1, options.pluginStyle) + 2, juce::dontSendNotification);
        pluginStyleBox.onChange = [this] { change ([&] { options.pluginStyle = pluginStyleBox.getSelectedId() - 2; }); };
        toggle (artMotion, tr ("Animate the plugin art (falling petals, glows; Themed style)"), options.artMotion,
                [this] (bool on) { options.artMotion = on; });
        toggle (reducedEffects, tr ("Reduced effects in plugin editors (no glows or animations, for slower graphics cards)"), options.reducedEffects,
                [this] (bool on) { options.reducedEffects = on; });
        // round 179a: FL's General › Animations, as one switch (REA_rodada179a.md part D)
        toggle (uiAnimations, tr ("Animations: panels fade in and out, buttons light up gently, the wheel glides"), options.uiAnimations,
                [this] (bool on) { options.uiAnimations = on; });
        // round 174e2: how often the playhead, meters and scopes move (the display's refresh by default, as FL)
        styleLabel (animationLabel, tr ("Animation rate"));
        // round 190ip: Balanced (Live's rule: the refresh halved until it is at most 100 Hz); the capped rates are evenly paced
        animationBox.addItem (tr ("Display (default)"), 1);
        animationBox.addItem (tr ("Balanced"), 4); // (the note says what it is: a narrow box)
        animationBox.addItem (tr ("60 Hz"), 2);
        animationBox.addItem (tr ("30 Hz"), 3);
        animationBox.setSelectedId (options.animationRate == 30 ? 3 : options.animationRate == 60 ? 2 : options.animationRate < 0 ? 4 : 1,
                                    juce::dontSendNotification);
        animationBox.onChange = [this] {
            change ([&] {
                const auto id = animationBox.getSelectedId();
                options.animationRate = id == 3 ? 30 : id == 2 ? 60 : id == 4 ? -1 : 0;
            });
        };
        styleNote (animationNote, tr ("How often the playhead, meters and scopes move. Display follows your screen's refresh rate "
                                      "(smoothest); Balanced halves it on fast screens (72 Hz on a 144 Hz screen); 60 Hz or 30 Hz "
                                      "use the least CPU (laptops, slower computers)."));
        toggle (gpuRendering, tr ("Draw with the GPU (Direct2D). Turn off if LYNX SHARE crashes in the graphics driver or the screen flickers"),
                options.gpuRendering, [this] (bool on) { options.gpuRendering = on; });
       #if ! JUCE_WINDOWS
        gpuRendering.setVisible (false);
       #endif
        // round 190jy (queue item 44, LYNX's own): a docked LYNX editor drawn by the main window, so its live display
        // costs only its paint; hosted plugins and detached windows keep windows of their own
        toggle (editorsInMain, tr ("Draw docked LYNX editors in the main window (lighter live displays)"), options.editorsInMainWindow,
                [this] (bool on) { options.editorsInMainWindow = on; });
        editorsInMain.setTooltip (tr ("On: a docked LYNX editor (Gross Beat, Fruity Limiter, Maximus...) is drawn by the main window: "
                                      "its moving displays use less CPU. Hosted plugins and detached windows keep windows of their own"));
       #if ! JUCE_WINDOWS
        editorsInMain.setVisible (false);
       #endif

        // FL's Options › General › Miscellaneous › "Default template" (REA_rodada186a; issue #123)
        styleLabel (templateLabel, tr ("Default template"));
        templateChoices = availableTemplates();
        for (int i = 0; i < (int) templateChoices.size(); ++i)
        {
            if (i == 1 || (templateChoices[(std::size_t) i].kind == TemplateChoice::Kind::user && templateChoices[(std::size_t) i - 1].kind != TemplateChoice::Kind::user))
                templateBox.addSeparator();
            templateBox.addItem (i == 0 ? tr ("Default (LYNX SHARE)") : templateChoiceName (templateChoices[(std::size_t) i]), i + 1);
        }
        {
            const auto current = TemplateChoice::parse (options.defaultTemplate.toStdString());
            auto at = std::find (templateChoices.begin(), templateChoices.end(), current);
            if (at == templateChoices.end()) // a user template whose file is gone: shown, so it can be changed
            {
                templateChoices.push_back (current);
                templateBox.addItem (tr ("{0} (not found)", { templateChoiceName (current) }), (int) templateChoices.size());
                at = templateChoices.end() - 1;
            }
            templateBox.setSelectedId ((int) (at - templateChoices.begin()) + 1, juce::dontSendNotification);
        }
        templateBox.setTooltip (tr ("Select default project template"));
        templateBox.onChange = [this] {
            const auto index = templateBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (index, (int) templateChoices.size()))
                change ([&] { options.defaultTemplate = juce::String::fromUTF8 (templateChoices[(std::size_t) index].toText().c_str()); });
        };
        // FL's "Startup project", under it (FL's order)
        styleLabel (startupProjectLabel, tr ("Startup project"));
        startupProjectBox.addItem (tr ("Empty project"), 1);
        startupProjectBox.addItem (tr ("Default template"), 2);
        startupProjectBox.addItem (tr ("Last used project"), 3);
        startupProjectBox.setSelectedId (juce::jlimit (0, 2, options.startupProject) + 1, juce::dontSendNotification);
        startupProjectBox.onChange = [this] { change ([&] { options.startupProject = startupProjectBox.getSelectedId() - 1; }); };
        styleNote (templateNote, tr ("The default template opens with File › New, and at start-up when Startup project is \"Default template\", "
                                     "as a new untitled project, so the template itself is never overwritten. File › Save as template... adds your own."));
        toggle (splash, tr ("Show the splash screen at startup"), options.splash, [this] (bool on) { options.splash = on; });
        toggle (startupSound, tr ("Play the startup sound"), options.startupSound, [this] (bool on) { options.startupSound = on; });
        toggle (scanAtStart, tr ("Look for new plugins in the background at every start"), options.scanAtStart,
                [this] (bool on) { options.scanAtStart = on; });
        // round 190fq (LYNX's own): the start-up note about supporting LYNX SHARE on Ko-fi (ui/SupportLynx.h)
        toggle (supportNoteBox, tr ("Show the support note"), options.supportNote, [this] (bool on) { options.supportNote = on; });
        supportNoteBox.setTooltip (tr ("Now and then at start-up, a small note in a corner about supporting LYNX SHARE on Ko-fi. "
                                       "Donating is optional and never unlocks anything."));
        // the AppImage on Linux (round 161): its menu entry and the .lynx file type, in the user's ~/.local/share
        toggle (menuEntry, tr ("Show LYNX SHARE in the applications menu and open .lynx files with it"), options.menuEntry,
                [this] (bool on) { options.menuEntry = on; });
        menuEntry.setVisible (runningAsAppImage() && ! appImageInMenuByManager()); // (issue #278: a manager has it in the menu)

        toggle (typingPiano, tr ("Typing keyboard to piano"), options.typingPiano, [this] (bool on) { options.typingPiano = on; });
        toggle (keepEditors, tr ("Keep channel editors open (a channel button click doesn't close the previous editor)"), options.keepEditorsOpen,
                [this] (bool on) { options.keepEditorsOpen = on; });
        toggle (firstChannel, tr ("Select first channel with notes when the pattern changes"), options.selectFirstChannelWithNotes,
                [this] (bool on) { options.selectFirstChannelWithNotes = on; });
        // FL's General › Miscellaneous (round 189o): the Paint tool is monophonic without Shift
        toggle (monoPaint, tr ("Monophonic painting in piano roll"), options.monophonicPainting,
                [this] (bool on) { options.monophonicPainting = on; });
        monoPaint.setTooltip (tr ("On: the Paint tool replaces the notes under each new note, and Shift paints over them. Off: the other way round"));
        // FL's General › Miscellaneous "Alternate meter scale" (round 190lx, DF-61; FL's hint "Alternate scale for all peak meters")
        toggle (altMeterScale, tr ("Alternate meter scale"), options.alternateMeterScale, [this] (bool on) { options.alternateMeterScale = on; });
        altMeterScale.setTooltip (tr ("Alternate scale for all peak meters"));
        // FL's next Miscellaneous box, DisablePlugFocusDefaultBox (off; round 187a, issue #96)
        toggle (noTypingPriority, tr ("Disable typing keyboard priority by default"), options.disableTypingPriority,
                [this] (bool on) { options.disableTypingPriority = on; });
        noTypingPriority.setTooltip (tr ("Disable typing keyboard priority for all newly added plugins by default"));
        // FL's Miscellaneous "Auto select linked modules" (AutoSelectBox, on; round 187a)
        toggle (autoSelectLinked, tr ("Auto select linked modules"), options.autoSelectLinked, [this] (bool on) { options.autoSelectLinked = on; });
        autoSelectLinked.setTooltip (tr ("Focusing plugin windows selects mixer tracks"));
        toggle (rememberSeek, tr ("Remember seek time: Stop goes back to the position clicked on the Playlist ruler"), options.rememberSeek,
                [this] (bool on) { options.rememberSeek = on; });
        toggle (hoverValues, tr ("Show a control's value next to the mouse when hovering over it (LYNX)"), options.hoverValues,
                [this] (bool on) { options.hoverValues = on; });
        // issue #217 (round 190ng): FL 2026 closes the focused window on Esc (core/EscClose.h); the LYNX extras page has it too
        toggle (escCloses, tr ("Esc closes the focused window"), escclose::enabled(), [] (bool on) { workflowextras::setExtra ("general.escCloses", on); });
        // LYNX extra (issue #25): FL plays a stretched pattern clip once; off = FL's behaviour
        toggle (clipsLoop, tr ("Stretched pattern clips loop their pattern (LYNX)"), options.stretchedClipsLoop,
                [this] (bool on) { options.stretchedClipsLoop = on; });
        clipsLoop.setTooltip (tr ("On: a pattern clip stretched past its pattern repeats the pattern. Off: it plays the pattern once, as in FL Studio"));
        // FL's Options › General › Miscellaneous (round 177b): where "Create automation clip" puts the clip
        // FL's General › Miscellaneous › "Detach all new plugins" (issue #116): plugin windows dock in the desktop by default
        toggle (detachNew, tr ("Detach all new plugins"), options.detachNewPlugins, [this] (bool on) { options.detachNewPlugins = on; });
        detachNew.setTooltip (tr ("On: the window of a plugin you add opens detached, on top of the panels. Off: it opens docked in the desktop, "
                                  "behind a panel you click. Each plugin's window menu › Detached changes it for that plugin"));
        toggle (autoClipsAtPlayback, tr ("Create automation clips at the playback position"), options.autoClipsAtPlayback,
                [this] (bool on) { options.autoClipsAtPlayback = on; });
        autoClipsAtPlayback.setTooltip (tr ("On: a new automation clip starts at the song position (without a time selection). Off: at the start of the song"));

        styleLabel (editInLabel, tr ("Double-click on a pattern clip opens"));
        const char* choices[] = { TR_NOOP ("Piano Roll when it has piano-roll notes, else Channel Rack"), TR_NOOP ("Channel Rack"), TR_NOOP ("Piano Roll") };
        for (int i = 0; i < 3; ++i)
            editInBox.addItem (tr (choices[i]), i + 1);
        editInBox.setSelectedId (options.editPatternsIn + 1, juce::dontSendNotification);
        editInBox.onChange = [this] { change ([&] { options.editPatternsIn = editInBox.getSelectedId() - 1; }); };

        styleLabel (undoLabel, tr ("Maximum undo levels"));
        for (auto n : { 25, 50, 100, 200, 500, 1000 })
            undoBox.addItem (juce::String (n), n);
        selectNearest (undoBox, options.undoLevels);
        undoBox.onChange = [this] { change ([&] { options.undoLevels = undoBox.getSelectedId(); }); };
        toggle (alternateUndo, tr ("Alternate undo mode (classic undo, instead of undo/redo on the same shortcut)"), options.alternateUndo,
                [this] (bool on) { options.alternateUndo = on; });
        // FL's General options of round 190bx (core/FlSettings.h; labels, hints and defaults are FL's): kept with the
        // other options, read where they act
        {
            auto flag = [] (juce::ToggleButton& t, const juce::String& text, const juce::String& hint, std::atomic<bool>& value) {
                styleToggle (t, text);
                t.setTooltip (hint);
                t.setToggleState (value.load(), juce::dontSendNotification);
                t.onClick = [&t, &value] {
                    value.store (t.getToggleState());
                    if (mixeroptions::changed)
                        mixeroptions::changed(); // (ui-options.txt)
                };
            };
            auto& fl = flsettings::options();
            flag (autoNameChannels, tr ("Auto name channels"),
                  tr ("Channel name will be set to the sample filename. Off: a channel that has a name keeps it when another sample is loaded"), fl.autoNameChannels);
            flag (restoreAfterSolo, tr ("Restore previous state after solo"),
                  tr ("Restore the previous active state of channels and playlist tracks after solo. Off: everything is switched on"), fl.restoreStateAfterSolo);
            flag (readSampleTempo, tr ("Read sample tempo information"),
                  tr ("Read tempo information stored in samples. Off: it is ignored for the samples loaded from then on"), fl.readSampleTempo);
            flag (historyNewestFirst, tr ("Show recent changes at the top"), tr ("Sort the undo history from new to old"), fl.historyNewestFirst);
            // round 190mv (DF-134; FL's label, on as in FL — capture 190mr A1.1)
            flag (undoKnobTweaks, tr ("Undo knob tweaks"),
                  tr ("Knob, fader and slider moves are undo steps. Off: they are left out of the undo history"), fl.undoKnobTweaks);
            // round 190mr (DF-135; FL's label, off as in FL)
            flag (historyOnUndo, tr ("Show undo history in the browser when undoing changes"),
                  tr ("An undo opens the Browser at Current project › History"), fl.historyOnUndo);
            // round 190mr (DF-141, owner F3; LYNX's switch: FL always does it): a project opens with the windows where they were
            flag (projectLayout, tr ("Load the window layout saved with a project"),
                  tr ("On: a project opens with its windows where they were when it was saved, as in FL Studio. Off: the layout on screen stays"),
                  fl.projectWindowLayout);
        }

        styleLabel (autosaveLabel, tr ("Save a backup copy"));
        for (auto minutes : prefs::autosaveIntervals())
            autosaveBox.addItem (minutes == 0 ? tr ("Never (just remind every 10 min)") // (FL's wording; round 190ih)
                                 : minutes == 1 ? tr ("Every minute")
                                                : tr ("Every {0} minutes", { juce::String (minutes) }),
                                 minutes + 1);
        autosaveBox.setSelectedId (prefs::clampAutosaveMinutes (options.autosaveMinutes) + 1, juce::dontSendNotification);
        autosaveBox.onChange = [this] {
            change ([&] { options.autosaveMinutes = autosaveBox.getSelectedId() - 1; });
            keepBox.setEnabled (options.autosaveMinutes > 0);
        };

        styleLabel (keepLabel, tr ("Backups kept per project"));
        for (auto n : { 1, 3, 5, 10, 20, 50 })
            keepBox.addItem (juce::String (n), n);
        selectNearest (keepBox, options.autosaveKeep);
        keepBox.setEnabled (options.autosaveMinutes > 0);
        keepBox.onChange = [this] { change ([&] { options.autosaveKeep = keepBox.getSelectedId(); }); };
        openBackups.onClick = [] {
            autosaveFolder().createDirectory();
            autosaveFolder().startAsProcess();
        };

        toggle (backupQuiet, tr ("Wait with backups while the song plays or a MIDI keyboard is being played"), options.backupWaitsForQuiet,
                [this] (bool on) {
                    options.backupWaitsForQuiet = on;
                    longestWaitBox.setEnabled (on);
                });
        styleLabel (longestWaitLabel, tr ("Longest wait between backups") /* (fits the label column, as the other rows) */);
        for (const auto minutes : prefs::backupLongestWaitChoices)
            longestWaitBox.addItem (tr ("{0} minutes", { juce::String (minutes) }), minutes);
        selectNearest (longestWaitBox, options.backupLongestWaitMinutes);
        longestWaitBox.setEnabled (options.backupWaitsForQuiet);
        longestWaitBox.onChange = [this] { change ([&] { options.backupLongestWaitMinutes = longestWaitBox.getSelectedId(); }); };
        toggle (unusedTakes, tr ("When a project closes, offer to delete the recordings it no longer uses"), options.askUnusedRecordings,
                [this] (bool on) { options.askUnusedRecordings = on; });

        // round 190jg: Bitwig's plugin hosting modes (was the toggle "Run plugins in a separate process")
        styleLabel (sandboxLabel, tr ("Plugin processes"));
        // (ordered from the most isolated; the id is the mode + 1, engine/PluginSandbox.h SandboxMode)
        sandboxBox.addItem (tr ("In LYNX SHARE's process (fastest)"), 1);
        sandboxBox.addItem (tr ("Each plugin instance in its own process"), 2);
        sandboxBox.addItem (tr ("One process per plugin (all its instances)"), 5);
        sandboxBox.addItem (tr ("One process per plugin vendor"), 3);
        sandboxBox.addItem (tr ("All plugins in one process"), 4);
        sandboxBox.setSelectedId (juce::jlimit (0, 4, options.pluginSandbox) + 1, juce::dontSendNotification);
        sandboxBox.onChange = [this] { change ([&] { options.pluginSandbox = sandboxBox.getSelectedId() - 1; }); };
        styleNote (sandboxNote, tr ("A plugin that crashes takes down only its own process, never LYNX SHARE.") + " "
                                    + tr ("Applies to plugins opened from now on; reopen the project to move the others."));
        toggle (substitutes, tr ("Substitute missing plugins with LYNX SHARE equivalents"), options.pluginSubstitutes,
                [this] (bool on) { options.pluginSubstitutes = on; });
        styleNote (substitutesNote, tr ("A project keeps the original plugin and uses it again once it is installed."));
        toggle (sampleAccurate, tr ("Sample-accurate automation for CLAP and VST3 plugins"), options.sampleAccurateAutomation,
                [this] (bool on) { options.sampleAccurateAutomation = on; });
        // FL's General › Miscellaneous (round 189o): at most 25 plugin editors open, the oldest closes; FL warns before
        // turning it off (FUN_013559c0) and keeps it on with No
        toggle (limitEditors, tr ("Limit the number of open plugin editors"), options.limitEditors, [this] (bool on) {
            options.limitEditors = on;
            if (on)
                return;
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::WarningIcon, tr ("Warning"),
                tr ("It is recommended to keep this setting turned on for performance reasons. Are you sure you want to turn it off?"),
                tr ("Yes"), tr ("No"), this, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer (this)] (int result) {
                    if (safe != nullptr && result == 0)
                        safe->limitEditors.setToggleState (true, juce::sendNotification);
                }));
        });
        limitEditors.setTooltip (tr ("Limit the number of open plugin editor windows to 25"));
        // FL's "Manage messages" (round 186c): which questions are asked; a ticked "Remember my choice" unticks its message
        manageMessages.onClick = [this] {
            messages::manageMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&manageMessages));
        };
        // issue #221 (round 190ng): where a remembered "Remember my choice" answer is undone
        styleNote (messagesNote, tr ("Ticked messages are shown; untick one to skip its question. A question answered with \"Remember my choice\" is unticked here: tick it, or Show all, to be asked again."));
        // FL's Hints and warnings › "Project data size warning" (round 190mr, DF-147): the size a saved project may reach
        // before LYNX warns, or "Don't warn me"
        styleLabel (dataSizeLabel, tr ("Project data size warning"));
        for (const auto mb : flsettings::projectSizeChoices) // (FL's texts: "50MB", "100MB (default)", "1GB", "1.5GB")
            dataSizeBox.addItem (mb == 0 ? tr ("Don't warn me")
                                 : (mb >= 1024 ? juce::String ((double) mb / 1024.0, mb % 1024 == 0 ? 0 : 1) + "GB" : juce::String (mb) + "MB")
                                       + (mb == flsettings::projectSizeDefault ? " " + tr ("(default)") : juce::String()),
                                 mb + 1);
        dataSizeBox.setSelectedId (flsettings::options().projectSizeWarnMb.load() + 1, juce::dontSendNotification);
        dataSizeBox.setTooltip (tr ("Warn when a saved project is larger than this"));
        dataSizeBox.onChange = [this] {
            flsettings::options().projectSizeWarnMb = flsettings::clampProjectSizeWarning (dataSizeBox.getSelectedId() - 1);
            if (mixeroptions::changed)
                mixeroptions::changed(); // (ui-options.txt)
        };
        // FL's General › Input (round 190mr, capture 190mr A1.1: both off in FL)
        {
            auto& fl = flsettings::options();
            const auto flagged = [] (juce::ToggleButton& t, const juce::String& text, const juce::String& hint, std::atomic<bool>& value) {
                styleToggle (t, text);
                t.setTooltip (hint);
                t.setToggleState (value.load(), juce::dontSendNotification);
                t.onClick = [&t, &value] {
                    value.store (t.getToggleState());
                    if (mixeroptions::changed)
                        mixeroptions::changed();
                };
            };
            flagged (clickHold, tr ("Click-and-hold & special gesture functions"),
                     tr ("Extra functions when holding clicks in piano roll: hold a note to copy it, hold while adding a note to cycle "
                         "slide / porta, hold between two touching notes to glue them"),
                     fl.clickHold);
            flagged (bothButtons, tr ("Use both mouse buttons in step sequencer"),
                     tr ("On: a left click only switches steps on and a right click switches them off. Off: a left click toggles a step"),
                     fl.bothButtonsSteps);
        }
        managePlugins.onClick = [this] {
            if (hooks.managePlugins)
                hooks.managePlugins();
        };
        manageShortcuts.onClick = [this] {
            if (hooks.manageShortcuts)
                hooks.manageShortcuts();
        };
        styleNote (shortcutsNote, tr ("See, search and change every keyboard shortcut."));

        styleLabel (crashLabel, tr ("Crash reports"));
        crashBox.addItem (tr ("Ask after a crash or freeze"), 1);
        crashBox.addItem (tr ("Always send"), 2);
        crashBox.addItem (tr ("Never send"), 3);
        crashBox.setSelectedId (juce::jlimit (0, 2, options.crashReports) + 1, juce::dontSendNotification);
        crashBox.onChange = [this] { change ([&] { options.crashReports = crashBox.getSelectedId() - 1; }); };
        styleNote (crashNote, tr ("A report holds LYNX SHARE's version, your system and audio device, the crash or freeze file and the last steps, without "
                                  "your name, file names or projects. It goes to lynx-share.com only with your consent (Help > Report a problem... "
                                  "sends one by hand)."));

        // round 166b: the update check (ui/Updater.h); the Store build has none (the Store updates it)
        toggle (checkUpdates, tr ("Check for updates automatically"), options.checkUpdates, [this] (bool on) { options.checkUpdates = on; });
        checkNow.onClick = [] { updater::checkNow(); };
        styleNote (updatesNote, tr ("At start-up LYNX SHARE asks GitHub for the newest version number (nothing about you is sent) and shows a bar "
                                    "when a new version is out. Help > Check for updates... asks at any time.")
                                    // round 190ns (issue #233)
                                    + " " + tr ("Off, LYNX SHARE never goes online by itself: plugin scans and everything else stay on this computer."));
        // round 190gh (B2): the version before the last update, and the way back to it (Help › Go back to... too)
        {
            const auto previous = persistent ? updater::previousVersion() : juce::String ("0.1.27");
            styleLabel (previousLabel, previous.isNotEmpty() ? tr ("Previous version: {0}", { previous }) : tr ("Previous version: none known yet"));
            goBack.setButtonText (previous.isNotEmpty() ? tr ("Go back to {0}...", { previous }) : tr ("Go back..."));
            goBack.setTooltip (tr ("Installs the earlier version again, checked like an update. Your projects stay as they are."));
            goBack.onClick = [] { updater::goBack(); };
        }
        for (auto* c : std::initializer_list<juce::Component*> { &updatesTitle, &checkUpdates, &checkNow, &updatesNote, &previousLabel, &goBack })
            c->setVisible (updater::offered());

        for (auto* b : { &templateBox, &startupProjectBox, &longestWaitBox, &languageBox, &fontBox, &textSizeBox, &scaleBox, &menuBox, &wavesBox, &noteNamesBox, &blackNotesBox, &pluginStyleBox, &animationBox, &editInBox, &undoBox, &longestWaitBox, &autosaveBox, &keepBox, &crashBox, &sandboxBox, &dataSizeBox })
            styleCombo (*b);
        juce::ignoreUnused (engine, persistent);
    }

private:
    int layout (int width) override
    {
        Rows r (width);
        r.section (workflowTitle, true);
        layoutStyleBox.setBounds (r.row (layoutStyleLabel, 260));
        layoutStyleNote.setBounds (r.note (layoutStyleNote, 66, 3, true)); // (round 190hw: longer, so measured)
        alsSessionBox.setBounds (r.row (alsSessionLabel, 260));
        r.section (languageTitle);
        languageBox.setBounds (r.row (languageLabel, 220));
        languageNote.setBounds (r.indentedNote (languageNote, languageNote.getText().length() > 60 ? 32 : 16).translated (0, -3));
        if (translationLink.isVisible())
            translationLink.setBounds (r.indented (18).translated (0, -4).withWidth (juce::jmin (420, width - 40)));
        {
            // (the reset button as wide as its text: "Auf Standard zurücksetzen" wrapped at a fixed width)
            const auto resetWidth = juce::jlimit (110, Rows::h (240), juce::roundToInt (juce::GlyphArrangement::getStringWidth (
                getLookAndFeel().getTextButtonFont (fontReset, Rows::h (24)), fontReset.getButtonText())) + 24);
            auto row = r.row (fontLabel, 228 + resetWidth);
            fontBox.setBounds (row.removeFromLeft (220));
            fontReset.setBounds (row.withTrimmedLeft (8));
        }
        fontPreview.setBounds (r.indented (22).translated (0, -3));
        fontNote.setBounds (r.indentedNote (fontNote, 16).translated (0, -5));
        {
            // the size, then Bold text on the same row (round 190jp)
            auto row = r.row (textSizeLabel, 0);
            textSizeBox.setBounds (row.removeFromLeft (120));
            row.removeFromLeft (8);
            boldLabel.setBounds (row.removeFromLeft (juce::jmin (row.getWidth() / 3, theme::labelColumnWidth ({ &boldLabel }, Rows::h (80), Rows::h (160)))));
            boldBox.setBounds (row.withTrimmedLeft (6).withWidth (juce::jmin (row.getWidth() - 6, Rows::h (220))));
        }
        condensedLabels.setBounds (r.indented (24));
        scaleBox.setBounds (r.row (scaleLabel, 120));
        menuBox.setBounds (r.row (menuLabel, 160));
        wavesBox.setBounds (r.row (wavesLabel, 0));
        stereoWaves.setBounds (r.indented (24));
        noteNamesBox.setBounds (r.row (noteNamesLabel, 220));
        blackNotesBox.setBounds (r.row (blackNotesLabel, 160));
        everyKeyNames.setBounds (r.indented (24));
        rackColumn.setBounds (r.full (24));
        rackStepColour.setBounds (r.full (24));
        pluginStyleBox.setBounds (r.row (pluginStyleLabel, 300));
        artMotion.setBounds (r.full (24));
        reducedEffects.setBounds (r.full (24));
        uiAnimations.setBounds (r.full (24));
        animationBox.setBounds (r.row (animationLabel, 180));
        animationNote.setBounds (r.indentedNote (animationNote, 32).translated (0, -3)); // (round 190oj WG-3)
       #if JUCE_WINDOWS
        gpuRendering.setBounds (r.full (24));
        editorsInMain.setBounds (r.full (24));
       #endif

        r.section (startupTitle);
        templateBox.setBounds (r.row (templateLabel, 260));
        startupProjectBox.setBounds (r.row (startupProjectLabel, 260));
        templateNote.setBounds (r.indentedNote (templateNote, 32).translated (0, -3)); // (round 190oj WG-3)
        for (auto* t : { &projectLayout, &splash, &startupSound, &scanAtStart, &supportNoteBox })
            t->setBounds (r.full (24));
        if (menuEntry.isVisible())
            menuEntry.setBounds (r.full (24));

        r.section (editingTitle);
        for (auto* t : { &clickHold, &bothButtons }) // (round 190mr: FL's General › Input)
            t->setBounds (r.full (24));
        for (auto* t : { &typingPiano, &keepEditors, &autoNameChannels, &detachNew, &rememberSeek, &clipsLoop, &autoClipsAtPlayback, &firstChannel, &monoPaint, &altMeterScale, &autoSelectLinked, &noTypingPriority,
                         &restoreAfterSolo, &readSampleTempo, &hoverValues, &escCloses })
            t->setBounds (r.full (24));
        editInBox.setBounds (r.row (editInLabel, 0));
        undoBox.setBounds (r.row (undoLabel, 120));
        undoKnobTweaks.setBounds (r.full (24)); // (round 190mv: FL's Undo history panel, after Maximum undo levels)
        alternateUndo.setBounds (r.full (24));
        historyOnUndo.setBounds (r.full (24)); // (round 190mr: FL's Undo history panel, before its last box)
        historyNewestFirst.setBounds (r.full (24)); // (FL's Undo history panel: its last box)

        r.section (hintsTitle); // FL's General › Hints and warnings (round 186c)
        manageMessages.setBounds (r.full().removeFromLeft (180));
        messagesNote.setBounds (r.note (messagesNote, 18)); // (round 190ng: its own wrapped line under the button)
        dataSizeBox.setBounds (r.row (dataSizeLabel, 160)); // (round 190mr, DF-147)

        r.section (backupTitle);
        {
            auto row = r.row (autosaveLabel, 0);
            autosaveBox.setBounds (row.removeFromLeft (220));
            row.removeFromLeft (8);
            openBackups.setBounds (row.removeFromLeft (juce::jmin (row.getWidth(), 170)));
        }
        keepBox.setBounds (r.row (keepLabel, 120));
        backupQuiet.setBounds (r.full (24));
        longestWaitBox.setBounds (r.row (longestWaitLabel, 120));
        unusedTakes.setBounds (r.full (24));

        r.section (pluginsTitle);
        sandboxBox.setBounds (r.row (sandboxLabel, 300));
        sandboxNote.setBounds (r.full (32).translated (0, -3));
        substitutes.setBounds (r.full (24));
        substitutesNote.setBounds (r.full (16).translated (0, -3));
        sampleAccurate.setBounds (r.full (24));
        limitEditors.setBounds (r.full (24));
        {
            auto row = r.full();
            managePlugins.setBounds (row.removeFromLeft (180));
            row.removeFromLeft (8);
            manageShortcuts.setBounds (row.removeFromLeft (180));
            row.removeFromLeft (12);
            shortcutsNote.setBounds (row.withTrimmedTop (6));
        }

        r.section (reportsTitle);
        crashBox.setBounds (r.row (crashLabel, 260));
        crashNote.setBounds (r.full (46).translated (0, -3));
        r.section (remoteTitle);
        remoteControl.setBounds (r.full (24));
        remoteStatus.setBounds (r.full (36));
        if (updatesTitle.isVisible())
        {
            r.section (updatesTitle);
            auto row = r.full (24);
            checkNow.setBounds (row.removeFromRight (juce::jmin (row.getWidth() / 3, 150)));
            checkUpdates.setBounds (row.withTrimmedRight (8));
            updatesNote.setBounds (r.full (32).translated (0, -1));
            row = r.full (24);
            goBack.setBounds (row.removeFromRight (juce::jmin (row.getWidth() / 3, 150)));
            previousLabel.setBounds (row.withTrimmedRight (8));
        }
        displayTitle.setVisible (false);
        return r.bottom();
    }

    void timerCallback() override
    {
        followLayoutStyle();
        remoteControl.setToggleState (remoteui::enabled(), juce::dontSendNotification);
        const auto historyError = engine.getRemoteHistoryError();
        remoteStatus.setText (historyError.isNotEmpty() ? historyError : tr (remoteui::status()), juce::dontSendNotification);
        remoteStatus.setColour (juce::Label::textColourId, remoteui::active() ? theme::colour::accent : theme::colour::textSecondary);
    }

    void toggle (juce::ToggleButton& t, const juce::String& text, bool value, std::function<void (bool)> set)
    {
        styleToggle (t, text);
        t.setToggleState (value, juce::dontSendNotification);
        t.onClick = [this, &t, set = std::move (set)] { change ([&] { set (t.getToggleState()); }); };
    }

    // the LYNX extras page switches this one too (round 190am): the copy kept here follows when the page comes back
    void visibilityChanged() override
    {
        followLayoutStyle();
        if (isVisible() && hooks.getGeneral)
        {
            options.hoverValues = hooks.getGeneral().hoverValues;
            hoverValues.setToggleState (options.hoverValues, juce::dontSendNotification);
        }
    }

    static void selectNearest (juce::ComboBox& box, int value)
    {
        auto best = box.getItemId (0);
        for (int i = 0; i < box.getNumItems(); ++i)
            if (std::abs (box.getItemId (i) - value) < std::abs (best - value))
                best = box.getItemId (i);
        box.setSelectedId (best, juce::dontSendNotification);
    }

    void change (const std::function<void()>& fn)
    {
        followLayoutStyle(); // (round 190jp: never hand back a layout style the user has left since)
        fn();
        if (hooks.setGeneral)
            hooks.setGeneral (options);
    }

    // Round 190jp (Discord, 0.1.29: "switching Layout style doesn't work"): the page kept the layout style it was opened
    // with. Switched meanwhile from View › Layout style, the Welcome screen or the Setup wizard, the box still showed the
    // old style, so picking the style the window really had changed nothing, and any other option changed on this page
    // put the old style back. The box and the page's copy now follow the window's style (the page's 2 Hz timer, which
    // runs only while Settings › General is open, and before every change it hands back).
    void followLayoutStyle()
    {
        if (hooks.getGeneral == nullptr) // (--snapshot: no window behind the page)
            return;
        const auto now = layoutstyle::current();
        options.layoutStyle = (int) now;
        if (layoutStyleBox.getSelectedId() != layoutstyle::comboId (now))
            layoutStyleBox.setSelectedId (layoutstyle::comboId (now), juce::dontSendNotification);
    }

    // The interface font: the platform's default or any installed family, saved at once, applied by the next start
    // (Labels and caches hold the old font's shapes). The sample line shows the choice right away.
    void setupFontRow()
    {
        for (auto* c : std::initializer_list<juce::Component*> { &fontLabel, &fontBox, &fontReset, &fontPreview, &fontNote, &textSizeLabel, &textSizeBox,
                                                                 &boldLabel, &boldBox, &condensedLabels })
            add (*c);
        // round 190je (design190ia FM-5): FL's condensed face for the FL-landmark labels, off by default; at the next start
        toggle (condensedLabels, tr ("Condensed labels (FL style)"), options.condensedLabels, [this] (bool on) { options.condensedLabels = on; });
        condensedLabels.setTooltip (tr ("Menus, the menu bar, the Browser, Playlist track names and Mixer strip names in a condensed face, "
                                        "a step larger, as FL Studio draws them (Open Sans Condensed). Applied when LYNX SHARE restarts."));
        styleLabel (fontLabel, tr ("Interface font"));
        styleNote (fontNote, tr ("A new interface font, text size or bold text is applied when LYNX SHARE restarts."));
        // issue #189: the text roles' size next to the font (core/TextSize.h), applied at the next start as the font is
        styleLabel (textSizeLabel, tr ("Text size"));
        for (const auto percent : textsize::choices)
            textSizeBox.addItem (juce::String (percent) + " %", percent);
        selectNearest (textSizeBox, options.textSize);
        textSizeBox.setTooltip (tr ("The size of the interface's text, role by role (labels, menus, buttons, titles). The UI size scales everything."));
        textSizeBox.onChange = [this] { change ([&] { options.textSize = textSizeBox.getSelectedId(); }); };
        // round 190jp (a Discord request): bold text next to the size, per role, applied at the next start as they are
        styleLabel (boldLabel, tr ("Bold text"));
        boldLabel.setJustificationType (juce::Justification::centredRight);
        boldBox.addItem (tr ("Off"), 1);
        boldBox.addItem (tr ("Labels, buttons and menus"), 2);
        boldBox.addItem (tr ("All text"), 3);
        boldBox.setSelectedId (juce::jlimit (0, 2, options.boldText) + 1, juce::dontSendNotification);
        boldBox.setTooltip (tr ("Draws the interface's text in the bold face: the labels, buttons and menus, or every text. Applied when LYNX SHARE restarts."));
        boldBox.onChange = [this] { change ([&] { options.boldText = boldBox.getSelectedId() - 1; }); };
        const auto fallback = theme::defaultUiFont();
        fontBox.addItem (fallback.isNotEmpty() ? tr ("Default ({0})", { fallback }) : tr ("Default"), 1);
        fontBox.addSeparator();
        fontNames = theme::installedUiFonts();
        for (int i = 0; i < fontNames.size(); ++i)
            fontBox.addItem (fontNames[i], i + 2);
        // (a saved font that is gone reads as the default here too, as at start-up)
        const auto at = fontNames.indexOf (options.uiFont);
        fontBox.setSelectedId (at >= 0 ? at + 2 : 1, juce::dontSendNotification);
        fontBox.onChange = [this] {
            const auto index = fontBox.getSelectedId() - 2;
            change ([&] { options.uiFont = juce::isPositiveAndBelow (index, fontNames.size()) ? fontNames[index] : juce::String(); });
            updateFontPreview();
        };
        fontReset.onClick = [this] { fontBox.setSelectedId (1, juce::sendNotificationSync); };
        fontPreview.setText ("LYNX SHARE  AaBbCc  0123456789", juce::dontSendNotification);
        fontPreview.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        updateFontPreview();
    }

    void updateFontPreview()
    {
        const auto index = fontBox.getSelectedId() - 2;
        const auto family = juce::isPositiveAndBelow (index, fontNames.size()) ? fontNames[index] : theme::defaultUiFont();
        fontPreview.setFont (family.isNotEmpty() ? juce::Font (juce::FontOptions (family, 14.0f, juce::Font::plain))
                                                 : juce::Font (juce::FontOptions (14.0f)));
        fontReset.setEnabled (fontBox.getSelectedId() != 1);
    }

    void updateTranslationLink()
    {
        // shown in the language just picked when it is already running, else in the current one
        const auto id = options.language;
        translationLink.setVisible (i18n::isNewTranslation (id));
        // Linux without Noto CJK & co.: the language would show empty boxes
        const auto noFont = theme::needsOwnFont (id) && theme::uiFontForLanguage (id).isEmpty();
        languageNote.setText (noFont ? tr ("This language needs a Chinese / Japanese / Korean font: install the Noto Sans CJK fonts "
                                           "(fonts-noto-cjk or google-noto-sans-cjk-fonts), then restart LYNX SHARE.")
                                     : tr ("A new language is applied when LYNX SHARE restarts."),
                              juce::dontSendNotification);
        languageNote.setColour (juce::Label::textColourId, noFont ? theme::colour::accent : theme::colour::textDisabled);
        translationLink.setButtonText (tr ("New translation — suggestions welcome"));
        translationLink.setTooltip (tr ("Opens a GitHub issue to suggest a better translation"));
        translationLink.setURL (juce::URL ("https://github.com/Ka1-S/LYNX-SHARE/issues/new")
                                    .withParameter ("title", "Translation (" + id + "): ")
                                    .withParameter ("labels", "translation"));
    }

    AudioEngine& engine;
    SettingsHooks hooks;
    bool persistent;
    GeneralOptions options;
    SectionTitle workflowTitle { tr ("Workflow") };
    juce::Label layoutStyleLabel, layoutStyleNote;
    juce::ComboBox layoutStyleBox;
    juce::Label alsSessionLabel;
    juce::ComboBox alsSessionBox;
    SectionTitle languageTitle { tr ("Language and display") }, displayTitle, startupTitle { tr ("Startup") }, editingTitle { tr ("Editing") },
        backupTitle { tr ("Backup") }, hintsTitle { tr ("Hints and warnings") }, pluginsTitle { tr ("Plugins and shortcuts") }, reportsTitle { tr ("Crash reports") }, updatesTitle { tr ("Updates") }, remoteTitle { tr ("Remote control") };
    juce::Label longestWaitLabel, animationLabel, animationNote, languageLabel, languageNote, scaleLabel, menuLabel, wavesLabel, noteNamesLabel, blackNotesLabel, pluginStyleLabel, editInLabel, undoLabel, autosaveLabel, keepLabel, sandboxLabel, sandboxNote, substitutesNote, shortcutsNote, crashLabel, crashNote, updatesNote, previousLabel;
    juce::Label fontLabel, fontPreview, fontNote, textSizeLabel, boldLabel;
    juce::ComboBox fontBox, textSizeBox, boldBox;
    juce::ToggleButton condensedLabels;
    juce::Label templateLabel, templateNote, startupProjectLabel;
    juce::ComboBox templateBox, startupProjectBox;
    std::vector<TemplateChoice> templateChoices; // templateBox's item id - 1
    juce::TextButton fontReset { tr ("Reset to default") };
    juce::StringArray fontNames;
    juce::ToggleButton remoteControl;
    juce::Label remoteStatus;
    juce::HyperlinkButton translationLink;
    juce::ComboBox animationBox, crashBox, languageBox, scaleBox, menuBox, wavesBox, noteNamesBox, blackNotesBox, pluginStyleBox, editInBox, undoBox, autosaveBox, keepBox, longestWaitBox, sandboxBox;
    juce::ToggleButton detachNew, editorsInMain, stereoWaves, everyKeyNames, rackColumn, rackStepColour, artMotion, reducedEffects, uiAnimations, gpuRendering, splash, startupSound, scanAtStart, supportNoteBox, menuEntry, typingPiano, keepEditors, rememberSeek, clipsLoop, autoClipsAtPlayback, firstChannel, monoPaint, altMeterScale, hoverValues, alternateUndo, substitutes, sampleAccurate, limitEditors, backupQuiet, unusedTakes, checkUpdates, noTypingPriority, autoSelectLinked;
    juce::ToggleButton autoNameChannels, restoreAfterSolo, readSampleTempo, historyNewestFirst; // FL's options of round 190bx (core/FlSettings.h)
    juce::ToggleButton projectLayout, historyOnUndo; // round 190mr (DF-141, DF-135)
    juce::ToggleButton undoKnobTweaks; // round 190mv (DF-134)
    juce::Label dataSizeLabel;                        // round 190mr (DF-147)
    juce::ComboBox dataSizeBox;
    juce::ToggleButton clickHold, bothButtons; // round 190mr (DE-63, DE-16)
    juce::ToggleButton escCloses; // round 190ng (issue #217)
    juce::TextButton openBackups { tr ("Open backup folder") }, managePlugins { tr ("Plugin manager...") }, manageShortcuts { tr ("Manage shortcuts...") },
        checkNow { tr ("Check now") }, manageMessages { tr ("Manage messages") }, goBack;
    juce::Label messagesNote;
};

// ================================================================================================================
// Audio
// ================================================================================================================
class AudioPage final : public ScrollingPage,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    AudioPage (AudioEngine& e) : engine (e), devices (e.getDeviceManager())
    {
        for (auto* c : std::initializer_list<juce::Component*> { &deviceTitle, &statusTitle, &recordTitle, &driverLabel, &driverBox, &driverNote, &outputLabel,
                                                                 &outputBox, &inputLabel, &inputBox, &rateLabel, &rateBox, &bufferLabel, &bufferBox,
                                                                 &bufferNote, &panelButton, &testButton, &status, &load, &outputChannels, &realtime, &recordInput, &recordNote, &inputChannels,
                                                                 &formatLabel, &formatBox, &takeNames, &latencyTitle, &latencyText, &pingOutLabel, &pingOutBox,
                                                                 &pingInLabel, &pingInBox, &measureButton, &pingNote, &useMeasured, &offsetLabel, &offsetSlider, &offsetNote,
                                                                 &mixerTitle, &metronomeTrackLabel, &metronomeTrackBox, &metronomeTrackNote, &playTruncated, &resetPlugins,
                                                                 &autoClose, &safeOverloads, &cpuTitle, &threadGenerators, &threadMixer, &smartDisableAll, &smartDisableNew, &renderAhead, &resamplingLabel, &resamplingBox, &resamplingNote })
            add (*c);
        setupRecordingControls();
        setupMixerControls();
        setupFlOptions();

        styleLabel (driverLabel, tr ("Driver"));
        styleLabel (outputLabel, tr ("Output device"));
        styleLabel (inputLabel, tr ("Input device"));
        styleLabel (rateLabel, tr ("Sample rate"));
        styleLabel (bufferLabel, tr ("Buffer size"));
        styleNote (driverNote, {});
        styleNote (bufferNote, tr ("Smaller buffers answer faster when you play; larger ones give the CPU more room (fewer clicks)."));
        styleNote (realtime, {});
        for (auto* l : { &status, &load })
        {
            styleLabel (*l, {}, false);
            l->setJustificationType (juce::Justification::centredLeft);
        }
        for (auto* b : { &driverBox, &outputBox, &inputBox, &rateBox, &bufferBox })
        {
            styleCombo (*b);
            b->setTextWhenNoChoicesAvailable (tr ("(open an output device first)"));
            b->setTextWhenNothingSelected (tr ("(choose)"));
        }

        driverBox.onChange = [this] {
            if (updating)
                return;
            auto& types = devices.getAvailableDeviceTypes();
            const auto index = driverBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (index, types.size()))
                devices.setCurrentAudioDeviceType (types[index]->getTypeName(), true);
            refresh();
        };
        outputBox.onChange = [this] {
            apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& s) { s.outputDeviceName = outputBox.getText(); });
        };
        inputBox.onChange = [this] {
            if (updating)
                return;
            if (inputBox.getSelectedId() <= 1)
                apply ([] (juce::AudioDeviceManager::AudioDeviceSetup& s) {
                    s.inputDeviceName = {};
                    s.useDefaultInputChannels = false;
                    s.inputChannels.clear();
                });
            else
            {
                // every channel of the device opens: each mixer insert picks its own (pair or single channel)
                const auto result = engine.openAudioInput (inputBox.getText());
                error = result.issue == AudioEngine::InputIssue::failed ? result.detail : juce::String();
                refresh();
            }
        };
        rateBox.onChange = [this] {
            apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& s) { s.sampleRate = (double) rateBox.getSelectedId(); });
        };
        bufferBox.onChange = [this] {
            apply ([this] (juce::AudioDeviceManager::AudioDeviceSetup& s) { s.bufferSize = bufferBox.getSelectedId(); });
        };
        panelButton.onClick = [this] {
            // the driver's own window (ASIO); AudioEngine::showDriverPanel reopens the device with what was set there
            // (0.1.17 / 0.1.18 closed the device first and then called the deleted device: issues #12 and #13)
            AudioEngine::showDriverPanel (devices);
            refresh();
        };
        testButton.onClick = [this] { devices.playTestSound(); };

        styleToggle (recordInput, tr ("Record input audio"));
        styleNote (recordNote, tr ("Choose each mixer insert's input in the Mixer (the input box above its effect slots), arm it, then Rec + Play. "
                                   "This switch records the first two input channels into a new insert when nothing is armed."));
        recordInput.onClick = [this] { engine.setAudioRecordingArmed (recordInput.getToggleState()); };

        devices.addChangeListener (this);
        refresh();
        startTimerHz (2);
    }

    ~AudioPage() override { devices.removeChangeListener (this); }

private:
    int layout (int width) override
    {
        Rows r (width);
        r.section (deviceTitle, true);
        driverBox.setBounds (r.row (driverLabel, 320));
        driverNote.setBounds (r.indented (46).translated (0, -3));
        outputBox.setBounds (r.row (outputLabel, 0));
        inputBox.setBounds (r.row (inputLabel, 0));
        rateBox.setBounds (r.row (rateLabel, 160));
        bufferBox.setBounds (r.row (bufferLabel, 260));
        bufferNote.setBounds (r.indented (18).translated (0, -3));
        {
            auto row = r.indented();
            panelButton.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (8);
            testButton.setBounds (row.removeFromLeft (150));
        }
        autoClose.setBounds (r.indented (24)); // (FL: "Auto close", under the device in the Input / output panel)
        safeOverloads.setBounds (r.indented (24)); // (round 190ok: FL's "Safe overloads", the same panel)
        r.section (statusTitle);
        status.setBounds (r.full (22));
        load.setBounds (r.full (22));
        outputChannels.setBounds (r.full (22));
       #if JUCE_LINUX
        realtime.setBounds (r.full (realtimeDenied ? 50 : 22));
       #endif
        r.section (recordTitle);
        inputChannels.setBounds (r.full (34));
        recordInput.setBounds (r.full (24));
        recordNote.setBounds (r.full (32).translated (0, -3));
        formatBox.setBounds (r.row (formatLabel, 320));
        takeNames.setBounds (r.full (24));
        r.section (latencyTitle);
        latencyText.setBounds (r.full (50));
        pingOutBox.setBounds (r.row (pingOutLabel, 260));
        pingInBox.setBounds (r.row (pingInLabel, 260));
        measureButton.setBounds (r.indented().withWidth (240));
        pingNote.setBounds (r.full (48).translated (0, -2));
        useMeasured.setBounds (r.full (24));
        offsetSlider.setBounds (r.row (offsetLabel, 200));
        offsetNote.setBounds (r.full (32).translated (0, -3));
        r.section (cpuTitle); // (FL's order: Input / output, CPU, Mixer)
        for (auto* t : { &threadGenerators, &threadMixer, &smartDisableAll, &smartDisableNew, &renderAhead })
            t->setBounds (r.full (24));
        r.section (mixerTitle);
        resamplingBox.setBounds (r.row (resamplingLabel, 260)); // (FL: the Mixer panel's first row)
        if (resamplingNote.isVisible())
            resamplingNote.setBounds (r.indented (32).translated (0, -3));
        metronomeTrackBox.setBounds (r.row (metronomeTrackLabel, 260));
        metronomeTrackNote.setBounds (r.full (32).translated (0, -3));
        playTruncated.setBounds (r.full (24));
        resetPlugins.setBounds (r.full (24));
        return r.bottom();
    }

    // FL's Settings › Audio › Mixer › "Metronome track" (round 165z-a2): "---" = the Master, else an insert. A global
    // setting, like FL's; a song without that insert plays the click on the Master.
    void setupMixerControls()
    {
        styleLabel (metronomeTrackLabel, tr ("Metronome track"));
        styleCombo (metronomeTrackBox);
        metronomeTrackBox.setTooltip (tr ("Metronome target mixer track"));
        styleNote (metronomeTrackNote, tr ("The mixer track the metronome plays through (--- is the Master). The click is never in an export."));
        const auto& mixer = engine.getSong().mixer;
        metronomeTrackBox.addItem ("---", 1);
        for (int t = 1; t < maxMixerTracks; ++t)
        {
            const auto name = t < (int) mixer.size() ? juce::String (mixer[(std::size_t) t].name) : juce::String();
            metronomeTrackBox.addItem (name.isEmpty() ? juce::String (t) : juce::String (t) + "  " + name, t + 1);
        }
        metronomeTrackBox.setSelectedId (engine.getTransport().metronomeTrack.load() + 1, juce::dontSendNotification);
        metronomeTrackBox.onChange = [this] {
            engine.getTransport().metronomeTrack.store (juce::jmax (0, metronomeTrackBox.getSelectedId() - 1));
            if (mixeroptions::changed)
                mixeroptions::changed(); // (ui-options.txt)
        };
        // FL's Settings › Audio › Mixer (round 167j): "Play truncated notes on transport" (FL: "Play notes crossing
        // start position", on by default) and "Reset plugins on transport" (FL: off by default)
        styleToggle (playTruncated, tr ("Play truncated notes on transport"));
        playTruncated.setTooltip (tr ("Play notes crossing start position"));
        playTruncated.setToggleState (engine.getTransport().playTruncatedOnTransport.load(), juce::dontSendNotification);
        playTruncated.onClick = [this] {
            engine.getTransport().playTruncatedOnTransport.store (playTruncated.getToggleState());
            if (mixeroptions::changed)
                mixeroptions::changed();
        };
        styleToggle (resetPlugins, tr ("Reset plugins on transport"));
        resetPlugins.setTooltip (tr ("Clears the effects' tails when playback starts or jumps. Uncheck for faster response when relocating"));
        resetPlugins.setToggleState (engine.getTransport().resetPluginsOnTransport.load(), juce::dontSendNotification);
        resetPlugins.onClick = [this] {
            engine.getTransport().resetPluginsOnTransport.store (resetPlugins.getToggleState());
            if (mixeroptions::changed)
                mixeroptions::changed();
        };
    }

    // FL's Settings › Audio options of round 190bx (core/FlSettings.h; docs/analise/REA_rodada190bx.md): the labels and
    // hints are FL's, the defaults too. App-wide, saved with the other options (ui-options.txt).
    void setupFlOptions()
    {
        auto& o = flsettings::options();
        auto flag = [] (juce::ToggleButton& t, const juce::String& text, const juce::String& hint, std::atomic<bool>& value) {
            styleToggle (t, text);
            t.setTooltip (hint);
            t.setToggleState (value.load(), juce::dontSendNotification);
            t.onClick = [&t, &value] {
                value.store (t.getToggleState());
                if (mixeroptions::changed)
                    mixeroptions::changed();
            };
        };
        flag (autoClose, tr ("Auto close"),
              tr ("Free the audio device for other applications while LYNX SHARE is in the background and nothing plays or records"), o.autoCloseDevice);
        // FL's "Safe overloads" (round 190ok, FL's hint; core/OverloadGuard.h): on by default, as in FL
        flag (safeOverloads, tr ("Safe overloads"), tr ("No risk of GUI freezing when overloads occur"), o.safeOverloads);
        flag (threadGenerators, tr ("Multithreaded generator processing"), tr ("May increase CPU performance"), o.multithreadGenerators);
        flag (threadMixer, tr ("Multithreaded mixer processing"), tr ("May increase CPU performance"), o.multithreadMixer);
        flag (smartDisableAll, tr ("Smart disable"),
              tr ("May increase CPU performance. Off: every effect is processed all the time, whatever its own Smart disable switch says"), o.smartDisable);
        // LYNX extra (round 190ok, owner decision D2): FL's generator smart disable on for new instrument plugins
        flag (smartDisableNew, tr ("Smart disable for new instrument plugins"),
              tr ("New channels with an instrument plugin get Smart disable on: the plugin is not called while it gets no notes and its "
                  "sound has been silent for 4 seconds. FL Studio starts a new generator with it off."),
              o.smartDisableNewPlugins);
        // LYNX extra (round 190gq, core/RenderAhead.h; owner decision O22: on by default)
        flag (renderAhead, tr ("Render ahead (lower CPU at small buffers)"),
              tr ("Tracks nobody plays live are rendered a little ahead on another thread, so heavy projects hold small buffers. Inputs, the "
                  "channels being played and everything they feed stay real-time; edits on the other tracks can be heard up to about 25 ms late."),
              renderAheadOption());

        styleLabel (resamplingLabel, tr ("Resampling quality"));
        styleCombo (resamplingBox);
        resamplingBox.setTooltip (tr ("Interpolation method for sampler channels"));
        resamplingBox.addSectionHeading (tr ("Low quality / fastest"));
        resamplingBox.addItem (tr ("2-point linear"), 1);
        resamplingBox.addItem (tr ("6-point Hermite"), 2);
        resamplingBox.addSectionHeading (tr ("Average quality"));
        for (int index = 3; index <= flsettings::interpolationMax; ++index)
        {
            if (index == flsettings::interpolationHeavyFrom)
                resamplingBox.addSectionHeading (tr ("High quality / slowest"));
            resamplingBox.addItem (tr ("{0}-point sinc", { juce::String (flsettings::interpolationPoints (index)) }), index);
        }
        resamplingBox.setSelectedId (flsettings::clampInterpolation (o.interpolation.load()), juce::dontSendNotification);
        styleNote (resamplingNote, tr ("64-point sinc and higher use much more CPU while playing. An export always uses 32-point sinc."));
        resamplingNote.setVisible (o.interpolation.load() >= flsettings::interpolationHeavyFrom);
        resamplingBox.onChange = [this] {
            const auto index = flsettings::clampInterpolation (resamplingBox.getSelectedId());
            flsettings::options().interpolation.store (index);
            // (FL warns here too, from 64 points on)
            resamplingNote.setVisible (index >= flsettings::interpolationHeavyFrom);
            if (mixeroptions::changed)
                mixeroptions::changed();
            resized();
        };
    }

    void apply (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>& change)
    {
        if (updating)
            return;
        auto setup = devices.getAudioDeviceSetup();
        change (setup);
        error = devices.setAudioDeviceSetup (setup, true);
        refresh();
    }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        auto* type = devices.getCurrentDeviceTypeObject();
        auto* device = devices.getCurrentAudioDevice();
        const auto setup = devices.getAudioDeviceSetup();

        driverBox.clear (juce::dontSendNotification);
        auto& types = devices.getAvailableDeviceTypes();
        for (int i = 0; i < types.size(); ++i)
        {
            driverBox.addItem (driverDisplayName (types[i]->getTypeName()), i + 1);
            if (types[i] == type)
                driverBox.setSelectedId (i + 1, juce::dontSendNotification);
        }

        outputBox.clear (juce::dontSendNotification);
        inputBox.clear (juce::dontSendNotification);
        inputBox.addItem (tr ("(none)"), 1);
        inputBox.setSelectedId (1, juce::dontSendNotification);
        if (type != nullptr)
        {
            if (type->getDeviceNames (false).isEmpty())
                type->scanForDevices(); // (a driver type not opened yet this session)
            const auto outs = type->getDeviceNames (false);
            for (int i = 0; i < outs.size(); ++i)
            {
                outputBox.addItem (outs[i], i + 1);
                if (outs[i] == setup.outputDeviceName)
                    outputBox.setSelectedId (i + 1, juce::dontSendNotification);
            }
            if (type->hasSeparateInputsAndOutputs())
            {
                const auto ins = type->getDeviceNames (true);
                for (int i = 0; i < ins.size(); ++i)
                {
                    inputBox.addItem (ins[i], i + 2);
                    if (ins[i] == setup.inputDeviceName && ! setup.inputChannels.isZero())
                        inputBox.setSelectedId (i + 2, juce::dontSendNotification);
                }
            }
            else if (setup.outputDeviceName.isNotEmpty())
            {
                // ASIO: one device does both
                inputBox.addItem (setup.outputDeviceName, 2);
                if (! setup.inputChannels.isZero())
                    inputBox.setSelectedId (2, juce::dontSendNotification);
            }
        }

        rateBox.clear (juce::dontSendNotification);
        bufferBox.clear (juce::dontSendNotification);
        if (device != nullptr)
        {
            const auto rate = device->getCurrentSampleRate();
            for (auto r : device->getAvailableSampleRates())
                rateBox.addItem (juce::String ((int) r) + " Hz", (int) r);
            rateBox.setSelectedId ((int) rate, juce::dontSendNotification);

            const auto kind = prefs::driverKind (device->getTypeName().toStdString());
            std::vector<int> offered;
            for (auto b : device->getAvailableBufferSizes())
                offered.push_back (b);
            for (auto b : prefs::usableBufferSizes (kind, offered, device->getDefaultBufferSize()))
                bufferBox.addItem (tr ("{0} samples ({1} ms)", { juce::String (b), juce::String (prefs::bufferLatencyMs (b, rate), 1) }), b);
            bufferBox.setSelectedId (device->getCurrentBufferSizeSamples(), juce::dontSendNotification);
        }
        for (auto* b : { &outputBox, &inputBox, &rateBox, &bufferBox })
            b->setEnabled (b->getNumItems() > 0);
        driverNote.setText (driverExplanation (type != nullptr ? type->getTypeName() : juce::String(), device), juce::dontSendNotification);
        panelButton.setEnabled (device != nullptr && device->hasControlPanel());
        testButton.setEnabled (device != nullptr);
        recordInput.setToggleState (engine.isAudioRecordingArmed(), juce::dontSendNotification);
        refreshRecording();
        {
            // what the mixer inserts can take their input from
            const auto names = engine.getInputChannelNames();
            juce::StringArray list;
            for (const auto& n : names)
                list.add (juce::String::fromUTF8 (n.c_str()));
            styleNote (inputChannels, names.empty() ? tr ("Input channels: none open. Choose an input device above to record a microphone or an instrument.")
                                                    : tr ("Input channels ({0}): {1}", { juce::String ((int) names.size()), list.joinIntoString (", ") }));
        }
        {
            // the device's outputs by name (PipeWire / JACK / ASIO name their channels)
            juce::StringArray outs;
            if (device != nullptr)
            {
                const auto names = device->getOutputChannelNames();
                const auto active = device->getActiveOutputChannels();
                for (int c = 0; c < names.size(); ++c)
                    outs.add (names[c] + (active[c] ? juce::String() : " (" + tr ("off") + ")"));
            }
            styleNote (outputChannels, outs.isEmpty() ? juce::String()
                                                      : tr ("Output channels ({0}): {1}", { juce::String (outs.size()), outs.joinIntoString (", ") }));
        }
        updateStatus();
    }

    void updateStatus()
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
        {
            status.setText (error.isNotEmpty() ? error : tr ("No audio device is open: choose an output device."), juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, theme::colour::warning);
            load.setText ({}, juce::dontSendNotification);
            return;
        }
        const auto rate = device->getCurrentSampleRate();
        const auto latency = prefs::bufferLatencyMs (device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples(), rate);
        status.setText (tr ("{0} is running: {1} Hz, output latency {2} ms",
                            { device->getName(), juce::String ((int) rate), juce::String (latency, 1) }),
                        juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        const auto xruns = device->getXRunCount();
        const auto rt = engine.getRtStats().snapshot();
        const auto dot = juce::String::fromUTF8 ("   \xc2\xb7   ");
        load.setText (tr ("Audio CPU load: {0}%", { juce::String (juce::roundToInt (devices.getCpuUsage() * 100.0)) })
                          + (xruns > 0 ? dot + tr ("{0} dropouts (buffer underruns) so far", { juce::String (xruns) }) : juce::String())
                          + (rt.overBudget > 0 ? dot + tr ("{0} blocks over the deadline (max {1}%)", { juce::String ((juce::int64) rt.overBudget), juce::String (juce::roundToInt (rt.maxLoad * 100.0)) })
                                               : juce::String()),
                      juce::dontSendNotification);
        load.setColour (juce::Label::textColourId, xruns > 0 || rt.overBudget > 0 ? theme::colour::warning : theme::colour::textSecondary);
       #if JUCE_LINUX
        updateRealtime();
       #endif
    }

    // Linux: whether the audio thread got real-time priority (round 161), and how to get it when it did not
    void updateRealtime()
    {
        const auto state = lynx::rt::status();
        const auto denied = state == lynx::rt::Status::denied;
        if (state == lynx::rt::Status::unknown)
            realtime.setText ({}, juce::dontSendNotification);
        else if (denied)
            realtime.setText (tr ("Audio thread: normal priority, so the sound breaks up more easily under load. To allow real-time priority, "
                                  "install rtkit (most desktops have it) or add yourself to the \"audio\" group with an rtprio limit "
                                  "(/etc/security/limits.d), then log in again. LYNX SHARE never needs root."),
                              juce::dontSendNotification);
        else
            realtime.setText (tr ("Audio thread: real-time priority ({0})", { lynx::rt::describe() }), juce::dontSendNotification);
        realtime.setColour (juce::Label::textColourId, denied ? theme::colour::warning : theme::colour::textSecondary);
        if (denied != realtimeDenied)
        {
            realtimeDenied = denied;
            resized();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override
    {
        updateStatus();
        if (measuring && ! engine.isPinging())
        {
            measuring = false;
            const auto r = engine.finishLatencyPing();
            pingResult = r.ok ? tr ("Measured: {0} samples ({1} ms) round trip, {2} of {3} clicks, level {4} dB.",
                                    { juce::String (r.samples), juce::String (1000.0 * r.samples / juce::jmax (1.0, sampleRate()), 2), juce::String (r.found),
                                      juce::String (r.sent), juce::String (juce::Decibels::gainToDecibels (r.peak, -100.0f), 1) })
                              : r.problem == "no signal" ? tr ("No click came back. Connect the output to the input (cable or loopback) and try again.")
                              : r.problem == "unstable"  ? tr ("The clicks came back at different times ({0} samples apart): the driver is not stable at this buffer size. Nothing was stored.",
                                                               { juce::String (r.spread) })
                                                         : tr ("Only {0} of {1} clicks came back: raise the input level or use a cable loopback. Nothing was stored.",
                                                               { juce::String (r.found), juce::String (r.sent) });
            refreshRecording();
        }
    }

    double sampleRate() const
    {
        auto* device = devices.getCurrentAudioDevice();
        return device != nullptr && device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
    }

    // Recording format, file names and the latency compensation (round 163)
    void setupRecordingControls()
    {
        styleLabel (formatLabel, tr ("File format"));
        styleCombo (formatBox);
        formatBox.addItem (tr ("WAV 16-bit (the device's sample rate)"), 3); // (round 190bf: FL's Disk recording "16Bit int")
        formatBox.addItem (tr ("WAV 24-bit (the device's sample rate)"), 1);
        formatBox.addItem (tr ("WAV 32-bit float (the device's sample rate)"), 2);
        formatBox.onChange = [this] {
            if (updating)
                return;
            auto s = engine.getRecordingSettings();
            s.format = formatBox.getSelectedId() == 2 ? takes::SampleFormat::float32 : formatBox.getSelectedId() == 3 ? takes::SampleFormat::pcm16 : takes::SampleFormat::pcm24;
            engine.setRecordingSettings (s);
        };
        styleToggle (takeNames, tr ("Name takes after the track (\"Bass DI_Take3.wav\"); off: project, date and time as FL does"));
        takeNames.onClick = [this] {
            auto s = engine.getRecordingSettings();
            s.trackTakeNames = takeNames.getToggleState();
            engine.setRecordingSettings (s);
        };
        styleNote (latencyText, {});
        styleLabel (pingOutLabel, tr ("Ping output"));
        styleLabel (pingInLabel, tr ("Ping input"));
        for (auto* b : { &pingOutBox, &pingInBox })
        {
            styleCombo (*b);
            b->setTextWhenNoChoicesAvailable (tr ("(open an input device first)"));
        }
        pingOutBox.onChange = [this] {
            if (updating)
                return;
            auto s = engine.getRecordingSettings();
            s.pingOutput = juce::jmax (0, pingOutBox.getSelectedId() - 1);
            engine.setRecordingSettings (s);
        };
        pingInBox.onChange = [this] {
            if (updating)
                return;
            auto s = engine.getRecordingSettings();
            s.pingInput = juce::jmax (0, pingInBox.getSelectedId() - 1);
            engine.setRecordingSettings (s);
        };
        measureButton.onClick = [this] {
            if (measuring)
            {
                engine.cancelLatencyPing();
                return;
            }
            const auto problem = engine.startLatencyPing();
            if (problem.isNotEmpty())
            {
                pingResult = tr ("Can't measure: {0}", { tr (problem) });
                refreshRecording();
                return;
            }
            measuring = true;
            pingResult = tr ("Measuring... (8 clicks)");
            refreshRecording();
        };
        styleNote (pingNote, tr ("Connect the ping output to the ping input with a cable (or a loopback in your interface's mixer) and turn your speakers "
                                 "down: LYNX SHARE plays 8 clicks and times their way back. The result is kept for this driver, device, sample rate and buffer size."));
        styleToggle (useMeasured, tr ("Use the measured round trip instead of the driver's figures"));
        useMeasured.onClick = [this] {
            auto s = engine.getRecordingSettings();
            s.useMeasured = useMeasured.getToggleState();
            engine.setRecordingSettings (s);
        };
        styleLabel (offsetLabel, tr ("Fine adjust (samples)"));
        offsetSlider.setSliderStyle (juce::Slider::IncDecButtons);
        offsetSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 24);
        offsetSlider.setRange (-4800.0, 4800.0, 1.0);
        offsetSlider.onValueChange = [this] {
            if (updating)
                return;
            auto s = engine.getRecordingSettings();
            s.manualOffset = (int) offsetSlider.getValue();
            engine.setRecordingSettings (s);
        };
        styleNote (offsetNote, tr ("Moves every new take: positive = earlier on the Playlist. Leave it at 0 after a measurement."));
    }

    void refreshRecording()
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto& s = engine.getRecordingSettings();
        formatBox.setSelectedId (s.format == takes::SampleFormat::float32 ? 2 : s.format == takes::SampleFormat::pcm16 ? 3 : 1, juce::dontSendNotification);
        takeNames.setToggleState (s.trackTakeNames, juce::dontSendNotification);
        useMeasured.setToggleState (s.useMeasured, juce::dontSendNotification);
        offsetSlider.setValue (s.manualOffset, juce::dontSendNotification);

        pingOutBox.clear (juce::dontSendNotification);
        pingInBox.clear (juce::dontSendNotification);
        if (auto* device = devices.getCurrentAudioDevice())
        {
            const auto outs = device->getOutputChannelNames();
            for (int c = 0; c + 1 < outs.size(); c += 2)
                pingOutBox.addItem (outs[c] + " - " + outs[c + 1], c + 1);
            if (outs.size() == 1)
                pingOutBox.addItem (outs[0], 1);
            pingOutBox.setSelectedId (s.pingOutput + 1, juce::dontSendNotification);
            const auto ins = engine.getInputChannelNames();
            for (int c = 0; c < (int) ins.size(); ++c)
                pingInBox.addItem (juce::String::fromUTF8 (ins[(std::size_t) c].c_str()), c + 1);
            pingInBox.setSelectedId (s.pingInput + 1, juce::dontSendNotification);
        }
        measureButton.setButtonText (measuring ? tr ("Stop measuring") : tr ("Measure the round trip"));
        measureButton.setEnabled (pingInBox.getNumItems() > 0);

        const auto l = engine.currentLatency();
        const auto rate = sampleRate();
        const auto ms = [rate] (int samples) { return juce::String (1000.0 * samples / rate, 2); };
        auto text = tr ("Takes are moved back by {0} samples ({1} ms) so they sit where you played them:", { juce::String (l.total()), ms (l.total()) }) + " ";
        text += l.measured >= 0 ? tr ("measured round trip {0}", { juce::String (l.measured) })
                                : tr ("driver's input {0} + output {1}", { juce::String (l.driverInput), juce::String (l.driverOutput) });
        if (l.manual != 0)
            text += ", " + tr ("fine adjust {0}", { juce::String (l.manual) });
        if (l.pdc != 0)
            text += ", " + tr ("plugin delay compensation {0}", { juce::String (l.pdc) });
        text += ".";
        if (pingResult.isNotEmpty())
            text += "\n" + pingResult;
        latencyText.setText (text, juce::dontSendNotification);
    }

    // The driver types with what they mean (JUCE's type names stay the identity in the saved setup).
    static juce::String driverDisplayName (const juce::String& typeName)
    {
        switch (prefs::driverKind (typeName.toStdString()))
        {
            case prefs::DriverKind::wasapiShared:     return tr ("Windows Audio (shared)");
            case prefs::DriverKind::wasapiLowLatency: return tr ("Windows Audio (shared, low-latency period)");
            case prefs::DriverKind::wasapiExclusive:  return tr ("Windows Audio (exclusive, lowest latency)");
            case prefs::DriverKind::directSound:      return tr ("DirectSound (legacy)");
            case prefs::DriverKind::asio:             return "ASIO";
            case prefs::DriverKind::lynxAsio:         return tr ("LYNX ASIO (built in)");
            case prefs::DriverKind::coreAudio:        return "Core Audio";
            case prefs::DriverKind::alsa:             return "ALSA";
            case prefs::DriverKind::jack:             return tr ("JACK / PipeWire");
            case prefs::DriverKind::pipewire:         return tr ("PipeWire (native)");
            case prefs::DriverKind::other:            break;
        }
        return typeName;
    }

    // What the selected driver type does, and for shared mode the period this device mixes at (REA round 97).
    static juce::String driverExplanation (const juce::String& typeName, juce::AudioIODevice* device)
    {
        const auto rate = device != nullptr && device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 48000.0;
        const auto period = device != nullptr ? device->getDefaultBufferSize() : 0;
        switch (prefs::driverKind (typeName.toStdString()))
        {
            case prefs::DriverKind::wasapiShared:
                return tr ("Other programs keep their sound. Windows mixes every program in steps of {0} ms on this device, so the buffer cannot go below that. "
                           "For lower latency choose the exclusive mode or an ASIO driver.",
                           { juce::String (prefs::bufferLatencyMs (period > 0 ? period : 480, rate), 1) });
            case prefs::DriverKind::wasapiLowLatency:
                return period > 0 && device != nullptr && device->getAvailableBufferSizes().size() <= 1
                           ? tr ("This device does not support a shorter period in shared mode (it stays at {0} ms). Use the exclusive mode or an ASIO driver for lower latency.",
                                 { juce::String (prefs::bufferLatencyMs (period, rate), 1) })
                           : tr ("Shared like the normal mode, but asks Windows for a shorter mixing period. Only some drivers support it.");
            case prefs::DriverKind::wasapiExclusive:
                return tr ("LYNX SHARE uses the device alone: the lowest latency without ASIO, but other programs go silent while LYNX SHARE plays.");
            case prefs::DriverKind::directSound:
                return tr ("Old Windows audio path with high latency. Use it only when the others fail.");
            case prefs::DriverKind::asio:
                return tr ("Low-latency drivers. LYNX ASIO, FL Studio ASIO and ASIO4ALL work with any device; a manufacturer's ASIO driver works with its own device.");
            case prefs::DriverKind::lynxAsio:
                if (platform::runningPackaged()) // the Store build: the package folder moves with every update
                    return tr ("LYNX SHARE's own ASIO driver, with any device: playback and recording (microphone) at low latency. \"Driver settings...\" chooses shared or exclusive mode, the output and input devices and shows the latency. "
                               "To use it in other programs, install LYNX SHARE from lynx-share.com (the installer registers it).");
                return tr ("LYNX SHARE's own ASIO driver, with any device: playback and recording (microphone) at low latency. \"Driver settings...\" chooses shared or exclusive mode, the output and input devices and shows the latency. "
                           "For other programs, run \"Register LYNX ASIO\" in LYNX SHARE's folder once.");
            case prefs::DriverKind::coreAudio:
                return tr ("macOS's own audio system: low latency with any device, shared with other programs.");
            case prefs::DriverKind::alsa:
                return tr ("Talks to the sound card directly. Choose \"default\" or \"pipewire\" to share the device with other programs; a \"hw:\" device gives the lowest latency but takes the card for LYNX SHARE alone.");
            case prefs::DriverKind::jack:
                return tr ("Low latency, shared with other programs and routable. On current distributions PipeWire answers as JACK (package pipewire-jack); otherwise start a JACK server first.");
            case prefs::DriverKind::pipewire:
                return tr ("LYNX SHARE talks to PipeWire directly: low latency, shared with other programs. \"System default\" follows the output chosen in "
                           "the system's sound settings (also when a device is plugged in or removed); LYNX SHARE appears as \"LYNX\" in pw-link, qpwgraph or Helvum.");
            case prefs::DriverKind::other:
                break;
        }
        return {};
    }

    AudioEngine& engine;
    juce::AudioDeviceManager& devices;
    bool updating = false;
    juce::String error;
    SectionTitle deviceTitle { tr ("Audio device") }, statusTitle { tr ("Status") }, recordTitle { tr ("Recording") };
    juce::Label driverLabel, driverNote, outputLabel, inputLabel, rateLabel, bufferLabel, bufferNote, status, load, outputChannels, realtime, recordNote, inputChannels;
    bool realtimeDenied = false;
    juce::ComboBox driverBox, outputBox, inputBox, rateBox, bufferBox;
    juce::TextButton panelButton { tr ("Driver settings...") }, testButton { tr ("Play a test sound") };
    juce::ToggleButton recordInput;
    // recording format and latency compensation (round 163)
    SectionTitle latencyTitle { tr ("Recording latency compensation") };
    juce::Label formatLabel, latencyText, pingOutLabel, pingInLabel, pingNote, offsetLabel, offsetNote;
    juce::ComboBox formatBox, pingOutBox, pingInBox;
    juce::ToggleButton takeNames, useMeasured;
    juce::TextButton measureButton { tr ("Measure the round trip") };
    juce::Slider offsetSlider;
    bool measuring = false;
    juce::String pingResult;
    SectionTitle mixerTitle { tr ("Mixer") };
    // FL's Settings › Audio options of round 190bx (core/FlSettings.h)
    juce::ToggleButton autoClose, safeOverloads;
    SectionTitle cpuTitle { tr ("CPU") };
    juce::ToggleButton threadGenerators, threadMixer, smartDisableAll, smartDisableNew, renderAhead;
    juce::Label resamplingLabel, resamplingNote;
    juce::ComboBox resamplingBox;
    juce::Label metronomeTrackLabel, metronomeTrackNote;
    juce::ComboBox metronomeTrackBox;
    juce::ToggleButton playTruncated, resetPlugins;
};

// ================================================================================================================
// MIDI
// ================================================================================================================
// One input device: on/off, which MIDI channel it listens to, what it plays, and its controller type (FL's Options ›
// MIDI "Controller type" of the selected input; round 190aw: the controller profiles of core/ControllerProfile.h).
class MidiInputRow final : public juce::Component
{
public:
    // What the Controller type list offers and shows for this device.
    struct Controllers
    {
        std::vector<std::pair<std::string, juce::String>> profiles; // id, name as listed
        juce::String detected;                                      // the profile its port name matches ("" = none)
        juce::String status;                                        // where the feedback goes
    };

    // round 190pl: the row's controls by component ID for --demo ("<id>.enabled", "<id>.channel", "<id>.plays")
    void setDemoIds (const juce::String& id)
    {
        enabled.setComponentID (id + ".enabled");
        channel.setComponentID (id + ".channel");
        plays.setComponentID (id + ".plays");
    }

    MidiInputRow (const juce::String& deviceName, const prefs::MidiInputPrefs& p, const std::vector<std::string>& channels,
                  const Controllers& controllers, std::function<void (const prefs::MidiInputPrefs&)> changed)
        : prefsNow (p), onChange (std::move (changed))
    {
        typeLabel.setText (tr ("Controller type"), juce::dontSendNotification);
        typeLabel.setFont (theme::font (theme::type::small));
        typeLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (typeLabel);
        // FL's list: "(generic controller)" first, then the device scripts; LYNX adds the automatic choice on top
        type.addItem (controllers.detected.isNotEmpty() ? tr ("Detect automatically ({0})", { controllers.detected })
                                                        : tr ("Detect automatically ({0})", { tr ("Generic controller") }),
                      1);
        type.addItem (tr ("Generic controller"), 2);
        if (! controllers.profiles.empty())
            type.addSeparator();
        auto chosen = p.controller.empty() ? 1 : p.controller == controller::genericChoice ? 2 : 0;
        for (int i = 0; i < (int) controllers.profiles.size(); ++i)
        {
            type.addItem (controllers.profiles[(std::size_t) i].second, i + 3);
            typeIds.push_back (controllers.profiles[(std::size_t) i].first);
            if (p.controller == controllers.profiles[(std::size_t) i].first)
                chosen = i + 3;
        }
        if (chosen == 0) // a profile that is gone (kept; the device runs as a generic controller)
        {
            typeIds.push_back (p.controller);
            chosen = (int) typeIds.size() + 2;
            type.addItem (juce::String::fromUTF8 (p.controller.c_str()), chosen);
        }
        type.setSelectedId (chosen, juce::dontSendNotification);
        type.setTooltip (tr ("A controller profile sets up the device's transport buttons, faders, knobs, pads, lights and display. "
                             "Controls you link by hand keep their links."));
        type.onChange = [this] {
            const auto id = type.getSelectedId();
            prefsNow.controller = id <= 1 ? std::string() : id == 2 ? std::string (controller::genericChoice) : typeIds[(std::size_t) (id - 3)];
            update();
        };
        styleCombo (type);
        addAndMakeVisible (type);
        status.setText (controllers.status, juce::dontSendNotification);
        status.setFont (theme::font (theme::type::small));
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        status.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (status);

        enabled.setButtonText (deviceName);
        enabled.setToggleState (p.enabled, juce::dontSendNotification);
        enabled.onClick = [this] {
            prefsNow.enabled = enabled.getToggleState();
            update();
        };
        addAndMakeVisible (enabled);

        channel.addItem (tr ("Any MIDI channel"), 1);
        for (int c = 1; c <= 16; ++c)
            channel.addItem (tr ("MIDI channel {0}", { juce::String (c) }), c + 1);
        // issue #108 (a LYNX addition; FL has no MPE input): the device is an MPE controller — every channel is
        // taken (channel 1 = master, the member channels from 2 up carry one note each with its bend, CC 74, pressure)
        channel.addSeparator();
        channel.addItem (tr ("MPE controller"), mpeItem);
        channel.setTooltip (tr ("MPE controller: each note has its own MIDI channel; its pitch bend, timbre (CC 74) and pressure are played and recorded per note (fine pitch, Mod X, Mod Y)"));
        channel.setSelectedId (p.mpe ? mpeItem : p.midiChannel + 1, juce::dontSendNotification);
        channel.onChange = [this] {
            prefsNow.mpe = channel.getSelectedId() == mpeItem;
            if (! prefsNow.mpe)
                prefsNow.midiChannel = channel.getSelectedId() - 1;
            update();
            resized();
        };
        for (int n = 15; n >= 1; --n)
            zone.addItem (n == 1 ? tr ("Channel 2") : tr ("Channels 2-{0}", { juce::String (n + 1) }), n);
        zone.setTooltip (tr ("MPE member channels (lower zone): the MIDI channels that carry one note each"));
        zone.setSelectedId (juce::jlimit (1, 15, p.mpeMembers), juce::dontSendNotification);
        zone.onChange = [this] {
            prefsNow.mpeMembers = zone.getSelectedId();
            update();
        };
        styleCombo (zone);
        addChildComponent (zone);

        plays.addItem (tr ("Plays the selected channels"), 1);
        for (int i = 0; i < (int) channels.size(); ++i)
            plays.addItem (tr ("Plays {0}", { juce::String (i + 1) + "  " + juce::String::fromUTF8 (channels[(std::size_t) i].c_str()) }), i + 2);
        if (p.target >= (int) channels.size()) // a channel this project doesn't have (kept)
            plays.addItem (tr ("Plays channel {0}", { juce::String (p.target + 1) }), p.target + 2);
        plays.setSelectedId (p.target + 2, juce::dontSendNotification);
        plays.onChange = [this] {
            prefsNow.target = plays.getSelectedId() - 2;
            update();
        };
        for (auto* b : { &channel, &plays })
        {
            styleCombo (*b);
            addAndMakeVisible (b);
        }
        // FL's input "Port" (InPortSel, a number wheel -1...255, none by default; round 190bt): the port number the
        // Remote control settings window's "Port" refers to
        portLabel.setText (tr ("Port"), juce::dontSendNotification);
        portLabel.setFont (theme::font (theme::type::small));
        portLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        portLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (portLabel);
        {
            ui::ValueSpec spec;
            spec.name = tr ("Port number: a controller link made with a port number follows only the devices with that port");
            spec.minimum = -1.0;
            spec.maximum = 255.0;
            spec.defaultValue = -1.0;
            spec.interval = 1.0;
            spec.toText = [] (double v) { return v < 0.0 ? juce::String::fromUTF8 ("\xe2\x8b\xaf") : juce::String (juce::roundToInt (v)); };
            port = std::make_unique<ui::NumBox> (ui::digitWheel (spec), [this] { return (double) prefsNow.port; }, [this] (double v) {
                const auto now = juce::jlimit (-1, 255, juce::roundToInt (v));
                if (now == prefsNow.port)
                    return;
                prefsNow.port = now;
                update();
            });
            addAndMakeVisible (*port);
        }
        update (false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 4);
        auto top = r.removeFromTop (26);
        // round 190bj (QA: at ~390 px the switch and the device's name were squeezed out): the boxes give way first,
        // down to about two thirds of their width, so the name keeps room (the page scrolls sideways below that)
        auto widths = std::array<float, 3> { 230.0f, 150.0f, prefsNow.mpe ? 124.0f : 0.0f };
        const auto boxes = widths[0] + widths[1] + widths[2];
        const auto room = (float) (top.getWidth() - nameRoom - (prefsNow.mpe ? 24 : 16));
        if (room < boxes)
            for (auto& w : widths)
                w *= juce::jmax (0.65f, room / boxes);
        plays.setBounds (top.removeFromRight (juce::roundToInt (widths[0])));
        top.removeFromRight (8);
        channel.setBounds (top.removeFromRight (juce::roundToInt (widths[1])));
        top.removeFromRight (8);
        zone.setVisible (prefsNow.mpe);
        if (prefsNow.mpe) // (issue #108: the zone's member channels beside the MPE choice; the channel box keeps its column)
        {
            zone.setBounds (top.removeFromRight (juce::roundToInt (widths[2])));
            top.removeFromRight (8);
        }
        enabled.setBounds (top);
        r.removeFromTop (4);
        // the second line, as the output rows have: the controller type under the device's name
        auto bottom = r.removeFromTop (26);
        bottom.removeFromLeft (28); // (under the name, past the check box)
        typeLabel.setBounds (bottom.removeFromLeft (110));
        type.setBounds (bottom.removeFromLeft (juce::jmin (260, bottom.getWidth())));
        bottom.removeFromLeft (10);
        port->setBounds (bottom.removeFromRight (48).reduced (0, 2));
        portLabel.setBounds (bottom.removeFromRight (44));
        status.setBounds (bottom);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radius::md);
    }

private:
    void update (bool notify = true)
    {
        channel.setEnabled (prefsNow.enabled);
        zone.setEnabled (prefsNow.enabled);
        plays.setEnabled (prefsNow.enabled);
        type.setEnabled (prefsNow.enabled);
        port->setEnabled (prefsNow.enabled);
        if (notify && onChange)
            onChange (prefsNow);
    }

    prefs::MidiInputPrefs prefsNow;
    std::function<void (const prefs::MidiInputPrefs&)> onChange;
    juce::ToggleButton enabled;
    juce::ComboBox channel, plays, type, zone;
    juce::Label typeLabel, status, portLabel;
    std::unique_ptr<ui::NumBox> port;
    std::vector<std::string> typeIds; // [item id − 3] the profile's id
    static constexpr int mpeItem = 100;

public:
    // the switch and the device's name keep at least this much of the first line (round 190bj)
    static constexpr int nameRoom = 150;
    // the narrowest row that keeps everything readable: the name's room and the boxes at two thirds
    static constexpr int minimumWidth = 16 + nameRoom + 24 + (230 + 150 + 124) * 65 / 100;
};

// One output device (FL's Options › MIDI "Output" panel, round 165f): master sync (MIDI clock or MTC) with its
// offset, all notes off on init and after an export, and (round 190jl) its port: the MIDI Out channels on that port
// play the device.
class MidiOutputRow final : public juce::Component
{
public:
    // `inheritedPort`: the port of the input that is the same device (FL: the output takes it while it has none of its own)
    MidiOutputRow (const juce::String& deviceName, const prefs::MidiOutputPrefs& p, int inheritedPort, std::function<void (const prefs::MidiOutputPrefs&)> changed)
        : prefsNow (p), inherited (inheritedPort), onChange (std::move (changed))
    {
        name.setText (deviceName, juce::dontSendNotification);
        name.setFont (theme::font (theme::type::body));
        name.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (name);

        sync.setButtonText (tr ("Send master sync"));
        sync.setTooltip (tr ("Send master synchronization to this device"));
        sync.setToggleState (p.masterSync, juce::dontSendNotification);
        sync.onClick = [this] {
            prefsNow.masterSync = sync.getToggleState();
            update();
        };
        for (int t = 0; t < midisync::syncTypeCount; ++t)
            type.addItem (tr (midisync::syncTypeName ((midisync::SyncType) t)), t + 1);
        type.setTooltip (tr ("MIDI clock: 24 pulses per quarter note, Start / Continue / Stop and Song Position Pointer. "
                             "MTC: MIDI Time Code quarter frames and a full frame when the position moves."));
        type.setSelectedId ((int) midisync::syncTypeFromKey (p.syncType) + 1, juce::dontSendNotification);
        type.onChange = [this] {
            prefsNow.syncType = midisync::syncTypeKey ((midisync::SyncType) (type.getSelectedId() - 1));
            update();
        };
        styleCombo (type);
        offsetLabel.setText (tr ("Offset (ms)"), juce::dontSendNotification);
        offsetLabel.setFont (theme::font (theme::type::small));
        offsetLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        offset.setSliderStyle (juce::Slider::IncDecButtons);
        offset.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
        offset.setRange (-1000.0, 1000.0, 1.0);
        offset.setValue (p.offsetMs, juce::dontSendNotification);
        offset.setTooltip (tr ("Master sync offset (milliseconds): positive sends the sync later, negative earlier"));
        offset.setDoubleClickReturnValue (true, 0.0);
        offset.onValueChange = [this] {
            prefsNow.offsetMs = (int) offset.getValue();
            update();
        };
        notesOff.setButtonText (tr ("Send all notes off"));
        notesOff.setTooltip (tr ("Send all notes off to this device on init and after an export"));
        notesOff.setToggleState (p.allNotesOff, juce::dontSendNotification);
        notesOff.onClick = [this] {
            prefsNow.allNotesOff = notesOff.getToggleState();
            update();
        };
        // FL's output "Port" (OutPortSel "Output port number", a number wheel, none by default; round 190jl), as the
        // input rows show theirs
        portLabel.setText (tr ("Port"), juce::dontSendNotification);
        portLabel.setFont (theme::font (theme::type::small));
        portLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        portLabel.setJustificationType (juce::Justification::centredRight);
        {
            ui::ValueSpec spec;
            spec.name = tr ("Output port number: the MIDI Out channels on this port send their notes and controllers to this device");
            spec.minimum = -1.0;
            spec.maximum = 255.0;
            spec.defaultValue = -1.0;
            spec.interval = 1.0;
            spec.toText = [] (double v) { return v < 0.0 ? juce::String::fromUTF8 ("\xe2\x8b\xaf") : juce::String (juce::roundToInt (v)); };
            port = std::make_unique<ui::NumBox> (ui::digitWheel (spec), [this] { return (double) (prefsNow.port >= 0 ? prefsNow.port : inherited); },
                                                 [this] (double v) {
                                                     const auto now = juce::jlimit (-1, 255, juce::roundToInt (v));
                                                     if (now == prefsNow.port)
                                                         return;
                                                     prefsNow.port = now;
                                                     update();
                                                 });
        }
        for (auto* c : std::initializer_list<juce::Component*> { &sync, &type, &offsetLabel, &offset, &notesOff, &portLabel, port.get() })
            addAndMakeVisible (c);
        update (false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 4);
        auto top = r.removeFromTop (24);
        notesOff.setBounds (top.removeFromRight (170));
        name.setBounds (top);
        r.removeFromTop (4);
        auto bottom = r.removeFromTop (26);
        sync.setBounds (bottom.removeFromLeft (150));
        bottom.removeFromLeft (8);
        type.setBounds (bottom.removeFromLeft (190));
        bottom.removeFromLeft (12);
        offsetLabel.setBounds (bottom.removeFromLeft (76));
        offset.setBounds (bottom.removeFromLeft (110));
        bottom.removeFromLeft (10);
        port->setBounds (bottom.removeFromRight (48).reduced (0, 2)); // (where the input rows have theirs)
        portLabel.setBounds (bottom.removeFromRight (44));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radius::md);
    }

private:
    void update (bool notify = true)
    {
        type.setEnabled (prefsNow.masterSync);
        offset.setEnabled (prefsNow.masterSync);
        if (notify && onChange)
            onChange (prefsNow);
    }

    prefs::MidiOutputPrefs prefsNow;
    int inherited = -1;
    std::function<void (const prefs::MidiOutputPrefs&)> onChange;
    juce::Label name, offsetLabel, portLabel;
    juce::ToggleButton sync, notesOff;
    juce::ComboBox type;
    juce::Slider offset;
    std::unique_ptr<ui::NumBox> port;
};

class MidiPage final : public ScrollingPage,
                       private juce::ChangeListener,
                       private juce::Timer,
                       private juce::ListBoxModel
{
public:
    MidiPage (AudioEngine& e, SettingsHooks h, bool keep) : engine (e), hooks (std::move (h)), persistent (keep)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &inputsTitle, &notesTitle, &controllersTitle, &inputsNote, &refresh, &noDevices,
                                                                 &velocityLabel, &velocityBox, &velocityNote, &perfLabel, &perfBox, &perfNote, &typeLabel, &typeValue, &pickup, &footPedal,
                                                                 &pickupNote, &linksLabel, &links, &removeLink, &removeAll, &outputsTitle,
                                                                 &outputsNote, &noOutputs, &syncTitle, &clockLabel, &clockBox, &clockOffsetLabel,
                                                                 &clockOffset, &clockStatusLabel, &clockStatus, &clockNote, &jumpLabel, &jumpBox,
                                                                 &jumpNote, &gamepadToggle, &gamepadNote })
            add (*c);
        styleNote (outputsNote, tr ("Give a device a port number and the MIDI Out channels on that port play it: their notes, controllers, "
                                    "program and bank changes and pitch bend. Send master sync drives drum machines, synths and other "
                                    "software from LYNX SHARE's transport: MIDI clock with Song Position Pointer, or MIDI Time Code."));
        styleLabel (noOutputs, tr ("No MIDI output devices found."));
        styleLabel (clockLabel, tr ("External clock sync"));
        clockBox.setTooltip (tr ("LYNX SHARE follows the MIDI clock of this input: its tempo, Start / Continue / Stop and Song Position Pointer"));
        clockBox.onChange = [this] {
            if (loading)
                return;
            auto p = engine.getMidiPrefs();
            const auto id = clockBox.getSelectedId();
            p.clockInput = id > 1 && id - 2 < clockInputs.size() ? clockInputs[id - 2].toStdString() : std::string();
            // (FL asks "The device isn't enabled" → Enable device: the clock input is turned on with it)
            if (! p.clockInput.empty() && ! p.input (p.clockInput).enabled)
            {
                auto in = p.input (p.clockInput);
                in.enabled = true;
                p.setInput (in);
                set (p);
                rebuildInputs();
                return;
            }
            set (p);
        };
        styleCombo (clockBox);
        styleLabel (clockOffsetLabel, tr ("External sync offset (milliseconds)"));
        clockOffset.setSliderStyle (juce::Slider::IncDecButtons);
        clockOffset.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
        clockOffset.setRange (-1000.0, 1000.0, 1.0);
        clockOffset.setDoubleClickReturnValue (true, 0.0);
        clockOffset.setTooltip (tr ("Positive plays LYNX SHARE later than the clock, negative earlier"));
        clockOffset.onValueChange = [this] {
            if (loading)
                return;
            auto p = engine.getMidiPrefs();
            p.clockOffsetMs = (int) clockOffset.getValue();
            set (p);
        };
        styleLabel (clockStatusLabel, tr ("Status"));
        styleLabel (clockStatus, tr ("No clock"), false);
        styleNote (clockNote, tr ("Choosing an input turns it on in Input devices above. Song time will match the clock."));
        startTimerHz (4);
        styleNote (inputsNote, tr ("Each input device can be turned off, listen to one MIDI channel only and play a fixed channel of the "
                                   "Channel Rack instead of the selected ones."));
        styleLabel (noDevices, tr ("No MIDI input devices found. Connect one and refresh the list."));
        refresh.onClick = [this] {
            if (persistent)
            {
                engine.reloadControllerProfiles(); // (FL's "Update MIDI scripts": the user's profile files are read again)
                engine.reconnectMidiInputs(); // also wakes a device that stopped sending
            }
            rebuildInputs();
        };
        profilesFolder.setTooltip (tr ("Your own controller profiles (*.lynxctl) go here; Refresh device list reads them again"));
        profilesFolder.onClick = [] {
            const auto folder = AudioEngine::controllerProfilesFolder();
            folder.createDirectory();
            folder.revealToUser();
        };
        add (profilesFolder);

        styleLabel (velocityLabel, tr ("Velocity"));
        velocityBox.addItem (tr ("As played"), 1);
        velocityBox.addItem (tr ("Soft (light playing sounds louder)"), 2);
        velocityBox.addItem (tr ("Hard (needs harder playing)"), 3);
        velocityBox.addItem (tr ("Fixed (every note at full velocity)"), 4);
        velocityBox.onChange = [this] {
            auto p = engine.getMidiPrefs();
            p.velocity = (prefs::VelocityCurve) (velocityBox.getSelectedId() - 1);
            set (p);
        };
        styleCombo (velocityBox);
        styleNote (velocityNote, tr ("For notes from MIDI devices (the typing keyboard always plays at the Piano Roll's velocity)."));

        // FL's "MIDI channel used for playlist live mode" (round 165m)
        styleLabel (perfLabel, tr ("Performance mode channel"));
        perfBox.addItem (tr ("Off"), 1);
        for (int c = 1; c <= 16; ++c)
            perfBox.addItem (tr ("Channel") + " " + juce::String (c), c + 1);
        perfBox.onChange = [this] {
            auto p = engine.getMidiPrefs();
            p.performanceChannel = juce::jlimit (0, 16, perfBox.getSelectedId() - 1);
            set (p);
        };
        styleCombo (perfBox);
        styleNote (perfNote, tr ("With Performance mode on, its notes launch the Playlist's clips: an octave per track, a note per clip; "
                                 "notes 120/121 move the tracks, 122/123 the clips, 124 switches big steps."));
        // FL's Options › MIDI "Song marker jump (MIDI channel)"
        styleLabel (jumpLabel, tr ("Song marker jump"));
        jumpBox.addItem (tr ("Off"), 1);
        for (int c = 1; c <= 16; ++c)
            jumpBox.addItem (tr ("MIDI channel {0}", { juce::String (c) }), c + 1);
        jumpBox.onChange = [this] {
            auto p = engine.getMidiPrefs();
            p.songMarkerJumpChannel = juce::jlimit (0, 16, jumpBox.getSelectedId() - 1);
            set (p);
        };
        styleCombo (jumpBox);
        styleNote (jumpNote, tr ("Notes on this channel move the song to a marker instead of playing: note 0 goes to the first "
                                 "marker, the next one to the second... (Playlist › Quantize song jumps applies while playing)."));

        // FL's MIDI input panel: "Foot pedal controls note off" (KbHoldSupportBox, on; round 187a)
        styleToggle (footPedal, tr ("Foot pedal controls note off"));
        footPedal.setTooltip (tr ("Disable to send sustain to plugins"));
        footPedal.onClick = [this] {
            auto p = engine.getMidiPrefs();
            p.footPedalNoteOff = footPedal.getToggleState();
            set (p);
        };

        styleLabel (typeLabel, tr ("Controller type"));
        typeLabel.setJustificationType (juce::Justification::topLeft);
        styleLabel (typeValue, tr ("Generic controller: link a knob or fader with a control's right-click menu > Link to MIDI controller."), false);
        typeValue.setMinimumHorizontalScale (1.0f);
        typeValue.setJustificationType (juce::Justification::topLeft);
        styleToggle (pickup, tr ("Pickup (takeover mode)"));
        pickup.onClick = [this] {
            auto p = engine.getMidiPrefs();
            p.pickup = pickup.getToggleState();
            set (p);
        };
        styleNote (pickupNote, tr ("A controller moves a parameter only after reaching its current value, so nothing jumps."));
        // round 184a, S16: a game controller as one more controller (core/GamepadMap.h)
        styleToggle (gamepadToggle, tr ("Use a game controller"));
        gamepadToggle.onClick = [this] {
            auto p = engine.getMidiPrefs();
            p.gamepad = gamepadToggle.getToggleState();
            set (p);
        };
        styleNote (gamepadNote, tr ("A plays and stops, B records, X undoes, the D-pad picks the track and moves by bars, the triggers set the "
                                    "track's volume. Hold Back to see the whole map. Every button and stick can be linked like a MIDI control."));

        styleLabel (linksLabel, tr ("Links (this project, then global)"));
        linksLabel.setJustificationType (juce::Justification::topLeft);
        links.setModel (this);
        links.setRowHeight (24);
        links.setColour (juce::ListBox::backgroundColourId, theme::colour::bg0);
        links.setColour (juce::ListBox::outlineColourId, theme::colour::border);
        links.setOutlineThickness (1);
        removeLink.onClick = [this] {
            const auto row = links.getSelectedRow();
            const auto projectLinks = (int) engine.getSong().midiLinks.size();
            if (juce::isPositiveAndBelow (row, projectLinks))
                engine.editSong ([row] (Song& s) { s.midiLinks.erase (s.midiLinks.begin() + row); });
            else if (const auto global = engine.getGlobalLinks(); juce::isPositiveAndBelow (row - projectLinks, (int) global.size()))
                engine.removeGlobalLink (global[(std::size_t) (row - projectLinks)].target);
            else if (const auto mapping = row - projectLinks - (int) global.size(); juce::isPositiveAndBelow (mapping, (int) engine.getMidiPrefs().globalMappings.size()))
            {
                remotemap::Row r; // (round 190hp: a MIDI Map Mode mapping kept for every project)
                r.global = true;
                r.index = mapping;
                engine.editGlobalMappingRow (r, [&r] (prefs::MidiPrefs& p) { return remotemap::removeGlobalRow (p, r); });
                links.updateContent();
                links.repaint();
            }
        };
        removeAll.onClick = [this] { engine.editSong ([] (Song& s) { s.midiLinks.clear(); }); };

        if (! persistent)
        {
            // --snapshot: devices of a typical setup, and two links
            sampleDevices = { "Mackie Control", "Akai MPD218", sampleMpeDevice }; // (one with a built-in controller profile, one generic, one shown as an MPE controller, issue #108)
            sampleOutputs = { "Roland TR-8S", "Korg volca sample" };
            auto p = engine.getMidiPrefs();
            prefs::MidiOutputPrefs drums;
            drums.name = "Roland TR-8S";
            drums.masterSync = true;
            drums.allNotesOff = true;
            p.setOutput (drums);
            prefs::MidiOutputPrefs volca; // (round 190jl: a synth the MIDI Out channels on port 1 play)
            volca.name = "Korg volca sample";
            volca.port = 1;
            p.setOutput (volca);
            p.clockInput = "Akai MPD218";
            engine.setMidiPrefs (p, false);
            engine.editSong ([] (Song& s) {
                AutomationTarget volume;
                volume.kind = AutomationTargetKind::mixerVolume;
                volume.index = 0;
                AutomationTarget pan;
                pan.kind = AutomationTargetKind::channelPan;
                pan.index = 0;
                s.midiLinks = { { 7, -1, volume }, { 10, 0, pan } };
            }, false);
        }

        engine.addChangeListener (this);
        rebuildInputs();
        load();
        timerCallback(); // (the status at once, not after the first tick)
    }

    ~MidiPage() override
    {
        stopTimer();
        engine.removeChangeListener (this);
    }

private:
    // the input and output rows' narrowest readable width, plus the page's margins (round 190bj)
    int minimumWidth() const override { return juce::jmax (MidiInputRow::minimumWidth, 16 + 150 + 8 + 190 + 12 + 76 + 110) + 40; }

    int layout (int width) override
    {
        Rows r (width);
        r.section (inputsTitle, true);
        inputsNote.setBounds (r.note (inputsNote, 32, 4));
        if (rows.empty())
            noDevices.setBounds (r.full (24));
        for (auto& row : rows)
            row->setBounds (r.full (66).translated (0, -4));
        {
            auto buttons = r.full();
            refresh.setBounds (buttons.removeFromLeft (150));
            buttons.removeFromLeft (8);
            profilesFolder.setBounds (buttons.removeFromLeft (210));
        }

        r.section (outputsTitle);
        outputsNote.setBounds (r.note (outputsNote, 32, 4));
        if (outputRows.empty())
            noOutputs.setBounds (r.full (24));
        for (auto& row : outputRows)
            row->setBounds (r.full (66).translated (0, -4));

        r.section (syncTitle);
        clockBox.setBounds (r.row (clockLabel, 300));
        clockOffset.setBounds (r.row (clockOffsetLabel, 140));
        clockStatus.setBounds (r.row (clockStatusLabel, 0));
        // (round 190oo WG-32: the notes grow when their text needs another line, as round 190oj's WG-3 on General: the
        // ru / de notes were squeezed and cut in their fixed heights; English keeps its layout to the pixel)
        clockNote.setBounds (r.indentedNote (clockNote, 16).translated (0, -3));

        r.section (notesTitle);
        velocityBox.setBounds (r.row (velocityLabel, 300));
        velocityNote.setBounds (r.indentedNote (velocityNote, 16).translated (0, -3));
        perfBox.setBounds (r.row (perfLabel, 300));
        perfNote.setBounds (r.indentedNote (perfNote, 32).translated (0, -3));
        jumpBox.setBounds (r.row (jumpLabel, 300));
        jumpNote.setBounds (r.indentedNote (jumpNote, 32).translated (0, -3));

        r.section (controllersTitle);
        typeLabel.setVisible (false); // (round 190aw: the Controller type is each input device's, above, as in FL)
        typeValue.setBounds (r.indentedNote (typeValue, 20, false)); // (round 190oo WG-32: "Generic controller: ..." grows too)
        pickup.setBounds (r.full (24));
        pickupNote.setBounds (r.note (pickupNote, 16));
        footPedal.setBounds (r.full (24)); // (FL: right under Pickup, round 187a [UI shots/fl d07c])
        gamepadToggle.setBounds (r.full (24));
        gamepadNote.setBounds (r.note (gamepadNote, 32));
        {
            auto row = r.row (linksLabel, 0, 110);
            linksLabel.setBounds (linksLabel.getBounds().withTrimmedTop (4));
            auto buttons = row.removeFromRight (120);
            row.removeFromRight (8);
            links.setBounds (row);
            removeLink.setBounds (buttons.removeFromTop (26));
            buttons.removeFromTop (6);
            removeAll.setBounds (buttons.removeFromTop (26));
        }
        return r.bottom();
    }

    void set (const prefs::MidiPrefs& p)
    {
        if (loading)
            return;
        engine.setMidiPrefs (p, persistent);
        if (hooks.midiChanged)
            hooks.midiChanged();
    }

    // the LYNX extras page switches the game controller too (round 190am)
    void visibilityChanged() override
    {
        if (isVisible())
            gamepadToggle.setToggleState (engine.getMidiPrefs().gamepad, juce::dontSendNotification);
    }

    void load()
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        const auto& p = engine.getMidiPrefs();
        velocityBox.setSelectedId ((int) p.velocity + 1, juce::dontSendNotification);
        pickup.setToggleState (p.pickup, juce::dontSendNotification);
        footPedal.setToggleState (p.footPedalNoteOff, juce::dontSendNotification);
        gamepadToggle.setToggleState (p.gamepad, juce::dontSendNotification);
        jumpBox.setSelectedId (p.songMarkerJumpChannel + 1, juce::dontSendNotification);
        perfBox.setSelectedId (p.performanceChannel + 1, juce::dontSendNotification);
        {
            auto id = 1;
            for (int i = 0; i < clockInputs.size(); ++i)
                if (clockInputs[i].toStdString() == p.clockInput)
                    id = i + 2;
            clockBox.setSelectedId (id, juce::dontSendNotification);
        }
        clockOffset.setValue (p.clockOffsetMs, juce::dontSendNotification);
        links.updateContent();
        links.repaint();
        removeLink.setEnabled (links.getSelectedRow() >= 0);
        removeAll.setEnabled (! engine.getSong().midiLinks.empty());
    }

    // --snapshot: the sample devices answer on a port of their own name
    static std::string sampleFeedback (const juce::String& input) { return input.toStdString(); }

    // the project's links, then the global ones (settings; FL's "Override global link...", REA round 146)
    int linkRows() const
    {
        return (int) (engine.getSong().midiLinks.size() + engine.getGlobalLinks().size() + engine.getMidiPrefs().globalMappings.size());
    }

    void rebuildInputs()
    {
        rows.clear();
        juce::StringArray names;
        if (persistent)
            for (const auto& d : juce::MidiInput::getAvailableDevices())
                names.add (d.name);
        else
            names = sampleDevices;
        if (persistent && demo::active()) // round 190pl: --demo's virtual input (midi.input plays it), listed as a device
            names.addIfNotAlreadyThere (demo::virtualKeyboardName());

        std::vector<std::string> channels;
        for (const auto& c : engine.getSong().channels)
            channels.push_back (c.name);

        // the controller profiles (round 190aw): FL lists its device scripts with "(user)" after the user's own
        const auto& controllers = engine.getControllers();
        MidiInputRow::Controllers types;
        for (const auto& profile : controllers.library().profiles)
            types.profiles.push_back ({ profile->id, profile->user ? tr ("{0} (user)", { juce::String::fromUTF8 (profile->name.c_str()) })
                                                                   : juce::String::fromUTF8 (profile->name.c_str()) });
        std::sort (types.profiles.begin(), types.profiles.end(),
                   [] (const auto& a, const auto& b) { return a.second.compareNatural (b.second) < 0; });

        for (const auto& name : names)
        {
            auto settings = engine.getMidiPrefs().input (name.toStdString());
            if (! persistent && name == sampleMpeDevice) // (the snapshot's sample row for an MPE controller)
                settings.mpe = true;
            const auto detected = controllers.library().detect (name.toStdString());
            types.detected = detected != nullptr ? juce::String::fromUTF8 (detected->name.c_str()) : juce::String();
            types.status = {};
            if (settings.enabled && controllers.library().resolve (settings.controller, name.toStdString()) != nullptr)
            {
                const auto output = persistent ? controllers.outputOf (name.toStdString()) : sampleFeedback (name);
                types.status = output.empty() ? tr ("No output port for its lights and display") : tr ("Feedback to {0}", { juce::String::fromUTF8 (output.c_str()) });
            }
            auto row = std::make_unique<MidiInputRow> (name, settings, channels, types,
                                                       [this] (const prefs::MidiInputPrefs& in) {
                                                           auto p = engine.getMidiPrefs();
                                                           const auto typeChanged = p.input (in.name).controller != in.controller;
                                                           p.setInput (in);
                                                           set (p);
                                                           if (typeChanged) // (the row's status line follows the new type)
                                                               juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiPage> (this)] {
                                                                   if (safe != nullptr)
                                                                       safe->rebuildInputs();
                                                               });
                                                       });
            // round 190pl: by component ID for --demo: midi.input.<n> (the row), .enabled, .channel, .plays
            const auto id = "midi.input." + juce::String ((int) rows.size());
            row->setComponentID (id);
            row->setDemoIds (id);
            add (*row);
            rows.push_back (std::move (row));
        }
        noDevices.setVisible (rows.empty());

        // the clock input list: off, then the inputs (a saved one missing now stays listed)
        clockInputs = names;
        if (const auto saved = juce::String::fromUTF8 (engine.getMidiPrefs().clockInput.c_str()); saved.isNotEmpty() && ! clockInputs.contains (saved))
            clockInputs.add (saved);
        clockBox.clear (juce::dontSendNotification);
        clockBox.addItem (tr ("Off"), 1);
        for (int i = 0; i < clockInputs.size(); ++i)
            clockBox.addItem (clockInputs[i], i + 2);

        outputRows.clear();
        juce::StringArray outputs;
        if (persistent)
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                outputs.add (d.name);
        else
            outputs = sampleOutputs;
        for (const auto& name : outputs)
        {
            auto row = std::make_unique<MidiOutputRow> (name, engine.getMidiPrefs().output (name.toStdString()),
                                                         engine.getMidiPrefs().inheritedOutputPort (name.toStdString()), [this] (const prefs::MidiOutputPrefs& out) {
                auto p = engine.getMidiPrefs();
                p.setOutput (out);
                set (p);
            });
            add (*row);
            outputRows.push_back (std::move (row));
        }
        noOutputs.setVisible (outputRows.empty());
        load();
        resized();
    }

    void timerCallback() override
    {
        // FL's "Status: off / ready / running [Beat n] / error" (REA 165f), with the tempo received
        const auto& p = engine.getMidiPrefs();
        auto& sync = engine.getMidiSync();
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        juce::String text;
        if (p.clockInput.empty())
            text = tr ("Off");
        else if (! persistent)
            text = tr ("Running: beat {0}, {1} BPM", { "17", "120.0" }); // --snapshot
        else if (! engine.getActiveMidiInputs().contains (juce::String::fromUTF8 (p.clockInput.c_str())))
            text = tr ("Error: the input is not open");
        else if (! sync.receiver().receiving (now))
            text = tr ("Ready, no clock");
        else if (sync.receiver().tooFast())
            text = tr ("Clock is too fast!");
        else if (sync.receiver().running())
            text = tr ("Running: beat {0}, {1} BPM", { juce::String ((int) (engine.getTransport().positionInBeats()) + 1),
                                                       juce::String (sync.receiver().bpm(), 1) });
        else
            text = tr ("Receiving clock {0} BPM", { juce::String (sync.receiver().bpm(), 1) });
        if (clockStatus.getText() != text)
            clockStatus.setText (text, juce::dontSendNotification);
    }

    // links list
    int getNumRows() override { return juce::jmax (1, linkRows()); }

    // round 190bt: a double-click opens the link in its Remote control settings / Global link settings window
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        const auto& project = engine.getSong().midiLinks;
        if (juce::isPositiveAndBelow (row, (int) project.size()))
            openRemoteControlSettings (engine, { project[(std::size_t) row].target }, false);
        else if (const auto global = engine.getGlobalLinks(); juce::isPositiveAndBelow (row - (int) project.size(), (int) global.size()))
            openRemoteControlSettings (engine, { global[(std::size_t) (row - (int) project.size())].target }, true);
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto list = engine.getSong().midiLinks;
        const auto projectLinks = (int) list.size();
        for (const auto& l : engine.getGlobalLinks())
            list.push_back (l);
        // round 190hp: then the MIDI Map Mode's mappings kept for every project (transport, Stop all clips, scenes)
        if (const auto& mappings = engine.getMidiPrefs().globalMappings; row >= (int) list.size() && row - (int) list.size() < (int) mappings.size())
        {
            const auto& m = mappings[(std::size_t) (row - (int) list.size())];
            if (selected)
            {
                g.setColour (theme::colour::accentMuted);
                g.fillRect (0, 0, width, height);
            }
            g.setFont (theme::font (theme::type::body));
            g.setColour (theme::colour::info);
            const auto source = (isNoteLink (m.number) ? tr ("Note") + " " + juce::MidiMessage::getMidiNoteName (m.number - linkNoteBase, true, true, 5)
                                                       : "CC " + juce::String (m.number))
                                + (m.channel >= 0 ? juce::String::fromUTF8 (" \xc2\xb7 ") + tr ("ch. {0}", { juce::String (m.channel + 1) }) : juce::String());
            g.drawText (source, 10, 0, 110, height, juce::Justification::centredLeft);
            g.setColour (theme::colour::textPrimary);
            const auto [path, name] = mapmode::describe (engine, { (RemoteAction) m.action, {}, -1, m.scene });
            g.drawText (path + juce::String::fromUTF8 (" \xe2\x80\xba ") + name + "  (" + tr ("global") + ")", 124, 0, width - 134, height, juce::Justification::centredLeft, true);
            return;
        }
        if (list.empty() || row >= (int) list.size())
        {
            g.setFont (theme::font (theme::type::small));
            g.setColour (theme::colour::textDisabled);
            g.drawText (tr ("(none yet)"), 10, 0, width - 20, height, juce::Justification::centredLeft);
            return;
        }
        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }
        const auto& link = list[(std::size_t) row];
        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::info);
        // (round 190cs: a key link reads as its note, FL's octaves: C5 = MIDI note 60)
        const auto source = (isNoteLink (link.cc) ? tr ("Note") + " " + juce::MidiMessage::getMidiNoteName (link.cc - linkNoteBase, true, true, 5)
                                                  : "CC " + juce::String (link.cc))
                            + (link.channel >= 0 ? juce::String::fromUTF8 (" \xc2\xb7 ") + tr ("ch. {0}", { juce::String (link.channel + 1) }) : juce::String());
        g.drawText (source, 10, 0, 110, height, juce::Justification::centredLeft);
        g.setColour (theme::colour::textPrimary);
        // (round 190bt: what the Remote control settings window set on the link)
        juce::StringArray extras;
        if (link.port >= 0)
            extras.add (tr ("Port") + " " + juce::String (link.port));
        if (! link.formula.empty())
            extras.add (juce::String::fromUTF8 (link.formula.c_str()));
        if (link.smooth)
            extras.add (tr ("Smoothing"));
        if (link.omni)
            extras.add (tr ("Omni"));
        if (link.mode > 0 && link.mode < 5) // FL's link Mode (round 190cs)
        {
            static const char* const modes[] { "Default", "Hold", "Latch", "Inc", "Stay" };
            extras.add (tr ("Mode") + " " + tr (modes[link.mode]));
        }
        g.drawText (describeAutomationTarget (engine, link.target) + (row >= projectLinks ? "  (" + tr ("global") + ")" : juce::String())
                        + (extras.isEmpty() ? juce::String() : juce::String::fromUTF8 ("  \xc2\xb7  ") + extras.joinIntoString (", ")),
                    124, 0, width - 134, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override { removeLink.setEnabled (links.getSelectedRow() >= 0 && linkRows() > 0); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // round 190gh (B8): a device plugged in or out refreshes the lists by itself (FL: "MIDI devices have changed...
        // refreshing..."); Refresh device list stays for a device that stopped sending
        if (engine.getSong().channels.size() != channelCount || engine.getMidiDeviceListVersion() != deviceListVersion)
        {
            channelCount = engine.getSong().channels.size();
            deviceListVersion = engine.getMidiDeviceListVersion();
            rebuildInputs();
        }
        load();
    }

    AudioEngine& engine;
    SettingsHooks hooks;
    bool persistent;
    bool loading = false;
    std::size_t channelCount = 0;
    int deviceListVersion = 0;
    juce::StringArray sampleDevices;
    static constexpr const char* sampleMpeDevice = "MPE keyboard";
    std::vector<std::unique_ptr<MidiInputRow>> rows;
    std::vector<std::unique_ptr<MidiOutputRow>> outputRows;
    juce::StringArray sampleOutputs, clockInputs;
    SectionTitle outputsTitle { tr ("Output devices") }, syncTitle { tr ("External sync") };
    juce::Label outputsNote, noOutputs, clockLabel, clockOffsetLabel, clockStatusLabel, clockStatus, clockNote;
    juce::ComboBox clockBox;
    juce::Slider clockOffset;
    SectionTitle inputsTitle { tr ("Input devices") }, notesTitle { tr ("Notes") }, controllersTitle { tr ("Controllers") };
    juce::Label inputsNote, noDevices, velocityLabel, velocityNote, perfLabel, perfNote, typeLabel, typeValue, pickupNote, linksLabel, jumpLabel, jumpNote;
    juce::TextButton refresh { tr ("Refresh device list") }, removeLink { tr ("Remove link") }, removeAll { tr ("Remove all") };
    juce::TextButton profilesFolder { tr ("Controller profiles folder") };
    juce::ComboBox velocityBox, perfBox, jumpBox;
    juce::ToggleButton pickup, footPedal, gamepadToggle;
    juce::Label gamepadNote;
    juce::ListBox links;
};

// ================================================================================================================
// LYNX extras (round 190am)
// ================================================================================================================
class ExtrasPage final : public ScrollingPage
{
public:
    ExtrasPage (AudioEngine& e, SettingsHooks h, bool keep) : engine (e), hooks (std::move (h)), persistent (keep)
    {
        styleNote (intro, tr ("LYNX's own improvements. All of them are off by default, so LYNX SHARE looks and behaves like FL Studio "
                              "until you switch one on. Each switch is the same setting as in the place its line names."));
        intro.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        add (intro);
        std::string_view section;
        for (const auto& item : extras::items)
        {
            if (item.section != section)
            {
                section = item.section;
                titles.push_back (std::make_unique<SectionTitle> (tr (std::string (item.section).c_str())));
                add (*titles.back());
            }
            auto& row = *rows.emplace_back (std::make_unique<Row>());
            row.id = std::string (item.id);
            row.title = titles.back().get();
            styleToggle (row.toggle, tr (std::string (item.title).c_str()));
            row.toggle.setComponentID (juce::String (row.id)); // (--snapshot and tests find a switch by its id)
            row.toggle.onClick = [this, &row] {
                setLynxExtra (engine, hooks, row.id, row.toggle.getToggleState(), persistent);
                refresh(); // (what the app really did, e.g. an action that was refused)
            };
            styleNote (row.note, tr (std::string (item.note).c_str()));
            row.note.setMinimumHorizontalScale (1.0f); // (round 190iu: a long note wraps instead of being squeezed onto one line)
            add (row.toggle);
            add (row.note);
        }
        // issue #239 (round 190od): the user's automatic colour / icon rules
        add (autoLookTitle);
        autoLook = std::make_unique<AutoLookEditor> (engine, persistent);
        autoLook->onHeightChange = [this] { resized(); };
        add (*autoLook);
        refresh();
    }

    // another page or a menu may have switched one meanwhile
    void visibilityChanged() override
    {
        if (isVisible())
            refresh();
    }

    int layout (int width) override
    {
        Rows r (width);
        const auto noteFont = theme::font (theme::type::small);
        const auto linesOf = [&noteFont] (const juce::Label& label, int w) {
            juce::AttributedString text (label.getText());
            text.setFont (noteFont);
            juce::TextLayout fitted;
            // (round 190oo WG-32: at the width the label draws in, its borders taken off; a ru / de note that wrapped one
            // line more there than measured here was cut in its box)
            fitted.createLayout (text, (float) juce::jmax (40, w - juce::jmax (8, label.getBorderSize().getLeftAndRight() + 2)));
            return juce::jmax (1, fitted.getNumLines());
        };
        intro.setBounds (r.full (4 + 15 * linesOf (intro, r.area.getWidth())));
        const SectionTitle* title = nullptr;
        for (auto& row : rows)
        {
            if (row->title != title)
            {
                r.section (*row->title, title == nullptr);
                title = row->title;
            }
            row->toggle.setBounds (r.full (24));
            const auto noteWidth = r.area.getWidth() - noteIndent;
            const auto noteHeight = 4 + 15 * linesOf (row->note, noteWidth);
            row->note.setBounds (r.full (noteHeight).withTrimmedLeft (noteIndent).translated (0, -5));
        }
        r.section (autoLookTitle, false);
        autoLook->setBounds (r.full (autoLook->heightFor (r.area.getWidth())));
        return r.bottom();
    }

private:
    struct Row
    {
        std::string id;
        SectionTitle* title = nullptr;
        juce::ToggleButton toggle;
        juce::Label note;
    };

    void refresh()
    {
        for (auto& row : rows)
            row->toggle.setToggleState (lynxExtraOn (engine, hooks, row->id), juce::dontSendNotification);
    }

    static constexpr int noteIndent = 27; // under the caption, past the check box
    AudioEngine& engine;
    SettingsHooks hooks;
    bool persistent;
    juce::Label intro;
    std::vector<std::unique_ptr<SectionTitle>> titles;
    std::vector<std::unique_ptr<Row>> rows;
    SectionTitle autoLookTitle { tr ("Automatic colors and icons") };
    std::unique_ptr<AutoLookEditor> autoLook;
};
// ================================================================================================================
// Link (round 190gl): the reference DAW's Settings › Link, its Link section (manual: "The Link Tab in Live's Settings")
// ================================================================================================================
class LinkPage final : public ScrollingPage,
                       private juce::Timer
{
public:
    LinkPage (AudioEngine& e, SettingsHooks h) : engine (e), hooks (std::move (h))
    {
        add (linkTitle);
        styleNote (intro, tr ("Ableton Link shares the tempo and the beat with other apps and devices on the same network "
                              "(phones, other DAWs, hardware). Switch it on with the Link toggle in the toolbar or in Options."));
        intro.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        add (intro);
        for (auto* l : { &enableLabel, &showLabel, &syncLabel, &statusLabel })
        {
            l->setColour (juce::Label::textColourId, theme::colour::textPrimary);
            add (*l);
        }
        enableLabel.setText (tr ("Ableton Link"), juce::dontSendNotification);
        showLabel.setText (tr ("Show Link Toggle"), juce::dontSendNotification);
        syncLabel.setText (tr ("Start Stop Sync"), juce::dontSendNotification);
        statusLabel.setText (tr ("Status"), juce::dontSendNotification);
        for (auto* b : { &enableButton, &showButton, &syncButton })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            b->setColour (juce::TextButton::textColourOnId, theme::colour::bg0);
            add (*b);
        }
        enableButton.setComponentID ("link.enabled");
        enableButton.onClick = [this] {
            engine.setLinkEnabled (enableButton.getToggleState());
            refresh();
        };
        showButton.setComponentID ("link.showToggle");
        syncButton.setComponentID ("link.startStopSync");
        showButton.onClick = [this] {
            if (hooks.showLinkToggle)
                hooks.showLinkToggle (showButton.getToggleState());
            refresh();
        };
        syncButton.onClick = [this] {
            engine.setLinkStartStopSync (syncButton.getToggleState());
            refresh();
        };
        styleNote (syncNote, tr ("Play and Stop are shared with the peers that also have Start Stop Sync on."));
        syncNote.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        add (syncNote);
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        add (status);
        refresh();
        startTimerHz (4);
    }

    int layout (int width) override
    {
        Rows r (width);
        r.section (linkTitle, true);
        intro.setBounds (r.note (intro, 34));
        enableButton.setBounds (r.row (enableLabel, 110));
        showButton.setBounds (r.row (showLabel, 110));
        syncButton.setBounds (r.row (syncLabel, 110));
        syncNote.setBounds (r.note (syncNote, 18));
        status.setBounds (r.row (statusLabel, 0));
        return r.bottom();
    }

private:
    void timerCallback() override { refresh(); }

    void refresh()
    {
        // (Ableton's Link integration guidelines: the state in the settings list, "Enabled" / "Disabled")
        const auto linkOn = engine.getLink().isEnabled();
        enableButton.setToggleState (linkOn, juce::dontSendNotification);
        enableButton.setButtonText (linkOn ? tr ("Enabled") : tr ("Disabled"));
        const auto shown = hooks.linkToggleShown ? hooks.linkToggleShown() : false;
        showButton.setToggleState (shown, juce::dontSendNotification);
        showButton.setButtonText (shown ? tr ("Show") : tr ("Hide"));
        const auto sync = engine.getLink().startStopSync();
        syncButton.setToggleState (sync, juce::dontSendNotification);
        syncButton.setButtonText (sync ? tr ("On") : tr ("Off"));
        const auto& link = engine.getLink();
        juce::String text;
        if (! link.isEnabled())
            text = tr ("Link is off");
        else
        {
            const auto peers = link.peers();
            text = (peers == 1 ? tr ("On: 1 other peer") : tr ("On: {0} other peers").replace ("{0}", juce::String (peers)));
            if (const auto tempo = link.sessionTempo(); tempo > 0.0)
                text += ", " + tr ("session tempo {0} BPM").replace ("{0}", juce::String (tempo, 2));
        }
        status.setText (text, juce::dontSendNotification);
    }

    AudioEngine& engine;
    SettingsHooks hooks;
    SectionTitle linkTitle { tr ("Ableton Link") };
    juce::Label intro, enableLabel, showLabel, syncLabel, statusLabel, syncNote, status;
    juce::TextButton enableButton, showButton, syncButton;
};
} // namespace

std::unique_ptr<juce::Component> makeLinkPage (AudioEngine& engine, const SettingsHooks& hooks)
{
    return std::make_unique<LinkPage> (engine, hooks);
}

bool lynxExtraOn (AudioEngine& engine, const SettingsHooks& hooks, std::string_view id)
{
    if (const auto own = workflowextras::extraOn (id)) return *own; // round 190iu (ui/WorkflowExtras.h)
    if (id == "mixer.loudness") return MixerStrips::showLoudness;
    if (id == "mixer.stripInserts") return MixerStrips::showStripInserts;
    if (id == "mixer.usedOnly") return MixerStrips::showUsedOnly;
    if (id == "mixer.keepLostLinks") return MixerStrips::keepLostLinks;
    if (id == "mixer.followPlaylist") return MixerStrips::followPlaylist;
    if (id == "playlist.automationLanes") return autolanes::shown();
    if (id == "playlist.trackInserts") return PlaylistComponent::showTrackInserts;
    if (id == "playlist.shadeFadedAudio") return PlaylistComponent::shadeFadedAudio; // (round 190oo O-1)
    if (id == "pianoRoll.songBarsRuler") return PianoRollComponent::songBarsRuler;
    if (id == "pianoRoll.stackedLanes") return PianoRollComponent::stackedLanesExtra; // (round 190on, P10)
    if (id == "polyEdit.prepareOnOpen") return PolyEditorWindows::prepareOnOpen; // (round 190lo)
    if (id == "general.hoverValues") return hooks.getGeneral && hooks.getGeneral().hoverValues;
    if (id == "project.linkTracks") return hooks.getGeneral && hooks.getGeneral().linkTracksInNewProjects;
    if (id == "project.clipLaunchStrips") return newProjectDefaults().clipLaunchStrips; // (round 190oo CS O-2)
    if (id == "record.lastLoopPassPlays") return engine.getRecordOptions().lastLoopPassPlays;
    if (id == "detail.windowKey") return DetailView::extraWindowKey;
    if (id == "detail.selectInMixer") return DetailView::extraSelectInMixer;
    if (id == "simple.projectName") return ui::simplelook::projectNameShown(); // round 190oo (O-4)
    if (id == "midi.gamepad") return engine.getMidiPrefs().gamepad;
    return false;
}

void setLynxExtra (AudioEngine& engine, const SettingsHooks& hooks, std::string_view id, bool on, bool persistent)
{
    if (lynxExtraOn (engine, hooks, id) == on)
        return;
    const auto saved = [] { // the main window writes ui-options.txt
        if (mixeroptions::changed)
            mixeroptions::changed();
    };
    // a view option with an action: the action is what its menu item runs (it also refreshes the editor and saves)
    const auto toggledBy = [&hooks] (const char* action) { return hooks.runAction && hooks.runAction (action); };

    if (workflowextras::setExtra (id, on)) // round 190iu (ui/WorkflowExtras.h): saves through its own hook
        return;

    if (id == "mixer.loudness")
    {
        if (mixeroptions::setShowLoudness)
            mixeroptions::setShowLoudness (on);
        else
            MixerStrips::showLoudness = on;
        saved();
    }
    else if (id == "mixer.stripInserts") // round 190io (issue #154 part 2)
    {
        if (mixeroptions::setShowStripInserts)
            mixeroptions::setShowStripInserts (on);
        else
            MixerStrips::showStripInserts = on;
        saved();
    }
    else if (id == "mixer.usedOnly")
    {
        if (mixeroptions::setShowUsedOnly)
            mixeroptions::setShowUsedOnly (on);
        else
            MixerStrips::showUsedOnly = on;
        saved();
    }
    else if (id == "mixer.keepLostLinks")
    {
        MixerStrips::keepLostLinks = on;
        saved();
    }
    else if (id == "mixer.followPlaylist")
    {
        MixerStrips::followPlaylist = on;
        if (on)
            engine.editSong ([] (Song& s) { mixerops::followPlaylistGroups (s); });
        saved();
    }
    else if (id == "playlist.automationLanes")
    {
        if (! toggledBy ("playlist.automationLanes"))
        {
            autolanes::setShown (on);
            saved();
        }
    }
    else if (id == "playlist.trackInserts")
    {
        if (! toggledBy ("playlist.trackInserts"))
        {
            PlaylistComponent::showTrackInserts = on;
            saved();
        }
    }
    else if (id == "pianoRoll.stackedLanes") // round 190on (P10): the Piano roll's action switches it and refreshes the editors
    {
        if (! toggledBy ("pianoRoll.stackedLanes"))
        {
            PianoRollComponent::stackedLanesExtra = on;
            saved();
        }
    }
    else if (id == "playlist.shadeFadedAudio") // round 190oo (O-1): Playlist › View › Shade faded audio
    {
        PlaylistComponent::shadeFadedAudio = on;
        saved(); // (saves and repaints the Playlist)
    }
    else if (id == "pianoRoll.songBarsRuler")
    {
        if (! toggledBy ("pianoRoll.songBarsRuler"))
        {
            PianoRollComponent::songBarsRuler = on;
            saved();
        }
    }
    else if (id == "polyEdit.prepareOnOpen") // round 190lo (ui/PolyEditor.h)
    {
        PolyEditorWindows::prepareOnOpen = on;
        saved();
    }
    else if (id == "general.hoverValues")
    {
        if (hooks.getGeneral && hooks.setGeneral)
        {
            auto options = hooks.getGeneral();
            options.hoverValues = on;
            hooks.setGeneral (options);
        }
    }
    else if (id == "project.linkTracks") // round 190gz: the new projects' default (the open project: Settings › Project)
    {
        if (hooks.getGeneral && hooks.setGeneral)
        {
            auto options = hooks.getGeneral();
            options.linkTracksInNewProjects = on;
            hooks.setGeneral (options);
        }
    }
    else if (id == "project.clipLaunchStrips") // round 190oo (CS O-2): kept with the new projects' defaults
    {
        newProjectDefaults().clipLaunchStrips = on;
        saved();
    }
    else if (id == "record.lastLoopPassPlays")
    {
        auto options = engine.getRecordOptions();
        options.lastLoopPassPlays = on;
        engine.setRecordOptions (options);
        saved();
    }
    else if (id == "simple.projectName") // round 190oo (O-4): the main window's action also lays the top bar out again
    {
        if (! toggledBy ("simple.projectName"))
        {
            ui::simplelook::projectNameShown() = on;
            saved();
        }
    }
    else if (id == "detail.windowKey" || id == "detail.selectInMixer")
    {
        (id == "detail.windowKey" ? DetailView::extraWindowKey : DetailView::extraSelectInMixer) = on;
        saved();
    }
    else if (id == "midi.gamepad")
    {
        auto prefs = engine.getMidiPrefs();
        prefs.gamepad = on;
        engine.setMidiPrefs (prefs, persistent);
        if (hooks.midiChanged)
            hooks.midiChanged();
    }
}

std::unique_ptr<juce::Component> makeExtrasPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent)
{
    return std::make_unique<ExtrasPage> (engine, hooks, persistent);
}

std::unique_ptr<juce::Component> makeGeneralPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent)
{
    return std::make_unique<GeneralPage> (engine, hooks, persistent);
}

std::unique_ptr<juce::Component> makeAudioPage (AudioEngine& engine, bool persistent)
{
    juce::ignoreUnused (persistent);
    return std::make_unique<AudioPage> (engine);
}

std::unique_ptr<juce::Component> makeMidiPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent)
{
    return std::make_unique<MidiPage> (engine, hooks, persistent);
}
} // namespace lynx
