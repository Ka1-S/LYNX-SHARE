#pragma once

// Issue #218 (round 190ng): the PC keyboard stays with the main window and the panels. A drop-down (the toolbar's
// Pattern box, the snap box, the Playlist's arrangement box, a Mixer strip's input box...) or a button that gets the
// keyboard focus kept it: Enter opened the drop-down again, and clicking a tool or a clip never took it back, because
// those views don't take the focus themselves. FL's toolbar selectors never take the keyboard.
//
// Such controls are mouse-only here: when one of them gets the focus inside the main window, the main window takes it
// back; inside a panel window of its own (a detached Mixer, Playlist...) the focus is dropped, so the window's own key
// handler, which hands every key to the main window, gets the keys again. Dialogs, the Settings and other windows keep
// their usual keyboard navigation (their drop-downs and buttons are left alone).

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ui::keyfocus
{
// A drop-down (or a part of one, e.g. its text label) or a button: controls that have nothing to do with the keys here.
bool isMouseOnlyControl (const juce::Component* c);

// True when `focused` is such a control inside `root` or inside a panel window (property "lynxPanel").
bool shouldGiveBack (const juce::Component* focused, const juce::Component& root);

// Esc while such a control holds the keyboard: gives it back; true when it did (the Esc is used up).
bool releaseOnEscape (juce::Component& root);

// The main window's watch (one at a time): installed in its constructor, removed in its destructor.
void install (juce::Component& root);
void uninstall (juce::Component& root);

// --issues190ng: hands the focus back now (no message-loop turn), as the watch would.
void giveBackNow (juce::Component& root);

// Issue #247, reopened (round 190ri): a text field that loses the keyboard while the user types into it (a page
// rebuilt or switched under it, a list refreshed) must not let the next letters run shortcuts — his own "A" made an
// automation clip. When a text field lost the keyboard less than `graceMs` ago without a mouse press, and the focus did
// not go to another text field, a plain character key (no Ctrl / Alt / Cmd) goes back into that field: it takes the
// keyboard again and the character is typed at its caret. True when the key was used so.
bool typeIntoFieldJustLeft (const juce::KeyPress& key, int graceMs = 400);
// (tests) the field that lost the keyboard last and when (0: none)
void noteTextFieldLeftForTests (juce::TextEditor* field, double whenMs);
} // namespace lynx::ui::keyfocus
