#pragma once

// Settings › "LYNX extras" (round 190am, decision D7): the list of LYNX's own improvements that are landed but off by
// default, so FL Studio's look and behaviour stay untouched until someone asks for them. The page shows each one with
// a switch and one line of explanation.
//
// This is only the list: every switch reads and writes the preference the feature already has (the same variable and
// the same settings key as its menu item or Settings page), so there is no second place where the state lives. An
// extra that is off by default and has its own switch elsewhere belongs here; add it when it lands.
//
// No JUCE: tested in tests/Extras190amTests.inc. The texts are English keys (TR_NOOP), translated by the page.

#include <string_view>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::extras
{
struct Item
{
    std::string_view id;        // what the page asks the app to read or switch
    std::string_view section;   // the page's group
    std::string_view title;     // the switch's caption: the same text as the feature's own menu item or check box
    std::string_view note;      // one line: what it does, and where else the switch is
    std::string_view settingsKey; // where the preference is kept ("file:key"), for the tests and the docs
};

inline constexpr Item items[] = {
    { "mixer.loudness", TR_NOOP ("Mixer"), TR_NOOP ("Loudness meter beside the Master (LYNX)"),
      TR_NOOP ("Short-term and integrated loudness (LUFS) and the true peak of the Master's output, with a Reset button. Also in the Mixer menu."),
      "ui-options.txt:mixerLoudness" },
    { "mixer.stripInserts", TR_NOOP ("Mixer"), TR_NOOP ("Insert effects on the strips (LYNX)"),
      TR_NOOP ("Each Mixer strip lists its insert's effects under its header: click one to open it, its dot to bypass it, drag to reorder or onto another strip (Ctrl: a copy), right-click for the slot menu. Also in the Mixer menu."),
      "ui-options.txt:mixerStripInserts" },
    { "mixer.usedOnly", TR_NOOP ("Mixer"), TR_NOOP ("Show only used inserts"),
      TR_NOOP ("Hides the inserts nothing uses. The Master, the selected insert and any insert that receives audio always stay. Also in the Mixer menu."),
      "ui-options.txt:mixerUsedOnly" },
    { "mixer.keepLostLinks", TR_NOOP ("Mixer"), TR_NOOP ("Keep links to deleted inserts, greyed (LYNX)"),
      TR_NOOP ("Deleting an insert keeps the links that went into it, greyed, so they can be connected again. Also in the Mixer menu."),
      "ui-options.txt:mixerKeepLostLinks" },
    { "mixer.followPlaylist", TR_NOOP ("Mixer"), TR_NOOP ("Mixer groups follow the Playlist (LYNX)"),
      TR_NOOP ("The mixer's groups mirror the Playlist's track groups after every change. Also in the Mixer menu."),
      "ui-options.txt:mixerFollowPlaylist" },
    { "playlist.automationLanes", TR_NOOP ("Playlist"), TR_NOOP ("Automation lanes"),
      TR_NOOP ("Automation can sit in lanes under a Playlist track; automation clips keep working as before. Also in the Playlist menu."),
      "ui-options.txt:automationLanes" },
    // round 190iu (issue #186): REAPER's track-panel wheel; FL and Live scroll with the wheel everywhere
    { "playlist.wheelZones", TR_NOOP ("Playlist"), TR_NOOP ("Wheel: scroll over the track headers, zoom over the arrangement"),
      TR_NOOP ("The plain mouse wheel scrolls the tracks over the track headers and zooms the time line over the clips; Ctrl+wheel does the other one. Also in the Playlist menu, and in the Clip-launch layout's arrangement."),
      "ui-options.txt:wheelZones" },
    // round 190iu (issue #191.2): FL takes MIDI files on the Channel rack and the Piano roll; Live in its arrangement
    { "playlist.midiDrop", TR_NOOP ("Playlist"), TR_NOOP ("MIDI files dropped on the Playlist become pattern clips"),
      TR_NOOP ("A .mid file from the Browser, the file manager or a plugin dropped on a track becomes a new pattern there: on an instrument track its notes go to the track's channel, elsewhere each MIDI track gets a new channel. The Clip-launch layout always takes them. Also in the Playlist menu."),
      "ui-options.txt:midiDrop" },
    { "project.linkTracks", TR_NOOP ("Playlist"), TR_NOOP ("Link Playlist tracks to mixer tracks in new projects"),
      TR_NOOP ("New projects start with every Playlist track that holds clips linked to a mixer track (Track N to insert N when that leaves the sound unchanged); linked track headers show mute, solo, volume and pan. Also on the Settings › Project page, with the switch for the open project."),
      "ui-options.txt:newLinkTracks" },
    { "playlist.trackInserts", TR_NOOP ("Playlist"), TR_NOOP ("Show insert effects on linked tracks"),
      TR_NOOP ("While Playlist tracks are linked to mixer tracks, each linked track header lists its mixer track's effects: click one to open it, its dot to bypass it, drag to reorder or onto another track. Also in Playlist › Edit."),
      "ui-options.txt:trackInserts" },
    // round 190jq (issue #164): FL's header column has a fixed width
    { "playlist.headerWidth", TR_NOOP ("Playlist"), TR_NOOP ("Drag the track header column's right edge to widen it"),
      TR_NOOP ("FL keeps the track header column at a fixed width. With this on, drag the column's right edge to make the headers wider (a double click there goes back to FL's width); the width is kept for every project. Also in the Playlist menu."),
      "ui-options.txt:headerWidthDrag" },
    // round 190jq (issue #191): FL's Pencil places the last pattern or clip with a single click
    { "playlist.pointerClick", TR_NOOP ("Playlist"), TR_NOOP ("Pointer click switch in the Playlist toolbar"),
      TR_NOOP ("A switch beside Stretch: while it is lit, a click on an empty spot with the Pencil or the Brush deselects and moves the song position there, a drag draws a selection box (as FL's Select tool), and a double click places the clip. Also in the Playlist menu."),
      "ui-options.txt:pointerClickButton" },
    // round 190jq (issue #191.3): FL draws automation clips on tracks of their own; Live draws lanes under each track
    { "playlist.clipLanes", TR_NOOP ("Playlist"), TR_NOOP ("Automation clips as lanes under their tracks"),
      TR_NOOP ("A track that holds only automation clips of one instrument (or of a linked mixer track) is drawn right under that instrument's track, as a lane that folds with the track's arrow; the clips stay clips on their own tracks. On by default in the Clip-launch layout, as Live's lanes. Also in the Playlist menu."),
      "ui-options.txt:clipLanesPattern" },
    // round 190oo (design review CS O-2): Live's new set has a full mixer strip on every track
    { "project.clipLaunchStrips", TR_NOOP ("Playlist"), TR_NOOP ("New Clip-launch projects: a mixer track for every track"),
      TR_NOOP ("A new project made in the Clip-launch layout links Track N to mixer track N, so every column of the Session mixer shows its strip, as a new Live set does. Only empty new projects; nothing is routed differently. Also in the Mixer menu › New projects."),
      "ui-options.txt:newClipLaunchStrips" },
    // round 190iu (issue #191.5): FL's instrument tracks, every instrument on one, the Channel rack hidden
    // round 190oq (issue #246): the rack is hidden when the mode comes on but opens again (F6), as FL keeps an instrument
    // track's channel in its rack
    { "layout.trackOnly", TR_NOOP ("Playlist"), TR_NOOP ("Track-only mode (1 track = 1 instrument, the Channel rack hidden)"),
      TR_NOOP ("In the Pattern layout every instrument you add gets its own instrument track, whose pencil places that instrument's own pattern. The Channel rack is hidden when the mode comes on; F6 still opens it, listing the same instruments, each playing through its track's mixer track. Also in View › Layout style."),
      "ui-options.txt:trackOnly" },
    { "pianoRoll.songBarsRuler", TR_NOOP ("Piano Roll"), TR_NOOP ("Ruler: song bars (LYNX)"),
      TR_NOOP ("The Piano roll's ruler shows the bar numbers of the place in the song where the pattern plays. Also in the Piano roll's View menu."),
      "ui-options.txt:prSongBars" },
    // round 190on (research190oe P10): FL's event editor shows one lane; Live 12's clip editor several
    { "pianoRoll.stackedLanes", TR_NOOP ("Piano Roll"), TR_NOOP ("Stacked controller lanes (LYNX)"),
      TR_NOOP ("A \"+\" beside the event lane's chooser adds lanes above it (up to five in all: velocity, pan, mod X, any pattern event lane...), each with its own chooser and \"×\"; drag the line between two lanes to share their height. Always offered in the Clip-launch layout. Also in the Piano roll's View menu."),
      "ui-options.txt:prStackedLanesExtra" },
    // round 190iu (issue #191.1): FL's tool keys only select a tool
    { "editing.tempTools", TR_NOOP ("Editing"), TR_NOOP ("Hold a tool key for a temporary tool"),
      TR_NOOP ("In the Playlist and the Piano roll, holding a tool's key (C for the Knife, D for the Eraser...) uses that tool until the key is let go; a quick tap still picks the tool. Also in the Playlist menu."),
      "ui-options.txt:tempTools" },
    { "general.hoverValues", TR_NOOP ("Editing"), TR_NOOP ("Show a control's value next to the mouse when hovering over it (LYNX)"),
      TR_NOOP ("The value of the knob or fader under the mouse shows beside the cursor, not only in the hint bar. Also on the General page."),
      "ui-options.txt:hoverValues" },
    // round 190lo: the polyphonic note editor's clips with edits get their notes' masks built on a background thread at open
    { "polyEdit.prepareOnOpen", TR_NOOP ("Editing"), TR_NOOP ("Prepare edited clips when a project opens (LYNX)"),
      TR_NOOP ("When a project opens, the audio clips whose notes were edited in Edit notes (polyphonic) are prepared in the background, so the editor opens ready to play a dragged note. Also in that editor's Options menu."),
      "ui-options.txt:polyPrepareOnOpen" },
    // round 190jz (issue #195): REAPER's key for FL's Home; FL leaves plain W free in its main windows
    { "transport.wToStart", TR_NOOP ("Transport"), TR_NOOP ("W moves the playback marker to the start (as Home)"),
      TR_NOOP ("The REAPER key for going back to the start of the song or pattern, beside FL's Home; the typing keyboard keeps W while it is on. Also in the Stop button's right-click menu."),
      "ui-options.txt:wToStart" },
    // round 190kx: BandLab fits its loops to the project's tempo and key; on by default in the Simple layout only
    { "simple.loopDetect", TR_NOOP ("Simple workflow"), TR_NOOP ("Dropped loops take the tempo and key found in their audio"),
      TR_NOOP ("In the Simple layout a loop whose file and name don't say its tempo or key is analysed when it is dropped: a loop of whole beats keeps the project's tempo and a key it finds follows the project key, as BandLab's loops do. On by default. Also in the Browser options menu of the Simple layout."),
      "ui-options.txt:simpleLoopDetect" },
    // round 190oo (design review 2, O-4 / CS-28): BandLab names the project above its transport
    { "simple.projectName", TR_NOOP ("Simple workflow"), TR_NOOP ("Project name at the top (BandLab)"),
      TR_NOOP ("In the Simple layout the project's name shows centred at the top of the window, over the transport, as BandLab shows it. Also in the View menu of the Simple layout."),
      "ui-options.txt:simpleProjectName" },
    { "record.lastLoopPassPlays", TR_NOOP ("Recording"), TR_NOOP ("After loop recording, only the last pass plays"),
      TR_NOOP ("The earlier passes stay under it, muted, to pick from. Also in the recording options menu."),
      "ui-options.txt:recordLastPassPlays" },
    { "detail.windowKey", TR_NOOP ("Device view"), TR_NOOP ("W shows or hides the selected device's window"),
      TR_NOOP ("A key for the selected device's own window. Also in the Device view's options menu."),
      "ui-options.txt:detailExtraWindowKey" },
    { "detail.selectInMixer", TR_NOOP ("Device view"), TR_NOOP ("Selecting a device selects its insert in the Mixer"),
      TR_NOOP ("The Mixer follows the device picked in the Device view. Also in the Device view's options menu."),
      "ui-options.txt:detailExtraSelectInMixer" },
    // round 190ng (issue #217): FL 2026 closes the focused window on Esc; the one LYNX extra that is on by default as FL is
    { "general.escCloses", TR_NOOP ("Windows"), TR_NOOP ("Esc closes the focused window"),
      TR_NOOP ("As in FL Studio, on by default: Esc closes the focused Playlist, Channel rack, Piano roll or Mixer (Pattern layout) and plugin and tool windows. While the Playlist or the Piano roll has a selection, the first Esc clears it. Off: Esc leaves the windows open. Also on the General page."),
      "ui-options.txt:escClosesWindows" },
    // round 190nq (issue #227): FL keeps the song position when the Select tool clicks the ruler; this makes every tool scrub
    { "playlist.rulerSeeksAnyTool", TR_NOOP ("Playlist"), TR_NOOP ("A click on the ruler moves the song position with every tool"),
      TR_NOOP ("In FL Studio a click on the Playlist's or Piano roll's ruler with the Select or Zoom tool selects or zooms a time range and leaves the song position. On: those tools move the song position too, like the others; Ctrl+drag, a double-click drag or a right drag still select a range. Off by default. Also in the Playlist ruler's right-click menu."),
      "ui-options.txt:rulerSeeksAnyTool" },
    // round 190ol (research190oe P1, owner decision O-1): the Pattern layout's Sections lane (the Simple layout shows it once
    // the song has a section; the Clip-launch layout has none)
    { "playlist.sectionsLane", TR_NOOP ("Playlist"), TR_NOOP ("Sections lane"),
      TR_NOOP ("A lane under the Playlist's ruler with named song sections (Intro, Verse, Chorus...): drag one to move it with every clip, automation and marker under it, Ctrl+drag to copy it, drop it onto another to swap them. FL Studio has only time markers; an .flp export writes a marker at each section's start. Also in the Playlist menu › View."),
      "ui-options.txt:sectionsLane" },
    // round 190pf (research190oe P13): Cubase's / Logic's / Studio Pro's chord track (FL and Live have none)
    { "playlist.chordLane", TR_NOOP ("Playlist"), TR_NOOP ("Chord lane"),
      TR_NOOP ("A lane under the Playlist's ruler with the song's chords: draw them, pick them from the key's degree chords or detect them from the notes or an audio clip; channels can follow them while the song plays, and the Chord pad can play them. FL Studio has no chord track; an .flp export writes a marker at each chord's start. Also in the Playlist menu › View."),
      "ui-options.txt:chordLane" },
    { "pianoRoll.chordHighlight", TR_NOOP ("Piano Roll"), TR_NOOP ("Highlight the Chord lane's chord (LYNX)"),
      TR_NOOP ("The Piano roll's rows light the notes of the Chord lane's chord at the song position instead of the scale, and switch as the song plays. Also in the Piano roll's View menu."),
      "ui-options.txt:prChordHighlight" },
    // round 190oo (design review O-1, issue #230): FL 2026 draws only the fade curve; Live (and #230's picture) shade
    { "playlist.shadeFadedAudio", TR_NOOP ("Playlist"), TR_NOOP ("Shade faded audio (LYNX)"),
      TR_NOOP ("The Pattern layout also darkens the part of an audio clip that a fade takes away, as the Clip-launch layout and Live do; FL Studio draws only the fade curve. Also in Playlist › View."),
      "ui-options.txt:shadeFadedAudio" },
    // round 190oo (CS O-3): Live folds a track's automation lanes; LYNX's Clip-launch arrangement shows them open
    { "playlist.clipLanesFolded", TR_NOOP ("Playlist"), TR_NOOP ("Automation lanes start folded when a project opens (Clip-launch layout)"),
      TR_NOOP ("As in Live, a project opened or imported shows its tracks' automation lanes folded the first time the Clip-launch arrangement shows it; each track's lane arrow unfolds them. Also in the Playlist menu of the Clip-launch layout."),
      "ui-options.txt:clipLanesFoldedLive" },
    // round 190oo (O-5): Live 12's scale switch, root and mode instead of the one key box
    { "controlBar.liveScale", TR_NOOP ("Clip-launch workflow"), TR_NOOP ("Live's scale control: switch, root and mode (LYNX)"),
      TR_NOOP ("The Clip-launch control bar shows Live's three scale controls instead of the one key box: the scale switch turns the project key on or off, the root and the mode choose it, and both stay dimmed while the switch is off. A right-click still opens the Key menu. Also in the Key menu of the Clip-launch layout."),
      "ui-options.txt:liveScaleControl" },
    // round 190op (research190oe P4): Cubase / Studio Pro hide and disable tracks; FL has neither
    { "playlist.trackStates", TR_NOOP ("Playlist"), TR_NOOP ("Hide and deactivate tracks"),
      TR_NOOP ("The Playlist track menu gets Hide track, the Playlist menu Show hidden tracks and Hide unused tracks, and the Channel rack and Mixer menus Deactivate: a deactivated channel or insert unloads its plugins and is silent, its settings kept. Also always on in the Clip-launch and Simple layouts."),
      "ui-options.txt:trackStates" },
    // round 190pe (research190oe P20): Cubase's Track Versions, Pro Tools' playlists; FL has none
    { "playlist.trackVersions", TR_NOOP ("Playlist"), TR_NOOP ("Track versions in the Playlist track menu"),
      TR_NOOP ("The Playlist track menu gets Track versions: named alternate versions of a track's clips, automation lanes and own insert's effects, switched from the menu or the version tag on the header; Promote copies the time selection of another version into the one that plays. Also always on in the Clip-launch layout, and for a track that has versions."),
      "ui-options.txt:trackVersions" },
    // round 190oq (issue #249): FL's Alt+M only mutes; this makes it switch both ways
    { "editing.muteKeyToggles", TR_NOOP ("Editing"), TR_NOOP ("Alt+M toggles: pressed again, it unmutes (LYNX)"),
      TR_NOOP ("In FL Studio Alt+M only mutes the selected clips or notes and Shift+Alt+M unmutes them. On: Alt+M on clips or notes that are all muted already unmutes them, in the Playlist and the Piano roll; Shift+Alt+M still unmutes. Also in the Playlist's Edit menu."),
      "ui-options.txt:muteKeyToggles" },
    // round 190oq (issue #252): a start page with the recent projects and templates (Bitwig's Dashboard, Studio One's Start page)
    { "general.startPage", TR_NOOP ("Windows"), TR_NOOP ("Show the start page when LYNX starts (recent projects and templates)"),
      TR_NOOP ("At start-up a page lists the recent projects (with a search box) and the templates over the start-up project, which stays open behind it (Esc keeps it). Not on the first start, which shows the welcome card, nor when a project is opened from a file. Also in File › Start page..."),
      "ui-options.txt:startPage" },
    // round 190pa (research190oe P14): REAPER's razor edits; FL's Playlist has no range on some tracks only
    { "playlist.rangeEdits", TR_NOOP ("Playlist"), TR_NOOP ("Range edits on chosen tracks (Ctrl+Alt+drag)"),
      TR_NOOP ("Ctrl+Alt+drag in the Playlist draws a time range over only the tracks (or the one automation lane) it crosses: Delete, Cut, Copy, Paste, Duplicate, Split and Render to audio then act on just that part, clips and automation lanes, and a drag inside it moves it. The other tracks stay as they are. Also always on in the Clip-launch layout's Arrangement."),
      "ui-options.txt:rangeEdits" },
    { "midi.gamepad", TR_NOOP ("Controllers"), TR_NOOP ("Use a game controller"),
      TR_NOOP ("A game controller plays the transport and can be linked to any control like a MIDI controller. Also on the MIDI page."),
      "midi-settings.txt:gamepad" },
};
} // namespace lynx::extras
