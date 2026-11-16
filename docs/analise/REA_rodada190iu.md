# REA round 190iu: issues #186 and #191 (workflow)

Static evidence only (no FL / Live session was driven in this round): the FL Studio online manual, FL 2026's own change
log (`Help/WhatsNew.htm`, up to 26.1.7, 2026-09-28), strings of `FLEngine_x64.dll`, earlier REA rounds, the Ableton Live 12
manual. Confidence: [BIN] binary / change log, [MAN] manual only, [INF] inferred.

## 1. Temporary tools (hold a tool key) — #191.1
- FL 2026 has **no** hold-a-tool-key switch. The shortcut page lists P B D T S C E Y Z as plain tool selectors (in the
  Piano roll S is "Slide note (toggle)") [MAN, basics_shortcuts.htm]; nothing in the change log; no string in the DLL
  [BIN]. The nearest entry: 26.1 Beta 1 "21307 Hold 'A' to temporarily switch between resizing and stretching an audio
  clip in the playlist" [BIN].
- FL's own temporary tools: right button = delete, double right-click = Mute (REA 165a), Ctrl = select, Shift = paint /
  draw (playlist.htm), right button + wheel = next / previous tool (pianoroll.htm; REA 167t decompiled), Right Shift + LMB
  slices (24.2.99 B4, 19448) [MAN/BIN].
- LYNX: a LYNX extra, off (core/TempTool.h). Hold ≥ 400 ms (below the typing auto-repeat delay) or use the mouse while
  the key is down → back to the previous tool on release; a tap selects for good; auto-repeats ignored; a second tool key
  during a hold returns to the tool from before the first; a tool picked meanwhile by a click stays.
- **Owner question:** the brief said FL has it; the evidence says it does not. A live check in FL 2026 (hold C in the
  Playlist) would settle it; until then it stays an extra, off.

## 2. MIDI files dropped — #191.2
- FL manual (automation_midiimport.htm): "Drag a MIDI file from the Browser and drop on: Channel Rack, Piano roll, FL
  Studio desktop"; Shift suppresses / Alt forces the Import MIDI data dialog. Defaults of the dialog (REA 143): Create one
  channel per track on, Realign off, Import time signatures on, Set mixer tracks for new channels on [BIN]. 26.1 RC1
  21941 fixed "MIDI import re-uses existing channels instead of creating new ones" [BIN]. A drop on the Playlist grid is
  not documented (old 20.6 bug: "Dropping a MIDI file on the picker panel creates an audio clip") [INF: needs a live check].
- Plugins: FL takes MIDI dragged out of plugins on the Piano roll and the Channel rack (21.0.99 b1 15561 MDrummer; 12.3
  Kontakt 5) [BIN]. For a host it is an ordinary file drop of a temporary .mid.
- Live 12 manual 4.10: items dragged into tracks in Session or Arrangement; into the space right of Session tracks /
  below Arrangement tracks → a new track; files from the Explorer too. 5.2: Import MIDI File at the insert marker /
  selected slot. Several clips into Session: one track by default (Ctrl: several tracks). Type 1 multi-track files: one
  track each (Ableton help article) [MAN].
- LYNX (core/MidiDrop.h): Pattern layout = LYNX extra, off; Clip-launch = Live, always. No tempo / time signature applied
  (a drop never retimes the song); the file's timing kept (no realign).

## 3. Automation lanes — #191.3
- Live 12 (automation.htm §26.5.2): Automation Mode (A) opens the main lane under each track; "Show Automation in New
  Lane", "Add Automation Lane", a toggle in the main lane hides / shows the extra lanes; U folds the track [MAN].
- FL: automation clips only. 24.1 RC1 17453: "Turn into automation clip" of an instrument track's control puts the clips
  on grouped sub-tracks under it; DLL "HideCollapsedSubTracks", "Delete instrument track|Do you want to delete associated
  automation channels?" [BIN].
- LYNX has lanes since round 177b. This round adds `laneview::clipsToLanes` (Playlist menu, while lanes are on). Not done:
  a pure view projection (the clips drawn in the owner track's lane area while staying clips): it touches the Playlist's
  layout, hit testing and drag code (PlaylistComponent, another round's file this week); FL's own grouped sub-tracks
  (24.1) are the FL-native way to get the same look and are the recommended follow-up (track groups exist in LYNX).

## 4. Patterns per track, Track-only mode — #191.5
- FL: Patterns › Split by channel ("one new pattern per channel"; needs ≥ 2 channels with data) [BIN, REA 167j]; Clip
  menu › Make unique; Picker panel "Auto group patterns/channels"; Track menu › Auto name, Lock to content [MAN]; Mixer ›
  Disk recording › "Auto name playlist tracks" (on) [BIN, REA 190fl]. No option creates a pattern per channel / track
  automatically [BIN: absence].
- FL instrument tracks (20.1 B1): "A default Pattern will also be created for Instrument tracks. The Playlist Track will
  be locked to the auto-created Pattern"; live FL 2026 (REA 151 E): the first pencil click makes an empty one-bar pattern
  named and coloured after the channel; later clicks place the same pattern [BIN/EXEC].
- LYNX Track-only mode (core/TrackOnly.h) builds on these: Pattern layout, rack hidden, every new instrument on its own
  instrument track. Point 5a ("separate patterns per track") is what instrument tracks already do; no separate
  auto-split switch was added (it would restructure patterns behind the user's back) — see the report.

## 5. Wheel — #186
- FL Piano roll and Playlist (REA 167t, decompiled): plain = vertical scroll, Ctrl = time zoom at the mouse, Shift =
  horizontal scroll, Alt = note property / track heights; right button held + wheel = tool. Over the track labels
  Shift+wheel reorders tracks (REA 190ft). No setting swaps scroll and zoom [BIN].
- Live 12: Ctrl / Cmd + wheel zooms, Shift scrolls sideways, Alt + wheel in a lane zooms it vertically [MAN].
- REAPER (the issue's screenshots): Mouse modifiers › Track control panel › mouse wheel: Default action "Scroll TCP",
  Ctrl "Adjust vertical zoom".
- LYNX: Settings › LYNX extras, off; over the headers plain = scroll, Ctrl = time zoom (LYNX keeps Alt for track
  heights, so Ctrl swaps the two wheel actions instead of REAPER's vertical zoom).

## 6. Takes and backups — #191.6
- FL Browser › Current project: History, Patterns (with Initialized controls & Automation), Effects, Generators, Remote
  control, Initialized controls, Samples; Patterns can be deleted from their menu; Initialized controls "Delete event";
  no delete on samples / recordings there [MAN, REA 115]. Clean-up: "Delete unused files and save project", "Delete
  unused recorded files", "Put undone recordings in the recycle bin", Playlist Tools › "Purge unused audio clips" [BIN].
- FL's picker panel lists patterns, audio clips (recordings) and automation clips; Picker panel › Enable (Alt+P) hides it.
- LYNX: deletes on the picker's audio / automation rows and the Browser's Current project rows, the two "unused" bulk
  items in the Picker panel menu; files on disk are never touched (LYNX's "ask about unused recordings" on save stays).
