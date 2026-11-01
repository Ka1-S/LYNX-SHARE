# Round 167j (v0.1.22) — Panning law, transport options, and a survey of FL's menus and shortcuts

Clean-room: Ghidra and objdump on FL Studio 2026's own `FLEngine_x64.dll` (SHA-256 6d9c5ab0…), its DFM form
resources and the ShortcutsModule action lists. One command-line render by FL itself (both FL locks held, FL exited
by itself). Behaviour and formats only; nothing of FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource string, **[REG]** the user's FL registry
key (read only), **[FL render]** FL's command-line render, **[TEST]** LYNX tests.

## 1. Panning law (Circular / Triangular) and FL's mixer pan matrix

- **Reporter's words (brief):** "Panning law (Circular/Triangular) with the mixer-wide pan recompute, proven by an
  FL render."
- **FL evidence:**
  - [DFM] `PanLawSelect` on the project's advanced panel: items Circular / Triangular, default 0, OnChange
    `CircPanBoxClick`.
  - [DECOMP 0x1363460] `CircPanBoxClick` stores the mode in a global (FUN_006a3a70), re-levels every channel
    (FUN_01186070 → FUN_00fea1e0) and recomputes every mixer track (FUN_0127b050).
  - [DECOMP FUN_006a3a00] the pan kernel. Pan is clamped to ±1. Mode 1 (triangular): the far side keeps v·0.70710677,
    the near side falls linearly (p ≥ 0 → R = v, L = (1−p)·v). Otherwise (circular): cos/sin of (1+p)·π/4.
  - [DECOMP FUN_0127b050] a mixer track (vol 12800 = unity, pan ±6400, separation ±64, polarity, swap) is turned into
    a 2×2 matrix. With g the fader law, the kernel gives gL/gR for (p, g·√2). The attenuated side is not just
    turned down: its signal folds into the other side. fl = 1 − gR/g if p < 0, fr = 1 − gL/g if p > 0; the
    separation term (narrowing (5^s−1)/4, widening linear) is added to both and clamped to ≤ 1. Then
    rl = gL·fl/2, ll = gL·(1−fl/2), lr = gR·fr/2, rr = gR·(1−fr/2). Swap trades the output rows. The fast path
    (unity, centre, no separation) is the identity. This is the model REA_rodada2 §2.2 described and LYNX never
    implemented.
  - [DECOMP FUN_00f40840] the .flp saver writes channel event 0x17 = mode×2 (so 2 = triangular).
  - [FL render] `j_tri.flp` (10 inserts, varied pan / volume / separation, triangular): FL's render matches the model
    on all 10 inserts. The fitted matrices are in the test `mixer_pan_fold_and_panning_law_match_fl_render`. The
    circular kernel was already proven by REA_rodada85's renders.
- **LYNX before:** the mixer applied plain per-side gains (pan only turned one side down, nothing folded) and there
  was no project panning law. The triangular law existed only as a Sampler modulation detail.
- **LYNX now:**
  - `Song::panTriangular`, imported from FLP (`panLaw >> 1 == 1`), saved as `"panLaw": "triangular"`. Old LYNX
    projects with FL's channel byte are migrated on load.
  - Project settings › "Panning law" (Circular / Triangular).
  - `RackRenderer::mixerMatrix` builds FL's matrix (volume, pan, separation, polarity, swap and the law) for every
    insert and the master. Each coefficient glides (0.001 per sample) so moves don't click. The Sampler follows the
    law too (`Instrument::setPanTriangular`).
  - [TEST] `mixer_pan_fold_and_panning_law_match_fl_render`, `mixer_stereo_separation_phase_swap`,
    `rea167j_panning_law_flp`, `rea167j_mixer_pan_folds_the_other_side_through_the_renderer`.

## 2. Play truncated notes on transport

- **Reporter's words (brief):** "Play truncated notes on transport."
- **FL evidence:** [DFM] `PlayTruncatedNotesBox`, Checked = True (default on), hint "Play notes crossing start
  position". Its OnClick [DECOMP 0x135b020] copies the state to a global that the transport-start code
  [DECOMP FUN_011e7470] reads to set every player's catch-up flag before the binary search for the start position
  (FUN_012df250). [REG] the user's FL has it on.
- **LYNX before:** a note that began before the play position was silent until its next start.
- **LYNX now:** Audio settings › Mixer › "Play truncated notes on transport" (default on). When playback starts, a note
  with start < position < end is triggered at the first sample (pattern and song mode). Saved in ui-options
  (`playTruncatedOnTransport`). [TEST] `rea167j_play_truncated_notes_on_transport`,
  `rea167j_transport_options_through_the_renderer`.

## 3. Reset plugins on transport

- **Reporter's words (brief):** "Reset plugins on transport."
- **FL evidence:** [DFM] `PlugResetBox`, default off, hint "Uncheck for faster response when relocating". The
  set-position code [DECOMP FUN_011e7a10] calls FUN_011c8a40 (mode 2), which with the option on calls FUN_012b5210:
  the plugin reset (vtable +0x48) on every plugin. [REG] the user's FL has it off.
- **LYNX before:** no such option; effect tails carried across a jump.
- **LYNX now:** Audio settings › Mixer › "Reset plugins on transport" (default off). With it on, every insert effect is
  reset when playback starts or jumps. Saved as `resetPluginsOnTransport`. [TEST]
  `rea167j_transport_options_through_the_renderer`.

## 4. Select first channel with notes when the pattern changes

- **Reporter's words (brief):** "Select first channel with notes when the pattern changes."
- **FL evidence:** [DFM] `SelFirstChanPatChanges`, default off, no OnClick (read when needed). The pattern-change
  handler [DECOMP FUN_00dce130 → FUN_00e67f20] takes the first channel in rack order whose note count in the new
  pattern is > 0, then selects it (FUN_011e84e0). [REG] off in the user's FL.
- **LYNX before:** missing.
- **LYNX now:** General settings toggle (default off), saved as `selFirstChanPatChanges`; `firstChannelWithNotes`
  (Song.cpp) gives the channel. [TEST] `rea167j_first_channel_with_notes`.

## 5. Export › "Enable insert effects"

- **FL evidence:** [DFM `WAVRenderForm`] the render dialog has an "Enable insert effects" switch (on by default).
- **LYNX before:** missing; insert effects were always rendered.
- **LYNX now:** Export dialog "Insert effects" toggle, saved in the export options (`insertEffects`). When off, the
  inserts' effects (not the master's) are skipped in the render. [TEST] `rea167j_export_insert_effects_switch`.

## 6. Survey: FL's menus, shortcuts and render dialog

The gap table is in `REA_rodada167j-gaps.md` (main menu, Playlist, Piano roll, Mixer, shortcuts, render dialog).
Sources: the DFM menu trees, the ShortcutsModule action lists (MainMenuActions, ToolbarActions,
PlaylistMenuActions; 125 shortcuts) and LYNX's `KeyMap.cpp`.

## 7. Piano roll keys and pattern keys (from the survey)

- **Reporter's words (brief):** "implement the most valuable FL-exact items" of the menu / shortcut survey.
- **FL evidence:** [ShortcutsModule] EEShiftUAction / EEShiftDAction = Shift+Up/Down "Transpose up/down",
  EEShiftU12Action / EEShiftD12Action = Ctrl+Up/Down "Transpose one octave up/down", EEShiftLAction / EEShiftRAction
  = Shift+Left/Right "Shift left/right", EEMuteAction = Alt+M, EEUnmuteAction = Shift+Alt+M, EEDeselAction = Ctrl+D.
  [DFM main menu] Patterns › "Find first empty..." Shift+F4, "Find next empty (no naming)" Ctrl+F4, "Clone"
  Ctrl+Shift+C, "Delete" Ctrl+Shift+Del; File › Export › "MP3 file" Ctrl+Shift+R, "MIDI file" Ctrl+Shift+M.
- **LYNX before:** piano roll Shift+Up/Down moved an octave, Shift+Left/Right a bar, Ctrl+M toggled mute (and hid
  the metronome key there), no Ctrl+D. Pattern clone was only Alt+C, Delete had no key, Shift+F4 / Ctrl+F4 were
  missing. The Playlist's Ctrl+Ins / Ctrl+Del / Ctrl+C also caught the Shift variants, so Ctrl+Shift+Ins never
  reached "insert pattern" while the Playlist had focus.
- **LYNX now:** the FL keys above (plain arrows still move: a LYNX extra; Ctrl+R stays on "Last exported
  format(s)"). The Patterns menu shows the new items and
  keys. [TEST] `rea167j_piano_roll_arrow_keys_are_fl_s`, `rea167j_fl_pattern_keys`.

## Where I stopped

- Truncated notes are chased when playback starts, not yet after a jump while playing.
- "Reset plugins on transport" resets the insert effects; instrument plugins are not reset yet.
- The channel-level triangular law covers the Sampler; other generators keep the circular channel pan (no FL
  evidence for plugin generators yet).
- Only the triangular law was rendered by FL in this round (circular was proven in round 85).
- The rest of the gap table (Undo backwards, playlist 7/8/9, time marker
  keys, mixer keys, export extras) is still open; see
  `REA_rodada167j-gaps.md`.
- The 167h leftovers (item 4 of the brief) were not reached.
