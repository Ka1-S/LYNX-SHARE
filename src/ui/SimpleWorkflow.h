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
        // round 190ch: the Lyrics / Notes panel (the main window's, the same in every layout), Reference match, and a
        // new project from the "Start from" list
        std::function<void()> toggleLyrics;
        std::function<void()> showReferenceMatch;
        std::function<void (Song song)> startFrom;
        std::function<void (int track)> trackAdded;        // a track was added (select it, scroll to it)
        // round 190t
        std::function<juce::File()> sampleFolder;          // where the Sampler tile's file dialog starts (the Browser's samples)
        std::function<void (int format)> exportAudio;      // the Export window on one format (0 WAV, 1 MP3): the Finish dock
        std::function<void()> exportOtherApps;             // File › Export › For other apps
        std::function<juce::String()> saveStatus;          // "Backed up 2 min ago", "Not saved yet"...: beside the title tools
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
    // The song changed (MainComponent's change callback, only while the style is on): a new or emptied project shows the
    // chooser again, unless it was closed for this project.
    void songChanged();

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

    // The dock for the selected track (slices S5, S6, S13, S14; ui/SimpleDock.h): its sound and its effects, under the
    // timeline. Made the first time it shows.
    void trackSelected (int track);      // a click on a track header
    void showEffectsOfInsert (int insert); // "+FX" on a header: the dock, on its Effects tab, for the track of `insert`
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

private:
    class Chooser;
    void closeByHand(); // the chooser was closed (✕, Esc, a click outside): it stays closed for this project
    void addDefaultInstrument();                   // the Instrument tile: LYNX's synth on a new track, ready to play
    void chooseInstrument (juce::Component& tile);
    void chooseSample();
    void importFiles();
    void addFiles (const juce::Array<juce::File>& files, std::int64_t start);
    void addDrums (const std::string& genre, bool steps, bool useTempo);
    void addAudio (simpletracks::Kind kind);
    void updateDock(); // shows or hides the dock as the setting says, on the Playlist's current track
    void showKeysForNewTrack(); // a new instrument: the dock's Keyboard tab (or the Touch keyboard window, dock hidden)
    void offerProjectKey (int root, bool minor); // a keyed loop came into a project without a key

    AudioEngine& engine;
    Host host;
    bool active = false;
    std::string dismissedFor; // the project (Song::projectId) whose chooser was closed by hand
    std::unique_ptr<juce::Component> startFrom, lyricsButton; // round 190ch
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
};
} // namespace lynx
