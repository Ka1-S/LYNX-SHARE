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
