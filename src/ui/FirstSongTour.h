#pragma once

// The Simple workflow's Welcome screen and first-song tour (round 190dv; core/FirstSongTour.h has the steps and the
// state machine). LYNX's own design: dark card, red accent, LYNX's words.
//
//  - Welcome screen: over the Playlist (the New track chooser's place) the first time the Simple workflow opens on an
//    empty project, and from Help › Welcome screen: "Make my first song" (the tour), "Open a demo song" (the Start from
//    list) and "Start empty" (the chooser).
//  - The tour: a small card in a corner of the window (Step n of 7, a title, one sentence, Skip tour · Back · Do it for
//    me · Next) and a ring around the control to use, with the rest of the window dimmed. Nothing of it catches the
//    mouse or the keys except the card's own buttons: the app is never blocked. The ring finds its control by component
//    ID (core/FirstSongTour.h targets()), never by screen position. A step advances when the song shows it done.
//  - The progress is kept in the settings folder (simple-tour.txt): closing the card pauses the tour, Help › First-song
//    tour continues it (also after a restart), Skip tour or the end card ends it.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/FirstSongTour.h"

namespace lynx
{
class AudioEngine;

class FirstSongTour
{
public:
    struct Host
    {
        std::function<juce::Component*()> window;          // the main window's content: targets and the coach marks
        std::function<void (juce::Component*)> setOverlay; // the Welcome screen over the Playlist (nullptr: none)
        std::function<void (int step)> doStep;             // "Do it for me" (core/FirstSongTour.h canDoForUser)
        std::function<void()> showLoops;                   // the Loop step's button
        std::function<void()> openDemos;                   // the Start from list (the demo songs first)
        std::function<void()> startEmpty;                  // the New track chooser
        std::function<void()> tourStarting;                // the tour starts: the chooser comes up for the first step
        std::function<void()> saveSong;                    // the end card's "Save as a file..."
        std::function<bool()> songIsEmpty;                 // a new project (the Welcome screen opens by itself only on one)
        std::function<void (std::function<void()> then)> newProjectFirst; // Help on a project with work: start a new one?
    };

    // `persistent`: the progress is read from and written to the settings folder (false for --snapshot routes and tests)
    FirstSongTour (AudioEngine& engine, Host host, bool persistent);
    ~FirstSongTour();
    FirstSongTour (const FirstSongTour&) = delete;
    FirstSongTour& operator= (const FirstSongTour&) = delete;

    static juce::File stateFile();

    // --- the Welcome screen
    void showWelcome();
    void hideWelcome();
    bool isWelcomeShowing() const noexcept;
    // the first time the Simple workflow opens on an empty project (once per user)
    bool shouldOfferWelcome() const;

    // --- the tour
    void start();              // from the first step the song hasn't done
    void startOrContinue();    // Help › First-song tour: a paused tour continues, else a new one (a new project first?)
    void pause();              // the card closed (or the Simple workflow was left)
    void skip();               // ended for good
    bool isRunning() const noexcept;
    bool canContinue() const noexcept;
    int step() const noexcept;
    void songChanged();        // the model changed: a step done moves the tour on

    // --- tests and --tour190dvshot
    tour::Machine& machineForCheck() noexcept { return machine; }
    juce::Component* welcomeForSnapshot();
    juce::Component* cardForSnapshot();
    juce::Rectangle<int> ringForSnapshot();    // where the ring is drawn now (window coordinates; empty: no target)
    juce::String targetIdForSnapshot() const;  // the component ID it rings
    void clickForSnapshot (const juce::String& button); // "next", "back", "do", "skip", "close", "save", "demo"

    class Card;
    class Layer;
    class Welcome;

private:
    void showCard();
    void hideCard();
    void update();             // the card's text and the ring's target
    bool cardShowing() const noexcept; // in its window (as a child component or a native child window, round 190fc)
    void save();
    void clicked (const juce::String& what);

    AudioEngine& engine;
    Host host;
    bool persistent;
    tour::Machine machine;
    std::unique_ptr<Card> card;
    std::unique_ptr<Layer> layer;
    std::unique_ptr<Welcome> welcome;
    juce::Component::SafePointer<juce::Component> shownIn; // the window the card and the ring are in (while shown)
};
} // namespace lynx
