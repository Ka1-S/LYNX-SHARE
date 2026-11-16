#pragma once

// Type-to-jump for LYNX's lists and menus (issue #182, round 190ib; the rule is core/TypeAhead.h). JUCE's PopupMenu and
// ListBox ask through `juce::lynxTypeAheadHook` (cmake/LynxPatchJuce.cmake, fix 37: menus, dropdowns and every ListBox
// whose model names its rows with getNameForRow); LYNX's own lists (the Browser, the Plugin manager's table) call
// typeAheadRow from their key handlers. One state for all of them: only one list has the keyboard at a time.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TypeAhead.h"

namespace juce
{
extern int (*lynxTypeAheadHook) (const void* owner, juce_wchar typed, int current, int count, const std::function<String (int)>& textOf);
}

namespace lynx::ui
{
inline typeahead::State& typeAheadState()
{
    static typeahead::State state;
    return state;
}

// The row a typed character selects in `owner` (its `count` rows, `current` selected; -1 none), -1 for none.
inline int typeAheadRow (const void* owner, juce::juce_wchar typed, int current, int count, const std::function<juce::String (int)>& textOf)
{
    return typeahead::find (typeAheadState(), owner, (char32_t) juce::CharacterFunctions::toLowerCase (typed), juce::Time::getMillisecondCounter(),
                            current, count, [&textOf] (int row) {
                                const auto text = textOf (row).toLowerCase();
                                return std::u32string ((const char32_t*) text.toUTF32().getAddress());
                            });
}

// The same for a key press: only a plain typed character (no Ctrl / Cmd / Alt) jumps.
inline int typeAheadRow (const void* owner, const juce::KeyPress& key, int current, int count, const std::function<juce::String (int)>& textOf)
{
    const auto mods = key.getModifiers();
    const auto typed = key.getTextCharacter();
    if (typed < 32 || mods.isCtrlDown() || mods.isCommandDown() || mods.isAltDown())
        return -1;
    return typeAheadRow (owner, typed, current, count, textOf);
}

// Installs the JUCE hook (once, at start-up).
inline void installTypeAhead()
{
    juce::lynxTypeAheadHook = [] (const void* owner, juce::juce_wchar typed, int current, int count, const std::function<juce::String (int)>& textOf) {
        return typeAheadRow (owner, typed, current, count, textOf);
    };
}
} // namespace lynx::ui
