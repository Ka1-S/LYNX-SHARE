# REA round 190ee — Gross Beat MIDI slots and Smooth points, Pitcher's MIDI port, Edison window items

Static study of FL Studio 2026's own plugin DLLs (`Gross Beat_x64.dll` sha256 9025d938…8f1a, `Edison_x64.dll`
928d4487…fe89bb) with REA / Ghidra 12.1.4, their form resources and Delphi method tables; Pitcher from rounds 16 / 19 /
30. FL was not started. Clean-room: behaviour in our words, no Image-Line code or content. The full fact list with
addresses is in the round's work folder (`/mnt/e/lynx-work/fled190ee/FL-FACTS.md`).

## 1. Gross Beat: "Trigger slots from MIDI notes"

- The first item of the plugin's Options menu (the small button left of HOLD) switches a flag that FL 2026 saves as
  one byte after the 72 slots of a version-9 state. Older states (all demo projects, all presets, Default.fst) load
  with it on; a bare instance has it off.
- Note ons select slots exactly as a click in the slot grids does: time slots on keys 60…95 (C5…B7), volume slots on
  24…59 (C2…B4). A Key-held slot remembers the slot it replaced and goes back to the last remembered slot when its key
  is released. Releases of notes never seen are ignored. Notes reach the plugin through its wrapper's MIDI input port
  (a MIDI Out channel on that port).
- LYNX: `grossbeat::State::midiTrigger` (read / written in FL's layout: version 8 bytes are kept as they were while
  the switch is on), `grossbeat::slotForKey`, `grossbeat::MidiSlots` (FL's key-held stack), the effect applies the
  notes at the next block (FL handles them on its editor's thread). The port is the slot's `plugin.midiInPort`, saved
  in .lynx (`midiInPort` on the effect). UI: the Options button (FL's place) with the switch and "MIDI input port";
  the effect window's menu has "MIDI input port" too.

## 2. Gross Beat: Smooth points

- Gross Beat's point menu has FL's thirteen curve modes with Hold first and Smooth second. Its Smooth preparation and
  players are the shared envelope library's (operation by operation the same as FLEngine's, round 190bw).
- LYNX: the engine prepares type 10 with `flcurve::prepareSmooth` and plays it with the same Hermite law; before, it
  played Smooth as a straight line. The point menu lists the modes in Gross Beat's order with Smooth.
- Old projects: no LYNX bank slot and no selected slot of FL's demo states uses Smooth, so they sound the same; a
  state that has Smooth points now plays them as FL does (a fix).

## 3. Pitcher: MIDI port

- FL's Pitcher takes MIDI mode / Harmonize notes from the MIDI Out channels on its own MIDI port (raw parameter 33,
  port = int(256 v) − 1). LYNX gave it the notes held on any Channel Rack channel (round 173d).
- LYNX: parameters 42 "MIDI port" (FL's raw value, read from the .flp) and 43 "Port routing" (on for new and imported
  Pitchers: the port's notes only; off for projects saved before this round: the Channel Rack's held notes, unchanged).

## 4. Edison window

- Snap: FL's Snap menu (grid, regions, samples, zero-crossing; keys 1–4) and the toolbar Snap button that switches
  snapping and names the mode. LYNX's grid is its ruler's grid; FL's own grid step law and "Snap to pitch period" are
  not done.
- Region trigger notes: Region menu › Trigger note (Default / none, Last hit, 128 notes in octave columns with General
  MIDI drum names), Regions › "Assign trigger notes to all..." (region after region, "Cancel all"). LYNX keeps a
  region's note in the WAV in a chunk of its own (`lxkn`, written only when a note is set). Last hit = the key played
  last on a MIDI input or the typing piano.
- Drag button: drags the sample or the selection to the Playlist / Channel Rack (as a Browser file); right-click copies
  it to the editor clipboard ("Sent to clipboard").
- Sample properties (FL's F2 window): layout read (FL-FACTS E10), not built — it needs new document fields (comments,
  tempo, root note / range / fine tune) and their WAV chunks; a round of its own.

## 5. Best way?

- **FL:** as above (REA proof per item).
- **Ableton:** Live has no slot-switching effect; the nearest is clip launching by MIDI keys (Session view key
  mapping). Live's Simpler slices answer one key each from C1 up — the same idea as Edison's trigger notes, assigned
  automatically. Live's Sample panel has no snap mode list; Warp markers snap to transients.
- **Users:** not gathered this round.
- **Verdict:** FL-exact defaults. No alternative proposed.
