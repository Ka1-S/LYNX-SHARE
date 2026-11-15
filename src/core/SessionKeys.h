#pragma once

// Round 190cm (the 0.1.27 rollup, coordinator's decision): the Ableton-style keys must not hide FL-layout globals.
// Three of the reference DAW's keys clash with a global shortcut of LYNX's key map (core/KeyMap.cpp):
//   Ctrl+Shift+M  new empty MIDI clip in the clip grid (round 190bv)      × file.exportMidi
//   Ctrl+I        insert warp markers in the audio editor (round 190cg)   × record.waitForInput
//   Ctrl+U        quantize audio in the audio editor (round 190cg)        (free in the key map today)
// Round 190ef adds the reference's keys for the scene, draw and map commands (its menu file names them, see
// docs/analise/REA_rodada190ef.md):
//   Ctrl+I        Insert Scene, with the clip grid focused                × record.waitForInput
//   Ctrl+Shift+I  Capture and Insert Scene, with the clip grid focused    (free in the key map today)
//   Ctrl+R        Rename the selected scene / track, clip grid focused    × file.export
//   B             Draw Mode on / off                                      × playlist.tool.brush, pianoRoll.tool.brush
//   Ctrl+M        MIDI Map Mode on / off                                  × transport.metronome
//   Ctrl+K        Key Map Mode on / off                                   × rack.graphEditor (Channel Rack focus)
// Round 190ff adds the reference's Freeze Track key (its clip menu shows it [SHOT live190ei/shots/live-audioclip-context-menu.png]):
//   Ctrl+Shift+Alt+F  Freeze / Unfreeze the selected track, clip grid focused  (free in the key map today)
// They act as default keys only in the Clip-launch (Ableton-style) layout: there the panel's key wins, as in the
// reference. In FL's Pattern layout and in the Simple layout the commands stay in their menus without a key, the
// chord is not taken and falls through to the key map — "Recording waits for input" keeps Ctrl+I and "Export a MIDI
// file" keeps Ctrl+Shift+M. The same rule as the hot-swap key Q (core/HotSwap.h keyActsInLayout). No JUCE here.

#include "core/LayoutStyle.h"

namespace lynx::sessionkeys
{
enum class Command
{
    gridNewMidiClip, audioInsertWarpMarkers, audioQuantize,
    gridInsertScene, gridCaptureInsertScene, gridRename, // round 190ef
    drawMode, midiMapMode, keyMapMode,                   // round 190ef
    freezeTrack                                          // round 190ff: Freeze / Unfreeze Track, the clip grid focused
};

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
        case Command::gridInsertScene: return "\tCtrl+I";
        case Command::gridCaptureInsertScene: return "\tCtrl+Shift+I";
        case Command::gridRename: return "\tCtrl+R";
        case Command::drawMode: return "\tB";
        case Command::midiMapMode: return "\tCtrl+M";
        case Command::keyMapMode: return "\tCtrl+K";
        case Command::freezeTrack: return "\tCtrl+Shift+Alt+F";
    }
    return "";
}
inline const char* menuKey (Command c) noexcept { return menuKey (c, layoutstyle::current()); }
} // namespace lynx::sessionkeys
