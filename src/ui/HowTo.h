#pragma once

// Round 190qx (the owner's "make the options easy"): "How to use this..." at the end of each main window's menu — a small
// guide card next to the window with four lines on how it is used (the gestures FL Studio, Live or BandLab users know,
// said plainly) and a button to the matching Learn tutorial on lynx-share.com (in the app's language). The card is a
// call-out: a click anywhere else or Esc closes it, nothing waits for it. No JUCE in the texts' table (core-style lists).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::howto
{
enum class Window : int { playlist = 0, channelRack, pianoRoll, mixer, browser, clipGrid, deviceView, simple, tracker, automation, settings };
inline constexpr int windowCount = 11;

const char* windowId (Window w) noexcept;     // "playlist" (the card's component ID is "howto.<id>")
const char* title (Window w) noexcept;        // English key: "How to use the Playlist"
int stepCount (Window w) noexcept;
const char* step (Window w, int index) noexcept; // English keys, plain sentences
const char* tutorialSlug (Window w) noexcept; // the Learn page: https://lynx-share.com/learn/<slug>/
juce::String tutorialUrl (Window w);          // with ?lang= for the app's language

// The menu item ("How to use this..."), at the end of a window's menu: a separator, then the item
void addMenuItem (juce::PopupMenu& menu, Window w, juce::Component* anchor);
// Shows the card beside `anchor` (nullptr: over the front main window's middle)
void show (Window w, juce::Component* anchor);
// Round 190qx: a window that tells the main window it shows for the first time (its one-time tip); the main window sets it
inline std::function<void (Window, juce::Component*)> onWindowShown;
inline void windowShown (Window w, juce::Component* c)
{
    if (onWindowShown)
        onWindowShown (w, c);
}

// (shots / tests) the card alone, and the card shown now (nullptr: none)
std::unique_ptr<juce::Component> makeCard (Window w);
juce::Component* shownCard();
} // namespace lynx::howto
