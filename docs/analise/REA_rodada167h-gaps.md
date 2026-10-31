# Round 167h — gap list part 2: the Options dialog, from FL's own form resources

Continues `REA_rodada167g-gaps.md` (the .flp items; this round closed #7 and the 0x28 part of #13, see
`REA_rodada167h.md`). Source: the Options form's DFM resource strings in FL Studio 2026's `FLEngine_x64.dll`
(control id `forms.options.<page>.<panel>.<control>`, its Caption and Hint, extracted by `tmp/r167h/py/gap.py`) [DFM],
handlers found in the form's RTTI method table [RTTI] and decompiled where traced [DECOMP]. LYNX columns: what
`src/ui/SettingsPages.cpp` / `src/ui/SettingsDialog.cpp` offer today.

Effort: S < ½ day, M ≈ 1 day, L > 1 day. Value: H = audible / visible on every imported project, M = visible on some,
L = cosmetic or rare.

## Project › General (`projgeneralpage`)

| # | Item (FL caption) | FL evidence | LYNX now | Effort | Value | Status |
|---|---|---|---|---|---|---|
| 1 | **Fast declick for cut groups** (`projadvpanel.fastdeclickbox`, hint "Use faster ramping when cutting notes") | DFM; FastDeclickBoxClick 0x1358d30 [RTTI → DECOMP] sets the global the voice-cut methods FUN_00d24160 / FUN_00d241a0 read; .flp 0x28 | was missing | S | M | **done** (REA_rodada167h §3) — see the open question there |
| 2 | **Panning law** (`projadvpanel.panlawselect`: Circular / Triangular) | DFM items; CircPanBoxClick 0x1363460 [RTTI → DECOMP]: stores the law in the global FUN_006a3a70 sets (DAT_015f49a4), re-levels every channel (FUN_00fea1e0 over the channel list) **and every mixer track** (FUN_0127b050 per track: pan + stereo separation through FUN_006a3a00, which picks one of two pan kernels by that global); .flp 0x17 | imported for the Sampler's own voice only (REA_rodada76 §9: triangular verified by FL render); no control; the mixer and the other generators always use the circular law | M (+1 FL render of a mixer pan with 0x17 = 2) | M | open — next |
| 3 | Play truncated notes in clips (`truncateclipnotesbox`) | DFM; .flp 0x1E | done in 167g | — | — | done |
| 4 | Automatic crossfades / …in new projects (`projplpanel`) | DFM | done (167a) | — | — | done |
| 5 | Time settings: Timebase (PPQ), steps per beat, beats per bar, pattern length (`patlengthpanel`) | DFM | present (Project page, "Time settings") | — | — | done |
| 6 | Data folder: "Copy used samples to the project folder", ask-for-folder mode, folder type, Rename project (`projdatapathpanel`) | DFM captions / hints | no project data folder | M | M | open |
| 7 | Back up to FL Cloud storage (`projcloudpanel.backupbox`) | DFM | — (FL's own service) | — | — | not planned |

## General (`generalpage`)

| # | Item (FL caption) | FL evidence | LYNX now | Effort | Value | Status |
|---|---|---|---|---|---|---|
| 8 | Undo knob tweaks / Alternate undo mode (classic undo) / Put undone recordings in the recycle bin / Show recent changes at the top (`undopanel`) | DFM captions + hints | undo levels and history only | M | M | open |
| 9 | Restore previous state after solo (`miscpanel.remembermutestatebox`, hint: "Restore the previous active state of channels, playlist and mixer tracks and effect slots after solo") | DFM | no such option (LYNX's un-solo behaviour not compared with FL's yet) | S | L | open |
| 10 | Select first channel with notes when the pattern changes (`selfirstchanpatchangesbox`) | DFM; no OnClick handler (read on OK) — the consumer is not traced yet | missing | S (+ REA of the consumer) | M | open |
| 11 | Create automation clips at the playback position (`createautoclipsatcursorbox`) | DFM | missing | S | M | open |
| 12 | Monophonic painting in piano roll (`prpaintmodebox`) | DFM | missing | S | L | open |
| 13 | Auto name channels / Auto name effect slots (`adjustchannamebox`, `adjustfxnamebox`: name = sample / preset file name) | DFM hints | no switch | S | L | open |
| 14 | Auto zip empty channels on pattern change (`autozipbox`) | DFM hint | Channel Rack zip exists, not automatic | S | L | open |
| 15 | Small scrollbars in editors / Alternate meter scale / Thick lines / High visibility / Transparent menus (`miscpanel`, `disppanel`) | DFM | own theme options | — | L | not planned (LYNX's own look) |
| 16 | Detach all new plugins / Open detached plugins with the FL Studio window / Hide plugin window toolbar by default / Disable typing keyboard priority by default / Limit the number of open plugin editors (hint: to 25) | DFM captions + hints | none of these switches | M | L | open |
| 17 | Use both mouse buttons in step sequencer / Click-and-hold gestures / Invert pencil buttons (`inputpanel`) | DFM | fixed mouse mapping | M | L | open |
| 18 | Read sample tempo information / Read sample root note / Fast sample preview / Auto keep long audio on disk / Threaded project sample loading (`advpanel`) | DFM captions + hints | none of these switches (LYNX's fixed behaviour not compared per item) | S | L | open |
| 19 | Force high performance power plan (`hppowerplanbox`) | DFM | missing | S | L | open (Windows only) |
| 20 | Note names (`languagepanel.notenameregionbox`, `accidentalsbox`) | DFM | present ("Note names": English / Germanic / Solfège, sharps / flats) | — | — | done |

## File / Audio / MIDI

| # | Item (FL caption) | FL evidence | LYNX now | Effort | Value | Status |
|---|---|---|---|---|---|---|
| 21 | Autosave + backup count + backup folder (`filepage.autosavepanel`) | DFM | present (backups kept per project, every-N-minutes) | — | — | done (folder choice missing: S, L) |
| 22 | External tools list (`exttoolspanel`), script editor (`scripteditorpanel`) | DFM | missing | M | L | not planned |
| 23 | Troubleshooting: Reset browser cache / Repair optional features (`troubleshootingpanel`) | DFM | — | S | L | open (a "rebuild search index" button would be the LYNX twin) |
| 24 | Audio (`audiopage`): `audiooutpanel` Safe overloads, Auto close (DirectSound), Priority, Playback tracking (mode + offset slider); `mixerpanel` Resampling quality, Play truncated notes on transport, Reset plugins on transport, Browser preview track, Metronome track; `cpupanel` Multithreaded generator processing, Multithreaded mixer processing, Smart disable, Align tick lengths | DFM control ids + captions | device, buffer, sample rate, drivers, latency compensation, metronome track; none of the other switches (Smart disable exists per plugin, not as the global option) | M each | M | open — Play truncated notes on transport and Reset plugins on transport first (audible) |
| 25 | MIDI (`midipage`): Foot pedal controls note off, Toggle on release, Link note on / release velocity to, velocity curves, Auto accept detected controller, Playlist live / marker channels, Image-Line Remote (`ilremotebox`) | DFM control ids + captions | inputs, outputs, master sync, external clock, pickup, links, song marker jump and performance channels | M | L | open |
