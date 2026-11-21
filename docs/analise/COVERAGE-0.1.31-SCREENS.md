# Screens coverage for 0.1.31 (round 190og, 2026-11-21)

Bases: **prev** = 190jw's r030p judgement (rollup030 + 13 pending branches, 2026-11-16); **v030** = tag v0.1.30-alpha
(0.1.30 as released); **r031** = wip/rollup031 85ac7cacd (0.1.31 step 3); **r031p** = r031 + tabs2_190nz + helix4_khs3_190oc.
Coverage % = (yes + 0.5 x partial) / rows that are not n/a. Every yes / partial citation re-opened in its own base by
`screens/summary.py` (0 failures). Files: `screens/{fl,live,flm,bandlab,garageband}.json` (new fields lynx_v030 /
where_v030, lynx_r031 / where_r031, lynx_r031p / where_r031p, changed_by; gap updated), `screens/summary.json`.
Method: every r030p citation re-located in each base (3 quotes had gone: renamed menu items / a reworded empty pad, re-cited);
then the families named in the 0.1.30-tail and 0.1.31 commit subjects and round write-ups were grepped in the
snapshots (each move below is cited in the JSON).

## Per program (yes / partial / no / n/a, %)

| program | rows | prev (r030p) | v030 | r031 | r031p |
|---|---|---|---|---|---|
| FL Studio 2026 | 302 | 209/59/21/13 **82.5** | 212/57/20/13 **83.2** | 83.2 | 83.2 |
| Ableton Live 12.4 | 185 | 59/100/20/6 **60.9** | 62/105/12/6 **64.0** | 64.0 | 64.0 |
| FL Studio Mobile | 122 | 59/54/5/4 **72.9** | 60/53/5/4 **73.3** | 73.3 | 73.3 |
| BandLab | 89 | 50/21/2/16 **82.9** | 53/18/2/16 **84.9** | 84.9 | 84.9 |
| GarageBand | 76 | 41/23/12/0 **69.1** | 46/18/12/0 **72.4** | 72.4 | 72.4 |
| all five | 774 | **74.4** | **76.0** | 76.0 | 76.0 |

No regressions. All 22 moves happened in the 0.1.30 tail (after 190jw measured); 0.1.31 (r031, r031p) moved no screen
row: its work is LYNX-own (track clips, tracker, project windows, LYNX Model, onboarding) or refines screens without
closing a listed gap (Live rack devices inline + Macro Map, Solo / Cue in the Device View, 190nc; key regions, 190nu).

## What moved (r030p -> v030), by round

- **190kc** (Live Session mixer): Live Sends and Returns -> yes (return tracks, Pre / Post); Crossfader with A / B assign -> yes (no -> yes).
- **190kk / 190ko** (Live devices that used to be dropped, now on LYNX's own engines): Erosion -> Corrode, Corpus -> Soundboard,
  Vinyl Distortion -> Platter, Looper -> Edison slot, Spectral Time -> Timeprism, Spectral Resonator -> Harmonia, Meld -> Braid:
  seven rows no -> partial (own panels, not Live's).
- **re-check**: Live Convert Audio to MIDI -> yes (Melody / Harmony / Drums have been in the clip menu since a2m189b; 190jw had a doubt).
- **190my**: FL Project picker (Ctrl+F8) -> yes.  **190kd**: FL Piano roll chord progression (chord blocks, lock, alternatives) -> yes.
- **gaps190kf**: Edison Sample properties -> yes (no -> yes); GarageBand Loop Browser and BandLab loop browser filters -> yes.
- **190ke**: GarageBand LCD tuner (Simple top-bar tuner) -> yes; Smart Controls (Controls tab) -> yes; iPad song settings fade-out -> yes.
- **als190ka-export (D5)**: GarageBand Export Song to Disk -> yes (AIFF, M4A where the OS has an AAC encoder).
- **190lr**: BandLab Drum Machine dock -> yes.  **190mr**: BandLab Sampler empty pad, Record into a pad -> yes.
- **natives190kj**: FLM audio editor -> yes (Detect pitch regions).
- Gap text updated, status kept: FL VIEW menu (Hint bar toggle, 190mr), Transistor Bass (program row, 190mc), FL Studio Mobile
  generator (rack rebuilt as a Layer, 190jv), Live scene page (per-scene follow actions, 190lr), Live Second Window (r031
  Open in new window, r031p Window menu), Live Arrangement (r031 track clips), BandLab Mixdown (M4A only in the Export
  window), BandLab tuner (top-bar tuner).

## Top 10 screen gaps on r031p (by value)

| # | program / screen | value | status | gap | buildable FL-/Live-exact now? proof needed |
|---|---|---|---|---|---|
| 1 | BandLab: Download / Mixdown As (M4A 128 / 192) | 5 | partial | Finish dock rows are WAV / MP3; M4A only via the Export window (and not on Linux) | yes, S: add an M4A row where aac::available(); BandLab capture exists (bandlab190p) |
| 2 | Live: Arrangement View | 5 | partial | FL-style Playlist; no Live track header (device, In / Out, sends) beside the lanes | design call (layout); Live shots exist (ableton175a / design190ia) |
| 3 | Live: Clip View audio sample editor | 5 | partial | waveform + warp markers live in the audio editor window, not in the docked Clip View | yes, M; Live 12.4 captures exist (gaps190kf raw) |
| 4 | FL: Export dialog Mastering | 5 | partial | no reference A/B algorithm choice / reference-file mastering | needs a new FL capture of the Mastering section |
| 5 | Live: Simpler | 5 | partial | Sampler page, no Simpler single panel with Slice mode / warp | M-L; needs a Live capture of Simpler's three modes |
| 6 | Live: MIDI Note Editor | 5 | partial | no Fold / Fold to Scale, no pad-name-only key column | yes, S-M; Live manual + existing captures |
| 7 | Live: Drum Rack pad view | 5 | partial | default view and I-O / S / R placement differed (ANSWERS B1a / 1c, fix not verified) | needs a live re-check (Live vs LYNX pair) |
| 8 | Live: Export Audio / Video | 5 | partial | no Rendered Track chooser, Render Start / Length fields | yes, S-M; Live capture exists (live190ei answers) |
| 9 | Live: EQ Eight / Compressor / Reverb / Utility / Delay | 5 | partial | LYNX's own device panels; Live's views and modes differ | per device, M each; needs Live device captures |
| 10 | Live: Clip View Sample box | 5 | partial | no Follow, RAM, HiQ, Complex Pro settings in the box | S-M; Live manual + captures |

Next tier (value 4): FLEX editor (no; encrypted packs), FL toolbar options, Wave Candy / Sytrus / Harmor / Kepler panels,
FLM DW Sampler and FLM effect panels (generated knob pages), BandLab Voice dock Input tab, BandLab chain-preset categories,
GarageBand Arrangement track sections, Drummer editor, iPad track controls, Beat Sequencer per-step repeat.

## Not verified / doubts
- Rows whose 190jw gap says "not confirmed" (Live 43 launch velocity, Live 57 lane choosers, Live 66 / 67 Drum Rack
  layout, Live 84 startup dialog) stay as judged: source alone does not settle them; they need a Live | LYNX shot pair.
- r031 onboarding (welcome card, Next-3, tips) touches FL 76 / 77, Live 22 / 84 and BandLab 17 but does not change what
  those rows lack (FL's per-window highlights, Live's lessons pane, a tempo / key setup step); statuses kept.
