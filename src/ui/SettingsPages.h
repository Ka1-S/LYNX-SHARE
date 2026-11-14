#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Preferences.h"
#include "ui/Theme.h"

namespace lynx
{
class AudioEngine;

// The app-wide options of Settings › General. MainComponent owns most of them (they were Options menu items);
// the page reads them through SettingsHooks::getGeneral and hands every change back through setGeneral.
struct GeneralOptions
{
    juce::String language;      // i18n id ("en", "pt-BR")
    juce::String uiFont;        // family; empty: the platform's default (ui-font.txt, read at start-up)
    int uiScale = 100;          // percent
    bool compactMenus = true;   // menu density: compact (native-like rows) or comfortable (round 163)
    bool waveSpectral = false, waveStereo = false;
    int noteNames = 0;          // notenames::Style: English, Germanic, Solfège (round 165f)
    bool noteFlats = false;     // black notes as flats
    bool noteNamesEveryKey = true; // the Piano roll names every key (LYNX, round 170a)
    bool rackPlayheadColumn = true; // the Channel Rack's playhead column + bar marks (LYNX, round 170a)
    bool rackStepsChannelColour = true; // lit Channel Rack steps in their channel's colour (LYNX, round 170a2)
    bool splash = true, startupSound = true, scanAtStart = true;
    int startupProject = 1;       // issue #123: FL's "Startup project" (StartupProject: 0 empty, 1 default template, 2 last)
    juce::String defaultTemplate; // issue #123: TemplateChoice::toText of FL's "Default template" ("" = LYNX's default)
    bool menuEntry = false;     // Linux AppImage: LYNX in the applications menu + the .lynx file type (round 161)
    bool typingPiano = true, keepEditorsOpen = false, rememberSeek = false;
    int layoutStyle = 0; // Settings › General › Workflow (rounds 181a, 184a): layoutstyle::Style, 0 = Pattern workflow, the default
    bool alsSessionOnPlaylist = false; // Settings › General › Workflow (round 181c): a Live set's Session clips → the clip grid (default) or the Playlist
    bool stretchedClipsLoop = true; // LYNX extra (issue #25): a stretched pattern clip loops its pattern; off = FL (once)
    bool disableTypingPriority = false; // FL's General › "Disable typing keyboard priority by default" (off, round 187a)
    bool autoClipsAtPlayback = true; // FL's General › "Create automation clips at the playback position" (on, round 177b)
    // FL's General › "Select first channel with notes when the pattern changes" (off by default; REA round 167j §5)
    bool selectFirstChannelWithNotes = false;
    bool monophonicPainting = false; // FL's "Monophonic painting in piano roll" (round 189o)
    // FL's General › "Detach all new plugins" (issue #116, off as in FL): new plugin windows open detached
    bool detachNewPlugins = false;
    bool autoSelectLinked = true; // FL's General › "Auto select linked modules" (on, round 187a)
    // round 177d (pain point 10, LYNX's own; off = FL, which shows the value in the hint bar only): the value of the
    // control under the mouse also shows next to the cursor
    bool hoverValues = false;
    int editPatternsIn = 0;     // 0 = Piano Roll when it has notes, 1 = Channel Rack, 2 = Piano Roll
    int undoLevels = prefs::defaultUndoLevels;
    // FL's General › "Alternate undo mode" (off by default: Ctrl+Z undoes, then redoes; on: classic undo; round 167k)
    bool alternateUndo = false;
    int autosaveMinutes = prefs::defaultAutosaveMinutes; // 0 = never
    int autosaveKeep = prefs::defaultAutosaveKeep;
    bool askUnusedRecordings = true; // offer to delete takes a project no longer uses when it closes
    bool backupWaitsForQuiet = true; // round 177d: a backup waits while playing and right after MIDI input too
    int backupLongestWaitMinutes = prefs::defaultBackupLongestWaitMinutes; // ...and is written anyway after this long
    bool crossfadesInNewProjects = true; // FL's "Enable automatic crossfades in new projects" (Settings › Project; round 166e)
    bool pluginSandbox = false;
    bool pluginSubstitutes = true; // missing plugins play a LYNX equivalent (core/PluginSubstitutes.h)
    bool limitEditors = true; // FL's "Limit the number of open plugin editors" (round 189o, core/EditorLimit.h; on as FL)
    int crashReports = 0;          // crashreport::Consent: 0 ask, 1 always send, 2 never (round 159)
    bool checkUpdates = true;      // "Check for updates automatically" (ui/Updater.h, round 166b)
    bool reducedEffects = false;
    bool uiAnimations = true;      // round 179a: panel fades, hover fades, wheel glide (ui/Motion.h)
    bool artMotion = true;         // round 170j: the Themed plugins' art moves (falling petals, glows...)
    int animationRate = 0;         // round 174e2: playhead / meters / scopes at the display's refresh (0), or at most 60 / 30 Hz
    bool gpuRendering = true;      // Windows: Direct2D (JUCE 8's default) or GDI for every LYNX window (round 157a)
    int pluginStyle = -1;          // −1 automatic (each plugin its own style), else a skin::Style forced everywhere   // plugin editors without glows, blooms or the open animation (ui/PluginSkin.h)

    bool operator== (const GeneralOptions&) const = default;
};

// What the Settings window needs from the main window (all optional: the --snapshot mode passes none).
struct SettingsHooks
{
    std::function<void()> browserFoldersChanged;         // the Browser reloads its folder list
    std::function<GeneralOptions()> getGeneral;
    std::function<void (const GeneralOptions&)> setGeneral; // applies and saves
    std::function<void()> manageShortcuts, managePlugins;
    std::function<void()> midiChanged;                   // the MIDI settings were changed (the engine already has them)
    // runs one of the main window's actions by id ("playlist.automationLanes"): the LYNX extras page switches a view
    // option exactly as its menu item does (round 190am)
    std::function<bool (const std::string&)> runAction;
    // Settings › Link's "Show Link Toggle" (round 190gl): the main toolbar's Link toggle shown, and showing / hiding it
    std::function<bool()> linkToggleShown;
    std::function<void (bool)> showLinkToggle;
};

// Settings pages (Settings window, ui/SettingsDialog.cpp)
std::unique_ptr<juce::Component> makeGeneralPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent);
std::unique_ptr<juce::Component> makeAudioPage (AudioEngine& engine, bool persistent);
std::unique_ptr<juce::Component> makeMidiPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent);
// Settings › LYNX extras (round 190am, decision D7): LYNX's own improvements that are off by default, each with its
// switch and one line of explanation (core/LynxExtras.h). The switches mirror the preferences the features already
// have: nothing is stored by the page itself.
std::unique_ptr<juce::Component> makeExtrasPage (AudioEngine& engine, const SettingsHooks& hooks, bool persistent);
// Settings › Ableton Link (round 190gl): Ableton Link Enabled / Disabled (Ableton's integration guidelines), then the
// reference DAW's Link page — "Show Link Toggle" (Show / Hide) and "Start Stop Sync" (On / Off). Its Link Audio section
// is left out: LYNX has no Link Audio.
std::unique_ptr<juce::Component> makeLinkPage (AudioEngine& engine, const SettingsHooks& hooks);
// Whether the extra `id` (core/LynxExtras.h) is on now, and switching it: the same variables and the same code paths
// as each feature's own menu item or check box.
bool lynxExtraOn (AudioEngine& engine, const SettingsHooks& hooks, std::string_view id);
void setLynxExtra (AudioEngine& engine, const SettingsHooks& hooks, std::string_view id, bool on, bool persistent);

// The autosave folder (%APPDATA%\LYNX\autosave).
juce::File autosaveFolder();

namespace settingsui
{
// a section title with a hairline under it
class SectionTitle final : public juce::Component
{
public:
    explicit SectionTitle (juce::String t = {}) : text (std::move (t)) {}
    void setText (const juce::String& t) { text = t; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.setFont (theme::font (theme::type::small, true));
        g.setColour (theme::colour::textSecondary);
        g.drawText (text.toUpperCase(), 0, 0, getWidth(), getHeight() - 6, juce::Justification::bottomLeft);
        g.setColour (theme::colour::border);
        g.fillRect (0, getHeight() - 2, getWidth(), 1);
    }

private:
    juce::String text;
};

inline juce::Label& styleLabel (juce::Label& label, const juce::String& text, bool secondary = true)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (theme::font (theme::type::body));
    label.setColour (juce::Label::textColourId, secondary ? theme::colour::textSecondary : theme::colour::textPrimary);
    return label;
}

inline void styleNote (juce::Label& label, const juce::String& text)
{
    styleLabel (label, text);
    label.setFont (theme::font (theme::type::small));
    label.setColour (juce::Label::textColourId, theme::colour::textDisabled);
    label.setJustificationType (juce::Justification::topLeft);
}

inline void styleEditor (juce::TextEditor& editor)
{
    editor.setFont (theme::font (theme::type::body));
    editor.setIndents (8, 5);
}

// A vertical page that scrolls when the window is short: rows are laid out by `layout(width)`, which returns the
// content height.
class ScrollingPage : public juce::Component
{
public:
    ScrollingPage()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true); // (sideways only below the page's minimumWidth, round 190bj)
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        // round 190bj (QA: Settings › MIDI at ~390 px lost each input row's switch and device name): below the page's
        // narrowest width its rows keep that width and the page scrolls sideways instead of squeezing them
        const auto narrow = getWidth() - viewport.getScrollBarThickness() < minimumWidth();
        const auto height = getHeight() - (narrow ? viewport.getScrollBarThickness() : 0);
        const auto width = juce::jmax (minimumWidth(), getWidth() - (needsScroll (height) ? viewport.getScrollBarThickness() : 0));
        content.setSize (width, juce::jmax (height, layout (width)));
    }

protected:
    virtual int layout (int width) = 0;
    // the narrowest width the page's rows are laid out at (a narrower window scrolls sideways)
    virtual int minimumWidth() const { return 560; }
    juce::Component& page() noexcept { return content; }

    void add (juce::Component& c) { content.addAndMakeVisible (c); }

private:
    bool needsScroll (int height) { return layout (juce::jmax (minimumWidth(), getWidth())) > height; }

    juce::Viewport viewport;
    juce::Component content;
};
} // namespace settingsui
} // namespace lynx
