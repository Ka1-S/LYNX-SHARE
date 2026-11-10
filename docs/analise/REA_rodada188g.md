# Round 188g: the rest of GitHub issue #63 (per-note properties)

Issue #63 ("panning of individual MIDI notes does not work") was fixed in parts: the built-in drums follow note pan and
fine pitch (0.1.24), the Arpeggiator keeps them and FL-imported sampler channels move their filter with Mod X / Y
(0.1.25), round 187c added an editable Release velocity lane. This round closes what was left: release velocity and
Mod X / Y on LYNX's own Sampler, note pan and fine pitch on VST3 (and CLAP) plugins, and round 180c's "notes 1 frame late".

Binaries (FL Studio 2026, files of 2026-10-08): `FLEngine_x64.dll` (Ghidra project ~/ghproj/r69/flea) and
`Plugins/Fruity/Generators/Fruity Wrapper/Fruity Wrapper_x64.dll` (~/ghproj/r134w/fw). Decompilations and queries:
`/mnt/e/lynx-work/notes188g/rea/` (fw-m2 … fw-m8, d1 … d11).

## 1. Mod X / Y on LYNX's own Sampler **[DECOMP FLEngine `FUN_00d265e0`; REA_rodada76 §1 / §4]**

FL's sampler voice takes the note's Mod X / Y bytes as its initial filter levels, `(b − 128) / 128`, and adds them to the
channel's filter X / Y (`Final.FCut = Init.FCut + channel`, `FUN_00d265e0`); the voice filter runs on that X / Y. LYNX's
FL-imported sampler voice already did this (round 178a). LYNX's own Sampler has its own SVF (cutoff in Hz, resonance
0…1): the note's offsets now go through FL's laws — X ↔ Hz with the 12 dB filters' law (FlpImport `channelFilterHz`,
types 1…5), Y ↔ Q with FL's biquad law and the SVF resonance `(2 − 1/Q) / 1.9` the import uses — so a note at Mod X 0.25
sounds like a centred note on a channel whose cutoff is half of FL's X range lower. A centred note is bit-identical to
before; a channel at 19.5 kHz or above counts as fully open (X = 1) and closes for a note below the centre, as in FL. A
VFX moving a sounding note's Mod X / Y (VFX Envelope, Color Mapper) reaches the same filter.

## 2. Fine pitch and pan on hosted plugins **[DECOMP Fruity Wrapper]**

The wrapper's voice (`0x879a00`, its Voice_TriggerVoice) builds the note from FL's voice levels:

- pitch `p = (Final.Pitch + 6000) · 0.01` — a float, the key plus the fine pitch in semitones;
- velocity from the note (`FPV_GetVelocity`), or from Init.Vol with "velocity link"; MIDI channel = the note's colour;
- it calls the plugin's note on (VMT slot `0x108`) with `p`, then `0x871c50`: Mod X as poly aftertouch, `(Final.FCut + 1)/2`,
  when "Send MOD X as polyphonic aftertouch" is on;
- **Final.Pan is not read.** No format gets a note's pan.

The note on per format (`0x108` of each class; VMTs at 0x743ad0 base, 0x841b90 VST3, 0x859718 CLAP):

| Format | Function | Key | Rest of the pitch |
|---|---|---|---|
| VST2 / base (MIDI) | `0x745f70` | `Round(p)` (`0x40c3c0`, `cvtsd2si`: ties to even) | lost |
| VST3 | `0x84d0e0` | `Round(p)`, clamped 0…127 | `NoteOnEvent::tuning = (p − Round(p)) · 100` cents |
| CLAP | `0x85e160` | `Trunc(p)` (`0x5bf660` → `0x40c4b0`, `cvttsd2si`) | lost; no `CLAP_EVENT_NOTE_EXPRESSION` anywhere (the only other event type it builds is 5, parameter values) |

The VST3 note on is the 0x30-byte `Event` of round 173a: noteId = the voice's id, velocity = byte / 127. Nothing in the
wrapper builds a `NoteExpressionValueEvent` (type 4): the IEventList's add is called only by note on (type 0), note off
(1) and poly pressure (3). FLEngine's own copy of the VST3 host (Patcher etc., `0xb4fef0`) is the same code.

So in FL a note at +70 cents reaches a VST3 one key up and 30 cents flat, a VST2 one key up (a semitone sharp), a CLAP on
its own key; −40 cents reaches a VST3 on its key 40 cents flat, a VST2 on its key, a CLAP one key down.

The note off (`0x84cfa0` VST3, `0x85e280` CLAP) rounds the same float: VST2 / VST3 end the key they started; FL's CLAP
note off rounds where its note on truncated, so a CLAP note at +50…+99 cents (or −1…−49) ends on another key than it
started (a plugin that matches note offs by key keeps it sounding; the note id still matches).

**LYNX now** (core/HostNotePitch.h, engine/PluginHost.cpp): the rack hands a hosted plugin the note's own fine pitch
(`Instrument::setNextNoteFinePitch`, the piano roll's / graph editor's value; the channel's fine tune and main pitch keep
reaching it as pitch bend, issue #14); the note goes out on FL's key for its format; a VST3 note on carries FL's tuning
(cmake/LynxPatchJuce.cmake fix 18: JUCE's VST3 host builds its events from MIDI, so the patched `MidiEventList` asks
LYNX's hook for each note on's tuning, engine/Vst3NoteTuning.h). Every note off goes to the key its note on went out on —
for CLAP too (LYNX's one difference from FL here: FL's rounding note off would leave such notes hanging in key-matching
plugins). Note pan stays unsent, as in FL.

Check: `/mnt/e/lynx-work/notes188g/tuncheck` (JUCE's real `hostToPluginEventList`, patched, and LYNX's side in a second
translation unit as PluginHost.cpp has it): +25 → key 60 tuning +25; +70 → 61 / −30; +50 on 64 → 64 / +50 (ties to even);
+150 on 64 → 66 / −50; −1200 → 55 / 0; every note off on its note on's key and id (`after.txt`, "ALL OK").

**Not verified:** whether FL's `Final.Pitch` for a wrapper voice also holds the channel's PITCH knob / fine tune (LYNX
keeps sending those as pitch bend, as before).

## 3. Release velocity on the Sampler **[DECOMP FLEngine `FUN_00d241e0`]**

FL's sampler voice ignores the note's release velocity. Its note off (`FUN_00d241e0`) releases articulators 1…4 with
the channel's envelope records only (`FUN_009cecc0 (voice art, channel + 0x198 + k·0xa0)`); the release velocity reaches
only plugin voices (the wrapper's note off, `FPV_GetRelVelocity`). LYNX's Sampler was already the same (its
`setNextNoteRelease` is the default no-op); test `notes188g_release_velocity_leaves_the_sampler_alone` keeps it so. The
release velocity reaches hosted plugins (VST2 / VST3 note off velocity), Patcher and VFX (round 187c).

## 4. "Notes 1 frame late from 2.25 s" (round 180c) **[EXEC: LYNX renders vs FL's]**

Reproduced on main (adf94fb68's train build): `sl_plain.flp` (16 Fruity Slicer slices of 11025 frames, a note every
eighth) — LYNX's notes 9…15 one frame late against FL's render, notes 0…8 exact. A plain sampler click every eighth (and
on odd ticks) lands on its exact frame (`p/clicks.py`): the scheduler is right. Playing the slices in reverse order moved
the lag with the slices (slices 9…15 late wherever they play): the slice starts were wrong, not the timing.

Cause: every slicer stores a slice start as `p / frames` (Fruity Slicer, Slicex, the auto slicer, FlpImport), but the
sampler turned it back with the last frame's index, `round((frames − 1) · start)` = `p − p/frames`, one frame early for
every slice past the sample's middle. Now `round(frames · start)`. Test `notes188g_slices_in_the_second_half_start_on_their_frame`.
