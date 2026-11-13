#include "core/KeyMap.h"
#include "core/ModifierMap.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace lynx::keys
{
namespace
{
struct KeyName
{
    int key;
    const char* name;
};

const KeyName keyNames[] = {
    { space, "Space" },        { enter, "Enter" },     { escape, "Esc" },      { tab, "Tab" },           { backspace, "Backspace" },
    { del, "Delete" },         { insert, "Insert" },   { up, "Up" },           { down, "Down" },         { left, "Left" },
    { right, "Right" },        { pageUp, "PageUp" },   { pageDown, "PageDown" }, { home, "Home" },       { end, "End" },
    { numpadPlus, "Numpad +" }, { numpadMinus, "Numpad -" }, { numpadMultiply, "Numpad *" }, { numpadDivide, "Numpad /" },
    { numpad0, "Numpad 0" }, { numpad1, "Numpad 1" }, { numpad2, "Numpad 2" }, { numpad3, "Numpad 3" }, { numpad4, "Numpad 4" },
    { numpad5, "Numpad 5" }, { numpad6, "Numpad 6" }, { numpad7, "Numpad 7" }, { numpad8, "Numpad 8" }, { numpad9, "Numpad 9" },
};

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t\r\n");
    const auto b = s.find_last_not_of (" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
}

Chord chord (int key, std::uint8_t mods = none)
{
    return { key, mods };
}

std::vector<Action> makeActions()
{
    std::vector<Action> list {
        // transport and modes
        { "transport.playStop", TR_NOOP ("Play / stop (back to where playback started)"), Context::global, { chord (space) } },
        { "transport.pause", TR_NOOP ("Play / pause"), Context::global, { chord (space, ctrl) } },
        // FL 2026 (checked in FL, round 165s): Shift+Space stops and goes back to the start of the song / pattern, or
        // plays from there when stopped; the position clicked on a ruler is ignored (issue #11)
        { "transport.playFromStart", TR_NOOP ("Play, or stop at the start of the song or pattern"), Context::global, { chord (space, shift) } },
        { "transport.toggleMode", TR_NOOP ("Pattern / song mode"), Context::global, { chord ('L') }, true },
        { "transport.nextMarker", TR_NOOP ("Jump to the next song marker (Quantize song jumps while playing)"), Context::global, { chord (numpadMultiply, alt) } },
        { "transport.previousMarker", TR_NOOP ("Jump to the previous song marker"), Context::global, { chord (numpadDivide, alt) } },
        { "view.typingPiano", TR_NOOP ("Typing keyboard to piano on/off"), Context::global, { chord ('T', ctrl) } },
        // FL's Options › Switches › "Multilink to controllers" (OptionsToggleMultiLinkAction, Ctrl+J; round 177b-4)
        { "options.multilink", TR_NOOP ("Multilink to controllers on/off"), Context::global, { chord ('J', ctrl) } },
        { "view.toolbar", TR_NOOP ("Show or hide the toolbar panels"), Context::global, { chord (f11, ctrl) } },
        { "view.audioEditor", TR_NOOP ("Open the audio editor"), Context::global, {} },
        { "view.tempoTapper", TR_NOOP ("Tempo tapper (tap the tempo)"), Context::global, { chord (f5, ctrl) } },
        { "view.sampleBrowser", TR_NOOP ("Sample browser (separate window)"), Context::global, {} },
        // round 177d (an FL wishlist item): solo the selected channel from any window, no key by default (bind one in
        // the shortcut editor); again = every channel plays (FL's channel solo)
        { "channel.soloSelected", TR_NOOP ("Solo the selected channel (from any window)"), Context::global, {} },
        // round 190an (CROSS-DAW F13): FL's Add › Automation for last tweaked parameter as a key action. FL gives it
        // no key (DFM Add_AutoLastMenu has no ShortCut and ShortcutsModule has no action for it), so none by default
        { "automation.lastTweaked", TR_NOOP ("Create automation clip for the last tweaked control"), Context::global, {} },

        // workspace panels
        { "panel.playlist", TR_NOOP ("Show, focus or hide the Playlist"), Context::global, { chord (f5) } },
        { "panel.channelRack", TR_NOOP ("Show, focus or hide the Channel Rack"), Context::global, { chord (f6) } },
        { "panel.pianoRoll", TR_NOOP ("Show, focus or hide the Piano Roll"), Context::global, { chord (f7) } },
        { "panel.browser", TR_NOOP ("Show, focus or hide the Browser"), Context::global, { chord (f8, alt) } },
        // FL's F8 (ShowPluginPickerAction, round 167o): opens the plugin picker, or closes it when it is open
        { "panel.quickAdd", TR_NOOP ("Quick-add palette: instruments and effects (press again to close)"), Context::global, {} },
        // FL's F8 (ShowPluginPickerAction, ShortCut 119): the Plugin picker view, again = closed (round 169k; the
        // quick-add palette that F8 opened since round 167o stays on the toolbar's plug button)
        { "view.pluginPicker", TR_NOOP ("Plugin picker: every generator and effect, the categories as tabs (press again to close)"), Context::global, { chord (f8) } },
        { "panel.mixer", TR_NOOP ("Show, focus or hide the Mixer"), Context::global, { chord (f9) } },
        // FL 2026's other window keys (its shortcut list, REA round 165a)
        { "view.pluginDatabase", TR_NOOP ("Plugin database (in the Browser)"), Context::global, { chord (f8, shift) } },
        // F10 / F11 toggle like FL's (round 167o, settingsKeyPress): Settings on another page switches to this one
        { "view.midiSettings", TR_NOOP ("MIDI settings (press again to close)"), Context::global, { chord (f10) } },
        { "view.projectInfo", TR_NOOP ("Project info (press again to close)"), Context::global, { chord (f11) } },
        { "view.closeWindows", TR_NOOP ("Close all windows (plugins, channel settings, tools)"), Context::global, { chord (f12) } },
        // Round 183a: the Clip-launch workflow's detail view, with the reference DAW's keys (its View menu, Live 12.4
        // [SHOT] detail183a/live/d05). They do nothing in Pattern workflow, where the keys keep their old meaning.
        { "view.detailToggle", TR_NOOP ("Clip-launch workflow: show or hide the detail view"), Context::global, { chord ('L', ctrl | alt) } },
        { "view.detailClip", TR_NOOP ("Clip-launch workflow: clip view"), Context::global, { chord ('3', ctrl | alt) } },
        { "view.detailDevices", TR_NOOP ("Clip-launch workflow: device view"), Context::global, { chord ('4', ctrl | alt) } },
        { "view.detailSwitch", TR_NOOP ("Clip-launch workflow: switch between clip view and device view"), Context::global, { chord (tab, shift) } },
        // Round 190ce: the reference DAW's hot-swap key (its lesson "Hot-Swapping Presets": Q links the Browser to the
        // selected device and Q again ends it; docs/analise/REA_rodada190ce.md §2). The typing keyboard keeps Q while on.
        { "view.hotSwap", TR_NOOP ("Hot-swap: link the Browser to the selected device, instrument or sample (again: end)"), Context::global, { chord ('Q') } },
        { "view.closePluginWindows", TR_NOOP ("Close all plugin windows"), Context::global, { chord (f12, alt) } },
        // FL's CloseAllUnfocusedWindowsAction (Ctrl+F12) and ShowTouchControllerAction (Alt+F7), round 167o
        { "view.closeUnfocusedWindows", TR_NOOP ("Close all windows except the focused one"), Context::global, { chord (f12, ctrl) } },
        // FL's AlignAllChannelEditorsAction, ShortCut 8315 = Shift+F12 (round 169k)
        { "view.alignChannelEditors", TR_NOOP ("Align all channel editors"), Context::global, { chord (f12, shift) } },
        { "view.touchKeyboard", TR_NOOP ("Show or hide the Touch keyboard"), Context::global, { chord (f7, alt) } },
        { "panel.maximisePlaylist", TR_NOOP ("Maximize or restore the Playlist"), Context::global, { chord (enter) } },

        // patterns
        { "pattern.next", TR_NOOP ("Next pattern"), Context::global, { chord (numpadPlus) } },
        { "pattern.previous", TR_NOOP ("Previous pattern"), Context::global, { chord (numpadMinus) } },
        { "pattern.nextEmpty", TR_NOOP ("Next empty pattern (and name it)"), Context::global, { chord (f4) } },
        { "pattern.firstEmpty", TR_NOOP ("First empty pattern (and name it)"), Context::global, { chord (f4, shift) } },
        { "pattern.nextEmptyNoName", TR_NOOP ("Next empty pattern (no naming)"), Context::global, { chord (f4, ctrl) } },
        { "pattern.rename", TR_NOOP ("Rename and color the pattern"), Context::global, { chord (f2) } },
        { "pattern.insert", TR_NOOP ("Insert an empty pattern before the current one"), Context::global, { chord (insert, ctrl | shift) } },
        { "pattern.clone", TR_NOOP ("Clone the current pattern"), Context::global, { chord ('C', ctrl | shift), chord ('C', alt) } },
        { "pattern.delete", TR_NOOP ("Delete the current pattern"), Context::global, { chord (del, ctrl | shift) } },
        { "pattern.moveUp", TR_NOOP ("Move the current pattern up"), Context::global, { chord (up, ctrl | shift) } },
        { "pattern.moveDown", TR_NOOP ("Move the current pattern down"), Context::global, { chord (down, ctrl | shift) } },

        // recording
        { "transport.record", TR_NOOP ("Recording on/off"), Context::global, { chord ('R') } },
        { "transport.metronome", TR_NOOP ("Metronome on/off"), Context::global, { chord ('M', ctrl) } },
        { "record.countIn", TR_NOOP ("Count-in before recording on/off"), Context::global, { chord ('P', ctrl) } },
        { "record.waitForInput", TR_NOOP ("Recording waits for the first note on/off"), Context::global, { chord ('I', ctrl) } },
        // round 170e3: FL's Options › Blend recorded notes (Ctrl+B), Loop record and Step edit (Ctrl+E) [DFM ShortcutsModule]
        { "record.blend", TR_NOOP ("Blend recorded notes on/off (off: replace)"), Context::global, { chord ('B', ctrl) } },
        { "record.loop", TR_NOOP ("Loop record on/off"), Context::global, {} },
        { "record.stepEdit", TR_NOOP ("Step edit on/off"), Context::global, { chord ('E', ctrl) } },
        { "record.punch", TR_NOOP ("Punch in/out on the loop region on/off"), Context::global, {} },
        { "record.quickAudio", TR_NOOP ("Record audio now: arm the selected insert and record (press again to stop)"), Context::global, { chord ('R', shift) } },
        { "record.captureMidi", TR_NOOP ("Capture MIDI: the notes just played without recording into the pattern"), Context::global, { chord ('R', ctrl | shift | alt) } },

        // files and history
        { "file.new", TR_NOOP ("New project"), Context::global, { chord ('N', ctrl) } },
        { "file.open", TR_NOOP ("Open project"), Context::global, { chord ('O', ctrl) } },
        { "file.save", TR_NOOP ("Save project"), Context::global, { chord ('S', ctrl) } },
        { "file.saveAs", TR_NOOP ("Save project as"), Context::global, { chord ('S', ctrl | shift) } },
        { "file.saveNewVersion", TR_NOOP ("Save new version (Song_2, Song_3...)"), Context::global, { chord ('S', ctrl | alt) } },
        { "file.export", TR_NOOP ("Export audio"), Context::global, { chord ('R', ctrl) } },
        { "file.exportMp3", TR_NOOP ("Export an MP3 file"), Context::global, { chord ('R', ctrl | shift) } },
        { "file.exportMidi", TR_NOOP ("Export a MIDI file"), Context::global, { chord ('M', ctrl | shift) } },
        // FL's File › Export › Master... (FileExportMasterAction, Ctrl+Alt+M; REA round 168e)
        { "file.exportMaster", TR_NOOP ("Export and master the project"), Context::global, { chord ('M', ctrl | alt) } },
        // FL's Tools › Consolidate playlist selection (EERenderPLSelectionLengthAction Ctrl+Alt+C, EEPLRenderSongLengthAction
        // Ctrl+Shift+Alt+C; REA round 168e)
        { "tools.consolidateSelection", TR_NOOP ("Consolidate playlist selection (from selection start)"), Context::global, { chord ('C', ctrl | alt) } },
        { "tools.consolidateSelectionSongStart", TR_NOOP ("Consolidate playlist selection (from song start)"), Context::global, { chord ('C', ctrl | shift | alt) } },
        { "edit.undo", TR_NOOP ("Undo"), Context::global, { chord ('Z', ctrl) } },
        // FL's Edit › "Undo backwards" (EditUndoBackwardsAction, Ctrl+Alt+Z; round 167k): one more step back
        { "edit.undoBackwards", TR_NOOP ("Undo backwards"), Context::global, { chord ('Z', ctrl | alt) } },
        { "edit.redo", TR_NOOP ("Redo"), Context::global, { chord ('Y', ctrl), chord ('Z', ctrl | shift) } },
        // FL's Tools › Macros › Stop sound (MacroStopSoundAction, Ctrl+H; round 170h2): the Undo history moved to
        // Ctrl+Alt+H, free in FL and in LYNX
        { "edit.history", TR_NOOP ("Undo history"), Context::global, { chord ('H', ctrl | alt) } },
        { "tools.stopSound", TR_NOOP ("Stop sound (all notes off)"), Context::global, { chord ('H', ctrl) } },
        { "help.shortcuts", TR_NOOP ("Shortcut sheet (press again to close)"), Context::global, { chord (f1) } },
        { "browser.search", TR_NOOP ("Search the Browser"), Context::global, { chord ('F', ctrl) } },

        // Channel Rack (focused)
        { "rack.selectPrevious", TR_NOOP ("Select the channel above"), Context::channelRack, { chord (up) } },
        { "rack.selectNext", TR_NOOP ("Select the channel below"), Context::channelRack, { chord (down) } },
        { "rack.extendUp", TR_NOOP ("Extend the selection up"), Context::channelRack, { chord (up, shift) } },
        { "rack.extendDown", TR_NOOP ("Extend the selection down"), Context::channelRack, { chord (down, shift) } },
        { "rack.moveUp", TR_NOOP ("Move the selected channels up"), Context::channelRack, { chord (up, alt) } },
        { "rack.moveDown", TR_NOOP ("Move the selected channels down"), Context::channelRack, { chord (down, alt) } },
        { "rack.selectAll", TR_NOOP ("Select all channels"), Context::channelRack, { chord ('A', ctrl) } },
        { "rack.clone", TR_NOOP ("Clone the selected channels"), Context::channelRack, { chord ('C', alt) } },
        { "rack.deleteSelected", TR_NOOP ("Delete the selected channels"), Context::channelRack, { chord (del, alt) } },
        // FL's Edit › Cut / Copy / Paste with the rack focused (REA round 170a §1): the selected channels' steps and notes
        { "rack.cut", TR_NOOP ("Cut the selected channels' steps"), Context::channelRack, { chord ('X', ctrl) } },
        { "rack.copy", TR_NOOP ("Copy the selected channels' steps"), Context::channelRack, { chord ('C', ctrl) } },
        { "rack.paste", TR_NOOP ("Paste steps into the selected channels"), Context::channelRack, { chord ('V', ctrl) } },
        { "rack.sendToInserts", TR_NOOP ("Send the selected channels to free mixer inserts"), Context::channelRack, { chord ('L', ctrl) } },
        { "rack.graphEditor", TR_NOOP ("Step graph lanes on/off"), Context::channelRack, { chord ('K', ctrl) } },
        { "rack.pianoRollView", TR_NOOP ("Piano-roll view of all channels on/off"), Context::channelRack, {} },
        { "rack.follow", TR_NOOP ("Follow the playhead (page along with the playing step) on/off"), Context::channelRack, {} },
        { "rack.groupSelected", TR_NOOP ("Put the selected channels in a new filter group"), Context::channelRack, { chord ('G', alt) } },
        { "rack.rotateLeft", TR_NOOP ("Rotate the selected channels' steps left"), Context::channelRack, { chord (left, ctrl | shift) } },
        { "rack.rotateRight", TR_NOOP ("Rotate the selected channels' steps right"), Context::channelRack, { chord (right, ctrl | shift) } },

        // Piano Roll (focused): tool dialogs and zoom
        { "pianoRoll.arpeggiate", TR_NOOP ("Arpeggiate..."), Context::pianoRoll, { chord ('A', alt) } },
        { "pianoRoll.strum", TR_NOOP ("Strum..."), Context::pianoRoll, { chord ('S', alt) } },
        { "pianoRoll.flip", TR_NOOP ("Flip..."), Context::pianoRoll, { chord ('Y', alt) } },
        { "pianoRoll.randomise", TR_NOOP ("Randomise..."), Context::pianoRoll, { chord ('R', alt) } },
        { "pianoRoll.scaleLevels", TR_NOOP ("Scale levels..."), Context::pianoRoll, { chord ('X', alt) } },
        { "pianoRoll.limit", TR_NOOP ("Limit to a range..."), Context::pianoRoll, { chord ('K', alt) } },
        // the other tools with FL's keys (REA round 143: the piano roll Tools menu)
        { "pianoRoll.quantize", TR_NOOP ("Quantize..."), Context::pianoRoll, { chord ('Q', alt) } },
        { "pianoRoll.chop", TR_NOOP ("Chop..."), Context::pianoRoll, { chord ('U', alt) } },
        { "pianoRoll.flam", TR_NOOP ("Flam..."), Context::pianoRoll, { chord ('F', alt) } },
        { "pianoRoll.claw", TR_NOOP ("Claw machine..."), Context::pianoRoll, { chord ('W', alt) } },
        { "pianoRoll.lfo", TR_NOOP ("LFO..."), Context::pianoRoll, { chord ('O', alt) } },
        { "pianoRoll.articulate", TR_NOOP ("Articulate..."), Context::pianoRoll, { chord ('L', alt) } },
        { "pianoRoll.riffMachine", TR_NOOP ("Riff machine..."), Context::pianoRoll, { chord ('E', alt) } },
        { "pianoRoll.chordProgression", TR_NOOP ("Generate chord progression..."), Context::pianoRoll, { chord ('P', alt) } },
        // FL's "Run last script again" (Piano roll scripts, round 190as)
        { "pianoRoll.runLastScript", TR_NOOP ("Run last script again"), Context::pianoRoll, { chord ('Y', ctrl | alt) } },
        // note colours and the Select menu (REA round 153: FL's keys from its piano roll manual)
        { "pianoRoll.colourSelected", TR_NOOP ("Colour the selected notes with the note colour"), Context::pianoRoll, { chord ('C', alt) } },
        { "pianoRoll.selectByColour", TR_NOOP ("Select the notes of the same colour"), Context::pianoRoll, { chord ('C', shift) } },
        { "pianoRoll.invertSelection", TR_NOOP ("Invert the note selection"), Context::pianoRoll, { chord ('I', shift) } },
        { "pianoRoll.groupNotes", TR_NOOP ("Group the selected notes"), Context::pianoRoll, { chord ('G', shift) } },
        { "pianoRoll.ungroupNotes", TR_NOOP ("Ungroup the selected notes"), Context::pianoRoll, { chord ('G', alt) } },
        { "pianoRoll.zoomIn", TR_NOOP ("Zoom in"), Context::pianoRoll, { chord (pageUp) } },
        { "pianoRoll.zoomOut", TR_NOOP ("Zoom out"), Context::pianoRoll, { chord (pageDown) } },
        { "pianoRoll.zoom1", TR_NOOP ("Zoom: one bar"), Context::pianoRoll, { chord ('1', shift) }, true },
        { "pianoRoll.zoom2", TR_NOOP ("Zoom: four bars"), Context::pianoRoll, { chord ('2', shift) }, true },
        { "pianoRoll.zoom3", TR_NOOP ("Zoom: sixteen bars"), Context::pianoRoll, { chord ('3', shift) }, true },
        { "pianoRoll.zoomAll", TR_NOOP ("Zoom to show everything"), Context::pianoRoll, { chord ('4', shift) }, true },
        { "pianoRoll.zoomSelection", TR_NOOP ("Zoom to the selection"), Context::pianoRoll, { chord ('5', shift) }, true },
        { "pianoRoll.centrePlayhead", TR_NOOP ("Centre the view on the playhead"), Context::pianoRoll, { chord ('0', shift) }, true },
        // quick quantize (round 162, issue #9): FL's Ctrl+Q / Shift+Q, plus a 50% one to give a key of your own
        { "pianoRoll.quickQuantize", TR_NOOP ("Quick quantize (starts and lengths)"), Context::pianoRoll, { chord ('Q', ctrl) } },
        { "pianoRoll.quantizeStarts", TR_NOOP ("Quantize starts"), Context::pianoRoll, { chord ('Q', shift) }, true },
        { "pianoRoll.quantizeHalf", TR_NOOP ("Quantize starts 50% (halfway to the grid)"), Context::pianoRoll, {} },
        { "pianoRoll.follow", TR_NOOP ("Follow the playhead (auto scrolling) on/off"), Context::pianoRoll, {} },
        // FL's Piano roll menu (round 168g, REA_rodada168g.md: the ShortcutsModule / EEMenu keys): View, Select, Stamp,
        // Edit and File items
        { "pianoRoll.gridHighlights", TR_NOOP ("Note grid highlights on/off"), Context::pianoRoll, { chord ('B', alt) } },
        { "pianoRoll.backgroundWave", TR_NOOP ("Background waveform on/off"), Context::pianoRoll, { chord ('N', alt) } },
        { "pianoRoll.showChordName", TR_NOOP ("Show chord name for selection on/off"), Context::pianoRoll, {} },
        { "pianoRoll.songBarsRuler", TR_NOOP ("Piano roll ruler: song bars on/off"), Context::pianoRoll, {} },
        { "pianoRoll.selectRandomOne", TR_NOOP ("Select one at random"), Context::pianoRoll, { chord ('R', shift) }, true },
        { "pianoRoll.selectRandomMore", TR_NOOP ("Select more at random"), Context::pianoRoll, { chord ('M', shift) }, true },
        { "pianoRoll.selectOddSteps", TR_NOOP ("Select odd (swung) notes"), Context::pianoRoll, { chord ('O', shift) }, true },
        { "pianoRoll.stampNone", TR_NOOP ("Stamp: none (single notes)"), Context::pianoRoll, { chord ('N', shift) }, true },
        { "pianoRoll.insertSpace", TR_NOOP ("Insert space (the time selection)"), Context::pianoRoll, { chord (insert, ctrl) } },
        { "pianoRoll.sliceInsertSpace", TR_NOOP ("Slice & insert space"), Context::pianoRoll, { chord (insert, ctrl | alt) } },
        { "pianoRoll.deleteSpace", TR_NOOP ("Delete space (the time selection)"), Context::pianoRoll, { chord (del, ctrl) } },
        { "pianoRoll.trimSpace", TR_NOOP ("Trim to the time selection"), Context::pianoRoll, {} },
        { "pianoRoll.deleteAll", TR_NOOP ("Delete all the channel's notes"), Context::pianoRoll, {} },
        { "pianoRoll.insertControllerValue", TR_NOOP ("Insert current controller value"), Context::pianoRoll, { chord ('I', ctrl) } },
        { "pianoRoll.importMidi", TR_NOOP ("Import MIDI file..."), Context::pianoRoll, { chord ('M', ctrl) } },
        { "pianoRoll.exportMidi", TR_NOOP ("Export as MIDI file..."), Context::pianoRoll, { chord ('M', ctrl | shift) } },
        { "pianoRoll.pasteMidiClipboard", TR_NOOP ("Paste from MIDI clipboard..."), Context::pianoRoll, { chord ('V', ctrl | shift) } },
        { "pianoRoll.copyMidiClipboard", TR_NOOP ("Copy to MIDI clipboard"), Context::pianoRoll, {} },
        { "pianoRoll.openScore", TR_NOOP ("Open score..."), Context::pianoRoll, {} },
        { "pianoRoll.saveScore", TR_NOOP ("Save score as..."), Context::pianoRoll, {} },
        { "pianoRoll.browseScores", TR_NOOP ("Browse scores"), Context::pianoRoll, {} },
        // FL's Piano roll › Time markers: the same EEMarker* items as the Playlist's, on the pattern's markers (round 168g)
        { "pianoRoll.markerAdd", TR_NOOP ("Pattern time markers: Add one..."), Context::pianoRoll, { chord ('T', alt) } },
        { "pianoRoll.markerAuto", TR_NOOP ("Pattern time markers: Add auto"), Context::pianoRoll, { chord ('T', ctrl) } },
        { "pianoRoll.markerSignature", TR_NOOP ("Pattern time markers: Add time signature change..."), Context::pianoRoll, { chord ('T', shift | alt) } },
        // the tools and the other single letters (FL's keys; issue #12: they were fixed in the editor and won over a
        // global shortcut given the same key)
        { "pianoRoll.tool.pencil", TR_NOOP ("Tool: Pencil"), Context::pianoRoll, { chord ('P') } },
        { "pianoRoll.tool.brush", TR_NOOP ("Tool: Brush"), Context::pianoRoll, { chord ('B') } },
        { "pianoRoll.tool.drumPaint", TR_NOOP ("Tool: Drum brush"), Context::pianoRoll, { chord ('N') } },
        { "pianoRoll.tool.eraser", TR_NOOP ("Tool: Eraser"), Context::pianoRoll, { chord ('D') } },
        { "pianoRoll.tool.mute", TR_NOOP ("Tool: Mute"), Context::pianoRoll, { chord ('T') } },
        { "pianoRoll.tool.knife", TR_NOOP ("Tool: Knife"), Context::pianoRoll, { chord ('C') } },
        { "pianoRoll.tool.lasso", TR_NOOP ("Tool: Lasso (select)"), Context::pianoRoll, { chord ('E') } },
        { "pianoRoll.tool.zoom", TR_NOOP ("Tool: Zoom"), Context::pianoRoll, { chord ('Z') } },
        { "pianoRoll.tool.listen", TR_NOOP ("Tool: Listen"), Context::pianoRoll, { chord ('Y') } },
        { "pianoRoll.slide", TR_NOOP ("Slide notes on/off (new notes)"), Context::pianoRoll, { chord ('S') } },
        // round 190an (LYNX's own option; FL has none, so no key by default): the Piano roll's Snap to scale switch
        { "pianoRoll.scaleSnap", TR_NOOP ("Snap notes to the scale on/off"), Context::pianoRoll, {} },
        { "pianoRoll.previousChannel", TR_NOOP ("Previous channel"), Context::pianoRoll, { chord ('G') } },
        { "pianoRoll.nextChannel", TR_NOOP ("Next channel"), Context::pianoRoll, { chord ('K') } },
        { "pianoRoll.previousChannelWithNotes", TR_NOOP ("Previous channel with notes"), Context::pianoRoll, { chord ('H') } },
        { "pianoRoll.nextChannelWithNotes", TR_NOOP ("Next channel with notes"), Context::pianoRoll, { chord ('J') } },

        // Playlist (focused)
        { "playlist.zoomIn", TR_NOOP ("Zoom in"), Context::playlist, { chord (pageUp) } },
        { "playlist.zoomOut", TR_NOOP ("Zoom out"), Context::playlist, { chord (pageDown) } },
        { "playlist.zoom1", TR_NOOP ("Zoom: one bar"), Context::playlist, { chord ('1', shift) }, true },
        { "playlist.zoom2", TR_NOOP ("Zoom: four bars"), Context::playlist, { chord ('2', shift) }, true },
        { "playlist.zoom3", TR_NOOP ("Zoom: sixteen bars"), Context::playlist, { chord ('3', shift) }, true },
        { "playlist.zoomAll", TR_NOOP ("Zoom to show everything"), Context::playlist, { chord ('4', shift) }, true },
        { "playlist.zoomSelection", TR_NOOP ("Zoom to the selection"), Context::playlist, { chord ('5', shift) }, true },
        { "playlist.centrePlayhead", TR_NOOP ("Centre the view on the playhead"), Context::playlist, { chord ('0', shift) }, true },
        { "playlist.rippleDelete", TR_NOOP ("Delete the selected clips and close the gaps"), Context::playlist, { chord (del, shift) } },
        { "playlist.ripple", TR_NOOP ("Ripple editing on/off"), Context::playlist, { chord ('S', shift) } },
        // FL's Playlist › Edit › "Automatic crossfades (current project)" (round 166e)
        { "playlist.autoCrossfades", TR_NOOP ("Automatic crossfades (current project) on/off"), Context::playlist, {} },
        // FL's legacy Pattern Blocks (round 170f, issue #60)
        { "playlist.patternBlocks", TR_NOOP ("Pattern blocks (current project) on/off"), Context::playlist, {} },
        { "playlist.automationLanes", TR_NOOP ("Automation lanes on/off"), Context::playlist, {} },
        { "playlist.blocksToClips", TR_NOOP ("Turn pattern blocks into clips"), Context::playlist, {} },
        // FL's Select › time (round 167k: EESelAroundSelAction, EESelPrevAction / EESelNextAction, Playlist focused)
        { "playlist.selectTimeAround", TR_NOOP ("Select time around selection"), Context::playlist, { chord (enter, ctrl) } },
        // comping (round 177c, Live's select + Enter; no key yet: Enter is FL's "Maximize or restore the Playlist")
        { "playlist.swipeSelectionToComp", TR_NOOP ("Send the time selection of the take lane to its comp"), Context::playlist, {} },
        { "playlist.timePrevious", TR_NOOP ("Select previous time"), Context::playlist, { chord (left, ctrl) } },
        { "playlist.timeNext", TR_NOOP ("Select next time"), Context::playlist, { chord (right, ctrl) } },
        // FL 2026's Playlist › Time markers (round 167k: EEMarkerAdd2Menu 32852, EEMarkerAddAutoMenu 16468,
        // EEMarkerAddTSChangeMenu 41044, EEMarkerAddSongLoopMenu 8276); Ctrl+T stays the typing piano elsewhere
        { "playlist.markerAdd", TR_NOOP ("Time markers: Add one..."), Context::playlist, { chord ('T', alt) } },
        { "playlist.markerAuto", TR_NOOP ("Time markers: Add auto"), Context::playlist, { chord ('T', ctrl) } },
        { "playlist.markerSignature", TR_NOOP ("Time markers: Add time signature change..."), Context::playlist, { chord ('T', shift | alt) } },
        { "playlist.markerLoop", TR_NOOP ("Time markers: Place loop"), Context::playlist, { chord ('T', shift) } },
        // FL 2026's Playlist › Edit (round 167k: EEPitchSelectedAudioClipsDown/UpAction, EEReverseSelectedAudioClipsAction)
        { "playlist.pitchClipsDown", TR_NOOP ("Pitch selected audio clips one semitone down"), Context::playlist, { chord ('7') } },
        { "playlist.pitchClipsUp", TR_NOOP ("Pitch selected audio clips one semitone up"), Context::playlist, { chord ('8') } },
        { "playlist.reverseClips", TR_NOOP ("Reverse selected audio clips"), Context::playlist, { chord ('9') } },
        { "playlist.quantizeStarts", TR_NOOP ("Quantize the clips' starts"), Context::playlist, { chord ('Q', ctrl) } },
        { "playlist.quantizeHalf", TR_NOOP ("Quantize the clips' starts 50% (halfway to the grid)"), Context::playlist, {} },
        { "playlist.follow", TR_NOOP ("Follow the playhead (auto scrolling) on/off"), Context::playlist, {} },
        { "playlist.tool.pencil", TR_NOOP ("Tool: Pencil"), Context::playlist, { chord ('P') } },
        { "playlist.tool.brush", TR_NOOP ("Tool: Brush"), Context::playlist, { chord ('B') } },
        { "playlist.tool.eraser", TR_NOOP ("Tool: Eraser"), Context::playlist, { chord ('D') } },
        { "playlist.tool.mute", TR_NOOP ("Tool: Mute"), Context::playlist, { chord ('T') } },
        { "playlist.tool.knife", TR_NOOP ("Tool: Knife"), Context::playlist, { chord ('C') } },
        { "playlist.tool.lasso", TR_NOOP ("Tool: Lasso (select)"), Context::playlist, { chord ('E') } },
        { "playlist.tool.zoom", TR_NOOP ("Tool: Zoom"), Context::playlist, { chord ('Z') } },
        { "playlist.tool.listen", TR_NOOP ("Tool: Listen"), Context::playlist, { chord ('Y') } },
        { "playlist.tool.slip", TR_NOOP ("Tool: Slip"), Context::playlist, { chord ('S') } },
        // FL's Performance mode (round 165m; Ctrl+P on FL's Playlist menu [DFM])
        { "playlist.performanceMode", TR_NOOP ("Performance mode on/off (launch the clips before the Start marker)"), Context::playlist, { chord ('P', ctrl) } },
        { "playlist.zoomPerformance", TR_NOOP ("Zoom on the performance clips"), Context::playlist, { chord ('6', shift) }, true },
        { "playlist.perfClipProgress", TR_NOOP ("Show the performance clips' progress"), Context::playlist, {} },
        { "playlist.perfTrackProgress", TR_NOOP ("Show the performance tracks' progress"), Context::playlist, {} },
        // round 168f: FL's Playlist keys and menu items ([DFM ShortcutsModule] EEMergeAction Ctrl+G, EEClonePLTracksAction
        // Ctrl+Shift+B, EEMakeUniqueAction Ctrl+Shift+V; the STRETCH button's hint "Shift+M")
        { "playlist.mergePatternClips", TR_NOOP ("Merge pattern clips"), Context::playlist, { chord ('G', ctrl) } },
        // round 180c: [DFM ShortcutsModule] EEMergeSimAction Ctrl+Shift+G, EEMergeAutoClipsAction Ctrl+Alt+G
        { "playlist.mergeSimilarClips", TR_NOOP ("Merge similar pattern clips"), Context::playlist, { chord ('G', ctrl | shift) } },
        { "playlist.mergeAutomationClips", TR_NOOP ("Merge automation clips"), Context::playlist, { chord ('G', ctrl | alt) } },
        { "playlist.cloneTracks", TR_NOOP ("Clone selected playlist track(s)"), Context::playlist, { chord ('B', ctrl | shift) } },
        { "playlist.makeUnique", TR_NOOP ("Make the selected clips unique"), Context::playlist, { chord ('V', ctrl | shift) } },
        // FL's Alt+M / Shift+Alt+M (EEMuteAction / EEUnmuteAction, issue #53): the selected clips, or all of them
        { "playlist.muteClips", TR_NOOP ("Mute the selected clips (all when none is selected)"), Context::playlist, { chord ('M', alt) } },
        { "playlist.unmuteClips", TR_NOOP ("Unmute the selected clips (all when none is selected)"), Context::playlist, { chord ('M', shift | alt) } },
        { "playlist.stretchMode", TR_NOOP ("Stretch audio when resizing audio clips on/off"), Context::playlist, { chord ('M', shift) } },
        { "playlist.createVariants", TR_NOOP ("Create variants when stretching audio clips on/off"), Context::playlist, {} },
        { "playlist.lockAllToContent", TR_NOOP ("Lock all playlist tracks to content"), Context::playlist, {} },
        { "playlist.unlockAllFromContent", TR_NOOP ("Unlock all playlist tracks from content"), Context::playlist, {} },
        { "playlist.resetEmptyTracks", TR_NOOP ("Reset empty playlist tracks"), Context::playlist, {} },
        { "playlist.collapseAllGroups", TR_NOOP ("Collapse all grouped tracks"), Context::playlist, {} },
        { "playlist.expandAllGroups", TR_NOOP ("Expand all grouped tracks"), Context::playlist, {} },
        { "playlist.purgeUnusedAudio", TR_NOOP ("Purge unused audio clips"), Context::playlist, {} },
        // round 189e: FL's Playlist Edit / Select items it lacked (EEMenu as the Playlist enables it, FL 2026 shots f04/f06)
        { "playlist.shiftLeft", TR_NOOP ("Shift the selected clips left (one snap step)"), Context::playlist, { chord (left, shift) } },
        { "playlist.shiftRight", TR_NOOP ("Shift the selected clips right (one snap step)"), Context::playlist, { chord (right, shift) } },
        { "playlist.shiftUp", TR_NOOP ("Shift the selected clips up (one track)"), Context::playlist, { chord (up, shift) } },
        { "playlist.shiftDown", TR_NOOP ("Shift the selected clips down (one track)"), Context::playlist, { chord (down, shift) } },
        // round 190ag (issue #147): FL's editor KeyDown (FUN_00e52360) sends Left / Right with Alt held, with or without
        // Shift, to the same shift as Shift+Left / Right (FUN_00e51730), which then steps by a pixel's worth of ticks
        { "playlist.nudgeLeft", TR_NOOP ("Nudge the selected clips left (fine step, no snap)"), Context::playlist, { chord (left, alt), chord (left, alt | shift) } },
        { "playlist.nudgeRight", TR_NOOP ("Nudge the selected clips right (fine step, no snap)"), Context::playlist, { chord (right, alt), chord (right, alt | shift) } },
        { "playlist.sliceInsertSpace", TR_NOOP ("Slice & insert space"), Context::playlist, { chord (insert, ctrl | alt) } },
        { "playlist.trimSpace", TR_NOOP ("Trim selection (keep only the time selection)"), Context::playlist, {} },
        // round 190cg: Live's arrangement commands on the time selection (no default keys: FL's stay as they are)
        { "playlist.loopSwitch", TR_NOOP ("Loop the time selection on/off (the range stays)"), Context::playlist, {} },
        { "playlist.loopHalve", TR_NOOP ("Halve the time selection"), Context::playlist, {} },
        { "playlist.loopDouble", TR_NOOP ("Double the time selection"), Context::playlist, {} },
        { "playlist.duplicateTime", TR_NOOP ("Duplicate time"), Context::playlist, {} },
        { "playlist.cutTime", TR_NOOP ("Cut time"), Context::playlist, {} },
        { "playlist.copyTime", TR_NOOP ("Copy time"), Context::playlist, {} },
        { "playlist.pasteTime", TR_NOOP ("Paste time"), Context::playlist, {} },
        { "playlist.cropClips", TR_NOOP ("Crop the selected clips to the time selection"), Context::playlist, {} },
        { "playlist.automationFollowsClips", TR_NOOP ("Automation follows clips on/off"), Context::playlist, {} },
        { "playlist.deselect", TR_NOOP ("Deselect (clips and time)"), Context::playlist, { chord ('D', ctrl) } },
        { "playlist.selectBySource", TR_NOOP ("Select by selected source"), Context::playlist, { chord ('C', shift) } },
        { "playlist.invertSelection", TR_NOOP ("Invert selection"), Context::playlist, { chord ('I', shift) } },
        // round 189g: FL's Edit / Select options ([DFM ShortcutsModule] EEMakeUniqueAsSampleAction, EELeftResizingAction
        // ShortCut 49188 = Ctrl+Alt+Home, EEShiftMouseWheelNudgeAction, EECreateNewClipsWithManualFadesAction,
        // EESnapFadeHandlesAction, EESelMagicAction)
        { "playlist.makeUniqueAsSample", TR_NOOP ("Make the selected audio clips unique as samples"), Context::playlist, {} },
        { "playlist.leftResizing", TR_NOOP ("Allow resizing clips from the left on/off"), Context::playlist, { chord (home, ctrl | alt) } },
        { "playlist.wheelNudging", TR_NOOP ("Nudge clips with Shift+mouse wheel on/off"), Context::playlist, {} },
        { "playlist.manualFades", TR_NOOP ("Create new clips with manual fades on/off"), Context::playlist, {} },
        { "playlist.snapFadeHandles", TR_NOOP ("Snap fade handles on/off"), Context::playlist, {} },
        { "playlist.magicLasso", TR_NOOP ("Magic lasso (circle clips with the mouse to select them) on/off"), Context::playlist, {} },

        // Browser (focused)
        { "browser.previous", TR_NOOP ("Previous file (preview)"), Context::browser, { chord (up) } },
        { "browser.next", TR_NOOP ("Next file (preview)"), Context::browser, { chord (down) } },
        { "browser.swapPrevious", TR_NOOP ("Previous file into the selected channel"), Context::browser, { chord (up, shift) } },
        { "browser.swapNext", TR_NOOP ("Next file into the selected channel"), Context::browser, { chord (down, shift) } },
        { "browser.load", TR_NOOP ("Load the file into the selected channel"), Context::browser, { chord (enter) } },
        { "browser.closeFolder", TR_NOOP ("Close the folder (or go to the parent folder)"), Context::browser, { chord (left) } },
        { "browser.openFolder", TR_NOOP ("Open the folder"), Context::browser, { chord (right) } },
        { "browser.rename", TR_NOOP ("Rename the selected plugin (Plugin database)"), Context::browser, { chord (f2) } },
        // Round 190ce: the Browser's collections with the reference DAW's keys (1-7 put the selected item into a colour
        // collection or take it out, 0 takes it out of all; docs/analise/REA_rodada190ce.md §1). Plain keys: the typing
        // keyboard keeps them while it is on.
        { "browser.collection1", TR_NOOP ("Collection 1 (Starred): add or remove the selected item"), Context::browser, { chord ('1') } },
        { "browser.collection2", TR_NOOP ("Collection 2: add or remove the selected item"), Context::browser, { chord ('2') } },
        { "browser.collection3", TR_NOOP ("Collection 3: add or remove the selected item"), Context::browser, { chord ('3') } },
        { "browser.collection4", TR_NOOP ("Collection 4: add or remove the selected item"), Context::browser, { chord ('4') } },
        { "browser.collection5", TR_NOOP ("Collection 5: add or remove the selected item"), Context::browser, { chord ('5') } },
        { "browser.collection6", TR_NOOP ("Collection 6: add or remove the selected item"), Context::browser, { chord ('6') } },
        { "browser.collection7", TR_NOOP ("Collection 7: add or remove the selected item"), Context::browser, { chord ('7') } },
        { "browser.clearCollections", TR_NOOP ("Remove the selected item from every collection"), Context::browser, { chord ('0') } },

        // Mixer (focused)
        { "mixer.moveLeft", TR_NOOP ("Move the selected inserts left"), Context::mixer, { chord (left, alt) } },
        { "mixer.moveRight", TR_NOOP ("Move the selected inserts right"), Context::mixer, { chord (right, alt) } },
        { "mixer.linkChannels", TR_NOOP ("Link the selected channels to the selected insert"), Context::mixer, { chord ('L', ctrl) } },
        { "mixer.linkChannelsFrom", TR_NOOP ("Link the selected channels to inserts starting from the selected one"), Context::mixer, { chord ('L', ctrl | shift) } },
        { "mixer.selectLinkedChannels", TR_NOOP ("Select the channels linked to the selected insert"), Context::mixer, { chord ('L', alt) } },
        // FL's mixer track menu keys (round 167k, the DFM's ShortCut values: F2, S, Alt+R, Shift+Alt+R)
        { "mixer.rename", TR_NOOP ("Rename and color the selected mixer track"), Context::mixer, { chord (f2) } },
        { "mixer.audioEditor", TR_NOOP ("Open the audio editor"), Context::mixer, { chord ('E', ctrl) } },
        { "mixer.audioEditorRecord", TR_NOOP ("Open the audio editor and record"), Context::mixer, { chord ('E', shift) } },
        { "mixer.soloSelected", TR_NOOP ("Solo the selected mixer track(s)"), Context::mixer, { chord ('S') } },
        { "mixer.altSoloSelected", TR_NOOP ("Alt solo the selected mixer track(s) (with the tracks that feed them)"), Context::mixer, { chord ('S', alt) } },
        { "mixer.renderArmed", TR_NOOP ("Disk recording: render the armed tracks to wave file(s)"), Context::mixer, { chord ('R', alt) } },
        { "mixer.renderSelected", TR_NOOP ("Disk recording: render the selected tracks to wave file(s)"), Context::mixer, { chord ('R', shift | alt) } },
        // FL 2026 (round 186c, checked by hand): Del deletes the selected mixer tracks (with FL's question); it never removes
        // an effect, except with the slot's menu open (issue #11, handled by that menu). The effect action stays, unbound.
        { "mixer.deleteTracks", TR_NOOP ("Delete the selected mixer tracks"), Context::mixer, { chord (del) } },
        { "mixer.deleteEffect", TR_NOOP ("Delete the selected effect slot's plugin"), Context::mixer, {} },
    };

    // numeric keypad 1-9: select patterns 1-9
    static const char* patternIds[] = { "pattern.select1", "pattern.select2", "pattern.select3", "pattern.select4", "pattern.select5",
                                        "pattern.select6", "pattern.select7", "pattern.select8", "pattern.select9" };

    for (int i = 0; i < 9; ++i)
        list.push_back ({ patternIds[i], TR_NOOP ("Select pattern {0}"), Context::global, { chord (numpad1 + i) } });

    // number keys: mute (plain) / solo (Ctrl) channels 1-10 while the rack has focus
    static const char* muteIds[] = { "rack.mute1", "rack.mute2", "rack.mute3", "rack.mute4", "rack.mute5",
                                     "rack.mute6", "rack.mute7", "rack.mute8", "rack.mute9", "rack.mute10" };
    static const char* soloIds[] = { "rack.solo1", "rack.solo2", "rack.solo3", "rack.solo4", "rack.solo5",
                                     "rack.solo6", "rack.solo7", "rack.solo8", "rack.solo9", "rack.solo10" };

    for (int i = 0; i < 10; ++i)
    {
        const auto digit = i == 9 ? '0' : (char) ('1' + i);
        list.push_back ({ muteIds[i], TR_NOOP ("Mute or unmute channel {0}"), Context::channelRack, { chord (digit) } });
        list.push_back ({ soloIds[i], TR_NOOP ("Solo channel {0}"), Context::channelRack, { chord (digit, ctrl) } });
    }

    return list;
}

bool contextsOverlap (Context a, Context b)
{
    return a == b;
}

// --- tiny JSON reader for {"id": "chord" | ["chord", ...], ...} ---------------------------------
struct Json
{
    const std::string& s;
    std::size_t i = 0;

    void skip()
    {
        while (i < s.size() && std::isspace ((unsigned char) s[i]))
            ++i;
    }

    bool eat (char c)
    {
        skip();
        if (i < s.size() && s[i] == c)
        {
            ++i;
            return true;
        }
        return false;
    }

    std::optional<std::string> string()
    {
        skip();
        if (i >= s.size() || s[i] != '"')
            return std::nullopt;

        std::string out;
        for (++i; i < s.size(); ++i)
        {
            const auto c = s[i];
            if (c == '"')
            {
                ++i;
                return out;
            }
            if (c == '\\' && i + 1 < s.size())
            {
                const auto n = s[++i];
                out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
            }
            else
                out += c;
        }
        return std::nullopt;
    }
};
} // namespace

bool Chord::isPrintable() const noexcept
{
    return key > ' ' && key < 0x7f;
}

std::string displayString (const Chord& c)
{
    return modmap::keyNames (toString (c), modmap::hostOs());
}

std::string toString (const Chord& c)
{
    std::string out;
    if (c.mods & ctrl)
        out += "Ctrl+";
    if (c.mods & shift)
        out += "Shift+";
    if (c.mods & alt)
        out += "Alt+";

    if (c.key >= f1 && c.key <= f12)
        return out + "F" + std::to_string (c.key - f1 + 1);

    for (const auto& k : keyNames)
        if (k.key == c.key)
            return out + k.name;

    if (c.isPrintable())
        return out + std::string (1, (char) c.key);

    return out + "?";
}

std::optional<Chord> parseChord (const std::string& text)
{
    auto rest = trim (text);
    Chord c;

    // modifiers first ("+" alone, or "Numpad +", is a key, so only split on "+" followed by more text)
    for (;;)
    {
        const auto l = lower (rest);
        auto take = [&] (const char* prefix, std::uint8_t mod) {
            const std::string p (prefix);
            if (l.size() > p.size() && l.compare (0, p.size(), p) == 0)
            {
                c.mods |= mod;
                rest = trim (rest.substr (p.size()));
                return true;
            }
            return false;
        };

        if (! (take ("ctrl+", ctrl) || take ("shift+", shift) || take ("alt+", alt)))
            break;
    }

    const auto name = lower (rest);

    if (name.empty())
        return std::nullopt;

    if (name.size() >= 2 && name[0] == 'f' && std::all_of (name.begin() + 1, name.end(), [] (char ch) { return std::isdigit ((unsigned char) ch); }))
    {
        const auto n = std::stoi (name.substr (1));
        if (n < 1 || n > 12)
            return std::nullopt;
        c.key = f1 + n - 1;
        return c;
    }

    for (const auto& k : keyNames)
        if (lower (k.name) == name)
        {
            c.key = k.key;
            return c;
        }

    if (name == "del")
        c.key = del;
    else if (name == "ins")
        c.key = insert;
    else if (name == "escape")
        c.key = escape;
    else if (name == "return")
        c.key = enter;
    else if (rest.size() == 1 && rest[0] > ' ' && rest[0] < 0x7f)
        c.key = std::toupper ((unsigned char) rest[0]);
    else
        return std::nullopt;

    return c;
}

const std::vector<Action>& actions()
{
    static const auto list = makeActions();
    return list;
}

const Action* findAction (const std::string& id)
{
    for (const auto& a : actions())
        if (id == a.id)
            return &a;

    return nullptr;
}

int typingPianoNote (int key, int octave, int root) noexcept
{
    const auto base = 12 * typingpiano::clampOctave (octave) + typingpiano::clampRoot (root); // octave 4 -> 48
    const auto inRange = [] (int n) { return n >= 0 && n <= 127 ? n : -1; };
    static const char lowerRow[] = "ZSXDCVGBHNJM";
    static const char upperRow[] = "Q2W3ER5T6Y7UI9O0P";
    const auto c = key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key;

    for (int i = 0; lowerRow[i] != 0; ++i)
        if (lowerRow[i] == c)
            return inRange (base + i);

    for (int i = 0; upperRow[i] != 0; ++i)
        if (upperRow[i] == c)
            return inRange (base + 12 + i);

    return -1;
}

namespace
{
struct TypingLayout
{
    const char* name;
    const char* keys; // one token per layoutKeys character: notes at octave 4, root C ("60,64,67" a chord), "-" LOCK, "~" FREE
};

// The printable keys of FL's .scr files, in their line order (FL reads a file's lines as the scan codes 2..0x35 of a US
// keyboard: number row, Q row, A row with ` , \ then the Z row; the Backspace, Tab, Enter, Ctrl / Caps and Shift lines
// are FREE in every file, so they are left out). LYNX matches the keys by character.
constexpr char layoutKeys[] = "1234567890-=QWERTYUIOP[]ASDFGHJKL;'`\\ZXCVBNM,./";

// FL 2026's System/Config/Typing to piano/*.scr, written as data (note names converted with C5 = 60), in the order of
// FL's Layout menu (by name). FL's parser (FUN_00661f70) reads FREE as -2: the key sends no note and stays a shortcut;
// LOCK as -1: no note and no shortcut. A key missing from a file is FREE (FUN_01182670 fills the table with -2 first).
const TypingLayout typingLayouts[] = {
        { "Layout FPC (C4)", "49 55 51 53 88 89 90 91 - - - - 48 47 45 43 84 85 86 87 - - - - 40 38 46 44 80 81 82 83 - - - ~ ~ 37 36 42 54 76 77 78 79 - -" },
        { "Layout Janko", "57 59 61 63 65 67 69 71 73 75 77 79 58 60 62 64 66 68 70 72 74 76 78 80 59 61 63 65 67 69 71 73 75 77 79 ~ ~ 60 62 64 66 68 70 72 74 76 78" },
        { "Layout Performance mode", "0 1 2 3 4 48 49 50 51 52 - - 12 13 14 15 16 60 61 62 63 64 - - 24 25 26 27 28 72 73 74 75 76 - ~ ~ 36 37 38 39 40 84 85 86 87 88" },
        { "Layout Piano", "- 61 63 - 66 68 70 - 73 75 - 78 60 62 64 65 67 69 71 72 74 76 77 79 ~ 49 51 - 54 56 58 - 61 63 - ~ ~ 48 50 52 53 55 57 59 60 62 64" },
        { "Layout Slicex (C4)", "84 85 86 87 88 89 90 91 - - - - 76 77 78 79 80 81 82 83 - - - - 68 69 70 71 72 73 74 75 - - - ~ ~ 60 61 62 63 64 65 66 67 - -" },
        { "Major", "84 86 88 89 91 93 95 96 98 100 101 103 72 74 76 77 79 81 83 84 86 88 89 91 60 62 64 65 67 69 71 72 74 76 77 ~ ~ 48 50 52 53 55 57 59 60 62 64" },
        { "Major + Chords", "60,64,67 62,65,69 64,67,71 65,69,72 67,71,74 69,72,76 71,74,77 72,76,79 74,77,81 76,79,83 77,81,84 79,83,86 72 74 76 77 79 81 83 84 86 88 89 91 60 62 64 65 67 69 71 72 74 76 77 ~ ~ 48 50 52 53 55 57 59 60 62 64" },
        { "Major Bebop", "84 86 88 89 91 92 93 95 96 98 100 101 72 74 76 77 79 80 81 83 84 86 88 89 60 62 64 65 67 68 69 71 72 74 76 ~ ~ 48 50 52 53 55 56 57 59 60 62" },
        { "Major Chordmap", "64,67,72 65,69,74 64,67,71 65,69,72 62,67,71 64,69,72 62,65,71 64,67,72 65,69,74 64,67,71 65,69,72 62,67,71 60,64,67 62,65,69 59,64,67 60,65,69 59,62,67 60,64,69 59,62,65 60,64,67 62,65,69 59,64,67 60,65,69 59,62,67 55,60,64 57,62,65 55,59,64 57,60,65 55,59,62 57,60,64 53,59,62 55,60,64 57,62,65 55,59,64 57,60,65 ~ ~ 36 38 40 41 43 45 47 48 50 52" },
        { "Major Pentatonic", "84 86 88 91 93 96 98 100 103 105 108 110 72 74 76 79 81 84 86 88 91 93 96 98 60 62 64 67 69 72 74 76 79 81 84 ~ ~ 48 50 52 55 57 60 62 64 67 69" },
        { "Minor Harmonic", "84 86 87 89 91 92 95 96 98 99 101 103 72 74 75 77 79 80 83 84 86 87 89 91 60 62 63 65 67 68 71 72 74 75 77 ~ ~ 48 50 51 53 55 56 59 60 62 63" },
        { "Minor Hungarian", "84 86 87 90 91 92 95 96 98 99 102 103 72 74 75 78 79 80 83 84 86 87 90 91 60 62 63 66 67 68 71 72 74 75 78 ~ ~ 48 50 51 54 55 56 59 60 62 63" },
        { "Minor Melodic", "84 86 87 89 91 93 95 96 98 99 101 103 72 74 75 77 79 81 83 84 86 87 89 91 60 62 63 65 67 69 71 72 74 75 77 ~ ~ 48 50 51 53 55 57 59 60 62 63" },
        { "Minor Natural (Aeolian)", "84 86 87 89 91 92 94 96 98 99 101 103 72 74 75 77 79 80 82 84 86 87 89 91 60 62 63 65 67 68 70 72 74 75 77 ~ ~ 48 50 51 53 55 56 58 60 62 63" },
        { "Minor Natural + Chords", "60,63,67 62,65,68 63,67,70 65,68,72 67,70,74 68,72,75 70,74,77 72,75,79 74,77,80 75,79,82 77,80,84 79,82,86 72 74 75 77 79 80 82 84 86 87 89 91 60 62 63 65 67 68 70 72 74 75 77 ~ ~ 48 50 51 53 55 56 58 60 62 63" },
        { "Minor Natural Chordmap", "63,67,72 65,68,74 63,67,70 65,68,72 62,67,70 63,68,72 62,65,70 63,67,72 65,68,74 63,67,70 65,68,72 62,67,70 60,63,67 62,65,68 58,63,67 60,65,68 58,62,67 60,63,68 58,62,65 60,63,67 62,65,68 58,63,67 60,65,68 58,62,67 55,60,63 56,62,65 55,58,63 56,60,65 55,58,62 56,60,63 53,58,62 55,60,63 56,62,65 55,58,63 56,60,65 ~ ~ 36 38 39 41 43 44 46 48 50 51" },
        { "Minor Neopolitan", "84 85 87 89 91 92 95 96 97 99 101 103 72 73 75 77 79 80 83 84 85 87 89 91 60 61 63 65 67 68 71 72 73 75 77 ~ ~ 48 49 51 53 55 56 59 60 61 63" },
        { "Minor Pentatonic", "84 87 89 91 94 96 99 101 103 106 108 111 72 75 77 79 82 84 87 89 91 94 96 99 60 63 65 67 70 72 75 77 79 82 84 ~ ~ 48 51 53 55 58 60 63 65 67 70" },
        { "Other Arabic", "84 85 88 89 91 92 95 96 97 100 101 103 72 73 76 77 79 80 83 84 85 88 89 91 60 61 64 65 67 68 71 72 73 76 77 ~ ~ 48 49 52 53 55 56 59 60 61 64" },
        { "Other Blues", "84 87 89 90 91 94 96 99 101 102 103 106 72 75 77 78 79 82 84 87 89 90 91 94 60 63 65 66 67 70 72 75 77 78 79 ~ ~ 48 51 53 54 55 58 60 63 65 66" },
        { "Other Diminished", "84 86 87 89 90 92 93 95 96 98 99 101 72 74 75 77 78 80 81 83 84 86 87 89 60 62 63 65 66 68 69 71 72 74 75 ~ ~ 48 50 51 53 54 56 57 59 60 62" },
        { "Other Dominant Bebop", "84 86 88 89 91 93 94 95 96 98 100 101 72 74 76 77 79 81 82 83 84 86 88 89 60 62 64 65 67 69 70 71 72 74 76 ~ ~ 48 50 52 53 55 57 58 59 60 62" },
        { "Other Dorian", "84 86 87 89 91 93 94 96 98 99 101 103 72 74 75 77 79 81 82 84 86 87 89 91 60 62 63 65 67 69 70 72 74 75 77 ~ ~ 48 50 51 53 55 57 58 60 62 63" },
        { "Other Enigmatic", "84 85 88 90 92 94 95 97 98 101 103 105 72 73 76 78 80 82 83 85 86 89 91 93 60 61 64 66 68 70 71 73 74 77 79 ~ ~ 48 49 52 54 56 58 59 61 62 65" },
        { "Other Japanese Insen", "84 85 89 91 94 96 97 101 103 106 108 109 72 73 77 79 82 84 85 89 91 94 96 97 60 61 65 67 70 72 73 77 79 82 84 ~ ~ 48 49 53 55 58 60 61 65 67 70" },
        { "Other Locrian", "84 85 87 89 90 92 94 96 97 99 101 102 72 73 75 77 78 80 82 84 85 87 89 90 60 61 63 65 66 68 70 72 73 75 77 ~ ~ 48 49 51 53 54 56 58 60 61 63" },
        { "Other Lydian", "84 86 88 90 91 93 95 96 98 100 102 103 72 74 76 78 79 81 83 84 86 88 90 91 60 62 64 66 67 69 71 72 74 76 78 ~ ~ 48 50 52 54 55 57 59 60 62 64" },
        { "Other Mixolydian", "84 86 88 89 91 93 94 96 98 100 101 103 72 74 76 77 79 81 82 84 86 88 89 91 60 62 64 65 67 69 70 72 74 76 77 ~ ~ 48 50 52 53 55 57 58 60 62 64" },
        { "Other Neopolitan", "84 85 87 89 91 93 95 96 97 99 101 103 72 73 75 77 79 81 83 84 85 87 89 91 60 61 63 65 67 69 71 72 73 75 77 ~ ~ 48 49 51 53 55 57 59 60 61 63" },
        { "Other Phrygian", "84 85 87 89 91 92 94 96 97 99 101 103 72 73 75 77 79 80 82 84 85 87 89 91 60 61 63 65 67 68 70 72 73 75 77 ~ ~ 48 49 51 53 55 56 58 60 61 63" },
        { "Other Whole Tone", "84 86 88 90 92 94 96 98 100 102 104 106 72 74 76 78 80 82 84 86 88 90 92 94 60 62 64 66 68 70 72 74 76 78 80 ~ ~ 48 50 52 54 56 58 60 62 64 66" },
};

// The token of `key` in `layout` (pointing into its string), nullptr when the key is not one of layoutKeys.
const char* layoutToken (int key, int layout) noexcept
{
    const auto c = key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key;
    const auto* found = c > 0 && c < 0x7f ? std::strchr (layoutKeys, c) : nullptr;
    if (found == nullptr || layout < 0 || layout >= (int) std::size (typingLayouts))
        return nullptr;

    const auto* token = typingLayouts[layout].keys;
    for (auto i = found - layoutKeys; i > 0 && token != nullptr; --i) // skip one space-separated token per key before it
        if ((token = std::strchr (token, ' ')) != nullptr)
            ++token;
    return token;
}
} // namespace

int typingpiano::layoutCount() noexcept
{
    return (int) std::size (typingLayouts);
}

const char* typingpiano::layoutName (int layout) noexcept
{
    return layout >= 0 && layout < layoutCount() ? typingLayouts[layout].name : "";
}

int typingpiano::layoutIndex (const std::string& name) noexcept
{
    for (int i = 0; i < layoutCount(); ++i)
        if (name == typingLayouts[i].name)
            return i;
    return pianoLayout();
}

int typingpiano::pianoLayout() noexcept
{
    for (int i = 0; i < layoutCount(); ++i)
        if (std::strcmp (typingLayouts[i].name, "Layout Piano") == 0)
            return i;
    return 0;
}

int typingpiano::performanceLayout() noexcept
{
    for (int i = 0; i < layoutCount(); ++i)
        if (std::strcmp (typingLayouts[i].name, "Layout Performance mode") == 0)
            return i;
    return -1;
}

TypingKey typingPianoKey (int key, int octave, int root, int layout)
{
    TypingKey out;
    const auto* token = layoutToken (key, layout);
    if (token == nullptr || *token == '~') // FREE (or not in the layout): the key stays a shortcut
        return out;

    out.taken = true; // (FL's key handler FUN_011828c0 reports any token but FREE as handled, even with no note)
    // FL's note: the table's + root + 12 x the octave offset (KbC2..KbC6 = -2..2), for every layout (FUN_011828c0)
    const auto shift = 12 * (typingpiano::clampOctave (octave) - typingpiano::defaultOctave) + typingpiano::clampRoot (root);
    for (char* end = nullptr; *token >= '0' && *token <= '9'; token = *end == ',' ? end + 1 : end)
        if (const auto n = (int) std::strtol (token, &end, 10) + shift; n >= 0 && n <= 127)
            out.notes.push_back (n);
    return out;
}

std::vector<int> typingPianoNotes (int key, int octave, int root, int layout)
{
    return typingPianoKey (key, octave, root, layout).notes;
}

namespace
{
// layoutKeys by PC scan code (set 1): 0x02..0x0D the number row, 0x10..0x1B the Q row, 0x1E..0x28 the A row, 0x29 `,
// 0x2B \, 0x2C..0x35 the Z row (the gaps are Backspace / Tab, Enter / Ctrl and the left Shift)
constexpr int firstTypingScanCode = 0x02, lastTypingScanCode = 0x35;
constexpr char usKeyByScanCode[] = "1234567890-=\0\0QWERTYUIOP[]\0\0ASDFGHJKL;'`\0\\ZXCVBNM,./";
static_assert (sizeof (usKeyByScanCode) - 1 == lastTypingScanCode - firstTypingScanCode + 1);
} // namespace

int typingpiano::usKeyOfScanCode (int scanCode) noexcept
{
    return scanCode >= firstTypingScanCode && scanCode <= lastTypingScanCode ? (int) (unsigned char) usKeyByScanCode[scanCode - firstTypingScanCode] : 0;
}

int typingpiano::scanCodeOfUsKey (int key) noexcept
{
    const auto c = key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key;
    for (int scan = firstTypingScanCode; scan <= lastTypingScanCode; ++scan)
        if (c > 0 && usKeyOfScanCode (scan) == c)
            return scan;
    return 0;
}

int typingpiano::physicalKey (int keyCode, const ScanCodeLookup& scanCodeOf)
{
    const auto scanCode = scanCodeOf != nullptr ? scanCodeOf (keyCode) : 0;
    return scanCode > 0 ? usKeyOfScanCode (scanCode) : keyCode;
}

KeyMap::KeyMap()
{
    resetToDefaults();
}

void KeyMap::resetToDefaults()
{
    chords.clear();

    for (const auto& a : actions())
        chords.push_back (a.defaults);
}

bool KeyMap::playsNote (const Chord& c, bool typingPiano) noexcept
{
    return typingPiano && (c.mods & (ctrl | alt | shift)) == 0 && typingPianoNote (c.key) >= 0;
}

std::string KeyMap::findFromSecondaryWindow (const Chord& c, bool typingIntoText, bool typingPiano) const
{
    if (! c.isValid() || c.key == escape) // (Esc closes the window)
        return {};
    const auto editingKey = c.isPrintable() || c.key == space || c.key == enter || c.key == tab || c.key == backspace || c.key == del
                            || c.key == insert || (c.key >= up && c.key <= end);
    if (typingIntoText && editingKey && (c.mods & (ctrl | alt)) == 0)
        return {};
    if (auto own = findUserBound (c, Context::global, typingPiano); ! own.empty())
        return own;
    return find (c, Context::global, typingPiano);
}

std::string KeyMap::findUserBound (const Chord& c, Context focus, bool typingPiano) const
{
    if (! c.isValid() || playsNote (c, typingPiano))
        return {};

    const auto plainPrintable = c.isPrintable() && (c.mods & (ctrl | alt)) == 0;
    const auto& list = actions();
    const Action* globalMatch = nullptr;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& a = list[i];

        if ((a.context != Context::global && a.context != focus) || chords[i] == a.defaults)
            continue;
        if (std::find (chords[i].begin(), chords[i].end(), c) == chords[i].end()
            || std::find (a.defaults.begin(), a.defaults.end(), c) != a.defaults.end())
            continue; // (a default key keeps its usual priority)
        if (typingPiano && plainPrintable && ! a.whilePiano)
            continue;
        if (a.context == focus && focus != Context::global)
            return a.id;
        if (globalMatch == nullptr)
            globalMatch = &a;
    }

    return globalMatch != nullptr ? globalMatch->id : std::string();
}

std::vector<std::string> KeyMap::shadowedBy (const Chord& c, const std::string& actionId) const
{
    std::vector<std::string> out;
    const auto* owner = findAction (actionId);
    if (owner == nullptr || ! c.isValid())
        return out;

    const auto& list = actions();
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& a = list[i];
        if (actionId == a.id || a.context == owner->context)
            continue;
        if (owner->context != Context::global && a.context != Context::global)
            continue; // two different panels never meet
        if (std::find (chords[i].begin(), chords[i].end(), c) != chords[i].end())
            out.push_back (a.id);
    }
    return out;
}

std::string KeyMap::find (const Chord& c, Context focus, bool typingPiano) const
{
    if (! c.isValid() || playsNote (c, typingPiano))
        return {};

    const auto plainPrintable = c.isPrintable() && (c.mods & (ctrl | alt)) == 0;
    const auto& list = actions();
    const Action* globalMatch = nullptr;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& a = list[i];

        if (a.context != Context::global && a.context != focus)
            continue;

        if (std::find (chords[i].begin(), chords[i].end(), c) == chords[i].end())
            continue;

        if (typingPiano && plainPrintable && ! a.whilePiano)
            continue;

        if (a.context == focus && focus != Context::global)
            return a.id; // the focused panel wins

        if (globalMatch == nullptr)
            globalMatch = &a;
    }

    return globalMatch != nullptr ? globalMatch->id : std::string();
}

const std::vector<Chord>& KeyMap::chordsFor (const std::string& actionId) const
{
    static const std::vector<Chord> empty;
    const auto& list = actions();

    for (std::size_t i = 0; i < list.size(); ++i)
        if (actionId == list[i].id)
            return chords[i];

    return empty;
}

void KeyMap::setChords (const std::string& actionId, std::vector<Chord> newChords)
{
    const auto& list = actions();

    for (std::size_t i = 0; i < list.size(); ++i)
        if (actionId == list[i].id)
            chords[i] = std::move (newChords);
}

bool KeyMap::loadJson (const std::string& json, std::vector<std::string>* errors)
{
    Json p { json };
    auto fail = [&] (const std::string& message) {
        if (errors != nullptr)
            errors->push_back (message);
    };

    if (! p.eat ('{'))
        return false;

    if (p.eat ('}'))
        return true;

    for (;;)
    {
        const auto id = p.string();

        if (! id || ! p.eat (':'))
            return false;

        std::vector<std::string> values;

        if (p.eat ('['))
        {
            if (! p.eat (']'))
                for (;;)
                {
                    const auto v = p.string();
                    if (! v)
                        return false;
                    values.push_back (*v);
                    if (p.eat (']'))
                        break;
                    if (! p.eat (','))
                        return false;
                }
        }
        else if (const auto v = p.string())
        {
            if (! trim (*v).empty())
                values.push_back (*v);
        }
        else
            return false;

        if (findAction (*id) == nullptr)
            fail ("unknown action: " + *id);
        else
        {
            std::vector<Chord> parsed;

            for (const auto& v : values)
                if (const auto c = parseChord (v))
                    parsed.push_back (*c);
                else
                    fail ("unreadable key for " + *id + ": " + v);

            setChords (*id, parsed);
        }

        if (p.eat ('}'))
            return true;

        if (! p.eat (','))
            return false;
    }
}

std::string KeyMap::toJson() const
{
    std::string out = "{\n";
    const auto& list = actions();

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        out += "  \"" + std::string (list[i].id) + "\": [";

        for (std::size_t k = 0; k < chords[i].size(); ++k)
            out += (k > 0 ? ", \"" : "\"") + toString (chords[i][k]) + "\"";

        out += i + 1 < list.size() ? "],\n" : "]\n";
    }

    return out + "}\n";
}

std::vector<KeyMap::Binding> KeyMap::functionKeyBindings() const
{
    std::vector<Binding> out;
    const auto& list = actions();
    for (std::size_t i = 0; i < list.size(); ++i)
        for (const auto& c : chords[i])
            if (c.key >= f1 && c.key <= f12)
                out.push_back ({ c, list[i].id, list[i].context });
    // plain, Shift, Ctrl, Alt, then the combinations; global before a panel's own
    auto modRank = [] (std::uint8_t m) {
        switch (m)
        {
            case none: return 0;
            case shift: return 1;
            case ctrl: return 2;
            case alt: return 3;
            default: return 4 + (int) m;
        }
    };
    std::stable_sort (out.begin(), out.end(), [&] (const Binding& a, const Binding& b) {
        if (a.chord.key != b.chord.key)
            return a.chord.key < b.chord.key;
        if (a.chord.mods != b.chord.mods)
            return modRank (a.chord.mods) < modRank (b.chord.mods);
        return (int) a.context < (int) b.context;
    });
    return out;
}

std::vector<std::pair<std::string, std::string>> KeyMap::conflicts() const
{
    std::vector<std::pair<std::string, std::string>> out;
    const auto& list = actions();

    for (std::size_t a = 0; a < list.size(); ++a)
        for (std::size_t b = a + 1; b < list.size(); ++b)
        {
            if (! contextsOverlap (list[a].context, list[b].context))
                continue;

            for (const auto& c : chords[a])
                if (std::find (chords[b].begin(), chords[b].end(), c) != chords[b].end())
                {
                    out.push_back ({ list[a].id, list[b].id });
                    break;
                }
        }

    return out;
}
std::vector<std::string> KeyMap::actionsWith (const Chord& chord, Context context, const std::string& except) const
{
    std::vector<std::string> out;
    const auto& list = actions();

    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].context == context && except != list[i].id
            && std::find (chords[i].begin(), chords[i].end(), chord) != chords[i].end())
            out.push_back (list[i].id);

    return out;
}

bool KeyMap::isDefault (const std::string& actionId) const
{
    const auto& list = actions();

    for (std::size_t i = 0; i < list.size(); ++i)
        if (actionId == list[i].id)
            return chords[i] == list[i].defaults;

    return true;
}

std::string KeyMap::overridesJson() const
{
    std::string out = "{\n";
    const auto& list = actions();
    bool first = true;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (chords[i] == list[i].defaults)
            continue;

        out += first ? "" : ",\n";
        first = false;
        out += "  \"" + std::string (list[i].id) + "\": [";

        for (std::size_t k = 0; k < chords[i].size(); ++k)
            out += (k > 0 ? ", \"" : "\"") + toString (chords[i][k]) + "\"";

        out += "]";
    }

    return out + (first ? "}\n" : "\n}\n");
}

} // namespace lynx::keys
