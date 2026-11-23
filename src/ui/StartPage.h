#pragma once

// Issue #252 (round 190oq; core/StartPage.h has the what and why): the start page, a card over the dimmed main window as
// the welcome card (ui/Onboarding.h), in LYNX's dialog frame (ui/DialogFrame.h) with FL's Welcome window's two panels
// (shot live190gu/screens-fl/fl-welcome-wizard.png): "Start a new project" — New project... and the templates ("Start
// from": the demo songs, LYNX's default, the built-in and the user's templates), each a title in the accent and a line —
// and "Recent projects" — "Other projects..." (Open...), then each recent project: its name, "<size>  <date time>
// (<ago>)" and its folder; missing files dimmed. LYNX adds a search box over the list (the caret in it: typing filters
// at once); Up / Down / Enter / a double click open one. Bottom: "Show the start page when LYNX starts" and the keys. Esc
// or × keep the project that is open behind it. Static paints, no timer.

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/StartPage.h"

namespace lynx::startpageui
{
struct Host
{
    std::vector<startpage::Recent> recent;       // File › Open recent's list, newest first
    std::vector<juce::String> templates;         // what "Start from" offers, in its order
    std::vector<juce::String> templateDetails;   // one line under each (kind), same size or empty
    std::function<void (const juce::String& path)> open;
    std::function<void()> newProject;            // the New project dialog
    std::function<void (int)> newFromTemplate;   // an index into `templates`
    std::function<void()> openDialog;            // File › Open...
    std::function<void()> close;                 // Esc / ×: the project behind stays
    bool showAtStart = false;
    std::function<void (bool)> setShowAtStart;
    std::int64_t nowMs = 0;                       // (shots: a fixed "now"; 0 = the clock)
};

inline constexpr int cardWidth = 900, cardHeight = 600;

// The card over a dimmed window (the caller adds it over its whole area); Esc = close
std::unique_ptr<juce::Component> makeOverlay (Host host);
// The card alone at its natural size (shots)
std::unique_ptr<juce::Component> makeCard (Host host);

// shots / tests
void typeForSnapshot (juce::Component& cardOrOverlay, const juce::String& text);
int shownRowsForSnapshot (juce::Component& cardOrOverlay);
void selectRowForSnapshot (juce::Component& cardOrOverlay, int row);
bool openSelectedForSnapshot (juce::Component& cardOrOverlay); // as Enter
juce::TextEditor* searchBoxForSnapshot (juce::Component& cardOrOverlay);
} // namespace lynx::startpageui
