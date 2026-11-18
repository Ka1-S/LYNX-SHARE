# Round 190kx — Melodyne 5, ARA 2 hosting, Newtone vs Melodyne

Clean-room notes (behaviour, structure and file layout only; no code, tables, presets or content of Celemony's or
Image-Line's products). Session log and captures: `/mnt/e/lynx-work/melodyne190kx/` (HANDOFF.md, s/notes.md, raw/,
ref/). Patent study: `/mnt/e/lynx-work/melodyne190kx/PATENTS.md`.

## 1. What is installed

- **Melodyne 5 studio**, version 5.4.2.006, 30-day trial (activated by the owner 2026-11-17). The edition is shown by the
  "melodyne studio" mark in the standalone's toolbar [SHOT raw/m03.png]; the trial runs the studio feature set.
- Formats on this PC: `C:\Program Files\Common Files\VST3\Celemony\Melodyne\Melodyne.vst3` (a single-file VST3 whose
  factory lists a class of category "ARA Main Factory Class": the VST3 carries ARA 2), the shared engine
  `C:\Program Files\Common Files\Celemony\Bundles\MelodyneCore-5.4.2.006.dll`, the standalone
  `C:\Program Files\Celemony\Melodyne 5\Melodyne.exe`, and `Melodyne.aaxplugin` (Pro Tools). No VST2.
- Copy protection: the plugin links PACE/iLok code (its strings name `pace::` exceptions and iLok commands). Nothing
  was attached to it, patched or debugged; only file listings, `strings` of the VST3 (class category) and the
  standalone's own UI were used. The licence files were not opened.

## 2. FL Studio 2026 and ARA

The brief assumed FL Studio 2026 hosts Melodyne through ARA 2. It does not:
- none of FL 2026's binaries (FL64.exe, FLEngine_x64.dll, the Fruity Wrapper DLLs and every other .dll/.exe under the
  install) contains an ARA API string (`ARA Main Factory Class`, `ARAFactory`, `ARAPlugInExtension`, `ARA_API`,
  `IMainFactory`) [EXEC strings scan, 2026-11-17];
- FL's own change log (Help\WhatsNew.htm and WhatsNew.rtf, up to 26.1.7) mentions ARA nowhere; its only Melodyne line
  is an old fix "Wrapper: Melodyne VST3 doesn't resize";
- Image-Line's forum thread "Request for ARA2 Support for Melodyne in FL Studio" is still open with posts from
  2026-11-17 and no staff statement that it shipped.
In FL, Melodyne runs as a plain VST3 effect in a mixer slot ("transfer" mode: the audio is recorded into the plugin in
real time before it can be edited). FL's audio clip menu has no ARA item [SHOT flgap189o FL-clipmenu-*.png].

LYNX therefore hosts ARA as a **LYNX extra** (as Cubase, Studio One, Logic, REAPER, Pro Tools do; FL and Live do not):
the audio clip menu's LYNX extras column gets "Edit in ARA plugin ›" (round 190jv's third column, FL's two columns
unchanged).

## 3. LYNX's ARA model (src/core/AraLink.h, AraRegions.h, src/engine/AraHost.cpp)

- One ARA document controller per plugin per project; one MusicalContext (project tempo + meter; tempo automation is
  not passed on); per ARA channel one AudioSource (the channel's sample, read from LYNX's decoded copy), one
  AudioModification and one RegionSequence; per Playlist audio clip one PlaybackRegion (timeline seconds at the project
  tempo; the clip's offset into the sample; a clip stretch = ARA time-stretching when the plugin supports it).
- The channel is FL's "sample": its clips share the edits (FL's "Make unique" gives a clip its own channel and so
  its own edits).
- Per ARA channel one plugin instance bound with the playback renderer, editor renderer and editor view roles; it is the
  channel's instrument in the rack (its output goes through the channel's volume / pan and mixer track). Opening the
  editor tells the plugin which clip to show (editor view selection).
- Model edits on the message thread inside begin/endEditing; renderer region changes only with the instance
  unprepared, under its callback lock (the audio thread renders silence for that block); the host calls
  notifyModelUpdates 10 times a second while a document exists; an export waits (≤ 3 min) for the plugins' analysis.
- Saved: `ara` on the channel (plugin id + name) and `araDocuments` on the song (the plugin's archive, base64); a song
  without ARA writes neither (files unchanged). 0.1.28 copies drop both with a loss line.
- Nothing is created for a song without ARA channels (no host object, no document, no instance): zero cost.

### 3.1 Verified with Melodyne 5 studio 5.4.2 (Windows, round builds of the branch)

- `LYNX --aratest Melodyne.vst3 mono-melody.wav out`: the scan lists Melodyne with its ARA factory; the clip played
  through Melodyne without an edit equals the plain clip (correlation 1.0000 at lag 0, 0.00 dB); the document archive
  (81 KB, archive id `com.celemony.ara.chunk.13`) is saved in the .lynx, read back, and a new engine restored from it
  renders the same (correlation 1.0000, 0.00 dB; restored render 0.11 s, no new analysis).
- In the app: the clip menu's "Edit in ARA plugin › Melodyne" opens Melodyne's editor in a LYNX plugin window showing
  the clip's notes at its place on LYNX's timeline (tempo 120, C major from LYNX's musical context); all notes dragged
  +3 semitones in that editor, the project saved, then rendered: against Melodyne standalone doing the same edit on
  the same file, pitch 0.0 cents apart, spectral envelope 0.21 dB rms, level +0.01 dB.
- Bugs found and fixed on the way: JUCE's ARA factory is released by code in the plugin's module, which unloaded with
  the last instance (the controller now keeps the first instance until it is gone); Melodyne reads the document archive
  id while restoring (it is now saved with the archive and handed back, never null); a plugin missing from a list that
  was still loading was given up for good (now asked again); ARA clips play at the audio clip unity gain.
- CPU, LYNX idle with the transport stopped (test profile, 20 s, % of one core): the project without ARA 10.0 (Melodyne
  not loaded: no ARA object exists), with the clip in Melodyne 13.4 before the idle rule (Melodyne's processBlock every
  block), see HANDOFF for the number after it.

## 4. Melodyne 5 behaviour used as reference (standalone, synthetic material of our own)

| Material | Melodyne | LYNX Newtone (before) | LYNX Newtone (round 190kx) |
|---|---|---|---|
| mono-melody (C D E F G, rest, F E D C; 0.5 s notes) | 9 notes, tempo 119.96 BPM, key C major [SHOT m06] | 9 notes | 9 notes (either analysis) |
| mono-vibrato (same, 35-cent vibrato) | — | 8 notes (F and E after the rest joined) | "Separate close notes": 9 |
| mono-offtune (C+30, D-25, E+40, F-15, G+20, 20-cent vibrato) | 5 notes [SHOT m08] | 4 notes: E+40 and F-15 joined (64.68), Center put both on F | "Separate close notes": 5 notes |
| poly-chords (C, Am, F, G triads) | Melodic: one note per chord; Polyphonic Decay: the chord notes + a chord track (Am, G) [SHOT m24] | not supported (see PATENTS.md) | not built |

Macros: Melodyne's Correct Pitch (Pitch Center %, Pitch Drift %, "Snap to chord scale") ≈ Newtone's Center and
Variation knobs; Melodyne's Quantize Time ≈ Newtone's Quantize time (100 %). Renders (ref/): Correct Pitch 100/100 on
mono-offtune; all notes +3 semitones on mono-melody (formants kept). Scores (s/score.py, YIN per 10 ms, cepstral
envelope 230 Hz–5 kHz):

| Render | pitch vs Melodyne (mean abs) | envelope vs Melodyne | shift error |
|---|---|---|---|
| +3 st, LYNX Newtone | 1.0 cents | 0.73 dB rms | 0.4 cents (Melodyne 0.9) |
| Correct pitch, Newtone FL analysis | 26.3 cents (one note on the wrong semitone) | 1.57 dB | — |
| Correct pitch, Newtone "Separate close notes" | 13.2 cents (the rest: vibrato kept differently) | 1.50 dB | — |

## 5. Interop

Melodyne's own documents (.mpd) were not needed: inside a host Melodyne keeps its edits in the ARA archive, which LYNX
stores opaquely in the project. Reading .mpd files is not part of this round.
