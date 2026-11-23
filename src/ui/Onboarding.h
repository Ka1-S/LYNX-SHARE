#pragma once

// First-run onboarding, the window's parts (round 190np; core/Onboarding.h has the logic, ONBOARDING-DESIGN.md of round
// 190nj the design and mockups M1–M5). LYNX's own look: dark cards in the LYNX dialog frame with the red accent strip,
// LYNX-drawn icons, product names in plain text (no logos).
//
//  - The welcome card (M1): an overlay over the main window, as the Setup wizard's, with one question and five choices;
//    one click applies a choice, Skip / × / Esc keep the Pattern layout. Language, a sound test and the background plugin
//    scan's status on one line; "More setup..." opens the old Setup wizard.
//  - The "Next: 3 things to try" card (M2): small, never modal, in the top-right of the Playlist's area; the items tick
//    themselves (the window passes what the song and the view show).
//  - A contextual tip (M5): one card beside the control it is about, with its action, "Got it" and "No more tips".
//  - An empty-state hint (M3): a card painted over an empty Playlist / clip grid / Simple timeline; only its buttons
//    take clicks, everything else reaches the view under it.
// Nothing here runs a timer while hidden; the welcome card polls the scan status once a second while it shows.

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Onboarding.h"

namespace lynx::onboardui
{
// Whether the settings folder held LYNX's UI settings when the app started (Main.cpp sets it before the window exists)
bool& profileExistedAtStart();

// --- the welcome card ---------------------------------------------------------------------------------------------------
struct WelcomeHost
{
    std::function<void (onboarding::Choice)> choose;   // a choice; Choice::none = Skip / × / Esc
    std::function<void()> moreSetup;                    // "More setup..." (the old Setup wizard)
    std::function<void (const juce::String&)> language; // another language was picked (the caller offers a restart)
    std::function<void()> testSound;                    // the Sound row's Test button
    std::function<juce::String()> deviceName;           // "Speakers (system default)" or the open device's name
    std::function<juce::String()> scanStatus;           // "Plugins: scanning in the background" ... (polled at 1 Hz)
    // issue #233 on the welcome card (rollup 0.1.31 step 2: round 190ns's Updates choice of the old wizard, moved here): the
    // bottom row's "Check for updates automatically" (only where LYNX updates itself, not in the Store build). `updates`
    // gets the toggle's state right before `choose` / `moreSetup`: every answer (a choice, Skip, ×, Esc) applies it.
    bool updatesShown = false;
    bool updatesOn = true;
    std::function<void (bool)> updates;
};

// The card over a dimmed window (the caller adds it over its whole area); Esc = Skip
std::unique_ptr<juce::Component> makeWelcomeOverlay (WelcomeHost host);
// The card alone at its natural size (shots)
std::unique_ptr<juce::Component> makeWelcomeCard (WelcomeHost host);
inline constexpr int welcomeWidth = 780;
int welcomeIdealHeight (const juce::Component& card);
// shots: one tile shown hovered (-1: none)
void setWelcomeHoverForSnapshot (juce::Component& cardOrOverlay, int tile);

// The choice's LYNX-drawn icon: white strokes on a rounded square of the choice's colour (as the New-track tiles)
void paintChoiceIcon (juce::Graphics& g, juce::Rectangle<float> area, onboarding::Choice choice);
juce::Colour choiceColour (onboarding::Choice choice);

// --- "Next: 3 things to try" --------------------------------------------------------------------------------------------
class Next3Card final : public juce::Component
{
public:
    std::function<void (onboarding::Item)> act; // an item's button
    std::function<void()> close;                 // × or "Hide"
    std::function<void()> tutorials;             // the "all done" line's link

    Next3Card();
    ~Next3Card() override;
    // the choice's items and which are done (bits per Item); `allDone` shows "Nice, that's the basics"
    void show (onboarding::Choice choice, std::uint32_t ticked, bool allDone);
    onboarding::Choice getChoice() const noexcept { return choice; }
    static constexpr int width = 460;
    int idealHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;
    onboarding::Choice choice = onboarding::Choice::none;
    std::uint32_t done = 0;
    bool finished = false;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") }, hideButton, tutorialsButton;
};

// --- a contextual tip ---------------------------------------------------------------------------------------------------
class TipCard final : public juce::Component
{
public:
    std::function<void()> action, gotIt, noMore;
    explicit TipCard (onboarding::Tip tip);
    onboarding::Tip getTip() const noexcept { return tip; }
    static constexpr int width = 430;
    int idealHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    onboarding::Tip tip;
    juce::TextButton actionButton, gotItButton, noMoreButton;
};

// --- an empty-state hint ------------------------------------------------------------------------------------------------
class EmptyHintCard final : public juce::Component,
                            private juce::ComponentListener
{
public:
    struct Button
    {
        juce::String text;
        std::function<void()> run;
        juce::String id; // component ID (the demo runner and the shots find it by it)
    };
    // `where` gives the rectangle of the parent to centre in (default: the whole parent)
    EmptyHintCard (onboarding::EmptyHint kind, std::vector<Button> buttons, std::function<juce::Rectangle<int>()> where = {});
    ~EmptyHintCard() override;
    onboarding::EmptyHint getKind() const noexcept { return kind; }
    // shown over `parent` (or hidden: nullptr); placed again when the parent is resized
    void attachTo (juce::Component* parent);
    void place();
    static constexpr int width = 520;
    int idealHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override; // only the buttons take the mouse

private:
    void componentMovedOrResized (juce::Component&, bool, bool) override { place(); }
    onboarding::EmptyHint kind;
    std::function<juce::Rectangle<int>()> where;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Component::SafePointer<juce::Component> watched;
};

// --- hosting a card in front of the window's native panels (Windows docking: panels are native child windows) -----------
// `card` goes over `window` at `bounds` (window coordinates): an attached floater where panels are native windows, else a
// plain child on top. remove() takes it away again.
void hostOnTop (juce::Component& card, juce::Component& window, juce::Rectangle<int> bounds);
void removeHosted (juce::Component& card);
// `c`'s bounds in `window`'s coordinates (also inside a panel that is a native child window)
juce::Rectangle<int> areaIn (juce::Component& window, juce::Component& c);
// The first showing component with this ID (or, failing that, this name / button text) inside `window` and its native
// panels; nullptr when none (the demo runner and the tips' anchors)
juce::Component* findShowing (juce::Component& window, const juce::String& idOrName);
} // namespace lynx::onboardui
