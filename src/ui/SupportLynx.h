#pragma once

// Supporting LYNX SHARE (round 190fq, LYNX's own; FL has nothing like it): an optional, low-key way to donate.
// - Help › "Support LYNX SHARE..." and the About window's "Support LYNX SHARE" button open the Support window: why
//   donations help, "Donate on Ko-fi" (the page of core/Links.h in the browser) and "Close".
// - The start-up note (core/SupportNote.h): after the 10th start, once, a small card in the main window's bottom-right
//   corner, never over a dialog and never while playing or recording; Settings › General › "Show the support note".
// Donating is optional and never unlocks anything.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SupportNote.h"

namespace lynx::support
{
void openDonatePage(); // core/Links.h's page in the default browser

// The Support window: result 1 = "Donate on Ko-fi", 0 = "Close" (Escape).
std::unique_ptr<juce::AlertWindow> makeSupportWindow();
void showSupportWindow(); // modal, above the main window; "Donate on Ko-fi" opens the page

// The note's state, in support-note.txt in the settings folder.
supportnote::State loadState();
void saveState (const supportnote::State& state);
bool noteEnabled();               // Settings › General › "Show the support note"
void setNoteEnabled (bool on);

// The card: the short text and "Support", "Not now", "Don't show again".
class NoteCard final : public juce::Component
{
public:
    static constexpr int width = 360, height = 112;
    explicit NoteCard (std::function<void (supportnote::Choice)> onChoice);
    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::TextButton* buttonForSnapshot (supportnote::Choice c);

private:
    std::function<void (supportnote::Choice)> choice;
    juce::TextButton supportButton, laterButton, neverButton;
};

// The start-up note in the main window: counts the start, waits for a quiet moment (up to two minutes after the
// start; otherwise it tries again at the next start) and shows the card in the bottom-right corner, above `bottomInset`
// pixels (the hint bar). The card goes away by itself if playback, recording or a dialog starts while it shows.
class StartupNote final : private juce::Timer,
                          private juce::ComponentListener
{
public:
    StartupNote (juce::Component& parent, std::function<supportnote::Moment()> moment, std::function<int()> bottomInset);
    ~StartupNote() override;

    void begin (bool quietRun); // once, at a normal start
    bool isCardShowing() const noexcept { return card != nullptr; }
    NoteCard* cardForSnapshot() noexcept { return card.get(); }
    void pollForSnapshot() { timerCallback(); } // (the shot doesn't wait for the timer)
    void showCardForSnapshot();                 // the card alone, for a picture (the saved state is not touched)
    void closeCard();

private:
    void timerCallback() override;
    void componentMovedOrResized (juce::Component&, bool, bool) override { place(); }
    void place();
    void showCard();
    void chosen (supportnote::Choice c);

    juce::Component& parent;
    std::function<supportnote::Moment()> moment;
    std::function<int()> bottomInset;
    std::unique_ptr<NoteCard> card;
    bool waiting = false;
    double waitStarted = 0.0;
};
} // namespace lynx::support
