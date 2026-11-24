#pragma once

// The Simple workflow (View › Layout style, round 184a; design: /mnt/e/lynx-work/simple184a/DESIGN.md): one window with
// the tracks and the timeline. This object owns the style's own parts: slice S3's New track chooser (the home screen of
// an empty project and the "Add track" button in the Playlist's title bar) and, in later slices, the dock for the
// selected track and the Finish dock. MainComponent makes it the first time the style is chosen and keeps it for the
// session, so in Pattern and Clip-launch workflow nothing of it exists (the selftest checks `instances` stays 0).

#include <atomic>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SimpleTracks.h"

namespace lynx
{
class AudioEngine;
struct SampleData;
class FirstSongTour;
class SimpleDock;

class SimpleWorkflow
{
public:
    // What the workflow needs from the main window
    struct Host
    {
        std::function<void (juce::Component*)> setOverlay; // shows a component over the Playlist area (nullptr: none)
        std::function<void()> showKeys;                    // an instrument was added from the chooser: the on-screen keyboard
        std::function<void (juce::Component*, int)> setTitleAccessory; // the Playlist title bar's accessory (nullptr: none)
        std::function<void()> showLoops;                   // the sample browser, to drag loops onto the timeline
        std::function<void()> toggleLoops;                 // round 190lx (DS-42): the Loops dock shown / hidden
        std::function<void()> toggleSections;              // round 190ol (P1): the Sections lane (adds the first section)
        // round 190ch: the Lyrics / Notes panel (the main window's, the same in every layout), Reference match, and a
        // new project from the "Start from" list
        std::function<void()> toggleLyrics;
        // round 190jr (BUILD-LIST 13): the Playlist's automation view for the Simple tracks (BandLab's automation switch)
        std::function<void (bool)> setAutomation;
        // round 190jr (B15): a BandLab project (.blx) picked or dropped in the Simple layout opens as the project
        std::function<void (const juce::File&)> openProject;
        std::function<void()> showReferenceMatch;
        std::function<void (Song song)> startFrom;
        std::function<void (int track)> trackAdded;        // a track was added (select it, scroll to it)
        // round 190t
        std::function<juce::File()> sampleFolder;          // where the Sampler tile's file dialog starts (the Browser's samples)
        std::function<void (int format)> exportAudio;      // the Export window on one format (0 WAV, 1 MP3): the Finish dock
        std::function<void()> exportOtherApps;             // File › Export › For other apps
        std::function<juce::String()> saveStatus;          // "Backed up 2 min ago", "Not saved yet"... (round 190iv: the top bar shows it)
        std::function<void()> saveStatusChanged;           // round 190iv: the song changed; the top bar's status may have
        std::function<void (const juce::String&)> statusForSnapshot; // round 190iv: setStatusForSnapshot, on the top bar
        std::function<void (juce::Component*, int)> setSideDock; // the dock on the right of the Playlist (nullptr: none)
        // round 190bu: the dock for the selected track, under the timeline
        std::function<void (juce::Component*, std::function<int (int)>)> setBottomDock; // (nullptr: none; its height out of the panel's)
        std::function<void (int channel)> toggleChannelWindow; // the instrument's own window
        std::function<int()> selectedTrack;                // the Playlist's current track (-1: none)
        std::function<void()> settingsChanged;             // the dock's height or shown state changed: save the UI settings
        // round 190ds: the dock's Editor tab docks the main window's Piano roll (ui/SimpleDock.h Hooks)
        std::function<juce::Component* (int pattern)> dockPianoRoll;
        std::function<void()> undockPianoRoll;
        std::function<void (bool)> setFoldToKey;
        std::function<bool()> foldedToKey;
        std::function<void()> openPianoRollPanel;
        // round 190dv: the Welcome screen and the first-song tour (ui/FirstSongTour.h)
        std::function<juce::Component*()> window;          // the main window's content (the tour's ring and card)
        std::function<void (std::function<void()> then)> newProjectForTour; // Help › tour on a project with work: a new one?
        bool offerWelcome = false;                         // the Welcome screen opens by itself the first time (not in --snapshot routes)
    };

    SimpleWorkflow (AudioEngine& engine, Host host);
    ~SimpleWorkflow();
    SimpleWorkflow (const SimpleWorkflow&) = delete;
    SimpleWorkflow& operator= (const SimpleWorkflow&) = delete;

    // The style was chosen / left: the title bar's Add track button comes and goes, and an empty project opens on the
    // chooser.
    void entered();
    void left();
    // round 190jr: the automation view was switched (here or by the Playlist): the title bar's switch follows
    void automationShown (bool on);
    // round 190ih: the main window's 30 Hz timer, while this workflow shows (the dock's input level)
    void tick();
    // The song changed (MainComponent's change callback, only while the style is on): a new or emptied project shows the
    // chooser again, unless it was closed for this project.
    void songChanged();
    // round 190jc: View › Large controls changed (the dock grows or shrinks; the tracks are the Playlist's)
    void lookChanged();

    void showChooser();
    void hideChooser();
    bool isChooserShowing() const noexcept;
    // "Start from" (round 190ch, ui/StartFromPanel.h): LYNX's demo song and the templates, with a preview, over the
    // Playlist like the chooser
    void showStartFrom();
    void hideStartFrom();
    bool isStartFromShowing() const noexcept;
    juce::Component* startFromForSnapshot(); // (made and shown if needed)

    // Adds a track built from `request` (one undo step): arms an audio track's insert, selects an instrument's channel,
    // hides the chooser. False (and a message) when it can't be added.
    bool add (simpletracks::Request request);
    // Files dropped on the timeline below the tracks (slice S4): one new track each (MIDI: one per MIDI track), from `tick`.
    void dropFiles (const juce::StringArray& files, std::int64_t tick);

    // The Finish dock (slice S8, ui/FinishDock.h): the Master looks, on the right of the Playlist
    void showFinish();
    void hideFinish();
    bool isFinishShowing() const noexcept;
    juce::Component* finishDockForSnapshot() const noexcept { return finishDock.get(); } // round 190ke
    // Round 190iv: the Finish button, placed by the main window at the right of the Simple top bar (design190ia SG-A)
    juce::Component* finishComponent() const noexcept { return finishButton.get(); }
    // Round 190ke (owner decision D5, GarageBand's Tuner button in its control bar): the tuner's button, placed by the main
    // window after the tempo capsule of the Simple top bar; a click opens the tuner (ui/SimpleTuner.h) under it for the
    // selected track's input, a second click (or a click elsewhere) closes it
    juce::Component* tunerComponent() const noexcept { return tunerButton.get(); }
    void toggleTuner();
    bool isTunerShowing() const noexcept { return tunerBox != nullptr; }

    // The dock for the selected track (slices S5, S6, S13, S14; ui/SimpleDock.h): its sound and its effects, under the
    // timeline. Made the first time it shows.
    void trackSelected (int track);      // a click on a track header
    void showEffectsOfInsert (int insert); // "+FX" on a header: the dock, on its Effects tab, for the track of `insert`
    // round 190qp: a double-clicked clip of an instrument track: the dock, on its Editor tab, for that track (the clip's
    // pattern is the current one, so the tab shows it). False when the dock can't show it (not a Simple instrument /
    // sampler track).
    bool openNotesOf (int track);
    void toggleDock();                   // Ctrl+Alt+L
    void cycleDockTab();                 // Shift+Tab
    bool isDockShowing() const noexcept;
    SimpleDock* dockForSnapshot();       // (made and shown if needed)
    // round 190ds: the Piano roll docked in the Editor tab was taken back by its own panel / must be given back
    void editorTakenBack();
    void releaseEditor();
    // round 190ds: what a dropped keyed loop asks when the project has no key ("Set the project key to D minor?");
    // `answer` (tests, --snapshot) answers it at once instead of a dialog: 1 yes, 0 no, -1 ask
    static inline int answerForSnapshot = -1;
    juce::String lastKeyQuestionForCheck() const { return lastKeyQuestion; }

    // round 190dv: the Welcome screen (Help › Welcome screen; by itself the first time on an empty project) and the
    // first-song tour (Help › First-song tour: continues a paused tour, else starts one)
    void showWelcome();
    void startOrContinueTour();
    bool tourCanContinue() const;
    bool tourRunning() const;
    int tourStep() const;
    FirstSongTour* tourForSnapshot() noexcept { return tour.get(); }
    void doTourStep (int step); // the tour's "Do it for me", with the code the controls run

    // round 190fo: the Sampler track's 16 pads (core/SimplePads.h). The chooser's Sampler tile adds one (`addPads`); while
    // the dock shows a Sampler track's Pads tab the typing keys V B N M / F G H J / R T Y U / 4 5 6 7 play its pads:
    // `padChannelForKey` gives the pad channel a key plays (−1: not a pad key now).
    bool addPads();
    int padChannelForKey (int keyCode);
    // round 190fo: files and folders as the Import tile and a drop bring them in (tests, --reabl190foshot)
    void addFilesForCheck (const juce::Array<juce::File>& files) { addFiles (files, 0); }

    // --simpleshot: the chooser's two pages, as drawn
    juce::Component* chooserForSnapshot();
    void showStarterPageForSnapshot();
    // round 190t: a click on the starter page's button `index` (0 = Empty grid), and the status line showing `text`
    void clickStarterForSnapshot (int index);
    void setStatusForSnapshot (const juce::String& text);

    // how many were ever made in this process (the "nothing exists in Pattern workflow" check)
    static inline std::atomic<int> instances { 0 };
    // how many exist now (round 190fc: none may outlive its MainComponent's destructor body)
    static inline std::atomic<int> alive { 0 };
    // round 190lr (--details190lrshot): a Drums track as the Drums tile's starter adds it
    void addDrumsForSnapshot (const std::string& genre, bool steps) { addDrums (genre, steps, false); }

private:
    class Chooser;
    void closeByHand(); // the chooser was closed (✕, Esc, a click outside): it stays closed for this project
    void addDefaultInstrument();                   // the Instrument tile: LYNX's synth on a new track, ready to play
    void chooseInstrument (juce::Component& tile);
    void chooseSample();
    void importFiles();
    void importFolder(); // round 190fo
    void addFiles (const juce::Array<juce::File>& files, std::int64_t start);
    void addDrums (const std::string& genre, bool steps, bool useTempo);
    void addAudio (simpletracks::Kind kind);
    void updateDock(); // shows or hides the dock as the setting says, on the Playlist's current track
    void showKeysForNewTrack(); // a new instrument: the dock's Keyboard tab (or the Touch keyboard window, dock hidden)
    void offerProjectKey (int root, bool minor); // a keyed loop came into a project without a key
    // round 190kx: a dropped loop the analysis cache didn't know: analysed on a worker, then fitted / keyed
    void applyLoopAnalysis (const juce::File& file, std::shared_ptr<const SampleData> data, bool fit, bool key);

    AudioEngine& engine;
    Host host;
    bool active = false;
    std::string dismissedFor; // the project (Song::projectId) whose chooser was closed by hand
    std::unique_ptr<juce::Component> startFrom, lyricsButton; // round 190ch
    std::unique_ptr<juce::Component> automationButton;        // round 190jr
    std::unique_ptr<juce::Component> loopsButton;             // round 190lx (DS-42)
    std::unique_ptr<juce::Component> sectionsButton;          // round 190ol (P1)
    std::unique_ptr<juce::Button> tunerButton;                // round 190ke
    juce::Component::SafePointer<juce::Component> tunerBox;   // the open tuner's call-out (null: closed)
    std::unique_ptr<Chooser> chooser;
    std::unique_ptr<juce::Component> addButton, finishButton, titleTools;
    std::unique_ptr<juce::Component> finishDock;
    std::unique_ptr<SimpleDock> dock;
    std::unique_ptr<FirstSongTour> tour; // round 190dv
    int dockTrack = -1;
    int lastAddedTrack = -1;
    juce::String lastKeyQuestion;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File sampleFolder; // where the last sample for a Sampler track came from (this session)
    std::shared_ptr<int> lifetime = std::make_shared<int> (0); // (round 190kx: late answers check it)
};
} // namespace lynx
