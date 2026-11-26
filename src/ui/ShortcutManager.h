#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/KeyMap.h"

namespace lynx
{
// The Shortcut manager (FL Studio's "Manage shortcuts", interpreted): every action of LYNX's key map with where it
// works and its keys; search by name or key, change a shortcut by pressing the new keys (a clash with another
// action of the same panel is shown first), clear, reset one or all. Saved at once in %APPDATA%\LYNX\keys.json
// (only the changed actions); `onChanged` reloads the key map in the main window.
// flFileKeys: the Pattern layout's FL keys (Ctrl+N = Save new version, round 190rs) are the defaults shown
void showShortcutManager (std::function<void()> onChanged, bool flFileKeys = false);

// The content, for --snapshot mode (`persistent` false: a sample change, nothing saved).
std::unique_ptr<juce::Component> makeShortcutManagerPanel (bool persistent, std::function<void()> onChanged = {});

// The key map file (%APPDATA%\LYNX\keys.json).
juce::File keyMapFile();

// JUCE key event -> LYNX key map chord.
keys::Chord chordFromKeyPress (const juce::KeyPress& key);
} // namespace lynx
