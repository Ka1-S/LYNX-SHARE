# Technology gap map 0.1.30: FL Studio 2026, Ableton Live 12.4, FL Studio Mobile, BandLab, GarageBand vs LYNX (round 190jw, 2026-11-16)

This re-judges every LYNX cell of `cov190gf/TECH-GAPS.md` (190gf, 2026-11-13) on three bases and adds interop and
GarageBand rows. It is the engine and technology view; screens are in `../screens/SCREENS-0.1.30.md`.

**Bases** (snapshots, source only, nothing built or run):
- **v029** = tag v0.1.29-alpha, 0.1.29 as released (= 0.1.28 + the Go back hotfix). This is the "before".
- **r030** = `origin/wip/rollup030` 4c45364e4, the 0.1.30 integration.
- **r030p** = r030 + the finished, pushed, not yet merged branches (hosting190jg, dawproj190jf, midiout190jl, bandlab190jr,
  livefix190jd, livelook190jn, polish190jp-je, playlist190jq, capfix190jj, parity190jh / js, workflow190iu, perfgate190id).

**Method.** Each status is read from source with a fresh `path:line ('quote')` citation; `tech_rows.py` re-opens every
quote in its own base (47 rows, 0 failures) and writes `tech-rows.json` (all three bases' evidence per row). Facts about
the originals are carried over from 190gf (REA, install strings, manuals; *(docs)* there means public documentation only).
GarageBand facts come from Apple's public guide topics (`in/garageband-rows.json`) and are given only where they matter.

**Legend:** `has`, `partial`, `missing`, `ahead` (LYNX does more than the originals). A **bold** status is a move.

## What changed since 190gf

190gf read a simulated 0.1.28 integration, which was missing several items that were in **0.1.28 as released**. So some
"missing" cells were already wrong on v029. These are corrections, not 0.1.30 work:
- **P2 render-ahead: missing → ahead on v029.** Round 190gq, on by default (owner decision O22). Non-live paths render
  ahead on the worker pool, while armed / monitored paths stay real-time. FL has only "Triple buffer" and Live has nothing.
- **P9 stems in one pass: partial → has on v029.** One render of the song makes every stem (StemPass).
- **T13 Ableton Link: missing → has on v029.** `engine/AbletonLink.cpp`, a toolbar toggle with the peer count, and
  Settings › Link.

**0.1.30 moves (v029 → r030 → r030p):**
- **r030:**
  - **T7 time-stretch: partial → has.** Round 190ik: Complex Pro formants and envelope, granular Texture with Flux, a
    pitch-aware Beats detector, and warped audio that follows tempo automation (issue #187).
  - **F5 .band: missing → partial.** Round 190ic.
  - **F6 .dwp / .dwb / .gig / NCW: partial → has.** Round 190ii.
- **r030p only** (these branches are not yet merged):
  - **P8 grouped sandbox processes: has → ahead.**
  - **P10 sample-accurate hosted automation: partial → has.**
  - **T4 CLAP extensions: partial → has.** thread-pool, tail, render, voice-info, state-context, remote-controls and
    preset-load.

    P8, P10 and T4 all come from 190jg.
  - **T9 stem options: partial → has.** Round 190jr.
  - **F3 DAWproject: missing → has.** Round 190jf.
  - **F4 BandLab .blx: missing → has.** Round 190jr.
  - **F7 hardware MIDI out: partial → has.** Round 190jl.
- **No regressions.** No cell is worse on r030 than on v029, or on r030p than on r030.

**Counts:**

| Base | has | ahead | partial | missing |
|---|---:|---:|---:|---:|
| v029 | 18 | 5 | 16 | 8 |
| r030 | 20 | 5 | 15 | 7 |
| r030p | 25 | 6 | 11 | 5 |

The counts include the new F / G rows: 47 rows in all.

## Tables
### 1. Performance technology (P1-P12)

| # | Technology | GarageBand | LYNX v029 → r030 → r030p | Evidence (r030p) | What changed / what is left |
|---|---|---|---|---|---|
| P1 | Multicore scheduling of the graph |  | has → has → has | src/core/WorkerPool.h:3 ('Thread pool for the audio thread to split a block'); src/ui/SettingsPages.cpp:1036 ('Multithreaded generator processing') | Unchanged: worker pool splits channels and mixer layers. The serial-chain ceiling is now lifted by P2 render-ahead. |
| P2 | Anticipative / render-ahead processing |  | ahead → ahead → ahead | src/core/RenderAhead.h:3 ('Render-ahead (anticipative processing; LYNX extra, round 190gq'); src/ui/SettingsPages.cpp:1041 ('Render ahead (lower CPU at small buffers)'); src/engine/ExportParityTest.cpp:340 ('setRenderAheadAlways (true)') | 190gf said missing; it landed in 0.1.28 (round 190gq, on by default, owner decision O22): non-live paths render ahead on the workers, armed / monitored paths stay real-time. FL has only "Triple buffer", Live none: LYNX is ahead. r030: the ahead part takes the automation grid when it engages mid-song (190jb, efba0895c) and the overrun / CPU trade-off owner decision (92d6fcd3f). |
| P3 | Smart disable / idle plugins |  | ahead → ahead → ahead | src/core/RackRenderer.cpp:2641 ('smartDisable.assign'); src/core/RackRenderer.cpp:3725 ('a hosted plugin too, when it declares its tail') | r030p: a hosted CLAP plugin's declared tail ends its smart-disable hold (190jg, 83664e011) - the gain 190gf listed. |
| P4 | Freeze / bounce |  | has → has → has | src/core/StandIns.h:49 ('withInsert (round 190ff, track freeze)') | Unchanged. Background freeze while editing is still not there (Live freezes in the foreground too). |
| P5 | Disk streaming / RAM |  | has → has → has | src/engine/SampleLibrary.h:45 ('streamAboveSeconds = 30.0') | r030: a .lynx opens with its samples decoded in parallel (0d442d629) and the workers' malloc arenas given back after it (5171803dc). |
| P6 | GPU UI |  | partial → partial → partial | src/ui/SettingsPages.cpp:295 ('Draw with the GPU (Direct2D)'); src/ui/MotionOverlay.h:14 ('Linux / macOS: not available yet') | r030: moving marks (playheads, locators) go through a GPU composition overlay on Windows (190ip MotionOverlay); Linux and macOS still draw on the CPU (JUCE software renderer) and get no overlay. |
| P7 | Low-latency monitoring path |  | ahead → ahead → ahead | src/core/LowLatency.h:3 ('"Low latency while recording"'); src/ui/MixerComponent.cpp:3343 ('Compensate for master latency') | Unchanged (190gf: has, and goes further). |
| P8 | Plugin sandboxing and its cost |  | has → has → **ahead** | src/engine/PluginSandbox.h:11 ('Optional out-of-process plugins ("sandbox")'); src/engine/PluginSandbox.h:30 ('juce::String sandboxGroupFor') | r030p: grouped plugin processes (one child per group / vendor / mode, 190jg, 2dff8416d) - the gain 190gf ranked #3. Not merged in r030. |
| P9 | Offline render speed / stems in one pass |  | has → has → has | src/ui/ExportDialog.cpp:144 ('one render of the song, as FL makes them') | 190gf said partial (N renders); one-pass stems (StemPass) are in 0.1.28 as released. r030: exports render with the live plugin instances under a hold, as FL (190jb, 4d39140ef): faster warm-up (1337 -> 395 ms on the test synth). |
| P10 | Sample-accurate automation into hosted plugins |  | partial → partial → **has** | src/engine/PluginHost.h:44 ('timing is sample-accurate'); src/engine/TimedParameters.h:3 ('sample-accurate automation for hosted plugins') | r030p: hosted plugins get parameter changes at their sample offsets inside the block (VST3 queues / CLAP events, also across the sandbox; 190jg). Not merged in r030. |
| P11 | Overload safety ("safe overloads") |  | partial → partial → partial | src/ui/SettingsPages.cpp:1202 ('dropouts (buffer underruns) so far') | Unchanged: dropout counter and per-slot profiling; no "Safe overloads" UI-priority guard (searched "Safe overloads", "safe overload": none). r030 added a performance gate in CI, not a runtime guard. |
| P12 | Plugin buffer handling |  | has → has → has | src/ui/PluginWindow.cpp:2740 ('Process maximum size buffers') | Unchanged. |

### 2. Other engine and plugin technology (T1-T23)

| # | Technology | GarageBand | LYNX v029 → r030 → r030p | Evidence (r030p) | What changed / what is left |
|---|---|---|---|---|---|
| T1 | ARA 2 hosting | no | missing → missing → missing | searched: ARA, ARAFactory, ARA_ in src/engine (no host code) | Unchanged. Neither FL nor Live has it; GarageBand neither (Logic does). |
| T2 | MPE | GarageBand iPad: MPE controller input only | has → has → has | src/engine/PluginHost.h:275 ('mpe::Allocator'); src/ui/PianoRollComponent.cpp:1768 ('tr ("MPE pitch bend")') | r030: per-note pitch-bend / slide / pressure curves edited in the clip (MPE stage 2, 190hy); r030p: Live 12.4 MPE tab (190jd). |
| T3 | VST3 note expression / per-note IDs |  | partial → partial → partial | src/engine/PluginHost.cpp:23 ('engine/Vst3NoteTuning.h') | Unchanged: per-note tuning and ids through the JUCE patch; no full NoteExpression (volume / pan / brightness per note). |
| T4 | CLAP hosting (extensions) | no (AU / AUv3 only) | partial → partial → **has** | src/engine/ClapFormat.h:66 ('LYNX hosts clap.thread-pool'); src/engine/ClapFormat.cpp:450 ('CLAP_EXT_THREAD_POOL') | r030p: thread-pool (plugin tasks on LYNX workers), tail, render (offline mode), voice-info, state-context, remote-controls, preset-load (190jg). Polyphonic modulation still not hosted. Not merged in r030. |
| T5 | Plugin formats | AU (Mac), AUv3 (iPad) | ahead → ahead → ahead | src/engine/Vst2Format.cpp:1 ('#include "engine/Vst2Format.h"') | Unchanged: VST2 / VST3 / CLAP / LV2 / AU hosting and a 32-bit bridge; LYNX also exports its plugins as VST3 / CLAP. |
| T6 | Plugin delay compensation |  | has → has → has | src/core/RackRenderer.h:163 ('PDC: per-edge delays') | Unchanged (r030 changed core/Pdc.cpp for the tempo-marker / delay audit of 190hq). |
| T7 | Time-stretch / warp quality | Flex Time (Mac) - LYNX has warp markers + audio quantize | partial → **has** → has | src/core/WarpEngines.h:6 ('Complex Pro: the clip'); src/ui/ClipAudioPanel.cpp:564 ('Formants: how much of the sound') | r030 (190ik): Complex Pro is its own engine with formant correction and envelope, Texture a granular engine with Flux, a pitch-aware Beats detector; warped audio follows tempo automation (issue #187, 48b32c735 / 97772dea4). Signalsmith-based, no commercial engine. |
| T8 | Pitch correction | Pitch Correction slider + Limit to Key: LYNX Tune covers it | has → has → has | src/core/LynxTune.h:5 ('TD-PSOLA') | Unchanged: LYNX Tune, Newtone-style editor, VocalPitch. Open: Auto Shift's MIDI side-chain harmonies. |
| T9 | Stem separation |  | partial → partial → **has** | src/ui/StemExtract.cpp:26 ('htdemucs-onnx'); src/ui/StemExtract.cpp:595 ('tr ("Limit CPU usage")'); src/ui/StemExtract.cpp:583 ('TR_NOOP ("Guitar")') | r030p (190jr): FL's Extract stems form - per-stem ticks, Limit CPU, Safe mode, When done, group tracks, new mixer tracks - and a 6-stem model (Guitar, Piano). Still no lead / backing vocal split (BandLab). |
| T10 | Audio-to-MIDI |  | has → has → has | src/ui/AudioToMidi.cpp:21 ('Basic Pitch') | Unchanged. |
| T11 | De-reverb / voice clean-up |  | has → has → has | src/ui/AudioEditorTools.h:57 ('deverb') | Unchanged. BandLab's AutoEQ still open. |
| T12 | Scripting (Python) and an MCP / AI-tool surface |  | partial → partial → partial | src/core/VfxScript.h:3 ('VFX Script') | Unchanged: piano-roll / Edison / VFX scripts; no FL-compatible controller scripts, no MCP server (searched MCP: none; O21). |
| T13 | Ableton Link | GarageBand iPad has Link; LYNX at parity | has → has → has | src/engine/AbletonLink.cpp:82 ('ableton::Link link;'); src/ui/Toolbar.h:245 ('class LinkToggle final') | 190gf said missing; Ableton Link is in 0.1.28 as released (toggle with peer count, start / stop sync, Settings › Link). Link Audio (Live 12.4) is not there. |
| T14 | MIDI 2.0 / UMP |  | missing → missing → missing | searched: UMP, MIDI 2.0, ump:: in src (none) | Unchanged. |
| T15 | Surround / spatial | no | missing → missing → missing | searched: Atmos, surround, ADM, binaural panner (none) | Unchanged; out of scope per decision J. |
| T16 | Audio driver handling |  | ahead → ahead → ahead | src/engine/PipeWireDevice.h:2 ('own PipeWire client (round 161)') | Unchanged: own PipeWire client, JACK, ALSA, ASIO loader, WASAPI, CoreAudio. Own multi-client ASIO driver: not now (O24). |
| T17 | Video | Movie track: at parity | has → has → has | src/ui/VisualEditors.cpp:2302 ('VideoExportThread') | Unchanged. |
| T18 | Cloud / collaboration | iCloud sync, Jam Session: LYNX partial | partial → partial → partial | src/engine/LiveLink.h:3 ('Online Sessions') | Unchanged: Online Sessions over the LYNX relay, chat, local versions; no cloud backup / sync, no cloud sound library. |
| T19 | Mobile app | GarageBand iPad / iPhone | missing → missing → missing | searched: no iOS / Android target; TouchKeyboard and Large controls only | Unchanged as an app; r030 adds View › Large controls (touch size) to the Simple layout (6127d8a33). |
| T20 | Tempo Follower / Lead-Follow warp |  | missing → missing → missing | searched: Tempo Follower, tempo follow from audio input (only a comment in core/LinkSync.h) | Unchanged (LinkSync.h only names the Live manual chapter). |
| T21 | Comping / take lanes | Take folders: at parity | has → has → has | src/core/Comping.h:3 ('Comping on take lanes') | r030p: deleting takes (190iu, cf9c0da57). |
| T22 | Crash protection / safe mode |  | has → has → has | src/platform/CrashGuard.h:5 ('On Windows this is SEH') | r030p: grouped sandbox processes keep a crash inside its group (190jg). |
| T23 | AI assistant / generators | Drummer is GarageBand's generator (see G1) | partial → partial → partial | src/ui/ReferenceMatchPanel.h:3 ('Reference match (round 190ch') | Unchanged: MIDI Tools Generate, Loop Starter, reference-match mastering; no chat assistant, no song starter. |

### 3. Interop formats and GarageBand-specific technology (new at 190jw)

| # | Technology | GarageBand | LYNX v029 → r030 → r030p | Evidence (r030p) | What changed / what is left |
|---|---|---|---|---|---|
| F1 | FL Studio .flp import / export |  | has → has → has | src/core/FlpImport.cpp:134 ('guessInstrument (const std::string& channelName, flp::ChannelType type)'); src/core/FlpExport.cpp:28 ('namespace lynx::flpexport') | r030: a copy for 0.1.28 knows the 0.1.30 rounds (8047336c2); automation clip colours round-trip (190ig). |
| F2 | Ableton Live .als / .adg / .adv import / export |  | has → has → has | src/core/AlsImport.cpp:136 ('bool inDrumPad = false;'); src/core/AlsDevicesWt.cpp:1 ('Wavetable → Helix') | r030: Live's Operator onto Tetra and Wavetable onto Helix and back (190hs / 190ja), racks as .adg (190hr). |
| F3 | DAWproject import / export |  | missing → missing → **has** | src/core/DawProject.h:3 ('DAWproject import / export (round 190jf, B22)') | r030p (190jf): open exchange format of Bitwig / Studio One / Cubase. Not merged in r030. |
| F4 | BandLab .blx import |  | missing → missing → **has** | src/core/BlxImport.h:3 ('BandLab project (.blx) import, round 190jr') | r030p (190jr): the Cakewalk / BandLab interchange file next to a downloaded project. Not merged in r030. |
| F5 | GarageBand .band import | the GarageBand project format | missing → **partial** → partial | src/core/BandImport.h:3 ('GarageBand project (.band) import, round 190ic') | r030 (190ic, 09e161828): tracks, MIDI / audio regions, tempo, key, level, pan, mute, patch names onto LYNX presets; effects, Drummer / Smart Drums regions, tempo changes and song sections are listed in the report, not converted; GarageBand 6-11 projects are refused. Never run on a real Mac project with tempo changes / take folders (live check). |
| F6 | Sampler instrument formats (.dwp / .dwb / .gig / Kontakt NCW) |  | partial → **has** → has | src/engine/NcwFormat.h:3 ('Kontakt'); src/core/GigFile.h:3 ('instruments (.gig) as LYNX DirectWave zones') | r030 (190ii, issue #149): .dwp / .dwb / .gig become DirectWave channels, NCW samples decode; v029 had SFZ / SoundFont only. |
| F7 | Hardware MIDI out on a port |  | partial → partial → **has** | src/ui/Dialogs.cpp:348 ('MIDI Out channels play the plugins and the MIDI output devices on their port') | r030p (190jl, 7a1371d40): MIDI Out channels play hardware MIDI outputs on their port, Stop sound sends CC 123. |
| F8 | Apple Loops metadata (tempo / key / beat tags in CAF / AIFF loops) | Apple Loops are GarageBand's loop format | partial → partial → partial | src/ui/ChannelSettings.cpp:3179 ('Load ACID and Apple Loops beat markers') | ACID beats and Apple Loops beat markers are read for tempo; the loop's key tag is not used for key-follow and Apple Loops keywords are not read for browsing. |
| F9 | Audio export encoders (M4A-AAC / AIFF besides WAV / MP3 / OGG / FLAC) | AAC / MP3 / AIFF / WAV | partial → partial → partial | src/ui/ExportDialog.cpp:429 ('"WAV", "MP3", "OGG", "FLAC"') | Unchanged: no AAC (M4A) or AIFF writer; BandLab Mixdown, GarageBand Export Song to Disk and FL Studio Mobile share all ask for M4A (BUILD-LIST O31: M4A through the OS encoder). |
| G1 | Virtual drummer (Drummer: genre, character, complexity / loudness, follow a track) | Drummer (Mac + iPad) | partial → partial → partial | src/ui/LoopStarterWindow.cpp:241 ('Loop Starter genre') | Loop Starter generates genre patterns and Generate steps; no performer model with XY complexity / loudness, fills, or kick / snare follow. .band Drummer regions are only listed on import. |
| G2 | Smart Controls (curated macro panel per patch) | Smart Controls | partial → partial → partial | src/ui/RackEditor.cpp:92 ('Macro {0}') | Rack macros and the Simple dock knobs; no per-sound curated panel or Transform Pad. |
| G3 | Live Loops / clip launching | Live Loops (Mac + iPad) | has → has → has | src/core/ClipLauncher.h:80 ('A pass the recorder turns into a Playlist clip') | Clip grid with launch quantisation, follow actions, capture; r030: Live's look and keys (190ig / 190ih / 190im). Remix FX XY panel and starter grids missing (screens). |
## Ranked biggest remaining technology gains (on r030p)

0. **Land the finished branches.** Seven of the ten 0.1.30 moves above exist only in r030p (P8, P10, T4, T9, F3, F4, F7).
   Until hosting190jg, dawproj190jf, bandlab190jr and midiout190jl are merged, 0.1.30 ships the r030 column. That is not
   new work, but it is the largest single step.
1. **P6 GPU drawing on Linux / macOS** (M-L).
   - P2, P8, P9 and P10 are now done, so this is the largest performance item left.
   - On Linux / macOS the main windows still use JUCE's software renderer, and 190ip's moving-mark overlay is Windows only
     (`src/ui/MotionOverlay.h:14`).
   - Owner priority: performance. Measure first with the perf gate (190id).
2. **P11 Safe overloads** (S-M). Keep the UI thread responsive when the audio thread overruns repeatedly. Nothing found in
   source. The CI performance gate (190id) measures; it does not guard at run time.
3. **F9 M4A / AAC (and AIFF) export** (S-M).
   - One encoder closes value-5 screen gaps in three programs: BandLab Mixdown As, GarageBand Export Song to Disk and FLM
     share.
   - Already decided: O31, through the OS encoder only. Queued as BUILD-LIST S5.
4. **G1 a virtual drummer** (L). This is GarageBand's main generator and the GarageBand "Drummer" area's 0-25 %. Loop
   Starter only generates genre patterns. A LYNX-own design under owner decision O21 / J ("later, non-AI") would cover:
   - a performer model with complexity / loudness;
   - fills and swing;
   - kick / snare follow;
   - converting .band Drummer regions.
5. **F5 .band depth** (M) and a live check on a real GarageBand 10 Mac project. Convert, instead of only listing:
   - tempo changes;
   - song sections (with their letters);
   - track effects (Channel EQ → Facet EQ, Compressor → Crest, …);
   - take folders.
6. **F8 Apple Loops metadata** (S-M). Read the loop's key tag, so a loop follows the project key when previewed and
   dropped. Read its instrument / genre / mood keywords for the Browser. This also lifts GarageBand's Loop Browser
   screen (value 5).
7. **G2 Smart Controls** (M). A curated macro panel per sound (preset-defined macros in the Simple dock), plus an XY
   morph pad. LYNX already has rack macros and Variation morph.
8. **T1 ARA 2** (L). A LYNX-only advantage: FL, Live and GarageBand have none. The ARA SDK is Apache-2.0.
9. **T12 MCP / controller-script surface** (M each). Owner decision O21: local only, opt-in.
10. **T3 VST3 NoteExpression** (M), **T20 Tempo Follower** (M), and **P4 background freeze** (S-M): smaller quality
    gains.

Out of scope or decided not now: T14 MIDI 2.0 (value 1), T15 surround (decision J), T16 own ASIO driver (O24), T18 cloud
sync and T19 mobile app (product decisions).

## Owner decisions

No new ones. O21 to O24 from 190gf still stand:
- O21: MCP, local and later.
- O22: render-ahead on by default, now taken and shipped.
- O23: no power-plan switch.
- O24: no own ASIO driver.

## What could not be verified

- **Source only.** Nothing was built, run or measured. Render-ahead, grouped sandboxing and sample-accurate automation
  are judged from code, tests and the HANDOFFs' measured numbers. Speed claims (export 1337 → 395 ms, perf gate) are the
  rounds' own.
- **r030p is a local merge.** Its conflicts were resolved "branch wins", so a few hunks may differ from the real merge.
- **GarageBand.** These facts are from Apple's public guides. GarageBand was not run. The .band import has never opened
  a project with tempo changes, take folders or Drummer regions (190ic).
- **macOS AU hosting** is judged from the build flags only.
