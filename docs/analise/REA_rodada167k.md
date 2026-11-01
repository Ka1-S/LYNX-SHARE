# Round 167k (v0.1.22) — 167j checked on Windows, transport jumps, clip pitch / reverse, gap-table keys

Clean-room: Ghidra on FL Studio 2026's own `FLEngine_x64.dll`, its DFM form resources and the ShortcutsModule action
lists; command-line renders by FL itself (`FL64.exe /R`, both FL locks held, FL exited by itself or was closed by
its own PID). Behaviour and formats only; nothing of FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource string, **[FL render]** FL's command-line
render, **[GUI]** LYNX driven on Windows (fresh profile, `LYNX_TEST_MODE=1`), **[TEST]** LYNX tests
(`tests/Rea167kTests.inc`).

## 1. Windows check of round 167j

- **Brief:** "Options › Project Panning law, Options › Audio/General switches, Export 'Insert effects', new
  PR/pattern/export shortcuts (real keyboard), mixer pan sound sanity. Fix anything broken."
- **Checked [GUI]** (build of main ad46fb98): the Panning law combo (Circular / Triangular, marks the project dirty);
  Audio › Mixer "Play truncated notes on transport" (on) and "Reset plugins on transport" (off); General "Select first
  channel with notes when the pattern changes" (switching to Melody selected Pluck); Piano roll Shift+Up (semitone),
  Ctrl+Up (octave), Shift+Right, Alt+M / Shift+Alt+M, Ctrl+D; patterns Ctrl+Shift+C (clone "Melody #2"),
  Ctrl+Shift+Del, Shift+F4 (new empty + name dialog), Ctrl+F4; Ctrl+Shift+R (Export with MP3 and "Insert effects"),
  Ctrl+Shift+M (MIDI save dialog). The options persist in `ui-options.txt`.
- **Pan sanity [FL render]:** Windows LYNX `--render j_tri.flp` against FL's `j_tri.wav`: residual RMS ~1e-5 on all
  10 inserts.
- **Broken, fixed:**
  - Export dialog: "Show the file(s) when complete" was squeezed into two clipped lines. The switches are now three
    columns (normalize | master effects | trim; HQ | polyphony | show; warm-up | insert effects), and the real-time
    switch gets its own row when it is shown (`ExportDialog.cpp`).
  - Piano roll: a selected muted note looked like a selected unmuted note. It is now drawn in the selection colour
    blended 60 % towards the muted colour (`PianoRollComponent.cpp`).
- **Noted, unchanged:** Ctrl+F4 on an empty current pattern with no other empty pattern stays on it (LYNX's
  `gotoNextEmptyPattern` rule; not checked against FL).

## 2. Transport after a jump: truncated notes and "Reset plugins on transport"

- **Brief:** "truncated notes after a jump while playing; Reset plugins on transport for instruments too".
- **FL evidence:**
  - [DECOMP FUN_011e7a10 → FUN_011e78a0 → FUN_011e7470] setting the song position while playing calls the position
    change with the catch-up flag (param_3 = 1) when the "Play truncated notes" global (PTR_DAT_015e4408) is on: the
    notes that cross the new position start there. The loop's wrap goes another way (not a set-position), so FL does
    not chase there.
  - [DECOMP FUN_011c8a40 mode 2 + PlugResetBox 0x49e → FUN_012b5210] the reset walks the list DAT_016bd510 and calls
    each plugin's reset (vtable +0x48). [DECOMP FUN_012b8620] every opened plugin, generator or effect, is appended to
    that list. FL's Sampler and audio clips are not plugins.
  - [DECOMP FUN_011c89c0] a reset jump also sends CC 123 to the MIDI out ports (0x7bb0 + channel). Not done in LYNX
    (MIDI Out belongs to another agent).
- **LYNX before:** notes were chased only when playback started; a jump while playing left the crossing notes silent.
  The reset reached only the mixer effects, and `PluginEffect::reset()` was a no-op, so hosted VST effects never
  reset.
- **LYNX now:** `Transport::userJumps` counts the user's jumps (rewind, seek, pattern / marker jumps;
  `AudioEngine.cpp`), and the rack chases on a new count (`RackRenderer::setTransportOptions`). The loop's wrap does
  not chase. The reset calls `transportReset()` on every effect and generator: native ones reset, `PluginInstrument`
  panics (resetting with `resetOnFlush`), `PluginEffect` resets the hosted plugin at its next block (inside the crash
  guard), and FL's Sampler / audio clips are left alone. [TEST] `rea167k_truncated_notes_chase_after_a_user_jump_not_the_loop_wrap`,
  `rea167k_reset_plugins_on_transport_reaches_the_generators`.

## 3. Triangular panning law at the channel level

- **Brief:** "triangular law at channel level for all generators (measure FL)".
- **FL evidence [FL render]:** `k_gen_tri.flp` (FL's Empty template + the "Patch bank" template's 3x Osc "Basic" and
  FPC "Drums", one channel per neutral insert, project law triangular). 3x Osc stays **circular** under the triangular
  law (relative L50: 1.3066 / 0.5412, R75: 0.2759 / 1.3870, L100: 1.4142 / 0). FPC follows the **triangular** law
  (L50: 1.0 / 0.5, R75: 0.25 / 1.0). FL's audio clip mixer (FUN_00d0cc10) uses the law-switching kernel FUN_006a3a00.
- **LYNX before:** FPC is imported as plain sampler channels (`splitDrumChannel`), and those were circular.
- **LYNX now:** `SamplerInstrument::render` turns the rack's circular pan gains into the triangular ones when the
  project law is triangular (the louder side at the centre level, the other at 1 − |p|). Other generators stay
  circular, like 3x Osc in FL. [TEST] `rea167k_triangular_law_reaches_sampler_channels`.
- **Seen, not fixed:** level differences in the same render (3x Osc 0.1276 vs FL 0.1135, FPC 0.00219 vs 0.00369):
  those belong to the generator ports (another area).

## 4. Edit › Undo backwards and the alternate undo mode

- **Gap table 167j:** "Edit › Undo backwards Ctrl+Alt+Z — missing".
- **FL evidence:** [DECOMP 0xf20180 EditUndoActionExecute] → FUN_00f18a30(−1) → FUN_010173a0: at history position 0
  it goes to 1, else to position − 1. So Ctrl+Z undoes, or redoes when something is undone (FL's undo/redo on one
  key). [DECOMP 0xf201c0 EditUndoBackwardsActionExecute] → position + 1: a step further back each time.
  [DFM] `AlternateUndoBox` (hint "Classic undo, instead of undo/redo on the same shortcut", off by default; its
  click FUN_010197f0 swaps the shortcuts).
- **LYNX before:** Ctrl+Z always undid, Ctrl+Y / Ctrl+Shift+Z redid. No Undo backwards.
- **LYNX now:** `edit.undoBackwards` = Ctrl+Alt+Z (always undoes a step). Ctrl+Z and the toolbar's undo follow FL
  (`MainComponent::flUndo`): undo, or redo when the last action was an undo. Options › General "Alternate undo mode
  (classic undo, instead of undo/redo on the same shortcut)" (off) makes Ctrl+Z always undo. Edit menu: "Undo
  backwards" and "Undo" / "Undo (redo)". Ctrl+Y / Ctrl+Shift+Z redo stays (LYNX extra). [TEST]
  `rea167k_undo_backwards_key`.

## 5. Playlist keys 7 / 8 / 9: clip pitch and clip reverse

- **Gap table 167j:** "(EE) 7 / 8 / 9: pitch audio clip −1/+1 semitone, reverse — missing keys".
- **FL evidence:**
  - [DFM / ShortcutsModule] `EEPitchSelectedAudioClipsDownAction` 7, `…UpAction` 8, `EEReverseSelectedAudioClipsAction`
    9, also on the Playlist's Edit menu.
  - [DECOMP FUN_00e2d7b0] for every **selected** playlist item (+0x13 bit 7) that is an audio clip: pitch (+0x3C,
    float semitones) += ±1, clamped to −12 … +12, then the clip's variant is refreshed (FUN_010650f0). Undo name
    "pitch selected audio clips", hint "Pitch selected clips one semitone up/down".
  - [DECOMP FUN_00e2db70 → FUN_01065520] reverse toggles bit 0 of +0x48 for each selected audio clip and mirrors the
    region it shows: start' = max(0, file − min(end, file)), end' = start' + (min(end, file) − start).
  - [DECOMP FUN_01064f40] a clip gets its own "variant" of the sample when pitch ≠ 0, stretch (+0x40, double) ≠ 1 or
    a flag is set. The playlist item record is 88 bytes in memory and in the file (80 B in 25.2 files): +0x3C pitch,
    +0x40 stretch, +0x48 flags.
  - [FL render] `r167k/fl/clip/*.flp` (FL-saved misc164p `base.flp`: one audio clip channel in Resample, 86 BPM, a 6-beat
    clip over a 2 s 440 Hz ramp; only +0x3C / +0x48 changed):

    | project | pitch | length of the audio | envelope |
    |---|---|---|---|
    | c_base | 440.0 Hz | 2.000 s | rising |
    | c_p3 (+3) | 522.5 Hz | 2.000 s | rising |
    | c_m5 (−5) | 329.2 Hz | 2.000 s | rising |
    | c_rev | 440.0 Hz | 2.000 s | falling |
    | c_p3rev | 522.5 Hz | 2.000 s | falling |

    So the clip pitch is a **pitch shift that keeps the length**, even though the channel resamples (FL's own shifter
    is about −2.4 cents low). Reverse plays the file backwards.
- **LYNX before:** no per-clip pitch or reverse. The clip menu's "Reverse" and "Pitch..." act on the whole channel.
- **LYNX now:**
  - `Clip::pitch` (−12 … +12) and `Clip::reversed`, saved in the clip array (indices 13, 14), compared by undo,
    imported from FLP records of 80 B and more (+0x3C, +0x48 bit 0).
  - Keys 7 / 8 / 9 in the Playlist (`playlist.pitchClipsDown/Up`, `playlist.reverseClips`), also on the Playlist menu
    and the main Playlist menu. They act on the selected audio clips only (`clipops::pitchAudioClips`,
    `clipops::reverseAudioClips` with FL's mirror).
  - Playback: the clip's pitch goes to the sampler voice through `setNextClipPitch` and the voice's pitch shifter
    (`startKeyShift` with `keepRate`). The channel's own pitch keeps its rule: it resamples unless "keep pitch". A
    channel with a pitched clip gets the shifters (`clipShiftWanted` in `RackState::build`). Reverse flips the voice's
    direction on top of the channel's Reverse (bit 2 of the event's `fadeShape`). The Playlist draws a reversed clip
    backwards.
  - [TEST] `rea167k_clip_pitch_and_reverse_keys`, `rea167k_clip_pitch_and_reverse_play`,
    `rea167k_clip_pitch_keeps_the_length`, `rea167k_flp_clip_pitch_and_reverse_import`.
  - [GUI] Windows build: Ctrl+A then 8 ×3 and 9 on the clip marks the project changed and draws the clip backwards;
    the Playlist menu lists the three items with their keys.
  - LYNX `--render` (Windows build) of the same nine projects against FL's renders: the same envelope at every probe
    (0.1 … 1.9 s, to the third decimal) and the same audio length. The pitch is 523.26 Hz for FL's 522.51 Hz (+3) and
    329.63 for 329.15 (−5): FL's shifter sits about 2.4 cents low, LYNX's is exact.
- **Not done:** the per-clip stretch (+0x40), and FL's "Create variants when stretching audio clips" switch.

## 6. Playlist › Time markers keys

- **Gap table 167j:** "Alt+T / Ctrl+T / Shift+Alt+T / Shift+T: time markers — missing keys".
- **FL evidence:** [DFM] `EEMarkerMainMenu`: "Add one..." ShortCut 32852 (Alt+T), "Add auto" 16468 (Ctrl+T), "Add time
  signature change..." 41044 (Shift+Alt+T), "Place loop" 8276 (Shift+T).
  - [DECOMP 0x1229d20 EEMarkerAdd2MenuClick] position: the song position when it is in view, else the bar nearest
    the view's left edge (FUN_01062ec0). The time selection's start (FUN_00e4e970) wins. Then the "Marker name"
    dialog, and a plain marker.
  - [DECOMP 0x122a070 → FUN_00e546a0] Ctrl+T: the song position on the grid (FUN_00e43fa0 with snap). If a marker is
    already there, it is kept and nothing is added. Otherwise the first free name of "Auto", "Auto #2", "Auto #3"…
    (the separator literal at 0xe54994 is " #").
  - [DECOMP 0x122bd20] Shift+Alt+T: the selection's start, else the snapped song position, floored to a beat. The
    signature dialog, then a signature marker "n/d". When the selection ends before the song's end and there is no
    signature there, a second marker at the end gives back the signature that was in force at the start.
  - [DECOMP 0x122bb80] Shift+T: the bar nearest the view's left edge, or the selection's start. Every song Loop
    marker (action 4) is removed (FUN_00e54b30). Unless the old one was at that spot, a new Loop marker is placed
    there, so the same key again removes it.
- **LYNX before:** markers came only from the ruler menu, with no keys.
- **[GUI]** Playlist focused: Ctrl+T put "Auto" at bar 1, and a second Ctrl+T there added nothing and no undo step.
  Shift+T put a Loop marker. Ctrl+Z undid it, Ctrl+Z again redid it ("Redone: Markers"), and Ctrl+Alt+Z twice went
  two steps back.
- **LYNX now:** `markers::addAuto`, `markers::placeSongLoop`, `markers::addSignatureChange`, `markers::signatureAt`
  (`MarkerPlan.cpp`). Playlist actions `playlist.markerAdd/markerAuto/markerSignature/markerLoop` with FL's keys
  (Playlist focused). Ctrl+T elsewhere stays the typing piano. Time markers submenu: "Add one...", "Add auto",
  "Add time signature change..." (numerator / denominator dialog), "Place loop". LYNX's time selection is the
  Playlist's loop range. [TEST] `rea167k_time_marker_keys`.

## 7. Mixer menu keys

- **Gap table 167j:** "Rename, color and icon F2 — missing key", "Solo selected S — missing", "Disk recording ›
  Render armed / selected tracks (Alt+R / Shift+Alt+R) — missing keys".
- **FL evidence:** [DFM] the mixer track menu: "Rename, color and icon... [F2]", "Solo selected track(s) [S]",
  "Render armed tracks to wave file(s)... [Alt+R]", "Render selected tracks to wave file(s)... [Shift+Alt+R]".
- **LYNX before:** the menu already showed "F2" and "S", but no key did anything.
- **LYNX now:** `mixer.rename` F2, `mixer.soloSelected` S, `mixer.renderArmed` Alt+R, `mixer.renderSelected`
  Shift+Alt+R (Mixer focused). Each one runs the selected track's own menu item (`MixerStrips::runMenuItem`), so the
  key does exactly what the menu does. With another panel focused, F2 is still the pattern's rename. [TEST]
  `rea167k_mixer_menu_keys`.
- **[GUI]** Insert 2 selected: S lit its solo; F2 opened "Rename track" with the name and colours.
- **Not done:** "Alt solo selected track(s)" (Alt+S). Its FL behaviour is not proven yet.

## 8. Audio clips through the precomputed effects (167h leftover)

- **Brief:** "audio clips through precompute".
- **FL evidence [FL render]:** `r167k/fl/clip/p_*.flp` (the same FL-saved project as §5, changes only in the audio
  clip channel's events):

  | project | change | FL plays |
  |---|---|---|
  | p_rev | 0x46 bit 1 (Reverse) | falling ramp, ends at 2.000 s |
  | p_normpol | 0xD7 +0x50 Normalize, +0x51 Reverse polarity | peak 0.995 (base 0.895), inverted sign |
  | p_fadein | 0x4C = 256 | starts at 0 and fades in |
  | p_fadeout | 0x4B = 256 | fades out to 0 at 2 s |

  So FL applies an audio clip channel's precomputed effects as it does a Sampler's.
- **LYNX before:** the import skipped them for audio clip channels (`def.instrument != audioClip`), Reverse included,
  and `buildInstrument` ran the precompute for samplers only ("audio clips: not yet").
- **LYNX now (LYNX `--render` against FL: the same samples for p_rev / p_normpol / p_fadein / p_fadeout):** imported for audio clip channels too. `buildInstrument` processes an audio clip's whole file (the
  region stays whole: a clip plays the whole file). [TEST] `rea167k_audio_clip_channels_keep_precomputed_effects`.

## 9. Playlist › Select › time keys (167j gap row)

- **FL evidence [REA]:** FL's Playlist actions `EESelAroundSelAction` (Ctrl+Enter), `EESelPrevAction` /
  `EESelNextAction` (Ctrl+Left / Ctrl+Right, Tag −1 / +1). `FUN_00eb1b90` → `FUN_00e4ea50`: the time selection spans
  the selected clips, or the visible range when none is selected; the same span again clears it. `FUN_00e43ea0`
  (Tag ±1): the time selection moves by its own length, never before the song's start, the song position goes to
  its new start and the view follows.
- **LYNX before:** no keys; Ctrl+Left / Ctrl+Right did nothing in the Playlist.
- **LYNX now:** `playlist.selectTimeAround`, `playlist.timePrevious`, `playlist.timeNext` (Playlist focused);
  `PlaylistComponent::selectTimeAroundSelection` / `moveTimeSelection`. [TEST] `rea167k_select_time_keys`.
  [GUI, Windows] the demo: one Chords clip selected, Ctrl+Enter selects bars 5–9; Ctrl+Right moves it to 9–13 with
  the playhead at bar 9; with nothing selected Ctrl+Enter selects the view and again clears it.

## 10. Declicker attacks and the sample's end (167h leftover)

- **Brief:** "declicker attacks" (the attack halves of FL's seven declickers, REA_rodada167h §3).
- **FL evidence [DECOMP]:** FUN_012c6ae0 builds the declickers with FUN_00977f50(declicker, attack shape, attack ms,
  attack curve, release shape, release ms, release curve): mode 0 none / 3·10 ms, mode 1 2·4 ms / 1·10 ms, mode 2
  2·4 ms / 3·20 ms, mode 3 3·20 / 3·20, mode 4 3·100 / 3·100, mode 5 3·200 c5 / 3·200 c5, fast none / 3·2 ms.
  FUN_00977870 makes the attack tables: shape 3 a rising raised cosine, shape 2 a falling one that FUN_00978250 uses
  to subtract the first frame (x − x₀·g). But the start of the attack is decided by FUN_00978070(declicker, state,
  use attack): its position is 0x7fffffff (done) unless the third argument is set, and **every** sampler voice start
  passes 0 (FUN_00d277d0, FUN_00d27cf0 at 00d27d74 / 00d286a5 / 00d28728, FUN_00d24400). So FL's Sampler notes
  start without an attack ramp in every mode.
- **FL evidence [FL render]:** `r167k/fl/dk/dk.flp` (`tmp/r167k/py/mkdk.py`): a 0.3 s DC sample (0.5) on six
  Sampler channels, modes 0–5, a 0.5 s note each. Every note starts at full level on its first frame (no attack in any
  mode). Where the sample ends (frame 13 230) FL holds the last frame and fades it with the **mode's release**:
  sound ends 438 / 369 / 877 / 877 / 4 388 / 8 782 frames later; modes 0, 2, 3, 4 a raised cosine (error ≤ 0.003),
  mode 1 the follower shape, mode 5 the curved one (0.69 at the middle).
- **LYNX before:** no attack (right), but the sample's end always faded with the fixed 10 ms end table: every mode
  ended 432 frames after the sample.
- **LYNX now:** with a declicking mode other than 0, a voice that runs past its sample's end holds the last frame and
  starts the mode's release (`startCut (v, false)`: the same declicker as a note cut); mode 0 keeps the 10 ms end
  table. [TEST] `rea167k_sample_end_uses_the_declicking_release`. LYNX `--render` of the same `dk.flp` against FL's
  render: max error 0.0000 / 0.0000 / 0.0004 / 0.0004 / 0.0001 / 0.0001 for modes 0–5 (before: up to 0.19).

## Where I stopped

- Done (feature/flopt167k): items 1–10 (first landing d2ad4494 = items 1–8), tests green (`tests/Rea167kTests.inc`), Windows GUI re-check (keys 7/8/9,
  marker keys, undo, mixer keys, export dialog) and render checks against FL.
- Left from the brief: 167h leftovers (Sampler legacy panel, .flp events 0x20 (+0x892),
  0x30 (+0x224), 0x32 (+0x1fc), 0xAA (+0x714 count + strings), 0x2B, 0xE2/0xE6, 0x26: they need the channel form
  handlers traced); more 167j gap rows (export "Save tempo information (acid)", markers in exports, Alt solo not yet
  proven, per-clip stretch at clip +0x40).
- Seen, not fixed: the export dialog has a large empty gap above Mix; Ctrl+F4 on an empty pattern; CC123 to MIDI
  out on a reset jump (MIDI Out is another round's area); generator level differences (plugin area, 167f).
