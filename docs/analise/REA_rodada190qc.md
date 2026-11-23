# REA round 190qc: FL-exact verify study (R7 of research190qa)

2026-11-23, FL Studio 2026.1.7 (`FLEngine_x64.dll` sha256 `6d9c5ab09c1d5594…`), Windows, 1920 × 1080 at 100 %.
Clean-room: notes and measurements only. No FL code, strings tables or art go into LYNX.

**Sources**
- FL's online manual (playlist.htm, pianoroll.htm, automation_eventeditor.htm, mixer_trackprops.htm, basics_shortcuts.htm).
- A GUI session on 2026-11-23, 02:55–03:12. It used FL's default template and the demo "Ookay - Thief", which was never saved.
- A headless Ghidra pass over the analysed FLEngine project (`~/ghidra-flgap187a`). The addresses are Ghidra VAs.

**Evidence markers:** [RUN] seen or measured in the running FL; [STATIC] decompile; [DOC] manual; [PRIOR] an earlier REA round.

The shots and raw captures are in `/mnt/e/lynx-work/verify_190qc/shots` and `raw` only.

## D2-PL1: edge auto-scroll while dragging

**Method [RUN].**
- A clip, a note, a marquee, a velocity-lane stroke or an event stroke was dragged with injected mouse input and held at fixed cursor positions.
- 20-fps crops of the ruler (or of the piano keys for vertical) were taken with timestamps.
- The scroll per frame came from cross-correlating the crops. The speed is a least-squares fit of position over time.
- Grid edges were measured on pixels.
- Fits: `rea/edge-rates.txt`. Script: `s/rates.py`.

| Editor, edge | Cursor beyond the grid edge | Speed |
|---|---|---|
| Playlist, left | 3 px inside | 0 |
| | 1 px inside | 20 px/s |
| | 1 / 3 / 8 px out | 41 / 62 / 114 px/s |
| | 20 / 40 / 80 px out | 223 / 410 / 775 px/s |
| | 150 / 250 / 400 px out | 1319 / 1927 / 2167 px/s |
| Playlist, right | 3 px inside | 0 |
| | 1 px inside | 9 px/s |
| | 21 px out, held 3 s | 228 px/s, flat over the 3 s |
| Playlist (maximised), right | 6 px inside / 16 px out | 0 / 182 px/s |
| Piano roll, notes, right | 6 / 1 px inside | 0 / 9 px/s |
| | 9 / 16 px out | 109 / 183 px/s |
| Piano roll, left | 2 / 12 px out | 44 / 144 px/s |
| Piano roll marquee (Ctrl+drag), right | 16 px out | 172 px/s |
| Piano roll velocity-lane stroke, right | 16 px out | 176 px/s |
| Piano roll, vertical (note dragged above the grid) | 11 px inside | 0 |
| | 7 / 37 px out | 134 / 396 px/s |
| Event editor window (mixer fader events), right | 6 px inside | 0 |
| | 21 / 51 px out | 233 / 550 px/s |

**The FL rule (what an FL-exact build should do):**
- **There is no inner zone.** Scrolling starts within about 1–2 px of the editor's edge and grows with the distance outside it. Up to about 80 px out, the speed is about **10 px/s per px** of distance, ≈ 20 px/s at the edge itself. Further out it flattens a little: 8.8 per px at 150 px, 7.7 at 250 px, 5.4 at 400 px. That is likely FL's repaint cost on a big project at high speed. No hard cap was seen up to 2167 px/s.
- **There is no acceleration over time.** At a fixed depth the speed stays flat for 3 s.
- **It counts in screen pixels, not in musical time.** The Playlist at 18 px per bar and the Piano roll at 384 px per bar give the same px/s at the same depth.
- **The same law holds in every editor:**
  - the Playlist (clips);
  - the Piano roll: notes, the marquee and velocity-lane strokes, horizontal and vertical;
  - the event editor window.
- **The dragged item stays under the cursor's side.** A clip dragged past the left edge is clamped at the visible edge while the view runs.
- **The view stops at the content start** (bar 1). It runs on past the song's end on the right.

**For R1:**
- Pattern: the FL law above, in place of the planned 24 px zone and 0.5–4 screens/s.
- Clip-launch and Simple: the Live / Logic-like default the brief planned is unchanged.

**Not found:** the static timer constant. The editors' drag-scroll routine was not located in the time box. The names `FScrollTimer` / `NoScrollZone` belong to the OLE drop-target component, not to the editors. The law above is measured only.

## D2-PL2: "Keep labels on screen"

- **Place** [RUN] [DOC]: Playlist options ▸ View ▸ Content ▸ "Keep labels on screen". It sits between "Time segments" and "Content in titlebars".
- **Default on** [STATIC] [RUN]:
  - The Playlist form reads `KeepLabelsOnScreen` with **default 1** (`FUN_005b1c10(…, L"KeepLabelsOnScreen", 1)` in `FUN_00e34450`) into form byte +0xc46.
  - The menu handler `FUN_00eba140` writes it back to `HKCU\…\FL Studio 26\Windows\PLForm\KeepLabelsOnScreen` and repaints.
  - The value did not exist in the registry before this round, and the item showed ticked.
  - This round toggled it off and on, so the registry now holds 1. That is the same behaviour.
- **Look, on** [RUN] (shots 06–08):
  - When a clip's start is scrolled off the left, its label (icon + name) is drawn at the view's left edge. The icon starts about 3 px inside the grid, the same inset as at a visible clip start (2 px after the clip body).
  - A clip that ends shortly after the edge gets a label shortened with a middle ellipsis ("Sy..ss"). "As long as possible before the clip disappears" [DOC].
- **Look, off** (shot 07): the label scrolls away with the clip start. Long clips show no name, and a nearby label is cut ("h Bass").
- **For LYNX:** an FL-named option, on by default. Label x = max(clip start, grid left) + FL's inset, with middle elision to the visible clip width.

## D2-PL3: track ↔ mixer colour rule

FL-exact through [PRIOR] REA 190ns (`docs/analise/REA_rodada190ns.md`, `FUN_012e6010` / `FUN_012be6f0`):
- **Playlist to mixer:** renaming or recolouring an **audio** Playlist track sets its linked mixer track's name, colour and icon.
- **Mixer to Playlist:** renaming or recolouring a mixer track sets the linked Playlist track's.
- **Instrument tracks:** not synced by FL.

LYNX already does this (#235, f7046b1fc, round 190ns).

Not re-run at runtime in this round. The time box went to PL1, and the Thief tracks' audio/instrument mode is not visible without more menus. The Thief project does show an instrument track ("Synth Bass") whose colour differs from its mixer track's.

## D2-PL13: selection length

**FL shows no length** [RUN] (shot 09).
- **While Ctrl+dragging** a time selection in the Playlist ruler, the hint bar reads "**45:04:00 to 61:08:00**": start to end in bar:step:tick.
- **The Piano roll marquee** reads "3:07:06 to 5:09:08" plus the key range "E4 to G4".
- **After release,** the hint shows the position under the mouse ("61:08:09") and its song time ("1'37''").
- **Neither shows a duration.**

**For LYNX:**
- FL-exact part: "<start> to <end>" in the hint while dragging. Check whether LYNX already shows it.
- The length readout D2-PL13 proposes ("(1 bar 3 beats, 2.81 s)") is an **X** extra.

## D2-PR2: relative lane edit

**FL has it, with Alt+mouse wheel** [RUN] [DOC] (shot 10).
- **Alt+wheel over a note** changes that note's selected property: velocity, pan, etc., whatever the event lane shows. FL's shortcut table calls it "Change selected note property".
- **One note alone:** 4 notches down moved only that note's velocity stem (22 px).
- **With a selection:** after Ctrl+A, 3 notches up over another note moved **every** selected note by the same step (16 px each). The 22 px gap between the first note and the rest was kept. That is a relative edit with the differences preserved. The step per notch is the same for each note.

**Related FL tools:**
- Shift+drag in the lane sets every crossed note to the same value [DOC].
- Alt+click resets a value [DOC].
- Tools ▸ Scale levels (Alt+X) multiplies, offsets or inverts [DOC].

A plain drag on a lollipop is not relative.

**For LYNX:** FL-exact. Alt+wheel over a note of a selection moves every selected note's lane value by the same step, clamped. The brief's "Alt+drag over a selected bar" would be an X addition, not FL.

## D2-MX9: PDC of a bypassed (disabled) slot

**FL drops a disabled slot's latency at once** [RUN] [STATIC] (shots 11–14).
- **Measured on Thief's Insert 27:** Fruity Limiter, Vocodex, Reverb 2, Stereo Enhancer, a second Fruity Limiter, Param EQ 2. The mixer's PDC icon and the track inspector read "Track latency 7.82ms (auto)".

| Slot state | Track latency |
|---|---|
| All slots on | 7.82 ms |
| Vocodex slot switched off (slot LED) | 3.99 ms |
| …and the first Fruity Limiter off | 2 ms |
| …and the second Fruity Limiter off | (none) |
| All back on | 7.82 ms |
| Track muted (strip LED) | 7.82 ms: muting a track does **not** drop its latency |

- **Static:**
  - The planner `FUN_010ff3b0` sums each effect's latency through `FUN_012b6960(plugin)`.
  - That getter returns **0 when `FUN_013308f0(plugin)` is true** (`*(int*)(plugin+0x60) > 0`, the slot's off state), otherwise the reported latency (+200).
  - So the dropped latency also leaves the other tracks' compensation. The planner's outLat is what the routes align to ([PRIOR] REA 165f-mixer).
- **For LYNX (R5):** FL-exact. A switched-off slot reports 0 latency and the PDC re-plans. A muted track keeps its latency. FL's re-plan was immediate (read 1.5 s later). No crossfade was observed in the menu, and audio was not captured, so the "50 ms crossfade" is a LYNX choice, not FL.

## D2-FI2: dirty flag after a template

**FL starts clean after a template** [RUN] (shots 01–02).
- **Default template:**
  - FL started on the default template, "Basic with limiter" on this install. The title is "FL Studio 2026" with no project name.
  - File ▸ "New (Basic with limiter)" right away gave no prompt.
- **View-only changes:** after vertical scroll and Ctrl+wheel zoom in the Playlist, File ▸ New again gave no prompt.
- **A real edit:** one step toggled in the Channel rack, then File ▸ New, gives "Confirm: Save changes to **untitled.flp**?" with Yes / No / Cancel.
- **Side note:** Ctrl+N in FL 2026 is "Save new version". On an unsaved project it opens the "Save as" form (TNewProjForm: Name, Location, Create new project folder, Skip / Save / Cancel), not File ▸ New.
- **For LYNX:** FL-exact. A template load and view-only changes (scroll, zoom, opening windows) leave the project clean. The first undoable edit dirties it.

## D2-RC1: Audio logger (FL 2026)

**Confirmed at runtime** (shot 04), matching [PRIOR] REA 181b.
- **Tools menu section header:** "Audio logger (memory used: 20MB)".
- **Items:** ✓ Enabled · Settings ▸ · Dump to audio editor.
- **Settings ▸ Maximum length:** 1 minute (default) / 2 minutes / 5 minutes / 10 minutes.
- **Settings ▸ Recording position:** Pre effects / Post effects, pre fader (default).
- **Registry:** `EnableRollingAudioBuffer=1`, `RollingAudioBufferLengthSeconds=60`, `RollingAudioBufferPreFX=0`.
- **Where the audio lands:**
  - It is held in memory only: a Master ring buffer, 20 MB for 1 min at 44.1 kHz.
  - A dump trims the zeros and writes an `Audiolog-<date time>` WAV into FL's Recorded folder, opened in Edison on the Master [PRIOR 181b].

**LYNX:** already built (`RollingAudioLog`, `engine/AudioLogger`, Tools menu, round 181b). The DETAILS-2 evidence "grep = 0" was stale. What is left for R9 is the pre-record / reveal-by-edge X part and its patent gate.

## D2-RC2: "Dump score log to selected pattern"

**Confirmed at runtime** (shot 03), matching [PRIOR] REA 165f.
- **Tools ▸ Score logger section:**
  - "Dump score log to selected pattern" ▸ Last 2 minutes / Last 5 minutes / Last 10 minutes / Last 20 minutes / Last 30 minutes.
  - "Clear log".
- **[STATIC] strings:**
  - "The score log is empty. That means you haven't input any notes yet.";
  - "The current pattern isn't empty. Do you want the score log to be merged, or to replace the current content ?" with Merge / Replace / Cancel;
  - undo "score log dump".

**LYNX:** already built (MainComponent Tools menu, `core/ScoreLog`, round 165f). The DETAILS-2 evidence "grep `Dump score` = Newtone only" was stale. Item 14 / R1-small needs only a check that LYNX's buffer reaches 30 minutes.

## Side observations

- In the Playlist options menu on this study install, "Performance mode (Ctrl+P)" showed ticked. That is a setting on this install, not part of these results.
- The study install's state changed in this round only in the KeepLabelsOnScreen registry value (now 1, the same as the default). Nothing was saved. Both projects were closed with "No".
