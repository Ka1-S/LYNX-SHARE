# Round 187c: note Chance, Velocity Range, probability groups and release velocity (GAPMAP item NC)

Tags as in ALS_FORMAT.md: [MAN] Live 12 manual, [LIVE] a guarded Live 12.4.16 session (2026-11-09 08:09–08:16),
[DATA] a Live-saved set, [CODE] LYNX source. No Live code, art or presets were used.

## 1. What Live does
- [MAN, "Editing MIDI"] "Note probability determines the likelihood of a MIDI note being triggered during clip
  playback." Velocity Deviation: "Velocity values are then chosen randomly from within the specified range each time a
  note is played"; +20 on velocity 60 plays between 60 and 80; negative ranges go down. Groups: "Play All: all notes are
  played with the probability value set with a probability marker"; "Play One: only one note in the group is played at
  a time … The note which plays is selected at random." Release velocity "is only supported by certain devices".
- [DATA, live/nc-saved Project/nc-saved.als] `MidiNoteEvent` attributes in this order: Time Duration Velocity
  VelocityDeviation OffVelocity Probability IsEnabled NoteId; Live 12 leaves out VelocityDeviation 0 and Probability 1.
  Groups: `Notes/NoteProbabilityGroups/NoteProbabilityGroup Id=k { GroupType Value (0 Play All, 1 Play One),
  Notes/NoteId NoteId=…, Probability Value (the group's), GroupId Value }`, then `ProbabilityGroupIdGenerator/NextId`.
  The notes keep their own Probability inside a group. NoteIds are renumbered key track by key track on save.
- [LIVE] A new group starts at 100 % (notes at 25–90 % grouped → "Probability: 100 % - Group: Play One"); Ctrl+G repeats
  the last group type. Lanes: "Show Lane: Velocity / Release Velocity / Chance", Velocity only by default. A range is a
  shaded box over the note's length in the velocity lane; notes under 100 % get a small top-left triangle.

## 2. What LYNX does now [CODE]
- `Note::chance`, `velocityRange` (−1..1 = deviation / 127), `chanceGroup`, `chanceOne`; `Song::chanceSeed`.
- core/NoteChance.h: a stateless hash of (seed, clip and pass, channel/key/start, loop pass) decides each occurrence, so
  the audio thread keeps no state, renders are reproducible sample for sample, an occurrence's "on" and "off" agree for
  any block size, and every loop pass draws again. Notes without chance take a fast path: bit-identical to before.
- Schedulers: pattern loop, Playlist clips (each clip and each pass of a long clip draws on its own), clip launcher and
  performance passes, the "play truncated notes" chase.
- Release velocity: already sent with the note's "off" to hosted plugins (FL's "Send note release velocity"), Patcher and
  VFX voices; now also editable in the Piano roll lane box and kept through .als (OffVelocity).
- .lynx: note arrays gain [chance, range, group, play one] after the group field; `chanceSeed` at the root.
- .als: Probability / VelocityDeviation / NoteProbabilityGroups both ways (groups: each pass of a looped clip its own).
- MIDI export: the realised notes (the first pass as it plays); notes without chance unchanged.
- UI: the Piano roll lane box lists FL's note properties (Velocity, Pan, Fine pitch, Release, Mod X, Mod Y: FL's
  "Control" list, FLEngine strings "Note release", "Note modulation X/Y") then "Chance (LYNX)" and "Velocity range
  (LYNX)"; Tools › Chance (LYNX): group play all / play one, ungroup, reset, new chance seed; Note inspector rows.

## 3. Best way?
- **FL:** no chance, no velocity range (IL forum "please add CHANCE" threads); release velocity per note in Note
  properties and the lane box.
- **Ableton:** as §1. Bitwig 6 has probability too.
- **Users:** Live 12's note tools are rated the core of the release (MusicTech, SOS); FL users ask for chance.
- **Verdict:** Live semantics (ALX) drawn in FL's lane box (FL workflow default unchanged: nothing changes until a note
  is given a chance). LYNX's draws are seeded, so what plays is what renders; "New chance seed" draws again. Live draws
  fresh every play; offered as an option later if users ask.

## 4. Open
- Live's Chance Randomize (± % around each note) and the velocity Ramp: for the MIDI Tools round (MT).
- A note's own chance kept under a group (LYNX's group shares one chance; ungrouping keeps the group's).
- No Live render compared (the statistics are LYNX's tests: 10 000 passes, ±4 σ).
