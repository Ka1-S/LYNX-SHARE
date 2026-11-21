# LYNX coverage for 0.1.31: FL Studio, Ableton Live (+ natives, + a Live 13 beta watch list), FL Studio Mobile, BandLab, GarageBand (round 190og, 2026-11-21)

Same method, rows and tools as rounds 190bk → 190dq → 190es → 190gf → 190jw:
- one obligation row for each thing the original program has (7,327 rows; 225 of them GarageBand);
- a status per row and base (covered, partial, missing, unknown, excluded) with a `file:line ('quoted string')` citation;
- a script that re-opens every citation in the base it was judged on (`tools/verify6now.py`: 3,760 / 3,764 / 3,764
  covered citations, **0 failures**);
- 11 judge clusters (`out/rejudge-*.json`, each validated by `tools/check_out6.py`: **0 errors**), diff-first: a family whose
  files did not change keeps its status.

**How to read the numbers:** percent = (covered + ½ partial) / rows not excluded; strict = covered only; unknown counts as
not covered. "Covered" means LYNX has the function in source, not that it behaves the same (sound, speed and look are
measured by the parity, perf and design rounds).

## The bases

| Column | What it is |
|---|---|
| **0.1.30 (190jw)** | 190jw's "+pending" measurement (2026-11-16: rollup030 4c45364e4 + 13 branches): the "prev" column |
| **0.1.30 released** | tag `v0.1.30-alpha` f9ef415dd. 936 files differ from 190jw's base: ~60 merges landed after 190jw measured (details passes 3–10, issues #191–#215, gaps190kf, natives190kj, polyedit2, chord tool 190kd, Waves / FabFilter / Valhalla / Kilohearts ports, Helix 1–3, ARA…). Its first judgement is in this round, so it also absorbs corrections of 190jw |
| **0.1.31 = rollup031** | `origin/wip/rollup031` 85ac7cacd (step 3): rack190nc, trackclips190ne, khs2_190nh, physmod (LYNX Model), leftovers030_190nr, issues223_190nq, issues231_190ns, onboard_190np, chord2_190nu, polish_190nx, tracker_190nw, site_learn_190nt |
| **+ pending** | rollup031 + the finished, pushed, unmerged 0.1.31 branches merged locally (branch wins): **tabs2_190nz** (copy / paste between windows, #232 stage 2) and **helix4_khs3_190oc** (Helix round 4, the Kilohearts Nonlinear Filter). Left out: perf4_190of (in progress), the rounds without a pushed branch (190ob tracker 2, 190od issues, details11_190oa), and old 0.1.29/0.1.30 opt-in branches (beatfl2_190kz-dsp, pror2_190lz, design190ig-s4, perfgate190id) |
| **+ 190og** | this round's own branch `flm_cov_190og` (FLM export, the FLM REA, the five gap rows): §5 |

## 1. Coverage per program

### 1.1 Functions

| Program | Rows (not excluded) | Covered % 0.1.30 (190jw) → **0.1.30 released** → **0.1.31** → + pending | Δ vs 0.1.30 (190jw) | Strict % | Value-weighted % |
|---|---:|---|---:|---|---|
| FL Studio 2026 | 3,581 | 68.1 → **73.4** → **73.5** → 73.5 | **+5.4** | 62.6 → 67.7 → 67.8 → 67.8 | 76.8 → 81.2 → 81.3 → 81.3 |
| Ableton Live 12 | 1,068 | 72.1 → **73.8** → **73.9** → 73.9 | +1.8 | 63.5 → 65.1 → 65.2 → 65.2 | 80.8 → 82.1 → 82.2 → 82.2 |
| Live natives | 121 | 63.2 → **67.4** → **67.4** → 67.4 | +4.2 | 40.5 → 42.1 → 42.1 → 42.1 | 71.1 → 74.4 → 74.4 → 74.4 |
| *Live with its natives* | *1,189* | *71.2 → 73.1 → 73.2 → 73.2* | *+2.0* | *61.1 → 62.7 → 62.8 → 62.8* | *79.8 → 81.3 → 81.4 → 81.4* |
| FL Studio Mobile | 282 | 83.2 → **84.0** → **84.0** → 84.0 (+190og: §5) | +0.8 | 75.9 → 76.2 → 76.2 → 76.2 | 89.5 → 90.0 → 90.0 → 90.0 |
| BandLab | 289 | 90.3 → **91.5** → **91.5** → 91.5 | +1.2 | 83.4 → 84.8 → 84.8 → 84.8 | 92.8 → 93.9 → 93.9 → 93.9 |
| GarageBand | 225 | 67.3 → **69.1** → **69.1** → 69.1 | +1.8 | 54.2 → 57.8 → 57.8 → 57.8 | 78.5 → 80.7 → 80.7 → 80.7 |
| **The four from 0.1.29** | 5,341 | 70.8 → **74.9** → **74.9** → 74.9 | +4.1 | 64.1 → 68.0 → 68.0 → 68.0 | 79.5 → 82.7 → 82.7 → 82.7 |
| **All five** | 5,566 | 70.6 → **74.6** → **74.7** → 74.7 | **+4.1** | 63.7 → 67.6 → 67.6 → 67.6 | 79.4 → 82.6 → 82.6 → 82.6 |

### 1.2 Rows of value ≥ 3 (used weekly or more)

| Program | Rows | Covered % 0.1.30 (190jw) → 0.1.30 released → 0.1.31 → + pending | Δ | Open points (value ≥ 3, +pending) |
|---|---:|---|---:|---:|
| FL | 1,409 | 90.6 → **93.8** → **93.8** → 93.8 | +3.2 | 87.5 |
| Live | 725 | 87.2 → **88.1** → **88.1** → 88.1 | +0.9 | 86.0 |
| Live natives | 78 | 80.1 → **81.4** → **81.4** → 81.4 | +1.3 | 14.5 |
| FLM | 206 | 92.7 → **93.2** → **93.2** → 93.2 | +0.5 | 14.0 |
| BandLab | 191 | 95.3 → **96.6** → **96.6** → 96.6 | +1.3 | 6.5 |
| GarageBand | 131 | 87.4 → **89.7** → **89.7** → 89.7 | +2.3 | 13.5 |
| **All five** | 2,740 | 89.7 → **91.9** → **91.9** → 91.9 | **+2.2** | 222.0 |

### 1.3 Screens (windows, dialogs, panels, menus)

774 screens (190jw's inventories), re-judged on the three bases (`screens/SCREENS-0.1.31.md`, `screens/summary.py`: 0 citation
failures). Percent = (yes + ½ partial) / screens not n/a.

| Program | Screens | 0.1.30 (190jw) | 0.1.30 released | 0.1.31 | + pending | Δ |
|---|---:|---:|---:|---:|---:|---:|
| FL Studio | 302 | 82.5 | **83.2** | 83.2 | 83.2 | +0.7 |
| Live | 185 | 60.9 | **64.0** | 64.0 | 64.0 | +3.1 |
| FL Studio Mobile | 122 | 72.9 | **73.3** | 73.3 | 73.3 (+190og: the export window, §5) | +0.4 |
| BandLab | 89 | 82.9 | **84.9** | 84.9 | 84.9 | +2.0 |
| GarageBand | 76 | 69.1 | **72.4** | 72.4 | 72.4 | +3.3 |
| **All five** | 774 | 74.4 | **76.0** | 76.0 | 76.0 | +1.6 |

### 1.4 Technology (47 rows + 4 new for 0.1.31; `tech/TECH-0.1.31.md`)

| Base | has | ahead | partial | missing |
|---|---:|---:|---:|---:|
| 0.1.30 (190jw, 47 rows) | 25 | 6 | 11 | 5 |
| 0.1.30 released (51 rows) | 27 | 7 | 10 | 7 |
| 0.1.31 | 30 | 7 | 10 | 4 |
| + pending | 31 | 7 | 9 | 4 |

Moves: ARA 2 hosting missing → **ahead** (190kx: FL, Live and GarageBand have none); M4A / AIFF export → has; Smart
Controls → has; new rows for 0.1.31: track clips (190ne) and tracker modules + editor (190nw) → has, LYNX Model
(physical modelling, 190ni) → has, project windows with copy / paste between them partial → has (+pending, 190nz).

### 1.5 What the yardsticks say

- **0.1.30 did most of the moving.** 289 rows moved up and none down, all of them between 190jw's base and the 0.1.30
  release; 0.1.31's rollup moved 5 (key markers 190nu, the welcome card 190np, the #230 fade previews 190nq, the Cue Out
  preview 190nc) and the pending branches none. 0.1.31's own work so far is mostly LYNX-own (track clips, the tracker,
  LYNX Model, project windows, onboarding), which the reference programs' rows don't hold: it shows in the technology rows.
- **FL value ≥ 3 is at 93.8 %** (90.6 at 0.1.30): the chord tool of 190kd alone closed 68 Piano-roll rows; FL's Piano roll
  went 52.9 → 71.4 %.
- **Live gained less (+1.8):** returns and the crossfader (190kc), scene launch preferences (190ld), audio-effect ports
  (190kk / 190ko), corrections in the Detail View. Live's weakest areas stay Link (48.8 %), the Groove pool (54.8 %) and
  the Device view (61.0 %).
- **FLM 84.0 %** before this round; §5 has what 190og adds.
- **BandLab 91.5 %, GarageBand 69.1 %** (Learn to Play 0 %, Drummer 33 %, Amps + pedals 38 % are the holes; 24 Apple-only
  rows stay counted, owner decision D1).

## 2. What moved (`notes/moved.txt`)

| Step | FL | Live | Live natives | FLM | BandLab | GarageBand | Total |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0.1.30 (190jw) → 0.1.30 released | 232 | 28 | 10 | 4 | 4 | 8 | 286 |
| 0.1.30 released → 0.1.31 | 3 | 1 | 0 | 0 | 0 | 0 | 4 |
| 0.1.31 → + pending | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

No row moved down. By round (0.1.30 tail): 190kd chord tool 112, corrections of 190jw (code already in rollup030) 106,
gaps190kf 28, 190jv (FLM residue + Piano roll tails) 16, details passes (190ld 13, 190lr 11, 190mk 10, 190mv 8, 190mr 7,
190mp 4, 190my 3, 190lx 2), 190ee 9, 190kc 8, natives190kj 6, 190kk 5, 190ke 4, als190ka 3, 190ko 3, others 1–2 each.
0.1.31: 190nu 2 (Add key marker, key regions), 190np 1 (Help › Welcome), 190nq 1 (Show fade previews, #230), 190nc 1
(Browser preview through the Cue Out). Cluster write-ups: `out/rejudge-<cluster>.md`.

## 3. The biggest gaps still open: value ≥ 3, by program (`notes/uncovered-v3.txt`, 372 rows)

**FL** (141 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `natives-E060` Gross Beat (effect) | 5 | partial | S | V030: Gross Beat got FL's full mapping menu, slot menu, Safety Line and dial ring; still partial: the selected / edited slot follows the plugin parameter (GrossBeat.cpp 'engine.set |
| `natives-G014` FLEX (generator) | 5 | partial | L | unchanged since 190gf |
| `edisontools-0477` Start | 4 | partial | S | The render-to-audio / consolidate windows now say Start (190gk); the main File > Export window still shows Export... then a file chooser |
| `chansettings-0087` Waveform editor | 4 | partial | M | unchanged since 190gf |
| `natives-E004` Edison (effect) | 4 | partial | L | V030: Edison gained Dual view, FL's grid law, grid-slicing, Scrub, Detect pitch regions, Sample properties (F2), click-free editing; the 190gf gaps (recording limits, host-selectio |
| `natives-E068` Newtone (effect) | 4 | partial | L | V030: FL's note analysis layout and window, natural engine for new slots; docs/PLUGINS.md still 'Close' (legato glides, re-alignment after edits and vibrato handles differ), so par |
| `natives-G030` Harmor (generator) | 4 | partial | L | V030: Harmor resynthesis (a sample in the IMG section) added (18 of 24 factory resynthesis presets null below -50 dB vs FL's DLL) and every factory filter shape modelled; still 'Cl |
| `midilink-X003` Controller scripts (device profiles with lights, motor faders, displays) | 4 | partial | L | unchanged since 190gf; r030p adds CLAP remote-controls pages ordering on encoders (190jg, ControllerBridge.cpp) but still no device profiles with lights / motor faders / displays |
| `browser_picker_export-0002` Collapse structure button (Ctrl+Up) | 3 | partial | S | unchanged since 190gf |
| `browser_picker_export-0011` Type to filter | 3 | partial | S | R030 (190ib fcf4ff6fa, issue #182): typing in the Browser list jumps to the first row starting with the typed letters; FL's 'Type to filter' option (typing filters the tree as a se |

**Live** (140 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `drumsimpler-080` Slice to New MIDI Track with a slicing preset | 4 | partial | S | unchanged since 190gf |
| `racks-068` Save the rack as a preset (.adg) | 4 | partial | S | r030/r030p: racks save as Live .adg (Live 12.4 opened them, 190hr); hosted plugins and LYNX built-ins without a Live device are left out of the file. |
| `arrangement-0002` Fold / unfold a track (triangle in the title bar) | 4 | partial | S | unchanged since 190gf |
| `deviceview-0012` Cut / Copy / Paste a device (Edit menu, Ctrl+X / C / V) | 4 | partial | S | unchanged since 190gf |
| `mapping-0028` Mappable: transport buttons (play, stop, record, overdub, metronome, loo | 4 | partial | S | unchanged since 190gf |
| `detail-072` Transpose (semitones) and Detune (cents) | 4 | partial | M | r030/r030p: Transpose ±48 st / Detune ±50 ct stored, shown and saved; Beats, Complex Pro and Texture render it offline (190ik) but the real-time pitch shifter of the other modes st |
| `detail-089` Device parameters shown and edited inside the chain (the device's own pa | 4 | partial | M | still partial: v030 shows Braid's engine knobs on its card (190ko); r031 shows a rack's chain devices inline with their first 7 knobs (190nc); other devices still show a few knobs  |
| `groove-039` Shipped library: 219 grooves in Percussion / Style / Swing / Utility | 4 | partial | M | unchanged since 190gf |
| `session-065` Session mixer: In/Out section (Audio/MIDI From, To, Monitor In/Auto/Off) | 4 | partial | M | unchanged since 190gf |
| `session-085` MIDI / key mapping of clip slots, Track Launch and Scene Launch | 4 | partial | M | still partial: v030 adds the Relative Session Mapping Strip (per-track launch at the highlighted scene, scene up / down / launch / cancel; 190kc); the controller threshold that fir |

**Live natives** (29 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `tech-0001` Warping engine: warp markers, clips follow the set tempo | 5 | partial | M | v030: Live's warp time line (loop brace, Start / End in beats) is in the Clip View for clip-grid clips (gaps190kf); Arrangement clips still lack it (tempo_warp-0062) |
| `tech-0030` Racks: chains, macros, macro variations, chain selector | 5 | partial | M | r030: Group / Ungroup of an instrument, a drop area, .adg rack presets with chains (chains, macros, variations and the chain selector were there); still partial: Live MIDI Effect R |
| `tech-0039` Format: .als read (Live 11 / 12 sets) | 5 | partial | L | v030: Meld -> Braid, Erosion -> Corrode, Vinyl Distortion -> Platter, Corpus -> Soundboard, Looper -> Edison, Spectral Time -> Timeprism, Spectral Resonator -> Harmonia and MIDI Ef |
| `instruments-0005` Operator | 4 | partial | S | r030: Tetra (LYNX's 4-operator FM synth, Operator's parameter set, import scores 9.2-9.7); still dropped: pan by key / random, envelope time by key, LFO rate by key / amount by vel |
| `audiofx-0007` Auto Filter | 4 | partial | M | unchanged since 190gf |
| `instruments-0006` Wavetable | 4 | partial | M | r030: Helix (LYNX's wavetable synth with Wavetable's parameters, matrix, MPE; .als in / out); its 73 tables are LYNX's own (Live's 194 names map to the nearest), user wavetables ar |
| `instruments-0007` Drift | 4 | partial | M | unchanged since 190gf |
| `tech-0016` Groove engine (Groove Pool, non-destructive grooves, commit) | 4 | partial | L | unchanged since 190gf |
| `tech-0032` Clip envelopes (per-clip automation and modulation) | 4 | partial | L | unchanged since 190gf |
| `tech-0040` Format: .als write | 4 | partial | L | r030: Helix exports as Wavetable (Live 12.4 opened the round trip, 190ja), Complex Pro / Texture transposes written back, racks as .adg; other LYNX effects and synths (Tetra includ |

**FLM** (25 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `audiotracks-0003` Audio clip time-stretch ratio (STR1) | 5 | partial | L | unchanged since 190gf |
| `effects-0018` Pitcher (type 25) | 4 | partial | M | mapping unchanged; 190jv measured it against FL renders of a held sine (t90 within 35 ms, settled within 0.1 semitone); left: LYNX Tune detects ~18 ms later and resets on silence w |
| `effects-0033` Pitcher 2 (type 45) | 4 | partial | M | better on v030 (190jv): target in the sung note's own octave, FL-measured speed law, the -3.02 dB output trim; pitch error 344-431 -> 20-256 cents vs FL, still not exact |
| `instruments-0022` FLM-exported .flp: FL Studio Mobile Rack generator (state in event 0xD5) | 4 | partial | M | better on v030 (190jv): racks with several instruments (Layer + parts), an effect before an instrument and Transpose / Scale / Randomizer are rebuilt, the Layer takes its instrumen |
| `app-0027` Quick Render | 3 | partial | S | unchanged since 190gf |
| `audiotracks-0020` Sync All Samples (stretch every clip to the tempo) | 3 | partial | S | moved v030: Channel rack options › "Restretch all" (FL's, round 190mp) re-stretches every channel already stretched offline; FLM's Sync All Samples (every sample to the tempo, unst |
| `format-0038` Module PRMS: older files with fewer values / remapped indices | 3 | partial | S | unchanged since 190gf |
| `pianoroll-0014` Set length 1/1, 1/2, 1/4, 1/8, 1/16 | 3 | partial | S | unchanged since 190gf |
| `pianoroll-0018` Guess Scale... (scale detection from selected notes) | 3 | partial | S | better on v030 (190jv): Scale highlighting › Automatic guesses the key from the selected notes (FL's law); still no FLM Guess Scale... result dialog that sets the project scale fro |
| `pianoroll-0025` MIDI import dialog: Split MIDI channels, Import automation | 3 | partial | S | unchanged since 190gf |

**BandLab** (13 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `instruments-018` Virtual instrument engine: sampled soundbanks | 5 | partial | L | unchanged since 190gf |
| `online-001` Splitter: split a song or region into stems (2 / 4 / 5 / 6 tracks, lead  | 4 | partial | M | v029/r030: unchanged since 190gf (4 stems only). r030p (190jr): a form before each extraction with a tick per stem and a model row (Standard 4 stems / 6 stems with guitar and piano |
| `online-003` Voice Cleaner (Noise Remover, DeReverb, AutoEQ) on a region or a whole t | 4 | partial | M | unchanged since 190gf |
| `drummachine-011` Drum kit picker for the track | 3 | partial | S | Still partial. v030 adds BandLab's Drum Machine tab (190lr) but leaves its kit picker out (the code says LYNX has no equivalent); the kit still comes from the Loop Starter genre or |
| `looper-002` Launch quantize | 3 | partial | S | unchanged since 190gf |
| `mastering-005` Mastering EQ (low / mid / high), reset per preset | 3 | partial | S | unchanged since 190gf |
| `sampler-002` Sampler Starter: pre-made kit or build from scratch | 3 | partial | S | unchanged since 190gf |
| `tracks-023` Input Gain of the track | 3 | partial | S | Still partial. From r030 on (190ih), the Simple dock shows BandLab's Input Level meter while a track is armed. It is a meter, not a gain: the input gain control is still only the M |
| `editing-021` Region: Denoise | 3 | partial | M | unchanged since 190gf |
| `looper-001` Looper pad grid: launch loops from pads | 3 | partial | M | unchanged since 190gf |

**GarageBand** (24 rows of value >= 3 still open on +pending; the 10 first by value, then effort):

| Row | Value | Status | Effort | What is missing |
|---|---:|---|---|---|
| `gb-loops_media-003` Loop preview in the Loop Browser at project tempo / key | 5 | partial | S | Still partial on all three bases: the preview follows the project tempo; v030's Loop Browser shows Tempo / Key columns and can filter to keys near the project key, but the preview  |
| `gb-touch_instruments_ios-004` Beat Sequencer: step grid with rows per drum, step velocity / repeat / c | 4 | partial | S | Still partial: v030 adds FL's per-note Repeat lane (issue #208, played by the scheduler) and BandLab's Drum Machine tab (190lr), so velocity / repeat / chance exist; no per-row pla |
| `gb-global_tracks-003` Arrangement markers: rename, move, copy or delete a whole section with i | 4 | partial | M | rename / select / duplicate / delete a marker section by time selection; no drag of a section with its regions |
| `gb-drummer-001` Drummer track: virtual drummer that plays a part | 4 | partial | L | genre pattern generator, not a virtual drummer with performance settings |
| `gb-projects_tracks-016` Create a Drummer track | 4 | partial | L | Loop Starter generates a genre drum pattern; no virtual drummer track that follows performance settings |
| `gb-amps_pedals-003` Amp Designer: amp EQ controls (bass/mid/treble/presence) | 3 | partial | S | low / mid / high EQ; no presence |
| `gb-drummer-006` Fills and swing knobs | 3 | partial | S | swing exists; no fills knob |
| `gb-file_interop-009` Apple Loops metadata (tempo/key tags in CAF/AIFF loops) read on import | 3 | partial | S | WAV ACID beat count read; Apple Loops tempo / key tags in CAF / AIFF not read |
| `gb-mix_smart_controls-006` Smart Control types (knobs, switches, pads) | 3 | partial | S | Still partial: v030's Controls tab (190ke) and the rack macros are knobs only; no switch or pad control types. |
| `gb-mix_smart_controls-014` Track controls on iPad: volume, pan, echo/reverb send, compressor, EQ, n | 3 | partial | S | Still partial: v030's Controls tab (190ke) adds Reverb send / Pan / Volume knobs; still no echo send in the Simple dock. |

## 4. Live 13 (beta) watch list — public information only (`live13/LIVE13.md`, `live13/live13-rows.json`)

Live 13 is in public beta (announced 2026-11-10, beta 13.0b25 per MusicRadar / Attack Magazine; release planned for
early 2027; Ableton's own release-notes pages answered 404, so the rows come from Ableton's product page and the press).
Live 13 is not installed here; the rows are a **watch list**, not counted above. 28 rows: covered 3 (ARA 2 hosting,
Round Robin rack mode, mixer device slots), partial 13, missing 5, unknown 7 (by grep only). The most valuable open ones:
Capture Automation (5, missing), the Step Sequencer clip with per-lane rate / direction / swing (5, partial), Envelope
Clips on Return tracks (4), Multiband and Mid-Side racks (4), Macro response curves with a Modulation mode (4), a
multi-engine Drum Synth (4), Clipper and Transient Shaper devices (4). They become rows when Live 13 is released and a
capture session is possible.

## 5. This round (+190og)

**FL Studio Mobile export and REA** (`docs/analise/REA_rodada190og.md` on the branch, `flm/`):
- **New:** File › *Save a copy as FL Studio Mobile project (.flm)...* and `LYNX --export-flm` (FLM coverage `app-0003`,
  missing → **partial**: Slicer and SoundFont Player channels and the automation of the synths' own parameters are still
  listed, not written). What the copy loses is listed before it is saved, in the "Save a copy for an older LYNX" style.
  LYNX → .flm → LYNX keeps the notes, the patterns (grouped again through the LYNX chunk), the tempo, the meter, the key,
  the chord set, the mixer levels / pans / mutes / sends, FLM's engines (MiniSynth, SuperSaw, GMSynth, Transistor Bass,
  DW Sampler, 3x Osc), drum kits, Layers, every Pocket effect and the automation of levels, pans and effects
  (`tests/FlmExport190ogTests.inc`). All 20 FLM demo songs and templates of FL's copy go .flm → LYNX → .flm → LYNX with the
  same notes, tempo and Master level. FLM itself opens and plays the exported files: FL's "FL Studio Mobile" generator renders them (FL /R); FLM's demo "Tokyo" against its LYNX round trip went from env_corr 0.54 to 0.87 after three fixes the renders found (kit sample places, a kit's held bend, MiniSynth automation); the drums alone match in shape (env_corr 0.99) at +3.6 dB, an open routing point of FLM 4.x songs (REA_rodada190og.md §4).
- **REA, format fields:** CHHD +1056 / +1060 are input monitoring and record arm, not mute / solo (`format-0044` unknown →
  partial); SEND [1] is the send's Enable, [3] / [4] the initial values (`format-0050` unknown → **covered**: a switched-off
  send now stays off on import); FLM's loader skips unknown chunks silently; FLM's stretcher is élastique Pro V3 (black
  box); the 5.0.3 Arpeggiator engine and Guess Scale located for the next rounds.
- **Import fix:** an audio track's empty slot no longer makes a silent "<empty>" sampler channel.
- `format-0012` (HEAD's play loop) partial → **covered**: the import keeps the range with looping off, the export writes it.
- FLM functions **84.0 → 84.9 %** (value ≥ 3: 93.2 → 93.5). The FLM screen inventory gains
  the export window (LYNX's own; no FLM counterpart).

**Five gap rows (Part 3, sub-round `gap/`, merged on this branch), all Live-exact in the Clip-launch layout, partial →
covered:**

| Row | Value | What was built | Proof |
|---|---:|---|---|
| `live4:deviceview-0012` | 4 | the Edit menu's Cut / Copy / Paste / Duplicate / Delete on a selected device in the Device View (Ctrl+X / C / V / D, Del); a pasted device goes in front of the selected one, as Live's manual says | Live 12 manual + existing Live captures (`gap/HANDOFF.md`) |
| `live4:mapping-0028` | 4 | Arrangement Overdub, Loop, Punch-In and Punch-Out are MIDI / key mappable in the map modes | Live 12 manual |
| `live1:detail-019` | 3 | Clip View: Duplicate Loop for MIDI clips | Live 12 manual + captures |
| `live1:detail-020` | 3 | Crop Clip in the clip menu and on Ctrl+Shift+J | Live 12 manual |
| `live1:detail-047` | 3 | the Velocity editor's footer: Randomize with its amount | Live 12 manual + captures |

Live 73.9 → **74.1 %** (value ≥ 3: 88.1 → 88.5). All five programs 74.70 → 74.79 %. Live 12.4.16 crashed while loading
any set during the sub-round, so no new Live capture was taken; the proofs rest on the manual and earlier captures.

## 6. Owner decisions

- **New, taken (reversible): what the .flm export writes for content FLM has no module for.** Taken: the notes stay on
  FLM's MiniSynth (its defaults) and the instrument is listed under "May sound different there"; effects without an FLM
  module are left out and listed. The FLM judge's recommendation (render such channels to audio lanes) is the option: it
  keeps the sound but not the notes; it needs the offline stem render from the export window (M). Recommended for later,
  as a choice in the export window.
- Carried: D1 (Apple-only GarageBand rows counted), D2 (tag LYNX's own content), D3 (virtual drummer later, Simple only),
  O8 (no sampled BandLab banks), O13 (FLEX stand-in), O17 (.als device export, exact maths only), O25 (the "FL Studio
  Mobile" generator in an .flp stays a stand-in).

## 7. Live checks (carried and new)

- FL: does a Gross Beat slot picked by MIDI become the edited slot (`natives-E060`, carried).
- Live 12.4: Session mixer In/Out (`session-065`), slot / scene MIDI mapping threshold (`session-085`, needs a controller),
  Split / Join (`detail-062`, new), the Cue-mode preview on a 4-output interface (new), the four Live screen rows kept
  unchanged (launch velocity, automation-lane choosers, the Drum Rack layout, the startup dialog).
- FLM: Pitcher / Pitcher 2 on a vocal, Note Echo presets, the 3x Osc held rack bend, the audio-clip stretch session (FLM's
  stretcher is zplane's élastique Pro V3 — `PSOLASynthForElastiqueProV3`, `CElastiqueV3` in FLM 5.0.3's library — a
  licensed black box), one real FLM-exported .flp.
- GarageBand: a real GarageBand 10 project with tempo changes (carried).

## 8. Not verified (plain list)

- Every status comes from reading source; nothing behavioural is scored here.
- "+ pending" is a local merge (branch wins); 0.1.31's real merges may differ in the i18n tables and tests.
- Sonnet judged flctl, flmenu, flpr, flpl and fledison (diff-first); Opus judged flrack, live1, live2, flm, bandlab +
  garageband, screens + tech. 7 FL, 25 Live and 8 FLM rows are still unknown and count as not covered.
- The REA obligation ledger of 190jw was not rebuilt for these bases (the per-row citations are re-opened by script
  instead; the ledger's "verified" stays 0 by method).

## Files (all in `/mnt/e/lynx-work/coverage190og/`)

`COVERAGE-0.1.31.md` (this file), `AGENT-BRIEF.md`, `SCREENS-BRIEF.md`, `HANDOFF.md`; `in/`, `out/` (11 clusters),
`ledger-rows-190og.json` (every row: prev / v030 / r031 / r031p, evidence, note, changed_by), `notes/` (`agg-final.txt`,
`summary.json`, `moved.txt`, `uncovered-v3.txt`, `top-uncovered.md`, `verify-*.json`, `changed-*.txt`, `commits.txt`),
`screens/`, `tech/`, `live13/`, `flm/` (the FLM REA notes, render projects and renders), `tools/` (`common6.py`,
`verify6.py`, `mkin6.py`, `check_out6.py`, `agg6.py`, `verify6now.py`). Snapshots `og-*` in the session scratch folder
(temporary).
