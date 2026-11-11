# Round 190ay — Freeze with true unload (LYNX's own feature)

Cross-DAW research item 9 / F29 (`research190ak`): a freeze that leaves the plugin loaded saves little. LYNX's existing
"Render to audio (freeze)..." on a mixer insert and "Render to audio" on a channel bounce to a new audio clip and leave
the instrument where it is. This round adds **Freeze / Unfreeze** of an instrument channel that really unloads it.
FL has no such command, so nothing of FL's is changed or removed (the bounce commands stay as they are).

## 1. What it does
- Channel rack, right-click a channel: **Freeze** (with several selected: "Freeze selected channels"), on a frozen
  channel **Unfreeze** and **Re-freeze**. In the Ableton-style layout the same three items are on the clip grid's
  track header (right-click).
- Freeze renders the channel alone, before the mixer (channel volume, pan and its insert's effects stay live), at the
  quality heard while playing, to 32-bit float WAV files in the project's data folder (`<project>/Audio/Frozen`):
  one file for the whole song (what Song mode plays) and one loop pass per pattern the channel plays in (what Pattern
  mode plays).
- While frozen the engine plays those files in place of the instrument. The instrument is **not built at all**: a
  hosted plugin instance is released, a built-in engine is not created. The channel's definition (plugin, state,
  settings, notes) is untouched in the song, so Unfreeze brings back exactly what was there and deletes the files.
- A frozen channel shows a snowflake tag and a dimmed name plate. Editing its notes (step grid, Piano roll) or
  opening its plugin / settings is refused with the hint "... is frozen: Unfreeze to edit".
- A tempo change, a note edit or an arrangement change does not re-render anything: the tag turns amber, the hint
  explains that the audio made before still plays, and Re-freeze updates it.
- Frozen audio that is not on disk: the instrument plays instead (never silence) and the hint says so.

## 2. How (reuse, not a second renderer)
Freeze is built on Online Sessions' stand-ins (round 168a, `core/StandIns.h`): the same render songs
(`channelTimelineRender`, `channelPatternRender`), the same projection (`standins::project`) that swaps a channel for
an audio-clip channel in the song the audio engine plays. New: `ChannelDef::freeze` (`FreezeState`: the stems and a
hash of what they were rendered from), `lynx::freeze` (rules: `whyNot`, `apply`, `clear`, `thawed`, `lighten`),
`sessionpack::renderFreeze` / `freezeSource`, `AudioEngine::freezeOutOfDate` / `frozenAudioMissing`.
- The render song is "lightened": every other channel becomes a muted default synth and every effect slot an idle
  one, so freezing one channel does not load the whole project's plugins a second time.
- What drives the instrument's own parameters (automation, pattern events, modulators) is in the stem, so the
  projection detaches those from the stand-in (a pitch bend would otherwise bend the audio a second time).
- `.lynx`: a `"freeze"` object on the channel, written only while frozen. A project with nothing frozen saves byte
  for byte as before; an older LYNX ignores the field and plays the instrument. `.flp` export writes the unfrozen
  project (`freeze::thawed`).

## 3. Refused, with the reason shown
Audio clips, channels with no sound of their own (controllers, MIDI Out, Layer...), a plugin that is not loaded or not
installed, plugins / Patchers with outputs on other inserts, children of a Layer, Performance mode, channels that play
in clips of the clip grid, instruments whose knobs a controller link moves live.

## 4. Best way?
- **FL:** no freeze. Its answers are "Render to audio" / "Consolidate" (a new audio clip, the instrument stays
  loaded) and Smart disable. Kept as they are.
- **Ableton:** Freeze Track renders the track with its devices and unloads them; edits are blocked until Unfreeze;
  Flatten makes it permanent. The frozen audio is what was heard (playback quality).
- **Logic / Reaper / Pro Tools:** Logic's frozen track uses no plugin CPU; Reaper can freeze up to a chosen effect;
  Pro Tools has Commit.
- **Users:** "freeze must switch the plugins off, or it saves little" (F29).
- **Verdict:** Ableton's model for the instrument (render, unload, block edits, exact undo) with Logic's "source
  only" split: the insert's effects stay live, so mixing goes on while the instrument is unloaded. Never an automatic
  re-render: out of date is shown and Re-freeze is one click. Freezing an insert with its effects (Reaper's "up to
  here") is the next stage.

## 5. Decisions taken (alternatives)
- **Playback quality** for the frozen audio (alternative: export quality "HQ for all plugins"). The frozen channel
  then sounds exactly as it did live; an export of a frozen project uses that same audio, so instruments with a
  separate render quality (Harmless, Sytrus...) are exported at playback quality while frozen.
- **One whole-song file** in Song mode (alternative: one file per pattern, placed at each clip, which would follow
  arrangement edits but is not exact for overlapping tails or free-running synths).
- **Own field** `freeze` (alternative: the `standIns` list, which an Online Session's apply would overwrite).
- **Edits blocked in the note editors** (alternative: one guard on every song edit); other edits that change the
  result show "out of date".

## 6. Measured (2026-11-11)
- Core tests (`tests/Freeze190ayTests.inc`, 4 tests, 52 checks): frozen vs live, Song mode, through a live delay +
  reverb: -133.7 dB whole song, -130.7 dB in the tail after the last note, -133.7 dB over a repeated pattern; the
  channel alone -128.7 dB; two frozen channels -130.2 dB; Pattern mode from the second pass -139.6 dB outside the
  first 10 ticks of a pass.
- `LYNX.exe --freezetest` (Windows build, real engine, files and export): demo song, 4 channels frozen: -129.1 dB;
  Unfreeze: bit-identical mix (-200 dB) and the same saved project byte for byte.
- FL's demo "Thief" (37 channels, 5 instruments frozen): the four 3x Osc / synth channels null at -94.5 dB.

## 7. Known limits
- **Harmless does not null** (Thief's "Synth Bass": frozen vs live +1 dB from the first note after a 41 s rest). Not
  the freeze: LYNX's Harmless renders differently after a rest depending on the block size and the channel's volume
  (the same song rendered in blocks of 480 and of 512 frames differs by +1.7 dB from that note on; the same block
  size twice is bit-identical). Its plugin-wide phases and counters only run while the channel is processed, and how
  long it stays awake after a note depends on level and block partition. A frozen Harmless is one valid rendition; the
  engine needs its own fix (reproducer: `/mnt/e/lynx-work/freeze190ay/harness`, `MINI_DIAG=1`).
- **Pattern mode, loop seam**: 3 ticks after each loop start the previous pass's audio-clip voice adds its held last
  value, fading out over 3 ms (-24.6 dB re peak in the test song, on sustained material). Same in Online Sessions' loop
  stems. The first pass of a loop already carries the previous pass's tail.
- An export of a frozen project uses the frozen audio (playback quality, the channel's Max polyphony), also when
  "HQ for all plugins" / "Disable max polyphony" are on, and also when the freeze is out of date.
- A stem's tail is capped at 4 bars and cut at the song's end; undoing a Freeze leaves its files on disk.
- Memory: the frozen audio is memory-mapped (about 84 MB of file per 4 minutes of stereo at 44.1 kHz). Freezing pays
  off for instruments heavier than that (samplers, big hosted plugins); for light built-in synths it only saves CPU.
