# REA round 171e — Export to FL Studio (idea by Tookolld)

Evidence markers: [DATA] FL's own files read byte by byte (the 990 .flp of the corpus, FL 2026's templates); [FL]
FL Studio 2026 itself (both FL locks, command-line renders `FL64.exe /R /Ewav`, one GUI launch for the screenshots);
[CODE] LYNX source; [TEST] LYNX tests. Clean-room: the .flp layout is the one FlpImport already reads (the inverse of
flp::parseProject); nothing of FL's is shipped (the default Fruity Limiter state is 169 bytes of FL's own default,
the same bytes any new instance saves).

## 1. What it does [CODE]

File › Export › **FL Studio project...** opens a small window: *Save as* `.flp` or the zipped loop package (`.zip`: the
.flp and every sample, by file name), and *What FL Studio doesn't have*: **render to audio** (option A, default) or
**the nearest FL plugin** (option B). Then a file dialog, a progress window (option A's renders), and a report window:
Exported exactly / Rendered to audio / Replaced by an FL plugin / Left out / Worth knowing / Samples not on this PC.
Command line: `LYNX --export-flp project.(lynx|flp) out.(flp|zip) [--substitute]` (report in `out.<ext>.txt`).

Layers:
- `src/flp/FlpWrite.{h,cpp}` — `flp::writeProject`, the exact inverse of `flp::parseProject`, events in FL 2026's order.
- `src/core/FlpExport.{h,cpp}` — Song → `flp::Project` (no JUCE): the mapping, the render plan, the report.
- `src/engine/FlStudioExport.{h,cpp}` — plugin lookups (Fruity Wrapper records), the renders, the .flp / .zip file.
- `src/ui/FlStudioExportDialog.{h,cpp}` — the window, the worker thread, the report.

## 2. Writer = inverse of the reader [DATA][TEST]

Round trip `parse → write → parse` over the 990 corpus .flp: identical projects except FL files whose absent values
LYNX had written as defaults; fixed (FL writes these only when set):
- pattern meta (0xC1 name, 0x96 colour, …) only for non-default patterns (g1.flp has none);
- channel colour 0x80 and pattern colour 0x96 only when set;
- a pattern clip's gain as FL wrote it (0 or 1, both seen);
- FL 26 0xD4 flags: samplers 0x50 / 0x51, generators 0x150, effects 0x140 / 0x160 (0x20 = smart disable);
- inserts the song does not describe route to the Master.
The 11 remaining files were LYNX-written synthetic test files with no envelopes (acceptable).

## 3. The mapping (inverse of FlpImport) [CODE]

| LYNX | FL |
|---|---|
| tempo, time signature, swing, title / info, PPQ | project globals; PPQ 96 when every note and Playlist clip lands on 96's grid (the rest is rounded), else 960; `--flp-ppq N` forces it |
| sampler / audio clip channel | sampler / audio clip with its settings (volume v·12800, pan, pitch = cents/100 + (60 − root), root, envelopes, filter, arp, time-stretch, loop points, fx flags) |
| FL-native generator (3x Osc, SimSynth, DX10, Plucked!, Wave Traveller with any state; the others with their FL state) | the generator by FL's exact name with its state |
| VST2 / VST3 / CLAP plugin | Fruity Wrapper v12 (records 01, 02, 1E, 1F, 20, 32, 33/34/3A, 36, 37, 35; 0x32 type VST2 0/4, VST3 7/8, CLAP 11/12 effect/generator); the state in FL's layout (VST3: u32 1 + {1: 64 B, 3: component, 2: controller}; VST2 as LYNX's host keeps it; CLAP: u32 0 + bytes) |
| an FL plugin imported from FL and unchanged | byte-exact: `FlOrigin` keeps FL's name and state, its fingerprint (params quantised 1e-4) says it is unchanged |
| patterns, notes (velocity·128, pan, fine pitch, slide/porta, colour), event lanes | the same; a one-step note on the grid is FL's step note (length 0) only on a sample-playing channel (§5.3) |
| Playlist: pattern / audio / automation clips, tracks, markers, arrangements; Pattern Blocks | the same; Pattern Blocks as pattern clips (`patternblocks::toClipsInEveryArrangement`) |
| mixer: names, colours, icons, routes, sends, volume (v·16000) / pan / stereo, the 3-band EQ, flags | the same |
| automation clips (mixer, channel, effect params, tempo over FL's whole 10..522) and links, initialised controls; clips linked to nothing | the same (FL automation channels after the song's, levels pan 0 / volume 12800 = the whole range, §5.3); unlinked clips stay unlinked |

## 4. What only LYNX has [CODE][TEST]

- **Option A** (`planRenders`): every channel with no FL equivalent (LYNX drums / synth / soundfont / multi-sample
  sampler / patcher...) is **frozen**: rendered alone through a clean Master (its own volume, pan and their automation in
  the render) to a 32-bit float WAV, then it becomes an audio clip channel at unity playing the render from tick 0 on a new
  Playlist track, its notes and automation removed (`applyFreeze`). Every insert holding an active effect FL can't have
  is **bounced** (downstream first; an insert heard only through bounced inserts is skipped): `makeBounceSong` → render →
  `applyBounce` (a new insert with the same routing, the original muted), the original's LYNX-only slots switched off with
  their automation (`applyInsertRender`). The Master is never bounced (it would be the whole song): its LYNX limiter
  becomes Fruity Limiter (FL's default state), other LYNX-only Master effects are left out (reported).
- **Option B**: the LYNX limiter → Fruity Limiter (default state); anything else LYNX-only is left out and reported.
- Muted Playlist clips with no FL equivalent are left out with a note.

## 5. FL verification [FL]

Material in `E:\lynx-work\flpexport171e\` (`proj/` the sources, `out/` + `out2/` the exports, `flr/` + `flr2/` FL's
renders, `shots/` the screenshots). Three projects: **p1** a beat (3 samplers, 3x Osc lead, LYNX test synth as VST3 and as
CLAP), **p2** a mix (2 samplers, 3x Osc bass, VST3 Pro-Q 4 + CLAP on the vocal insert, a LYNX reverb insert fed by a
send, mixer volume and pan automation), **thief** an FL 2026 demo (45 channels, 82 FL plugin states) imported into LYNX
and exported back. Two FL sessions (both locks): command-line renders `FL64.exe /R /Ewav` of every export, then one GUI
launch each for screenshots. FL opened every file (.flp and .zip) with no error dialog.

### 5.1 What FL showed

- **Opens**: all 8 exports render; the GUI shows the right channels, playlist, mixer routing, automation clips, the
  rendered clips on their new tracks and the muted originals (`shots/h1-p2.png`), the zip's samples found (`g1-p1zip.png`).
- **The .zip** (FL's loop package): FL renders it straight from the zip; its render equals the .flp's (p1z = p1, bit
  for bit after alignment).
- **Plugin states**: the VST3 and CLAP plugins load inside Fruity Wrapper with their states and play; thief's 82 FL
  plugin states are byte-identical after LYNX's round trip (86.4 % of all events identical; the rest are LYNX's
  defaults FL never wrote, order and re-quantised ticks).
- **PPQ**: a PPQ-960 export plays the same as its PPQ-96 twin (p1: rms +0.02 dB, env_corr 0.999, spectrum 0.11 dB) but
  FL renders it ~4× slower (thief: 525 s vs 128 s); hence 96 whenever the song allows it.

### 5.2 Fidelity numbers (`scripts/flref_compare.py pair`)

| comparison | rms | env_corr | spectrum | note |
|---|---|---|---|---|
| thief: FL original vs LYNX's import (baseline: LYNX's FL parity) | −0.55 dB | 0.942 | 0.98 dB | |
| thief: LYNX original vs LYNX's import of the export | −0.53 dB | 0.952 | 0.27 dB | the export loses nothing LYNX plays |
| p1 PPQ 960 vs PPQ 96 (both in FL) | +0.02 dB | 0.999 | 0.11 dB | |
| p1: FL render of the export vs LYNX | −5.5 dB (FL louder) | 0.998 | 5.2 dB | same shape; a level gap, see 5.3 |
| thief: FL original vs FL render of the export (sessions 1–2) | — | −0.23 | — | **wrong**: plays ~2.5× too fast and stops at 95 s, fixed (5.3) |
| p2: FL render of the export (sessions 1–2) | grows −10 → −0.6 dBFS | — | — | **wrong**: the bass piles up to clipping, fixed (5.3) |

### 5.3 What the FL renders taught (fixed this round)

1. **An automation clip's channel levels are its range** [DATA][FL]: pan = the low end, volume = the high end (of
   12800), in the target's whole range. FL's own tempo clips store 1250 / 4250 = 60..180 of FL's 10..522 BPM. LYNX wrote
   the channel defaults (6400 / 10000), so thief's TEMPO clip played ~2.5× faster and the song ran out at 95 s. Now the
   export writes 0 / 12800 (the whole range, tempo values unmapped) and **the importer honours the range** (it assumed
   60..180 for every tempo clip and 0..1 for the rest; a clip with no level record keeps those defaults).
2. **Step notes on generators** [FL]: a length-0 note (FL's step) on a 3x Osc is held until something cuts it; p2's
   one-step bass notes became 64 overlapping voices. Step notes now go only to sample-playing channels; a generator
   gets the same note with its real length (thief has no step note on a generator, so its round trip is unchanged).
3. **3x Osc with no state** [FL][REA]: an empty state made FL's 3x Osc notes never end; LYNX's default now goes as
   FL's own version-14 state (460 B: 21 params + the 364-byte snapshot FUN_00738580 loads).
4. **Engines LYNX doesn't play byte-exact** (SimSynth etc. made in LYNX, no FL state): rendered (option A).
5. **A plugin not on this PC** (stand-in) keeps its Fruity Wrapper record, so FL loads it where it is installed.
6. **Channel envelopes**: FL applies a channel's 0xDA records after the plugin state; the export writes the volume
   envelope enabled only when LYNX shapes the note (checked: p1/p2's generators go with it off).
7. A link LYNX's importer can't map (thief's "SNARE PITCH", channel param 0x000D0004) stays an automation clip
   linked to nothing; FL keeps it, it moves nothing (as in LYNX).

The p1 level gap (FL 5.5 dB louder, shape identical) is LYNX-vs-FL engine parity of the instruments themselves (the
VST/CLAP test synth and the 3x Osc default patch), not the file: LYNX's import of the export plays within 1.1 dB of the original (env_corr 0.998).

## 6. Decisions for the user

1. LYNX-made FL-engine effects (e.g. Fruity Reeverb 2 added inside LYNX, never seen by FL) have no FL state LYNX can
   write yet: option A bounces their insert, option B leaves them out. Writing their state from LYNX's parameters needs a
   per-plugin state writer (one per FL effect; the import side exists for most). Worth doing for the common ones?
2. LYNX-only effects on the Master can't be rendered without rendering the whole song (left out, reported).

## Where I stopped

(filled in at the end of the round)
