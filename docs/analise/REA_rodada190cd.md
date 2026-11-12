# Round 190cd: the audio editor as a mixer-slot plugin (FL's Edison)

Date: 2026-11-11. Area: FL parity, item 14 of the 0.1.27 "what LYNX lacks" list and the coverage areas "FL sample
editor / Edison window" and "FL Edison tool dialogs".

Source: FL Studio 2026's own Edison effect plugin on this machine (`Plugins/Fruity/Effects/Edison/Edison_x64.dll`),
read statically only: its form resources, pseudocode of a handful of procedures, and the plugin's saved state in three
of FL's demo projects (read as bytes, nothing copied). Everything below is written in our own words; no FL code,
content, preset or sample is in the repository. Tags: **[DFM]** form resource, **[DECOMP]** pseudocode, **[BYTES]**
state bytes of a demo project, **[INFERRED]** our reading, to be checked live.

## 1. What FL has

### 1.1 The plugin
Edison is an effect: it is loaded in a mixer insert slot, the insert's audio passes through it, and its sample is saved
inside the project with the slot. Several instances can be loaded. FL's "open the audio editor" commands of a mixer
track load one in the track's first empty slot.

### 1.2 Toolbar [DFM, the editor form]
- Record mode list ("When / what to record"), four items in this order: **Now**, **On input**, **Input**, **On play**.
  The form's default and the constructor both select the second, On input.
- Maximum recording time: Forever, 1, 3, 5, 10, 30, 60 minutes, then sizes in megabytes. The saved value is an index
  into the time list; a new instance starts at the fourth entry, 5 minutes [DECOMP].
- APPEND switch, on in a new instance.
- Slave playback to host (key H), Loop (L), Play (Space), Scrub, Stop, Record (R), Mute input (I), auto scroll (A),
  "send to playlist / to channel", and a drag source for the sample or selection.
- Status texts while Record is on: recording / waiting for audio input / waiting for project playback. A marker
  named "Song jump".
- Settings: which piano keys preview what; for slave playback, "Ignore host selection" and "Ignore own selection".

### 1.3 The record modes [DECOMP, the plugin's render procedure]
Times follow the sample rate: a hold of 1.0 s and a pre-roll of 0.1 s (the buffer is trimmed in steps of 0.2 s).

| Mode | When blocks reach the take |
|---|---|
| Now | every block from the moment Record is switched on |
| On input | from the first block whose peak is **above** the noise threshold; then it never stops by itself |
| Input | while input is heard: each block above the threshold restarts the 1 s hold; in silence the hold runs down and the recording pauses; the next sound resumes it |
| On play | only while the host plays; stopped = nothing written |

- The threshold is the editor's noise threshold (linear peak, 0.02 in a new instance, saved with the state): the same
  value the "acquire noise threshold" command of the editor measures.
- While nothing has been heard (or Input's hold ran out) only the last 0.1 s is kept, so a take starts with the 0.1 s
  before the sound.
- On play: when the host reports that the song position jumped, the next written block gets a marker "Song jump" at
  that place of the take.
- Stopping On input / Input before anything was heard leaves no take.

### 1.4 The audio path [DECOMP]
- Mute input off: the output is the input, untouched. On: the output starts silent.
- The plugin's own playback is then **added** to the output.
- Slave playback: while the host plays, the play position is the song time (minus the host selection's start unless
  "Ignore host selection") plus the plugin's own selection start (unless "Ignore own selection" or no selection). A
  difference of more than a few dozen frames re-seeks.

### 1.5 The saved state [DECOMP + BYTES]
In the slot's plugin-state record: a version word (13), 12 bytes, a 64-bit size followed by a complete RIFF/WAVE image
of the sample (with its cue / loop / tempo chunks), the sample's name, then selection start and end (doubles, −1 =
none), two view values, the noise threshold (float), the editor's envelopes, and last 20 bytes of switches: record
mode, "new project" (Append off), loop, mute input, no auto scroll, the maximum-time index, the key link, slave,
ignore host selection, ignore own selection. Older versions have a shorter tail (8 or 12 bytes) and a 32-bit size.

Three of FL's demo projects have Edison slots; one holds 94 KB of audio. All three parse with this layout.

### 1.6 Tools menu
FL's Tools › Macros has "Remove all Edison instances", between "Reset empty playlist tracks" and "Delete unused files
and save project" (main-menu evidence of round 190bc, row mainmenu_toolbar-0262).

## 2. What LYNX does now

- **A slot effect** (`EffectKind::edison`, `core/EdisonFx.*`): in the effect list, loadable in any insert slot, several
  per track. Its window is LYNX's existing audio editor (`ui/AudioEditor.cpp`) in a "slot" mode; no second editor.
- **Record modes** Now / On input / Input / On play with FL's names, order and default; hold 1.0 s, pre-roll 0.1 s,
  threshold 0.02 until one is acquired (the editor's "Acquire noise threshold" sets the slot's value). Status texts as
  in FL. The 0.2 s trimming step is not reproduced: the pre-roll is exactly the last 0.1 s.
- **Maximum recording time**: the seven time choices, default 5 minutes. The megabyte choices are left out.
- **Append**: on = the take goes after the audio already there; off = it replaces it.
- **Pass-through** bit-exact when idle and while recording; **Mute input** silences the slot; playback (Play, Loop) is
  added into the slot, so it goes through the effects below and the track's fader.
- **Slave playback**: while the song plays, the content plays at the song time, from the editor's selection start;
  "Ignore own selection" in the Record menu. The host's selection is not used ("Ignore host selection" is not offered).
- **On play** puts a "Song jump" marker where the song looped or was moved, so a loop recording comes out as one
  take with a marker per pass.
- Recording goes on with the window closed and survives closing and reopening the window.
- **The audio belongs to the project**: after a take, when the window closes and before every save it is written as a
  32-bit float WAV (markers, regions and loop in it) into the project's recordings folder (`Audio/Recorded`, the rule
  of takes), and the slot keeps the path. Such files take part in the "unused recordings" prompt and in the move of
  recordings when an unsaved project is first saved. Superseded files are not deleted by the plugin.
- **.flp import**: an Edison slot stays an Edison slot with its mode, Append, loop, mute input, maximum time, slave,
  ignore-own-selection and noise threshold; its audio is decoded from the WAV image in FL's state, with markers,
  regions and loop. FL's bytes stay with the slot until the audio is changed in LYNX.
- **Commands**: the mixer track menu's "Open audio editor" (Ctrl+E) and "…and record" (Shift+E) load the plugin in the
  track's first empty slot and open it (a full track falls back to the plain editor window, as before). "Send to
  playlist", "Send to the selected channel" and the other Send items work from the slot window. Tools › Macros ›
  "Remove all Edison instances" in FL's place.
- Editor commands added in the same round, for every audio editor window: FL's Select menu (before / after the
  selection, the zoomed part, previous / next region), Zoom on left / right of selection, Rename all…, Set info…,
  Clone as region, and View switches for regions, the loop and a noise-threshold mark.

## 3. Best way?

- **FL**: the editor *is* a plugin. Strong points: it records exactly what the insert carries (post-effects of the
  slots above), it can be left armed in "On input" while playing an instrument, and the audio travels with the project.
  Weak points users complain about: the audio is embedded in the .flp (projects grow and autosaves get slow), and a
  recording in an instance whose window is closed is easy to forget.
- **Ableton Live**: no editor plugin. Recording an insert means resampling to another track; destructive editing is
  delegated to an external editor. Simpler model, but no "capture what I just played on this insert" tool.
- **Users**: not researched again this round (no web survey was run). From general knowledge, unverified: "On input"
  recording and sending the result straight to the Playlist are the liked parts; project size and takes lost because
  they were never saved to a file are the disliked ones. Treat this line as an assumption, not as evidence.
- **Verdict**: FL's model, with one deliberate difference: the audio is a file in the project folder (the same rule as
  recorded takes) instead of bytes inside the project file. It keeps FL's workflow (the slot owns its audio, saved and
  reopened with the project, several instances), keeps .lynx files and undo snapshots small, and gives every take a
  file the user can find. Imported FL slots keep FL's embedded bytes until edited, so nothing is lost on import.
  Option left open for the owner: also offer "embed in the project" as a per-project choice.

## 4. Exact / approximate / left out

Exact (static evidence): the four modes and their rules, defaults, hold and pre-roll, threshold default, Append /
Mute input / Slave / Loop / maximum-time meaning, the "Song jump" marker, pass-through and "playback is added", the
state layout read on import.

Approximate: slave playback uses the song time in seconds at the current tempo; playback at a rate other than the
device's uses 4-point interpolation; the pre-roll is not trimmed in 0.2 s steps.

Left out: megabyte limits, "Ignore host selection", the piano-key preview link, scrub, auto-scroll switch as a saved
parameter, writing an FL state on .flp export for slots made in LYNX (imported slots export their original bytes).

## 5. Needs a live check

1. Append off: does FL clear the sample when Record is pressed, or when the first block is written?
2. Append on: does the take land at the end, or at the cursor / selection?
3. Slave playback: is the song time taken in seconds, or tempo-synced to the sample's own tempo?
4. Input mode: does FL put a marker where the recording resumed?
5. Which mode FL arms from the mixer's "open audio editor and record" (LYNX uses the slot's saved mode).
6. How a non-empty sample name is stored in the state (the demos have empty names; LYNX then skips the selection /
   threshold block and keeps defaults).
7. Does "Remove all Edison instances" ask before removing? (LYNX: one undo step, no question.)
