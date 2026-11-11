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
        std::function<void (int track)> trackAdded;        // a track was added (select it, scroll to it)
        // round 190t
        std::function<juce::File()> sampleFolder;          // where the Sampler tile's file dialog starts (the Browser's samples)
        std::function<void (int format)> exportAudio;      // the Export window on one format (0 WAV, 1 MP3): the Finish dock
        std::function<void()> exportOtherApps;             // File › Export › For other apps
        std::function<juce::String()> saveStatus;          // "Backed up 2 min ago", "Not saved yet"...: beside the title tools
        std::function<void (juce::Component*, int)> setSideDock; // the dock on the right of the Playlist (nullptr: none)
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

    // Adds a track built from `request` (one undo step): arms an audio track's insert, selects an instrument's channel,
    // hides the chooser. False (and a message) when it can't be added.
    bool add (simpletracks::Request request);
    // Files dropped on the timeline below the tracks (slice S4): one new track each (MIDI: one per MIDI track), from `tick`.
    void dropFiles (const juce::StringArray& files, std::int64_t tick);

    // The Finish dock (slice S8, ui/FinishDock.h): the Master looks, on the right of the Playlist
    void showFinish();
    void hideFinish();
    bool isFinishShowing() const noexcept;

    // --simpleshot: the chooser's two pages, as drawn
    juce::Component* chooserForSnapshot();
    void showStarterPageForSnapshot();
    // round 190t: a click on the starter page's button `index` (0 = Empty grid), and the status line showing `text`
    void clickStarterForSnapshot (int index);
    void setStatusForSnapshot (const juce::String& text);

    // how many were ever made in this process (the "nothing exists in Pattern workflow" check)
    static inline std::atomic<int> instances { 0 };

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

    AudioEngine& engine;
    Host host;
    bool active = false;
    std::string dismissedFor; // the project (Song::projectId) whose chooser was closed by hand
    std::unique_ptr<Chooser> chooser;
    std::unique_ptr<juce::Component> addButton, finishButton, titleTools;
    std::unique_ptr<juce::Component> finishDock;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File sampleFolder; // where the last sample for a Sampler track came from (this session)
};
} // namespace lynx
