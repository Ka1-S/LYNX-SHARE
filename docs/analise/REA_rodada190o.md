# Round 190o: Ableton Live study for 0.1.26 — MIDI Tools and Scale Mode

Date: 2026-11-10. LYNX base: origin/main 9bf2f8515. Live: Live 12 Trial 12.4.16 (one guarded GUI session, 13:20–13:34,
trial box OK, quit with Ctrl+Q, Preferences untouched). Clean room: Live was driven through its own UI and its saved
set was read; nothing of Live's code, art or content is in LYNX.

Evidence (outside the repo): `/mnt/e/lynx-work/ableton190o/`
- `live/mt-test.als`: the set Live opened: 16 arrangement clips with the same 7 notes (C major chord 60/64/67 at beat 0,
  length 1, velocity 100; 62 at 1, 0.5, 80; 65 at 2, 0.5, 90; 69 at 2.5, 0.25, 70; 72 at 3, 1, 110). Grid 1/16.
- `live/mt-saved Project/mt-saved.als`: what Live saved after one tool per clip. `dumpals.py` prints the notes.
- `shots/live-*.png`: the Clip View and every tool's panel. `fit/fit.py`: the Strum curve fit.
- The gap map refresh: `/mnt/e/lynx-work/ableton182f/GAPMAP-0126.md`.

Tags: **[LIVE]** seen in Live 12.4 (UI or saved set), **[MAN]** Live 12 manual "MIDI Tools", **[BIN]** string tables
of Live's exe (round 175a's extract), **[INFERRED]** LYNX's reading.

## 1. The panels [LIVE]
Clip View, left column, below "Pitch & Time": two foldable sections, **Transform** and **Generate**. Each has a tool
chooser, a reset button, the tool's controls, an **Auto** toggle (on by default) and a **Transform** / **Generate**
button. With Auto on, every control change re-applies the tool to the notes as they were before the tool (three
changes in a row gave the result of the last settings only).

Transform chooser: Arpeggiate, Chop, Connect, Glissando (MPE), LFO (MPE), Ornament, Quantize, Recombine, Span, Strum,
Time Warp, Velocity Shaper; "User: Max MIDI Transformation". Generate chooser: Rhythm, Seed, Shape, Stacks, Euclidean;
"User: Max MIDI Generator".

| Tool | Controls and defaults [LIVE] |
|---|---|
| Arpeggiate | Style Up (18 styles [BIN]), Distance +12 st, Steps 2, Rate 1/32, Gate 100 % |
| Chop | Parts 4, Gaps 0, pattern toggles (4 shown), emphasis row, Stretch ×2, Variation 0.0 % |
| Connect | Spread 0 st, Density 100 %, Rate Grid, Tie 0.0 % |
| Ornament | Flam on: Position −20 %, Vel. 50 %. Grace notes off: Pitch High / Same / Low (Same), Position −20 %, Vel. 50 %, Chance 100 %, Amount 3 |
| Quantize | Grid, 1/4, 1/8, 1/16, 1/32, Triplets, Adjust Note Start / End (Start), Amount 100 % |
| Recombine | dimension chooser (Position), Shuffle, Mirror, Rotate bar display (−5…5 for the clip's 5 chords), Grid toggle, step −/+ |
| Span | Legato / Tenuto / Staccato (Tenuto), Offset 0.0 %, Variation 0.0 % |
| Strum | Low 0.0 %, High 0.0 %, Tension 0.0 %, a curve display with two handles |
| Time Warp | Time 1.1.1, Speed ×1.0, breakpoints 1–3 (1 and 3 on), Quantize Off, Range Fit, Note End Include |
| Velocity Shaper | envelope display (127 falling to 1), Loop 1, Rotate 0, Division 1/16 |
| Rhythm | Pitch C3, Steps 8, Pattern 14, Density 4, Step Duration 1/8, Split 0.0 %, Shift 0, velocity 100, accent 127, Freq 4, accent offset arrows |
| Seed | Pitch C3–C4, Duration 1/64–1/8, Velocity 30–100, Voices 4, Density 50 % |
| Shape | preset Up, Pitch C3–C4, Rate Grid, Tie 0.0 %, Density 100 %, Jitter 0.0 % |
| Stacks | chord pad, add / delete chord, Root C3, Inversion 0, Duration 1, Offset 0 |
| Euclidean | tabs Pattern / Voices, four voices with Rotation 0, Steps 16, Density 8, Division 1/16 |

## 2. What each tool did to the 7 notes [LIVE]
Times in beats; `time:key/length/velocity`.

- **Arpeggiate** (defaults): the chord became `0:60 .125:64 .25:67 .375:72 .5:76 .625:79 .75:84 .875:88`, each 0.125
  long; D (1–1.5) became 62, 74, 86, 62; A (2.5–2.75) became 69, 81; the last C (3–4) 72 84 96 72 84 96 72 84. So: one
  note per Rate while a note is held; the pattern is the style over the held notes, then again `Distance` higher,
  `Steps` times; each new note has the velocity of its source; a note that follows another with no gap starts the
  pattern again.
- **Connect** (defaults): the silences 1.5–2 and 2.75–3 got `1.5:63/.25/83.33 1.75:64/.25/86.67` and `2.75:71/.25/90`:
  notes of one grid step, pitch and velocity stepping evenly from the note before to the note after (k/(n+1); 70.5
  rounds to 71).
- **Ornament**, Flam −20 %, 50 %: a note 0.05 long (20 % of the grid) ending at each note's start, same pitch, half
  the velocity, OffVelocity 32 (half of 64); the notes stay. Live wrote the first chord's flams at time −0.05.
  Flam +20 %: the flam starts where the note started; the note starts 0.05 later and keeps its end.
  Grace notes High, 3: three notes of 0.05/3 before the note: one semitone above, the same pitch, above; velocity
  50 % as a fraction (127 → 63.5; 1 stays 1).
- **Recombine**, Position, rotate +1: every chord took the start of the next one, the last moved to the first place
  (`0:72 1:60/64/67 2:62 2.5:65 3:69`); lengths and velocities travel with the notes.
- **Span**: Legato made every note end at the next start (69 at 2.5 grew from 0.25 to 0.5; the last note stayed);
  Staccato made every note 0.25 long = half of the smallest distance between two starts (0.5).
- **Strum**: Low 100 % left the chord's highest note and moved the lowest by one grid step (0.249999), the middle one
  by half; lengths stayed. High 100 % is the mirror (lowest stays). Every chord of the clip is strummed. Tension:
  section 3.
- **Chop** (defaults): every note became 4 equal notes.
- **Velocity Shaper** (default curve): velocities 127 at beat 0, 25 at 1, 1 from 2 on (the curve over the clip).
- **Rhythm** (defaults, pitch C3): notes at 0, 1, 2, 3, length 0.5, velocity 127 then 100 ×3; the original C3 at 0
  was replaced (it overlapped), the other notes stayed. **Shape** (Up): 16 notes of one grid step from 60 to 72
  (`60 + round (12·i/15)`), velocity 64. **Stacks** (default): 60, 63, 65 over the whole clip, velocity 64.
  **Euclidean** (defaults): four voices (36–39) hitting every second 1/16, length 0.25, velocity 100.

## 3. Strum's Tension curve [LIVE, fitted]
Low 100 %, positions measured as a share of the full offset, note index counted from the chord's top:

| Tension | notes | shares |
|---|---|---|
| 0 % | 3 | 0.5 |
| 25 % | 3 | 0.681668 |
| 50 % | 3 | 0.829436 |
| 50 % | 5 | 0.617532, 0.829436, 0.936652 |
| 100 % | 3 | 0.991220 |
| −50 % | 7 | 0.038811, 0.092510, 0.170564, 0.292382, 0.506876 |
| −50 % | 4, 5 | 0.0925, 0.2924; 0.0634 … (the same curve) |

`fit/fit.py` tried exponential, rational, power, Kumaraswamy, logarithmic, tanh, atan and Bézier families. Only a
**cubic Bézier** fits (1e-6): from (0,0) to (1,1) in (note position, share), control points (1/8, 5/8) and (3/8, 7/8)
at 50 %. Those are the diagonal's quarter points (1/4,1/4) and (3/4,3/4) moved towards the corner (0,1) by the
tension: `P1 = (¼(1−T), 1−¾(1−T))`, `P2 = (¾(1−T), 1−¼(1−T))`. That predicts 0.681670 at 25 % and 0.991220 at 100 %
(measured 0.681668, 0.991220). Negative tension is the same curve turned round: `f(u, −T) = 1 − f(1−u, T)`.
It is the curve Live draws in the Strum display. `miditools::strumCurve`.
Not measured: Low and High both set, and tension with High alone (LYNX interpolates `High + (Low − High)·f`).

## 4. Scale Mode in the set file [LIVE]
- A clip's Scale toggle + root + name chooser are in the Clip panel; the Control Bar shows the same pair. With the
  scale on, the editor gets "Scale" (fold to scale) and "Highlight Scale" buttons and the key column is marked purple.
- Saved: `MidiClip/IsInKey true` + `ScaleInformation { Root 0, Name 22 }` for C Bhairav, and the set's own
  `LiveSet/ScaleInformation { Root 0, Name 22 }` + `InKey true` followed it.
- So `Name` is the index into the chooser's list: Major 0, Minor 1, Dorian 2, Mixolydian 3, Lydian 4, Phrygian 5,
  Locrian 6, Whole Tone 7, Half-whole Dim. 8, Whole-half Dim. 9, Minor Blues 10, Minor Pentatonic 11, Major
  Pentatonic 12, Harmonic Minor 13, Harmonic Major 14, Dorian #4 15, Phrygian Dominant 16, Melodic Minor 17, Lydian
  Augmented 18, Lydian Dominant 19, Super Locrian 20, 8-Tone Spanish 21, Bhairav 22, Hungarian Minor 23, Hirajoshi
  24, In-Sen 25, Iwato 26, Kumoi 27, Pelog Selisir 28, Pelog Tembung 29, Messiaen 3–7 30–34. The tail (from Lydian
  Dominant) was read off the open chooser; the head is the order of the Live API help text [BIN], which differs from
  the chooser only in where 8-Tone Spanish sits (the help text has it after Hungarian Minor; the file says 21).
- The degrees in `core/LiveScales.h` are the textbook ones; Bhairav's were checked against the purple key marks.

## 5. What LYNX took this round
- `core/MidiTools.{h,cpp}`: Span, Strum, Ornament, Recombine, Arpeggiate (Live's 18 styles), Connect, Chop as pure
  functions "selected notes in, notes out", with `replaceNotes()` for a pattern. No UI yet: the panel is the next
  slice and needs Live | LYNX shots first. FL's own tools are untouched.
- `core/LiveScales.h` + .als import/export: the project key (`core/ProjectKey.h`) goes out as the set's and every MIDI
  clip's scale with Scale Mode on, and comes back from the set's scale (Live 12) or the first clip with a scale on
  (Live 11). A project without a key is now written with Scale Mode off (the Live 12 skeleton had it on with C Major).
  A Live scale FL's list doesn't have (FL's list holds the key) imports as "no key".
- Tests: `tests/MidiTools190oTests.inc` (166 checks), Live's notes as the expected values.

Differences from Live, on purpose or open:
- Ornaments before tick 0 are left out (Live keeps notes at negative times).
- Arpeggiate: only Up was measured; the other styles follow the manual's description of the Arpeggiator device. How
  Live treats notes that overlap partly was not measured (LYNX: an arpeggiator holding those keys).
- Connect below 100 % density, Spread and Tie are random in Live; LYNX draws them from a seed. Which chord note Live
  connects from was not measured (LYNX: the lowest).
- Recombine's "Grid" rotation, Span's Offset / Variation, Chop's Gaps follow the manual only.
- Not built: Time Warp, Velocity Shaper, Quantize (FL's quantiser exists), Glissando / LFO (MPE), all five generators
  (Rhythm's Pattern knob and Stacks' chord bank need more Live data; Euclidean and chord tools exist in FL's form).

## 6. Best way?
- **FL:** one-shot dialogs from the Piano roll's Tools menu (Strum Alt+S, Arpeggiate Alt+A, Chop Alt+U, Articulate
  Alt+L, Flam, Claw, LFO, Randomize…), each with a live preview and Accept; presets per tool. REA rounds 143a / 168g.
- **Ableton:** a docked panel beside the notes, one tool at a time, Auto apply, values in grid percentages and scale
  degrees; fewer options per tool than FL but faster to try (section 1).
- **Users:** reviewers single Live 12's tools out ("a MIDI revolution", Attack Magazine; Sound On Sound's two
  technique pieces), and FL users praise the Piano roll's tools as the best of any DAW (research-web-2.md, the
  strongest pro-FL point).
- **Verdict:** keep FL's tools as the default; add Live's tools as a docked panel in the Clip-launch workflow's clip
  column (where Live has it) and as a "MIDI Tools" entry in the Piano roll's Tools menu, so both workflows reach them.
  Nothing is removed. Live's Strum (percent of the grid + tension curve) and Recombine have no FL twin and are the
  first to surface.
