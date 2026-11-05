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
| tempo, time signature, swing, title / info, PPQ | project globals; PPQ 96 when every tick is a multiple of 10, else 960 |
| sampler / audio clip channel | sampler / audio clip with its settings (volume v·12800, pan, pitch = cents/100 + (60 − root), root, envelopes, filter, arp, time-stretch, loop points, fx flags) |
| FL-native generator (3x Osc, SimSynth, DX10, Plucked!, Wave Traveller with any state; the others with their FL state) | the generator by FL's exact name with its state |
| VST2 / VST3 / CLAP plugin | Fruity Wrapper v12 (records 01, 02, 1E, 1F, 20, 32, 33/34/3A, 36, 37, 35; 0x32 type VST2 0/4, VST3 7/8, CLAP 11/12 effect/generator); the state in FL's layout (VST3: u32 1 + {1: 64 B, 3: component, 2: controller}; VST2 as LYNX's host keeps it; CLAP: u32 0 + bytes) |
| an FL plugin imported from FL and unchanged | byte-exact: `FlOrigin` keeps FL's name and state, its fingerprint (params quantised 1e-4) says it is unchanged |
| patterns, notes (velocity·128, pan, fine pitch, slide/porta, colour), step notes (length 0), event lanes | the same |
| Playlist: pattern / audio / automation clips, tracks, markers, arrangements; Pattern Blocks | the same; Pattern Blocks as pattern clips (`patternblocks::toClipsInEveryArrangement`) |
| mixer: names, colours, icons, routes, sends, volume (v·16000) / pan / stereo, the 3-band EQ, flags | the same |
| automation clips (mixer, channel, effect params, tempo 60..180) and links, initialised controls | the same (FL automation channels after the song's) |

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

(filled in below by the session)

## 6. Decisions for the user

1. LYNX-made FL-engine effects (e.g. Fruity Reeverb 2 added inside LYNX, never seen by FL) have no FL state LYNX can
   write yet: option A bounces their insert, option B leaves them out. Writing their state from LYNX's parameters needs a
   per-plugin state writer (one per FL effect; the import side exists for most). Worth doing for the common ones?
2. LYNX-only effects on the Master can't be rendered without rendering the whole song (left out, reported).

## Where I stopped

(filled in at the end of the round)
