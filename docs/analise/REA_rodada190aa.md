# Round 190aa: Live 12's MIDI Transformations measured tool by tool (Ableton Live study, 0.1.26)

Date: 2026-11-10. Base: wip/ableton190s (the MIDI Tools panel, rounds 190o / 190s). Live: Live 12 Trial 12.4.16, one
guarded GUI session 16:44–17:12 (GUI lock, idle gate, trial box OK, File › Quit; Preferences untouched, no scan). Clean
room: Live was driven through its own UI and the set it saved was read; nothing of Live's code, art or content is in
LYNX. Velocity Shaper is a Max for Live device whose file is encrypted: black box only.

Evidence (outside the repo): `/mnt/e/lynx-work/ableton190aa/`
- `live/mt3-test.als` (built by `mkset3.py`): 60 arrangement clips, one test each, with their own inputs: the 7 notes
  of round 190o, off-grid notes for Quantize, a chord before a gap for Connect, overlapping notes for Arpeggiate, 16
  rising notes 1/16 apart for Time Warp, 32 notes 1/32 apart for Velocity Shaper.
- `live/mt3-saved Project/mt3-saved.als`: what Live saved; `live/saved-dump.txt` its notes (`dumpals.py`).
- `compare.py`: runs `miditools::` on each input with the clip's settings and compares note for note.
- `live/x-*.png`: the Clip View after each clip (the settings are visible in the panel).

Tags: **[LIVE]** measured in that save, **[MAN]** Live 12 manual, **[INFERRED]** LYNX's reading.

## 1. Results per tool (before → after this round)

| Tool | Clips | Before | After |
|---|---|---|---|
| Arpeggiate, 14 fixed styles | A01–A14 | 13 match; **Diverge** wrong (LYNX 64 67 60, Live 64 60 67) | all match |
| Arpeggiate, overlapping notes | A18 | wrong: LYNX went on with the pattern when a key was added | match: the pattern starts again whenever the held keys change |
| Arpeggiate Random / Random Other / Random Once | A15–A17 | Random drew only from the current pass | ranges and structure as Live: Random = any note of any pass; Other = a new order each pass; Once = one order |
| Connect, defaults | 190o | match | match |
| Connect, chord to a note | C04 | wrong: only the chord's lowest note drew a line (15 notes, Live 22) | match: every voice draws its own line, equal notes at one time kept once |
| Connect, Tie 100 % | C03 | wrong pitch (63, Live 64) | match: a tied note has the pitch of the last step it covers, the velocity of the first |
| Connect Density 50 %, Spread 12 | C01, C02 | random | in range (subset of the 100 % notes; within ±12 st) |
| Recombine on Grid, rotate 1 / 2 | R01, R02 | built from the manual | match (unchanged) |
| Span Offset ±50 % | S01, S02 | built from the manual | match (unchanged) |
| Span Variation ±50 % | S03, S04 | LYNX scaled lengths one way | Live moves lengths both ways by up to half a half-step (−0.052…+0.061 beats on 1/16) for either sign; LYNX now draws in that range |
| Strum Low 100 % + High 50 % | T01 | match | match |
| Strum High 100 %, Tension 50 % | T02 | wrong (middle note 0.043, Live 0.207) | match: each side's curve runs from the note that stays |
| Quantize Grid / End / Start+End / 50 % / 1/4 / 1/16T | Q01–Q06 | built from the manual (never measured) | all match unchanged |
| Quantize 1/8 + 1/8T | Q07 | missing | match: with Triplets on, Live's four buttons are 1/8T, 1/8 + 1/8T, 1/16T, 1/16 + 1/16T; "+" = the nearer of both kinds of line (`straightToo`) |
| Chop Parts 4 / 6 / 20, Gaps 2 / −1 | H01, H02, H07, H08 | match | match |
| Chop pattern toggles, emphasis + Stretch, Variation | H03–H06, H09 | missing | see section 2 |
| Time Warp, Velocity Shaper | W01–W08, V01–V06 | missing | see sections 3 and 4 |

Notes on the session:
- [LIVE] With Triplets on, **Grid** is greyed out and the four value buttons change their labels (above).
- [LIVE] Typing "0" with notes selected deactivates them (Live's shortcut): clip T02's notes were saved deactivated by a
  stray key; their times are unaffected and are what the test uses.
- Arpeggiate's Diverge for four or more notes follows the triad's rule [INFERRED].

## 2. Chop in full [LIVE]
Panel (`live/x-H04.png`, top to bottom): Parts and Gaps knobs; the **pattern row**, one cell per part (16 at most,
repeated over more parts [MAN]) in two halves: the upper half lit = the part plays, dark = a gap; the lower half lit
(cyan) = the part is **emphasised**; **Stretch** "×2" (greyed while nothing is emphasised); **Variation** "0.0 %".
- A hand-edited pattern makes Gaps read "-"; turning Gaps writes a fresh pattern (H03: part 2 off → `0 .5 .75`).
- An emphasised part (a note or a gap) lasts `Stretch` parts: H04 part 1 ×2 → shares 2/5 1/5 1/5 1/5; H05 ×4 → 4/7
  1/7 1/7 1/7; H09 Gaps 1 with the gap emphasised ×4 → notes at 0 and 5/7 of each note, 1/7 long.
- Variation 50 % (H06, on top of part 1 ×4): shares 0.6146 0.0789 0.1327 0.1739 for **every** note alike: one random
  draw per run. LYNX scales each share by a random 1 ± variation (the same for every note) [INFERRED law, Live's draw
  lies inside it].
- Clicks: Live's lower half cycled an emphasised note into an emphasised gap; LYNX's halves are plain toggles (upper =
  plays, lower = emphasis). Interaction only; the look and the results are Live's.

## 3. Time Warp [LIVE, 7 of 8 settings within 1e-5 beats]
Panel (`live/x-W04.png`): "Time 1.1.1" and "Speed ×1.0" of the selected breakpoint; the display (the range's ends as
two lines, the speed line, numbered circles: up = faster); toggles **1 2 3** (1 and 3 on); **Quantize** Off/On,
**Range** Fit, **Note End** Include.
- Model (fitted, `fit/tw.py`): a note at t goes to `first + c · ∫ r` from the first start to t, where r is **1 / speed
  interpolated linearly** between the breakpoints that are on, which sit at fractions of the clip's range (here the
  one-bar clip, not the notes' span: 4 beats fits, 3.875 does not), flat beyond them. Fit: c keeps the last end in
  place; Range off: c = 1 (W06). Quantize: the warped starts go to the nearest grid line, lengths stay (W05).
  Breakpoint 2 defaults to the middle; Time's arrow key moved it by a tenth of the range (W08: 0.4 shown as 1.2.3).
- Note End off (W07): lengths stay; Live's c is 0.2 % lower than with Include (starts up to 0.0076 beats = 7 ticks
  apart). Not explained by any span tried (`fit/tw2.py`, `tw3.py`); LYNX keeps Include's c. Open.

## 4. Velocity Shaper [LIVE, 6 settings note for note; Max for Live device, black box]
Panel (`live/x-V01.png`): the envelope display with Maximum (127) at its top right and Minimum (1) at its bottom
right; **Loop** 1, **Rotate** 0, **Division** 1/16 (choices 1/16, 1/8, Grid — Live's own hint text).
- The new velocity ignores the old one (V01 = V02 with inputs 100 and 50): `floor (min + (max − min) · shape (x))`,
  x = the note's place in the span **from the first start to the last end** (V03: the second cycle starts at 2 / 3.9375,
  so its first note is 115, not 127), times Loop, minus Rotate × Division (wrapping: V04 1/16 → peak at 0.25 beats,
  V05 1/8 → 0.5).
- The default envelope: from the top, an exponential fall to the floor at x = 0.503, then flat. Of the shapes tried
  (`fit/vs.py`), only `(e^(−a·u) − e^(−a)) / (1 − e^(−a))`, a = 2.825, with round-down fits all 96 measured notes.
- Editing (Live: click to add breakpoints and drag them): LYNX drags points, adds one on a click, removes one on a
  double-click, and bends a line with Alt + drag (the curve value of the segment).

## 5. MPE tools: what LYNX would need (issue #108; not built)
- **Glissando** [MAN]: needs at least two notes; a pitch bend per note from Start (% of the note's length) to the next
  note's pitch, with a Curve. LYNX needs per-note pitch-bend envelopes (an MPE note expression lane), their playback to
  MPE-capable instruments (per-note channel or MIDI 2.0 per-note pitch bend) and the .als `PerNoteEventStore` import /
  export (Live writes them there; 190o's sets carry an empty store).
- **LFO** [MAN]: Target Pitch Bend / Slide / Pressure, Attack / Decay, Shape (Sine, Square, Triangle, Random +
  Reseed), Rate 1 … 1/128, Time Shift, Amount, Amplitude Base. Needs the same per-note expression lanes for the three
  targets (pitch bend, slide = CC74, pressure), an editor to see them (Live: MPE tab) and the same file support.
- Both stay out until LYNX has MPE notes; the panel's chooser lists only the tools LYNX can run.

## 6. Best way?
- **FL:** Piano roll › Tools: Chop (Alt+U, with its own pattern presets), Quantize (Alt+Q / Ctrl+Q with start/end and
  amount), Articulate / Randomize; no Time Warp and no velocity envelope tool (FL's Randomize and Scale levels do part of
  it). REA rounds 143a / 168g.
- **Ableton:** the same jobs live in the Clip View, one panel each, with Auto apply (sections 2–4).
- **Users:** Live 12's Time Warp and Velocity Shaper are named as the quick way to make fills and accelerating rolls
  without drawing (research-web.md row 9; Attack Magazine's MIDI Tools piece).
- **Verdict:** FL's tools stay the default in the FL workflows; Live's pages are added to the MIDI Tools panel
  (Clip View and Piano roll › Tools › MIDI Tools…). Nothing removed.
