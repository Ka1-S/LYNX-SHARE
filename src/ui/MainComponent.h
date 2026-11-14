#pragma once

#include "core/ProjectKey.h"
#include "core/Preferences.h"
#include "core/ChordReadout.h"

#include <atomic>
#include <map>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/KeyMap.h"
#include "core/UndoKeys.h"
#include "core/LayoutStyle.h"
#include "core/Performance.h"
#include "ui/UiTrace.h"
#include "core/ToolbarLayout.h"
#include "core/ValueMath.h"
#include "engine/AudioEngine.h"
#include "engine/MidiFileIo.h"
#include "ui/BrowserComponent.h"
#include "ui/MapMode.h"
#include "ui/Icons.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/MixerComponent.h"
#include "ui/DetailView.h"
#include "ui/PlaylistComponent.h"
#include "ui/SessionDialogs.h"
#include "ui/SimpleWorkflow.h"
#include "ui/SupportLynx.h"
#include "ui/LiveSessionUi.h"
#include "ui/HintBar.h"
#include "ui/Splash.h"
#include "ui/FrameTicker.h"
#include "ui/Motion.h"
#include "ui/Toolbar.h"
#include "ui/Updater.h"
#include "ui/ValueControl.h"
#include "ui/Workspace.h"

namespace lynx
{
class GamepadInput;
namespace folderwatch
{
class Watcher;
}
namespace gamepad
{
struct Changes;
struct State;
} // namespace gamepad

class LyricsPanel;

class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            public ui::HintSource,
                            private juce::MenuBarModel,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    void backupNowForTest() { autosave(); } // --rtstress --backuptest (round 177d)
    // `restoreUserSettings`: load/save the workspace layout, key map and UI options from the user
    // settings (off for --snapshot, which must look the same on every machine).
    explicit MainComponent (AudioEngine& engine, bool restoreUserSettings = true);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override; // right-click on the toolbar: the toolbar menu
    bool keyPressed (const juce::KeyPress& key) override;
    void dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&) override; // (issue #12: drops on windows)
    bool keyFromSecondaryWindow (const juce::KeyPress& key); // a key a tool / plugin window left unused
    bool keyFromPluginView (const juce::KeyPress& key, bool optionOn); // a plain key over a focused plugin (issue #96)
    bool keyStateChanged (bool isKeyDown) override;
    ui::Hint hintAt (juce::Point<int> position) override; // the toolbar's background: how to edit it

    // Opens .lynx or .flp (also used by the command line).
    void openFile (const juce::File& file);
    // A file the operating system asks LYNX to open while it runs (macOS Finder): as File › Open, unsaved changes are
    // asked about first; waits for a project still loading.
    void openFileRequest (const juce::File& file);
    void showSetupWizard(); // first run, or Options > Setup wizard (an overlay over this component)
    void closeSetupWizardForSnapshot() { setupOverlay.reset(); }
    void refreshTweakReadoutForSnapshot() { refreshTweakReadout(); resized(); }

    // Asks "save changes?" if there are any; `then` runs unless the user cancels.
    void confirmDiscard (std::function<void()> then);
    // The main window came up (issue #62): the detached panels restored with the layout go to their saved place, above it.
    void restoreDetachedWindows() { workspace.restoreDetachedWindows(); }
    // Issue #123: at start-up without a file to open, FL's "Startup project" (Settings › General › Startup): the default
    // template as a new untitled project (nothing changes while it is LYNX's default), an empty project or the last one.
    void openStartupProject();

    // For --snapshot mode: 0 = Channel Rack, 1 = Piano Roll (on the channel with the most notes), 2 = Playlist, 3 = Mixer,
    // each maximized on the workspace; showDesktopForSnapshot shows the default layout (with `extraPanels`, the
    // Piano Roll and Mixer floating too).
    void selectTabForSnapshot (int tab);
    // --uiperf / tests: F8's Plugin picker (round 171c), and whether it is open
    void togglePluginPickerForSnapshot() { togglePluginPicker(); }
    juce::Component* pluginPickerForSnapshot() const noexcept { return pluginPickerView.get(); }
    void showDesktopForSnapshot (bool extraPanels);
    // The Clip-launch workflow's layout (round 181a): the clip grid beside the Playlist, or flipped. Nothing exists until
    // it is first turned on. Moves panels only; the project does not change, except that it gets an (empty) grid.
    void setClipLaunchWorkflow (bool on, bool swapLayout = true) { setLayoutStyle (on ? layoutstyle::Style::clipLaunch : layoutstyle::Style::pattern, swapLayout); }
    void setClipGridView (PlaylistArea::GridView view);
    bool isClipLaunchWorkflow() const noexcept { return layoutStyle == layoutstyle::Style::clipLaunch; }
    // Round 190ef: the reference's MIDI Map Mode / Key Map Mode (ui/MapMode.h; Ctrl+M / Ctrl+K and the MIDI / Key
    // switches), in the Clip-launch layout only (elsewhere it stays off).
    void setMapMode (remotemap::Mode mode);
    remotemap::Mode getMapMode() const noexcept { return mapOverlay != nullptr ? mapOverlay->getMode() : remotemap::Mode::off; }
    mapmode::Overlay* mapOverlayForSnapshot() noexcept { return mapOverlay.get(); }
    mapmode::MappingBrowser* mappingBrowserForSnapshot() noexcept { return mappingBrowser.get(); }
    void fireRemote (const remotemap::Fire& fire); // a mapping acts (MIDI from the engine, keys from keyPressed)
    // View › Layout style (round 184a: Pattern, Clip-launch or Simple workflow; core/LayoutStyle.h). Moves panels only;
    // each style keeps its own workspace layout. The Simple workflow's parts are made when it is first chosen.
    void setLayoutStyle (layoutstyle::Style style, bool swapLayout = true);
    layoutstyle::Style getLayoutStyle() const noexcept { return layoutStyle; }
    SimpleWorkflow* getSimpleWorkflowForSnapshot() noexcept { return simpleWorkflow.get(); } // --simpleshot
    // round 190fc (B1): how many Simple workflow objects were still alive when the last MainComponent's destructor body
    // ended (must be 0: what is left is destroyed after the Workspace, which its Host still reaches); -1: none ended yet
    static inline int simpleWorkflowAliveAfterTeardown = -1;
    // The Clip-launch workflow's detail view (round 183a), made with the grid; nullptr until then.
    DetailView* getDetailViewForSnapshot() const noexcept { return detailView.get(); }
    juce::Component* getClipGridForSnapshot() const noexcept { return clipGridView.get(); } // --gridshot (round 190bv)
    // round 190ch (LYNX's own): the project's Lyrics / Notes panel — docked on the right of the Playlist in every layout,
    // or in its own window — and the Reference match window
    void toggleLyrics();
    void showLyrics (bool floating);
    void hideLyrics();
    bool isLyricsShowing() const;
    void showReferenceMatch();
    LyricsPanel* getLyricsPanelForSnapshot(); // (docked, made if needed)
    void commitSongNotes(); // what is being typed in the Lyrics / Notes panel reaches the project now (before a save)
    juce::PopupMenu getKeyMenuForSnapshot() { return makeKeyMenu(); } // --simpleshot (round 184a S11)
    // --uiperf (round 173f): the default layout with these panels shown too (the reporter's Channel Rack + Mixer)
    void showPanelsForSnapshot (std::initializer_list<workspace::Panel> panels);
    void openQuickAddForTest() { showQuickAdd(); } // --windowtest
    Workspace& workspaceForTest() { return workspace; } // --plugwintest dock mode (issue #116)
    juce::Component* setupOverlayForTest() const { return setupOverlay.get(); }
    juce::Component* danceStageForTest() const { return danceStage.get(); } // (a DanceStage, ui/DanceView.h)
    void waitForBrowserForSnapshot() { browser.waitForListing (5000); }
    // --no-plugins project.lynx (round 190ap): the next .lynx project opens with its third-party plugins off
    void openNextWithoutPlugins (bool on) { nextOpenWithoutPlugins = on; }
    juce::StringArray getMenuNamesForSnapshot() { return getMenuBarNames(); }
    juce::PopupMenu getMenuForSnapshot (int index) { return getMenuForIndex (index, {}); }
    juce::PopupMenu getPanelMenuForSnapshot (workspace::Panel panel)
    {
        juce::PopupMenu menu;
        addPanelMenuItems (panel, menu);
        return menu;
    }
    juce::PopupMenu getFullRecordMenuForSnapshot() { return buildRecordMenu(); } // the record button's whole menu
    juce::PopupMenu getRecordMenuForSnapshot()
    {
        juce::PopupMenu menu;
        addRecordOptions (menu);
        return menu;
    }
    void setHintForSnapshot (const ui::Hint& hint, bool live = false)
    {
        hintsFrozen = true; // the timer would replace it with the hint under the (off-screen) mouse
        hintBar.setHint (hint, live);
    }
    juce::Rectangle<int> getToolbarBoundsForSnapshot() const { return { 0, 0, getWidth(), toolbarHeight }; }
    // every visible control with a quick tip (toolbar and panel title bars), in this component's coordinates
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> getQuickTipsForSnapshot();
    // --snapshot: a toolbar layout (icon size, hidden/reordered groups) without saving it
    void setToolbarLayoutForSnapshot (const toolbarlayout::Layout& layout) { setToolbarLayout (layout, false); }
    juce::PopupMenu getToolbarMenuForSnapshot() { return makeToolbarMenu(); }
    void setToolbarEditingForSnapshot (bool editing) { setToolbarEditing (editing); }
    int getToolbarAndTrayHeightForSnapshot() const { return toolbarHeight + (toolbarEditor != nullptr ? toolbar::EditOverlay::trayHeight : 0); }
    juce::Rectangle<int> getToolStripAreaForSnapshot (bool playlistStrip);
    void setMetersForSnapshot (double cpu, std::int64_t memory, int polyphony) { perfMeter.setValues (cpu, memory, polyphony); }
    static std::unique_ptr<juce::Component> makeHelpContent (const keys::KeyMap* keyMap = nullptr); // nullptr: the defaults
    static std::unique_ptr<juce::AlertWindow> makeAboutWindow(); // result 1 = OK, 2 = "Support LYNX SHARE"
    // round 190fq: the start-up support note (ui/SupportLynx.h), once per normal start; `quietRun` = test mode / CI
    void startSupportNote (bool quietRun);
    support::StartupNote& supportNoteForSnapshot();
    supportnote::Moment supportNoteMoment(); // a dialog / overlay in front, playing, recording
    std::unique_ptr<juce::AlertWindow> makeConfirmDiscardWindow();
    PianoRollComponent& getPianoRollForSnapshot() { return pianoRoll; }
    ChannelRackComponent& getRackForSnapshot() { return rack; }
    void scrollRackForSnapshot (int x) { rackViewport.setViewPosition (x, rackViewport.getViewPositionY()); }
    BrowserComponent& getBrowserForSnapshot() { return browser; }
    MixerComponent& getMixerForSnapshot() { return mixer; }
    // the Multilink button's right-click menu (round 190ey: shown by it, and the shot route's)
    juce::PopupMenu getMultilinkMenuForSnapshot();
    PlaylistComponent& getPlaylistForSnapshot() { return playlist; }
    juce::Component& getRackViewportForSnapshot() { return rackViewport; }
    std::unique_ptr<juce::Component> makeQuickAddForSnapshot();
    PatternPicker& getPickerForSnapshot() { return picker; }
    void selectMixerTrackForSnapshot (int track, int slot = -1)
    {
        mixer.selectTrack (track);
        if (slot >= 0)
            mixer.selectSlot (slot);
    }

private:
    using Panel = workspace::Panel;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showAudioSettings();
    void showImportReport();
    void importFlp (const juce::File& file);
    void flpImported (const juce::File& file); // after the import: title, recent files, missing files
    void openLynxProject (const juce::File& file, bool withoutPlugins = false);
    // round 190ap: "Open without third-party plugins" (File menu; --no-plugins on the command line): the project opens
    // with every hosted plugin switched off, its settings kept; the panel switches them back on
    void chooseFileToOpenWithoutPlugins();
    void showPluginsOff();
    // The "Missing files" window (ui/MissingFilesDialog.h) for the current project; `afterOpening`: shown by opening a
    // project (only when something is missing; the FL import report follows it), else from the menu (always).
    void showMissingFiles (bool afterOpening);
    void openSettings (int page); // ui/SettingsDialog.h SettingsPage: 0 Project, 1 Info, 2 Files, 3 General, 4 Audio, 5 MIDI
    void showPluginManager();
    void showShortcutManager();
    int scansSeen = 0; // background plugin scans already announced in the hint bar
    int pluginCrashesSeen = 0; // hosted plugins that crashed (engine/PluginHost.h) and were already announced
    int editorCrashesSeen = 0; // faults in plugin editor windows (ui/PluginWindow.h, issue #32) already announced
    int sandboxDeathsSeen = 0; // separate-process plugins that died (engine/PluginSandbox.h, round 176a) already announced
    void announceEditorCrash (const juce::String& plugin);
    void announceSeparateProcessCrash (const juce::String& plugin, bool crashed, bool bits32);
    void reloadCrashedPlugins (const juce::String& plugin); // round 190ao: a crash prompt's "Reload"
    void showHelp();
    void closeUnfocusedWindows();
    void closeHelp();
    juce::Component::SafePointer<juce::DialogWindow> helpWindow; // the shortcut sheet (F1 toggles it)

    // transport
    void playStop();     // Space: play, or stop and go back to where playback started
    void playPause();    // Ctrl+Space: pause / resume where it is
    void playFromStart(); // Shift+Space (FL): play; while playing, stop at the start of the time selection / song / pattern
    void stopAndRewind (bool toStart = false); // toStart: the very start, ignoring "Remember seek time"
    void showStopMenu(); // right-click on Stop: "remember seek time", panic // the Stop button
    double playStartTicks = 0.0;

    // key map and actions
    bool runAction (const std::string& id);
    keys::Context focusContext() const;
    void loadKeyMap();
    keys::KeyMap keyMap;
    // On by default, as FL 2026 (OptionsTypingKbToPianoAction: Checked = True; measured round 168t2). While it is on, FL
    // too takes the plain letter keys for the piano: P / E / R played notes and K (next channel) did nothing.
    bool typingPiano = true;
    int typingOctave = keys::typingpiano::defaultOctave, typingRoot = 0; // the button's right-click menu, as FL's
    int typingLayout = keys::typingpiano::pianoLayout();
    // the typing keyboard's notes launching performance clips (FL: they are MIDI channel 1, see keyPressed): its own
    // offsets and modifier keys (notes 120..127), and the keys held down with the notes they launched
    perf::MidiMapping typingPerformance;
    std::map<int, std::vector<int>> typingLaunches;
    static bool isPlainKey (const keys::Chord& chord) noexcept; // no Ctrl / Alt / Shift
    // what the typing keyboard does with a key (nothing when off): by the key's place on the keyboard, not its letter
    // (QWERTZ, AZERTY, Dvorak... play as a US keyboard does, as FL)
    keys::TypingKey typingKey (const juce::KeyPress& key) const;
    void releaseTypingLaunches (bool all); // the released keys' launches (all: every one)
    int lastCurrentPattern = -1; // ("Select first channel with notes when the pattern changes")
    int lastLinkedChannel = -1;  // (Auto select linked modules, round 187a)
    void setTypingPiano (bool on);
    void toggleTypingPiano(); // the user's toggle (button, menus, Ctrl+T): also says so in the hint bar
    juce::String shortcutText (const char* actionId) const; // "\tF5" for menus ("" when unbound)

    // panels
    void togglePanel (Panel panel);
    void fitChannelRack (bool width); // the Channel Rack pop-up sizes itself to its rows (and steps)
    int fittedChannelCount = -1;
    void showPanel (Panel panel);
    void refreshPanelButtons();
    void showWindowsTip(); // the first starts name the window buttons in the hint bar (round 168t)
    void saveLayoutAs();
    juce::PopupMenu layoutsMenu();

    // patterns
    void selectPattern (int index);
    void nextEmptyPattern();
    void insertPatternHere();
    void deleteCurrentPattern();
    void splitPatternByChannelNow();
    void clonePatternNow();
    void movePatternNow (int delta);

    // workflow P2 windows and menus
    void showQuickAdd();
    void addPanelMenuItems (Panel panel, juce::PopupMenu& menu); // a window's own items in its title-bar menu
    void showHistory();
    void addRecordOptions (juce::PopupMenu& menu); // count-in, wait, overdub/replace, quantise, song clip
    void addRecordQuantizeItems (juce::PopupMenu& menu); // FL's Quantize items: start, end, duration, step sequencer
    void updateRecordQuantise(); // record quantise follows the global grid
    bool recordQuantiseOn = true;
    void showRenamePattern (int index);

    // user settings (UI options)
    juce::File uiSettingsFile() const;
    void loadUiSettings();
    void saveUiSettings();
    bool persistent = true;
    bool nextOpenWithoutPlugins = false; // --no-plugins (round 190ap)
    bool shownPluginsOff = false;        // what the title says now
    StartupOptions startup; // splash screen and start-up sound (read by Main.cpp before this window exists)

    // menu bar
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}
    void rememberRecent (const juce::File& file);
    // `picked`: the patterns picked in the Playlist picker when the menu opens from there (FL renders all of them)
    juce::PopupMenu makePatternMenu (std::vector<int> picked = {});
    juce::PopupMenu playlistTrackExportMenu (bool groups); // File › Export / Playlist Tools: the three starts (round 168e)
    // Tools › Last tweaked / Next to last tweaked (REA round 143): the parameter menu of a recently moved control
    juce::PopupMenu makeLastTweakedMenu (int which);
    // the last tweaked control's own items (automation, remote control, value): its menu, and FL's left column of the
    // Multilink button's menu while multilink is off (round 190ey)
    void addLastTweakedItems (juce::PopupMenu& menu, const AutomationTarget& target);
    // File › Import › MIDI file... (REA round 143)
    void importMidiFile();
    void importMidiFile (const juce::File& file, const MidiFileIo::ImportOptions& options);
    static float& lastTweakedClipboard();
    void setTweakedValue (const AutomationTarget& target, float value);
    juce::MenuBarComponent menuBar { this };
    juce::RecentlyOpenedFilesList recent;
    // The main toolbar is two rows high, laid out the way FL Studio arranges its panels (docs/WORKFLOW_FL.md,
    // section 5.1): the menu with the hint bar under it, transport (with the time display and the song
    // position slider), master volume and pitch, recording options, global snap and pattern selector, window
    // and file shortcuts, then CPU / memory / polyphony and the output monitor on the right. Panels drop
    // out by priority when the window is narrow; the hint bar takes the room that is left.
    // The user can hide and reorder the groups, pick the icon size and the UI scale (right-click on the
    // toolbar, or Options > Toolbar; core/ToolbarLayout.h). Row height follows the icon size.
    toolbarlayout::Layout toolbarLayout = toolbarlayout::Layout::makeDefault();
    int rowHeight = toolbarlayout::rowHeight (toolbarlayout::IconSize::medium);
    int toolbarHeight = 2 * rowHeight + 12;
    toolbarlayout::IconSize appliedIconSize = toolbarlayout::IconSize::medium;
    bool compactRecord = false;
    bool narrowSnap = false; // round 170e5, 1280 px: the "+" new-pattern button takes the pattern icon's place
    juce::Rectangle<int> modeArea; // the Pattern / Song switch (painted as one control) // a narrow window: four recording switches per row (round 170e3)
    void applyIconSizing (toolbarlayout::IconSize size);
    void setToolbarLayout (const toolbarlayout::Layout& layout, bool save = true);
    juce::PopupMenu makeToolbarMenu();
    void showToolbarMenu();
    void showCustomizeToolbar();
    // The toolbar's edit mode (drag panels to move, add or remove them; toolbar::EditOverlay)
    void setToolbarEditing (bool editing);
    void undoRedo (bool redo); // with "Undone: ..." and the undo level in the hint bar
    // FL's Edit › Undo (Ctrl+Z, round 167k): undoes the last step, or redoes one when something is undone (FL's
    // FUN_010173a0: history position 0 → 1, else position − 1); with "Alternate undo mode" a plain multi-level undo
    void flUndo();
    // FL's Edit › Undo backwards (Ctrl+Alt+Z): one more step back; with "Alternate undo mode" the two shortcuts swap, so
    // it undoes, or redoes when something is undone (round 190b, issue #140; core/UndoKeys.h)
    void flUndoBackwards();
    void runUndoStep (undokeys::Step step);
    bool alternateUndo = false; // Settings › General › "Alternate undo mode" (FL's default: off)
    std::unique_ptr<toolbar::EditOverlay> toolbarEditor;
    std::unique_ptr<juce::Component> pluginPickerView; // FL's Plugin picker (F8), its own window over the screen (rounds 169k, 175b)
    // round 184a, S16: the game controller, made only while Settings › MIDI › "Use a game controller" is on (no timer
    // otherwise); the hints overlay while Back is held
    std::unique_ptr<GamepadInput> gamepadInput;
    std::unique_ptr<juce::Component> gamepadHints;
    void updateGamepad();
    void gamepadChanges (const gamepad::Changes& changes, const gamepad::State& state, double seconds);
    int gamepadInsert() const; // the selected track's insert (its track mode's link, else the selected channel's)
    void togglePluginPicker();
    std::unique_ptr<juce::Component> setupOverlay; // the first-start / Options > Setup wizard, over everything
    std::unique_ptr<support::StartupNote> supportNote; // round 190fq (made on first use)
    void hostSetupOverlayNatively(); // issue #116: in front of the native panels (ui/NativeChild.h)
    toolbar::EditOverlay::Bounds panelBounds {}; // each shown toolbar panel (for the edit mode)
    juce::Rectangle<int> fixedToolbarArea;       // the menu and the hint bar
    void newProject (bool askAlways = false); // File › New: the New project window unless turned off
    void startNewProject (Song song, const juce::File& file);
    void saveAsTemplate();
    void chooseFileToOpen();
    void chooseFileToOpenNow();
    // `afterSaved`: runs once the project is on disk, also when a "Save as" dialog had to ask first (never after a
    // cancelled dialog or a failed save).
    void save (bool askForFile, std::function<void()> afterSaved = {});
    void exportAudio (int format = -1); // −1: the last export's format(s); 0 WAV, 1 MP3, 2 OGG, 3 FLAC alone
    void exportForOtherApps();          // File › Export › For other apps... (round 184a S9; also the Finish dock's row)
    void exportMidiFile();
    void switchSmartDisableForAll();
    void autoCloseDevice(); // Settings › Audio › "Auto close" (round 190bx), from the timer
    bool deviceClosedByAutoClose = false;
    void confirmPrepareForMidiExport();
    void saveClipboardToBrowser (bool midi);
    juce::PopupMenu makeSwitchAudioClipsMenu (bool selectedOnly);
    void switchAudioClips (bool selectedOnly, bool keepPitch, StretchMethod method, const juce::String& modeName);
    void changeDeclickOfSelectedClips (int mode);
    void prepareForPerformance(); // Tools › Macros (round 165m)
    void refreshToolbar();
    void updateTitle();
    bool shouldReclaimKeyboardFocus(); // the main window is active but nothing in it has the keyboard focus
    void autosave();
    // project safety (REA round 118, FL's backup folder / save new version / project data files interpreted)
    void saveNewVersion();
    void revertToLastBackup();
    // round 190ap: the Versions panel (saved versions and backups of this project) and its "Open as a copy"
    void showVersions();
    void openProjectAsCopy (const juce::File& file, const juce::String& name);
    void exportPackage();
    sessionui::Context sessionContext(); // the File › Online session commands (round 168a)
    liveui::Context liveContext();       // the live session's (round 169b)
    void startLiveFromCommandLine();     // --live-host code.txt / --live-join CODE|@code.txt (scripted GUI checks)
    void collectSamples();
    void keepOverwrittenCopy (const juce::File& file); // the previous file of a save, kept in the backup folder
    void rotateBackups (const juce::String& stem);
    // unused recordings (REA round 134, FL's "Delete unused recorded files" / "Delete unused files and save project")
    void offerToDeleteUnusedRecordings (bool keepsCurrentState, std::function<void()> then);
    void deleteUnusedRecordingsAndSave();
    void saveToNewProjectFolder();                    // FL's File › "Save to new project folder..."
    juce::File recordingFolderFor (const juce::File& projectFile) const;
    std::vector<std::string> savedSamples;            // samples of the project as it is on disk (when last unmodified)
    bool askUnusedRecordings = true;                  // Settings › General › Backup
    void startSession();                              // crash detection: a marker that a clean exit removes
    void checkPreviousSession (juce::Time sessionStart);
    void checkInterruptedTakes(); // round 163: takes left by a crash while recording
    void quickRecordAudio();      // round 163: one key from "record" to a clip
    std::unique_ptr<juce::InterProcessLock> sessionLock;
    juce::File sessionMarker;
    int autosaveTicks = 0;
    std::shared_ptr<std::atomic<bool>> autosaveRunning = std::make_shared<std::atomic<bool>> (false); // the backup file is being written
    int autosaveMinutes = 2, autosaveKeep = 10; // Settings › General › Backup
    juce::int64 lastBackupMs = 0;  // when the last timed backup was written, and for which project (Song::projectId)
    std::string lastBackupProject;
    juce::String simpleSaveStatus() const;
public:
    // --simpleshot: the status texts as drawn (saved, not saved yet, backed up now / minutes ago)
    static juce::String simpleSaveStatusText (bool modified, bool hasFile, juce::int64 backupMs, juce::int64 nowMs);
private:
    bool backupWaitsForQuiet = true;              // (round 177d) also while playing / right after MIDI input
    int backupLongestWaitMinutes = prefs::defaultBackupLongestWaitMinutes; // ...but a backup is written after this long
    bool hoverValues = false;                     // (round 177d) the hovered control's value next to the mouse
    bool hoverTipShown = false;                   // ...and it is showing now
    juce::String hoverTipText;
    juce::Point<int> hoverTipAt;
    std::uint32_t backupMidiSeen = 0;
    double backupMidiMs = -1.0e9;
    bool lastModified = false;
    void openPianoRoll();
    void toggleMode();
    void refreshModeButton();
    void showPatternMenu();

    AudioEngine& engine;
    std::unique_ptr<class ControllerBridge> controllerBridge; // (round 190aw)

    IconButton newButton { "new", Icon::newFile }, openButton { "open", Icon::open }, saveButton { "save", Icon::save },
        exportButton { "export", Icon::exportAudio }, undoButton { "undo", Icon::undo }, redoButton { "redo", Icon::redo };
    IconButton playButton { "play", Icon::play };
    IconButton stopButton { "stop", Icon::stop };
    IconButton recordButton { "record", Icon::record };
    IconButton recordMenuButton { "recordMenu", Icon::mic };
    IconButton patternModeButton { "patternMode", Icon::pattern }, songModeButton { "songMode", Icon::song };
    IconButton metronomeButton { "metronome", Icon::metronome };
    IconButton multilinkButton { "multilink", Icon::link }; // FL's MultiLinkBtn (round 177b-4)
    IconButton typingPianoButton { "typingPiano", Icon::keyboard };
    IconButton countInButton { "countIn", Icon::countIn };
    IconButton waitInputButton { "waitInput", Icon::waitInput };
    // FL's recording panel toggles (round 170e2): Blend recorded notes (overdub, off = replace) and the tempo tapper
    IconButton blendButton { "blend", Icon::blend };
    IconButton tapTempoButton { "tapTempo", Icon::tapTempo };
    // round 170e3: FL's Loop record and Step edit switches, and LYNX's punch in/out on the loop region, as toggles
    IconButton loopRecordButton { "loopRecord", Icon::loopRecord };
    IconButton stepEditButton { "stepEdit", Icon::stepEdit };
    IconButton punchButton { "punch", Icon::punch };
    stepedit::Writer stepWriter;               // Step edit: where the next played note goes (core/StepEdit.h)
    double stepSeekTick = -1.0, stepPrevTick = -1.0; // the position step edit last moved to / moved from
    void applyStepEdit();
    void refreshRecordOptionButtons();
    void showRecordMenu();
    juce::PopupMenu buildRecordMenu(); // (what showRecordMenu shows)
    // right-click on the metronome: on/off, its volume, count-in (FL keeps the metronome's options there)
    void showMetronomeMenu();
    void showTypingPianoMenu();
    // the metronome volume knob in a callout under the metronome button
    void showMetronomeVolume();
    void dumpScoreLog (int minutes); // Tools › Score logger (REA round 165f)
    // View > Audio editor: the selected channel's sample when it is a Sampler with one, else an empty editor
    void openAudioEditorForSelection();
    void connectBrowser (BrowserComponent& b); // the main Browser and the Sample browser window (round 163)
    void toggleSampleBrowser();
    void openSampleBrowser();
    void toggleRecording();       // the record button: asks what to record first (unless told not to)
    void armSelectedInsertForAudio(); // "Audio, into the Playlist": arms the selected insert (asks for its input)
    // FL's Tools › One-click audio recording › Into Edison audio editor/recorder | Into playlist as an audio clip (round
    // 190dx, core/OneClickRec.h): `into` = 3 (Edison) or 2 (Playlist), FL's modes
    void oneClickRecord (int into);
    void setControlHints(); // the hint bar text of every toolbar control (markup, see ui/HintBar.h)
    juce::String keyMarkup (const char* actionId) const; // "[Ctrl][S]" for the action's first key ("" when unbound)

    // tempo: a number dragged by its digits (whole BPM or decimals), with tap tempo in its menu
    class TempoDisplay final : public juce::Component,
                               public ui::HintSource
    {
    public:
        explicit TempoDisplay (AudioEngine& e) : engine (e) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        ui::Hint hintAt (juce::Point<int> position) override;
        void showMenu();
        void showTap();

    private:
        bool isOnDecimals (int x) const;
        void setBpm (double bpm);
        ui::ValueSpec spec() const;
        AudioEngine& engine;
        double startBpm = 0.0;
        bool dragOnDecimals = false, dragging = false;
        int decimalX = 0; // x of the decimal point, from the last paint
    };
    TempoDisplay tempoDisplay { engine };

    // The toolbar's pattern selector (FL's PatSelect, a TVectorDigiWheel; REA round 170a §3): a click opens the list as
    // before, a left-drag up / down and the wheel step through the patterns (no list to open and read).
    struct PatternBox final : public juce::ComboBox
    {
        std::function<void (int delta)> onStep;
        float downY = 0.0f;
        int appliedSteps = 0;
        bool dragging = false, pressed = false;
        patterndrag::Wheel wheelSteps;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    };
    PatternBox patternBox;
    IconButton newPatternButton { "newPattern", Icon::plus };
    IconButton patternMenuButton { "patternMenu", Icon::more };
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; // in the Channel Rack's title bar
    // The Channel Rack's title tools (FL keeps them in the rack's title bar, REA round 104): the channel filter
    // group box ("All"), the global swing knob and the graph editor switch.
    struct FilterBox final : public juce::Component
    {
        juce::String text;
        bool hovered = false;
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override;
        void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered = false; repaint(); }
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };
    // The Channel Rack's pattern selector (round 162, issue #9; FL's pattern selector): "<" previous, the current
    // pattern's name (click: the list of patterns, wheel: step), ">" next.
    struct PatternSelector final : public juce::Component
    {
        juce::String text;
        juce::Colour colour;
        int hoveredPart = -1; // 0 = previous, 1 = name, 2 = next
        int appliedSteps = 0;  // a left-drag on the name steps through the patterns (FL's pattern selector)
        bool namePressed = false, dragging = false;
        patterndrag::Wheel wheelSteps;
        std::function<void (int delta)> onStep;
        std::function<void()> onMenu;
        static constexpr int arrowWidth = 16;
        int partAt (int x) const { return x < arrowWidth ? 0 : x >= getWidth() - arrowWidth ? 2 : 1; }
        void paint (juce::Graphics& g) override;
        void mouseMove (const juce::MouseEvent& e) override { setHovered (partAt (e.x)); }
        void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
        void setHovered (int part) { if (part != hoveredPart) { hoveredPart = part; repaint(); } }
    };
    PatternSelector rackPatternSelector;
    void showRackPatternList();
    void stepPatternFromSelector (int delta); // drag / wheel on the toolbar box or the rack selector
    void refreshRackPatternSelector();
    // The current pattern's length in bars next to the swing knob (issue #12; drawn like the rack's insert number
    // box): drag up / down or the wheel changes it, double-click = automatic (from the notes), right-click = menu.
    struct PatternLengthBox final : public juce::Component, public juce::SettableTooltipClient
    {
        int bars = 1;         // the length shown (the effective one when automatic)
        bool automatic = true;
        bool hovered = false;
        std::function<void (int bars)> onSet; // 0 = automatic
        std::function<void()> onMenu;
        void paint (juce::Graphics& g) override;
        void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onSet) onSet (0); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;
        int dragStartBars = 1;
        static int stepBars (int bars, int delta); // 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64
    };
    PatternLengthBox rackLengthBox;
    void refreshRackLengthBox();
    void showPatternLengthMenu();
    struct RackTitleTools final : public juce::Component
    {
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
    };
    RackTitleTools rackTitleTools;
    FilterBox rackFilterBox;
    IconButton rackGraphButton { "rackGraph", Icon::graph };
    IconButton rackPianoRollButton { "rackPianoRoll", Icon::pianoRoll };
    IconButton rackLoopButton { "rackLoop", Icon::loop }; // FL's Loop button (round 165f-rack)
    IconButton rackLoopStarterButton { "rackLoopStarter", Icon::dice }; // Loop Starter (round 168b)
    IconButton rackFollowButton { "rackFollow", Icon::follow }; // Follow the playing step (issue #12)
    static constexpr int rackPatternSelectorWidth = 132;
    static constexpr int rackLengthBoxWidth = 40;
    static constexpr int rackFilterBoxWidth = 108;
    static constexpr int rackTitleToolsWidth = rackPatternSelectorWidth + 6 + rackFilterBoxWidth + 4 + 26 + 4 + rackLengthBoxWidth + 4 + 24 + 4 + 24 + 4 + 24 + 4 + 24 + 4 + 24;
    // one icon per workspace panel (F5..F9): click toggles, right-click shows it maximized
    std::array<IconButton, workspace::panelCount> panelButtons { IconButton { "playlistPanel", Icon::playlist },
                                                                 IconButton { "rackPanel", Icon::channelRack },
                                                                 IconButton { "pianoRollPanel", Icon::pianoRoll },
                                                                 IconButton { "mixerPanel", Icon::mixer },
                                                                 IconButton { "browserPanel", Icon::browser } };
    IconButton pluginPickerButton { "pluginPicker", Icon::pluginPicker };
    GlobalGridBox gridBox; // global grid of the editors (see ui/EditorTools.h)
    // FL's Snap panel: the magnet before the snap box (TToolbarForm.SnapLabel, a click toggles the snap like Backspace;
    // round 170e, Busy Works review #5)
    IconButton snapToggleButton { "snapToggle", Icon::magnet };
    std::vector<int> dividers; // x positions of the toolbar panel separators
    juce::Rectangle<int> snapLabelArea, patternLabelArea, titleArea, transportArea;
    std::unique_ptr<ui::QuickTipWindow> quickTips; // the tool names under the mouse (ui/HintBar.h)
    void setQuickTips();
    void showLanguageChoice (const juce::String& id);

    // the time display with the song position slider under it, master volume and pitch, meters
    toolbar::TimeDisplay timeDisplay { engine };
    toolbar::SongPositionBar songPosition { engine };
    toolbar::MasterKnob masterVolume { engine, toolbar::MasterKnob::Kind::volume };
    toolbar::MasterKnob masterPitch { engine, toolbar::MasterKnob::Kind::pitch };
    toolbar::PerformanceMeter perfMeter;
    toolbar::ChordPanel chordPanel;
    toolbar::ChordHint chordNearHint; // View › Toolbars: "Show detected MIDI chords near the hint bar"
    chordread::Readout chordReadout;
    std::array<std::array<std::uint64_t, 2>, chordread::channels> chordHeld {};
    std::array<std::string, 12> chordNotes;
    bool chordShowNearHint = false;
    std::uint32_t chordNearHintUntil = 0;
    void updateChordPanel();
    void showChordPanelMenu();
    // The project key (round 184a S11): the toolbar's Key panel and its menu. The Simple and Clip-launch workflows show the
    // panel until the user shows or hides it by hand (then the toolbar layout decides, in every workflow).
    toolbar::KeyPanel keyPanel;
    bool keyPanelFollowsStyle = true;
    void showKeyMenu();
    juce::PopupMenu makeKeyMenu();
    void refreshKeyPanel();
    void setProjectKey (std::optional<projectkey::Key> key);
    void showChordText();
    void addAudioLoggerMenu (juce::PopupMenu& menu);
    void dumpAudioLog();
    void refreshChordOptions();
    uitrace::Monitor uiTraceMonitor; // LYNX_TRACE_UI: message-thread stalls to ui.log (round 151)
    toolbar::MidiActivityLight midiLight;
    juce::StringArray midiLightInputs; // the open MIDI inputs (refreshed once a second)
    int midiInputTicks = 0;
    bool realtimeHintChecked = false; // Linux: the real-time priority hint was considered (round 161)
    toolbar::OutputMonitor output { engine };
    // the toolbar's live displays (output monitor, song position, time), at the display's refresh up to 72 Hz (round
    // 174e: the oscilloscope is a 1024-point path; at 72 Hz it already moves fluidly and costs half of 144)
    bool toolbarFrame (double nowMs);
    ui::FrameTicker toolbarFrames { *this, [this] (double ms) { return toolbarFrame (ms); }, 72.0 };
    int meterTicks = 0;
    juce::String driverTipType; // the device type the ASIO suggestion was worked out for

    // hint bar: what the control under the mouse does, its live value, and markers for
    // "can be automated" and "has a right-click menu"
    ui::HintBar hintBar;
    ui::TweakReadout tweakReadout; // FL's Tools › Last tweaked, at the hint bar's right end (round 162d)
    void refreshTweakReadout();
    void updateHoverValue (juce::Component* hovered, const ui::Hint& hint, bool live);
    int tweakTicks = 0;
    void placeHintBar (juce::Rectangle<int> row); // the hint bar and, at its right end, the last-tweaked read-out
    bool hintsFrozen = false; // --snapshot: keep the hint that was set
    juce::String idleHint() const;

    ChannelRackComponent rack;
    ChannelRackViewport rackViewport { rack };
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    PatternPicker picker { engine, playlist };
    PlaylistArea playlistArea { picker, playlist };
    layoutstyle::Style layoutStyle = layoutstyle::Style::pattern;
    layoutstyle::SettingsReader startStyle; // the style saved in the UI settings (applied when the window is ready)
    // round 190ch: the docked Lyrics / Notes panel (made when first shown) and the ones living in tool windows
    std::unique_ptr<juce::Component> lyricsDock;
    juce::Component::SafePointer<juce::Component> lyricsWindowPanel, referenceMatchPanel;
    void refreshSongNotes(); // the song changed: the panels above follow
    bool lanesBeforeClipLaunch = false; // the Automation lanes setting to go back to
    void addLayoutStyleItems (juce::PopupMenu& menu);
    std::unique_ptr<juce::Component> clipGridView, clipGridSwitch; // made when Clip-launch workflow is first chosen (round 181a)
    // round 190ef: the map modes' overlay and Mapping Browser (made when a map mode is first switched on), and the
    // computer keys held that fired a key mapping (their release fires it again)
    std::unique_ptr<mapmode::Overlay> mapOverlay;
    std::unique_ptr<mapmode::MappingBrowser> mappingBrowser;
    std::vector<int> heldMappedKeys;
    bool mapKeyPressed (const juce::KeyPress& key);
    void layoutMapMode();
    void syncClipLaunchSwitches();
    std::unique_ptr<SimpleWorkflow> simpleWorkflow; // made when Simple workflow is first chosen (round 184a)
    std::unique_ptr<DetailView> detailView; // made with the grid (round 183a); never in Pattern workflow
    void makeDetailView();
    void toggleHotSwap (const hotswap::Target& target); // round 190ce (core/HotSwap.h)
    // Slice 2: the Piano roll docked in the detail view's Clip page (its panel lets go of it meanwhile)
    bool pianoRollDocked = false;
    juce::Component* dockPianoRoll (int pattern);
    void pianoRollPanelWillShow (Panel panel);
    void undockPianoRoll();
    bool pianoRollKeysHere() const; // the docked Piano roll was clicked last: the keys are its own
    struct DockClickWatcher final : juce::MouseListener
    {
        std::function<void (const juce::MouseEvent&)> down;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (down != nullptr)
                down (e);
        }
    };
    DockClickWatcher dockClickWatcher; // a global listener only while the Piano roll is docked
    // round 190fc (H2): after a click in the Simple window the main window keeps the keys (MainComponent.cpp)
    struct SimpleClickKeys final : juce::MouseListener
    {
        std::function<void (juce::Component*)> up;
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (up != nullptr)
                up (e.eventComponent);
        }
    };
    SimpleClickKeys simpleClickKeys; // (its `up` is set in the constructor)
    void simpleClickEnded (juce::Component* clicked);
    bool pianoRollDockActive = false;
    bool runDetailAction (const std::string& id);
    void openGridClip (int track, int scene);
    MixerComponent mixer;
    BrowserComponent browser;
    Workspace workspace; // after the panels' contents: it is destroyed first
    updater::UpdateBar updateBar; // "LYNX SHARE x.y.z is available" under the toolbar (round 166b)
    std::unique_ptr<juce::Component> danceStage; // the dance channels' dancers over the workspace (ui/DanceView.h, round 165e)

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    juce::File importedFlpFile; // the .flp the current project was imported from (until another project is opened)
    juce::String importedSuffix; // " (imported from FL Studio)" in the title until saved as .lynx
    juce::String projectName; // suggested name for export (also from an imported .flp)
    HeldNotes heldKeys; // notes the typing piano is playing (tag: the key code)
    bool wasForeground = true; // (the Plugin database folder is read again when LYNX comes back to the front: issue #104)
    // round 190bf: the Plugin database folder's watcher (core/FolderWatcher.h); its thread only posts to this one
    std::unique_ptr<folderwatch::Watcher> pluginDatabaseWatcher;
    void readPluginDatabaseAgain();
    // Online Sessions P2 (round 169b): the live session (last: it ends, and tells the others, before anything goes)
    std::unique_ptr<LiveSessionManager> live;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace lynx
