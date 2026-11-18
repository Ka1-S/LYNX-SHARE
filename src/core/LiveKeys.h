#pragma once

// Round 190ih (PLAN R12, owner decision L1): Ableton Live's keyboard shortcuts in the Clip-launch layout. Live's
// manual (Keyboard Shortcuts; Live Concepts 3.6; Arrangement View 6.13) gives them; in this layout they take Live's
// meaning, as round 190cm did for Ctrl+I / Ctrl+R (core/SessionKeys.h). In the Pattern (FL) and Simple layouts
// nothing changes: the chords fall through to LYNX's key map there.
//
//   Tab           toggle Session ↔ Arrangement view       (LYNX: the clip grid ↔ the Playlist; from "Both", the grid)
//   O             metronome on / off                      (FL layout: Ctrl+M, which is MIDI Map Mode here)
//   F9            Arrangement Record (recording on / off)  (FL layout: F9 = Mixer)
//   Ctrl+Shift+F9 Session Record (the selected scene)
//   F10           Back to Arrangement                     (FL layout: F10 = MIDI settings)
//   M             Computer MIDI Keyboard on / off          (FL layout: Ctrl+T)
//   Ctrl+Alt+B    show / hide the Browser                 (FL layout: Alt+F8)
//   Ctrl+Shift+F  Follow on / off                         (the Playlist's follow)
//   Ctrl+J        Consolidate (LYNX's consolidate of the Playlist selection, which renders it to audio)
// Not here yet (they need the clip grid or the Playlist, edited by another round): 0 = deactivate the selected clip,
// Ctrl+E = split, Shift+? = Info View.

#include "core/KeyMap.h"
#include "core/LayoutStyle.h"

namespace lynx::livekeys
{
enum class Command
{
    none, toggleView, metronome, arrangementRecord, sessionRecord, backToArrangement, computerKeyboard, browser, follow, consolidate,
    continuePlay // round 190ld (DL-66): Live's Shift+Space "Continue Play" from where the song stopped
};

constexpr Command commandFor (keys::Chord chord, layoutstyle::Style style) noexcept
{
    using namespace keys;
    if (style != layoutstyle::Style::clipLaunch)
        return Command::none;
    const auto key = chord.key >= 'a' && chord.key <= 'z' ? chord.key - 'a' + 'A' : chord.key;
    switch (chord.mods)
    {
        case none:
            if (key == tab) return Command::toggleView;
            if (key == 'O') return Command::metronome;
            if (key == f9) return Command::arrangementRecord;
            if (key == f10) return Command::backToArrangement;
            if (key == 'M') return Command::computerKeyboard;
            return Command::none;
        case ctrl | shift:
            if (key == f9) return Command::sessionRecord;
            if (key == 'F') return Command::follow;
            return Command::none;
        case ctrl | alt:
            return key == 'B' ? Command::browser : Command::none;
        case shift:
            return key == space ? Command::continuePlay : Command::none;
        case ctrl:
            return key == 'J' ? Command::consolidate : Command::none;
        default:
            return Command::none;
    }
}

// the key's text for a menu row ("\tO"), empty where the key does not act
constexpr const char* menuKey (Command c, layoutstyle::Style style) noexcept
{
    if (style != layoutstyle::Style::clipLaunch)
        return "";
    switch (c)
    {
        case Command::toggleView: return "\tTab";
        case Command::metronome: return "\tO";
        case Command::arrangementRecord: return "\tF9";
        case Command::sessionRecord: return "\tCtrl+Shift+F9";
        case Command::backToArrangement: return "\tF10";
        case Command::computerKeyboard: return "\tM";
        case Command::browser: return "\tCtrl+Alt+B";
        case Command::follow: return "\tCtrl+Shift+F";
        case Command::consolidate: return "\tCtrl+J";
        case Command::continuePlay: return "\tShift+Space";
        case Command::none: break;
    }
    return "";
}
} // namespace lynx::livekeys
