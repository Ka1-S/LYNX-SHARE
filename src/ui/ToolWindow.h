#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// A small non-modal window that owns `content`, stays above the main window and deletes itself when
// closed (close button or Esc). Used by the note tool dialogs, the undo history and the quick-add palette.
// `key` identifies the window: opening another with the same key closes the previous one.
// `resizable` (round 190nw, the Tracker): the window can be resized, down to a third of the content's size.
juce::DocumentWindow* openToolWindow (std::unique_ptr<juce::Component> content, const juce::String& title, const juce::String& key = {},
                                      bool resizable = false);

// Issue #244 (round 190oq): `c` (a search box) takes the keyboard focus when its tool window opens, so typing goes
// straight into it. Marks the component; openToolWindow gives it the focus once the window shows (and once more right
// after, as the window's activation arrives on some systems only then).
void focusOnOpen (juce::Component& c);
// The component marked with focusOnOpen inside `root`, or nullptr (tests / shots).
juce::Component* focusOnOpenTarget (juce::Component& root);

// Closes the tool window that holds `content` (from inside it, e.g. an Accept button).
void closeToolWindowOf (juce::Component& content);

// Deletes every tool window now (the main window is closing).
void closeAllToolWindows();

// The open tool window with this key, or nullptr.
juce::DocumentWindow* findToolWindow (const juce::String& key);
} // namespace lynx
