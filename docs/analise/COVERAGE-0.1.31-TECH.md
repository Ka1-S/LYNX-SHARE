# Technology rows for 0.1.31 (round 190og, 2026-11-21)

Bases as in `screens/SCREENS-0.1.31.md` (prev = 190jw's r030p; v030 = 0.1.30 as released; r031 = rollup031 step 3;
r031p = r031 + tabs2_190nz + helix4_khs3_190oc). File: `tech/tech-rows.json` (status_v030 / evidence_v030, status_r031 /
..., status_r031p / ..., note, changed_by; 190jw's note kept as note_190jw), built by `tech/build_tech.py`, which
re-opens every has / ahead / partial citation in its own base (0 failures).

## Counts

| rows | prev r030p | v030 | r031 | r031p |
|---|---|---|---|---|
| the 47 rows of 190jw | has 25, ahead 6, partial 11, missing 5 | has 27, ahead 7, partial 9, missing 4 | same as v030 | same as v030 |
| + 4 new rows (F10, T24, T25, T26) | - | has 27, ahead 7, partial 10, missing 7 | has 30, ahead 7, partial 10, missing 4 | has 31, ahead 7, partial 9, missing 4 |

No regressions: every r030p citation still opens in v030, r031 and r031p (P6's and F9's quotes were reworded and re-cited).

## Moves
- **T1 ARA 2 hosting: missing -> ahead (v030)**: ARA 2 on audio clips (Melodyne, RipX, VocAlign; ARA SDK 2.1, document /
  region model, .lynx round trip; 190kx, 440e3c3d4). FL Studio, Live and GarageBand have none.
- **F9 export encoders: partial -> has (v030)**: AIFF and M4A (AAC through the system encoder; Windows / macOS, not Linux),
  owner decision D5 (e3f33f64d, 9cd820728, 7e3289e30).
- **G2 Smart Controls: partial -> has (v030)**: the Simple dock's Controls tab (eight labelled knobs, LYNX mapping per
  instrument, Edit mapping, Reset; 190ke). Transform Pad still missing.
- New rows (0.1.31 work, statuses per base):
  - **T25 track clips**: missing -> has on r031 (190ne, "Clips own their notes" on instrument tracks).
  - **T24 several project windows + copy / paste between them**: missing -> partial on r031 (File > Open in new window, 190ns)
    -> has on r031p (cross-window clipboard for notes, clips, channels, mixer states, automation points; Window menu; 190nz).
  - **F10 tracker modules (.mod / .xm / .s3m / .it) + Tracker editor**: missing -> has on r031 (190nw).
  - **T26 physical-modelling instrument**: partial (Sakura / Plucked!) -> has on r031 (LYNX Model, 190ni / 190nl).
- Notes only, status kept: P2 (batched blocks with live controllers, perf3_190lv), P5 (shared identical samples), P6 (X11
  overlay for moving marks, macOS layer behind a flag, cheaper software renderer from paint190md / paint2_190ms /
  tiles190mh - still partial: no GPU renderer on Linux / macOS), P8 / P10 / T4 / F3 / F4 (the r030p-only branches are in
  0.1.30 as released), T7 (Clip View warp time line), T10 (polyphonic Convert to MIDI and note editor), F1 (Save a copy
  for an older LYNX gets a 0.1.30 target on r031), F5 (.band iOS 2.3+ notes / time signature), F8, G3, T23.
- Performance rounds of the tail (perf3_190lv, heap190mo, paint190md, paint2_190ms, tiles190mh): they cut cost on rows
  that were already has / ahead (P1, P2, P6) and fixed a self-test heap corruption; none changes a status. The perf
  verdict vs FL is measured elsewhere (tracker/perf-lynx-vs-fl-0.1.30.md: Thief 27.5 % of a core vs FL 59.5 %).

## Top gaps still open on r031p
| row | status | value | effort | gap |
|---|---|---|---|---|
| G1 virtual drummer | partial | 4 | L | no performer model (XY complexity / loudness, fills, kick / snare follow) |
| T19 mobile app | missing | 3 | L+ | desktop only (Simple layout + Large controls) |
| F5 GarageBand .band | partial | 3 | M | effects, Drummer regions, tempo changes, sections listed, not converted |
| F8 Apple Loops metadata | partial | 3 | S-M | keywords / key from names, folders and tags; the loop file's own metadata not read |
| P6 GPU UI | partial | - | M-L | GPU renderer on Windows only; Linux / macOS draw on the CPU (moving marks excepted) |
| P11 safe overloads | partial | - | S-M | no UI-priority guard under overload |
| T12 scripting / MCP | partial | - | M | no FL-compatible controller scripts, no MCP surface |
| T20 tempo follower | missing | 2 | M | none |
| T3 VST3 note expression | partial | - | M | per-note tuning only |
| T23 AI assistant | partial | - | M-L | no assistant, no song starter |
