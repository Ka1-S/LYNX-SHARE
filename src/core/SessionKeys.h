#pragma once

// Round 190cm (the 0.1.27 rollup, coordinator's decision): the Ableton-style keys must not hide FL-layout globals.
// Three of the reference DAW's keys clash with a global shortcut of LYNX's key map (core/KeyMap.cpp):
//   Ctrl+Shift+M  new empty MIDI clip in the clip grid (round 190bv)      × file.exportMidi
//   Ctrl+I        insert warp markers in the audio editor (round 190cg)   × record.waitForInput
//   Ctrl+U        quantize audio in the audio editor (round 190cg)        (free in the key map today)
// They act as default keys only in the Clip-launch (Ableton-style) layout: there the panel's key wins, as in the
// reference. In FL's Pattern layout and in the Simple layout the commands stay in their menus without a key, the
// chord is not taken and falls through to the key map — "Recording waits for input" keeps Ctrl+I and "Export a MIDI
// file" keeps Ctrl+Shift+M. The same rule as the hot-swap key Q (core/HotSwap.h keyActsInLayout). No JUCE here.

#include "core/LayoutStyle.h"

namespace lynx::sessionkeys
{
enum class Command { gridNewMidiClip, audioInsertWarpMarkers, audioQuantize };

constexpr bool defaultKeyActs (Command, layoutstyle::Style style) noexcept { return style == layoutstyle::Style::clipLaunch; }
// … in the layout the main window shows now
inline bool defaultKeyActs (Command c) noexcept { return defaultKeyActs (c, layoutstyle::current()); }

// the key's text for a menu row ("\tCtrl+I"), empty where the key does not act
inline const char* menuKey (Command c, layoutstyle::Style style) noexcept
{
    if (! defaultKeyActs (c, style))
        return "";
    switch (c)
    {
        case Command::gridNewMidiClip: return "\tCtrl+Shift+M";
        case Command::audioInsertWarpMarkers: return "\tCtrl+I";
        case Command::audioQuantize: return "\tCtrl+U";
    }
    return "";
}
inline const char* menuKey (Command c) noexcept { return menuKey (c, layoutstyle::current()); }
} // namespace lynx::sessionkeys
