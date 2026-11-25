#pragma once

// Round 190qx (the owner's "make the options easy"): LYNX's notices — one consistent, non-blocking way to say that something
// happened ("Saved a copy for LYNX SHARE 0.1.31", "Couldn't open Serum: it isn't installed or hasn't been scanned"). A
// notice is a small card at the main window's lower right (never over the middle of the work, never modal, nothing to
// click away) and the same words in the hint bar; it goes by itself after a few seconds (a problem stays longer, the mouse
// over it keeps it), can carry one button (Show in folder, Plugin manager...), and × closes it at once. At most three show,
// newest at the bottom; one with the same `id` as a shown one replaces it. Nothing runs while none shows.
//
// Wording (OPTIONS.md, Notices): sentence case, what happened first, then what to do; no "Error" titles, no exclamation
// marks; names in the user's words (the plugin's name, the file's name).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui
{
struct Notice
{
    enum class Kind { info, done, problem };
    Kind kind = Kind::info;
    juce::String text;       // one line: what happened
    juce::String detail;     // a second line: a file name, what to do (optional)
    juce::String actionText; // the button's caption (optional)
    std::function<void()> action;
    int holdMs = 0;          // 0: 6 s, a problem 10 s
    juce::String id;         // the same id replaces the notice shown (e.g. "plugin.missing")
};

// Shows the notice in the window set by setNoticeHost (the main window), and its text in the hint bar. Message thread.
void notify (Notice notice);
// The window notices show in (the main window sets itself, and nullptr when it goes); several windows: the one in front.
void addNoticeHost (juce::Component* window);
void removeNoticeHost (juce::Component* window);
// The usual notices, worded once (OPTIONS.md, Notices)
// A plugin window can't open: the plugin isn't installed here or wasn't scanned (button: Plugin manager...)
void pluginNotLoaded (const juce::String& pluginName);
// A file was written ("Saved a copy for LYNX SHARE 0.1.31", the file; button: Show in folder)
void savedFile (const juce::String& what, const juce::File& file);
// The main window opens its Plugin manager with this (set by it)
inline std::function<void()> openPluginManager;

// (tests / shots) the cards shown, and closing them all
int shownNoticeCount();
juce::Component* shownNotice (int index);
void clearNotices();
// (shots) a card alone, as notify() would show it
std::unique_ptr<juce::Component> makeNoticeCard (const Notice& notice);
} // namespace lynx::ui
