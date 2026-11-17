# REA round 190kd: the Piano roll's chord tool (its strip of chord blocks, Generate modes, Analyze, Learn from MIDI input), scale shading, Channel name alignment, and an FL screen session

Round 190kd (LYNX SHARE 0.1.30). Sources: one FL Studio 2026 screen session (GUI lock + FL lock + idle gate + input
wall, 2026-11-17 00:31–00:46, shots in `/mnt/e/lynx-work/prchord190kd/raw/f*.png`), the static Python 3.12 byte-code
listing of FL's Chord progression tool (`System/Tools/Chord progression/*.pyc`, disassembled with `dis` only; nothing of
FL's was run; listings in `/mnt/e/lynx-work/prchord190kd/rea/dis` and round 190jv's `notes/pr-dis`), FL's form resource
(DFM `TPRChordToolForm`, rea190bc `areas/prtools.tsv`) and Image-Line's online manual (`pianoroll_chordprogression`).
Clean-room: behaviour in our own words, no Image-Line code, data, presets or images shipped; FL's trained chord data
(style matrices, priorities, `cpt_config`) was not read. Marks: **[SHOT]** FL screenshot, **[DEC]** static byte-code
read, **[DFM]** form resource, **[MANUAL]** manual, **[HYP]** not proven. The full byte-code spec with line evidence is
`/mnt/e/lynx-work/prchord190kd/rea/chordtool-spec.md` (§ numbers below refer to it).

Contents:
1. The FL screen session (what was captured)
2. FL's chord tool: the strip of chord blocks and its top panel
3. Generate modes, Analyze, the options for generation, Learn from MIDI input
4. Piano roll scale shading with a chosen scale
5. Channel rack › Options › Channel name alignment
6. Arpeggiator pattern list, Trim unused tracks (references for round 190jv's dialogs)
7. What LYNX builds, and the differences left

## 1. The FL screen session

FL Studio 2026 (FL64.exe), new project from the Welcome window, 1920x1080 at 100 %. Every input batch ran under the
GUI lock and the FL lock, behind the input wall, after `safeinput.ps1` and a foreground check per input; the wall log
did not grow. Closing: Alt+F4 › "Save changes to untitled.flp?" › **No**. Settings touched and put back: Channel name
alignment (Left, then Center again), Scale highlighting (Automatic, Minor Natural, then Black notes again).

| Shot | What |
|---|---|
| f03, f04-name-align | Channel rack menu, Options › Channel name alignment › Left / Center |
| f05-rack-left, f06/f43 | the rack with the names left / centred |
| f07, f08-mixer-menu | Mixer and its menu (Options › Trim unused tracks) |
| f09-trim-choices, f10-trim-confirm, f11-trim-after, f12 | "Remove unused tracks" (choices), "This will delete 11 unused mixer track(s). Are you sure?", the mixer after, undo |
| f14, f16-view, f17-scalehl | Piano roll with the A minor line; View menu; View › Scale highlighting (two columns: Root note, Scale) |
| f21-automatic | Scale highlighting › Automatic on the line |
| f39-minor-c, f40-minor-a, f40-menu-minor | a chosen scale: Minor Natural (Aeolian) on C, then on A |
| f22, f23-arp-presets | the arpeggiator form; its preset list |
| f26 … f36 | the chord tool: welcome, the form, hints, spanner menu, chord menu, selected chord, Analyze, Learn, Start over, Names rel, alternatives, key tab menu, Performance / Advanced tabs |
| f37b | closing the tool with changes: "Pending changes" (Discard changes / Cancel, Remember my choice) |

## 2. FL's chord tool: the strip of chord blocks and its top panel

| # | Fact | Evidence |
|---|---|---|
| C1 | Tools › Generate chord progression (Alt+P) opens "Piano roll - generate chord progression - <channel>", first time with a welcome panel ("Welcome to the Chord progression tool" … "Start creating"). | [SHOT] f26, f27; [DFM] WelcomePanel |
| C2 | Top panel: Play, Solo; in the middle the spanner ("Options for generation"), **Generate** (orange), then three mode icons: arrows = "Generate mode" (Rework, lit orange by default), dice = "Generate mode: Start over", magnifier = "Generate mode: Analyze the notes found in the Piano roll" (shown only when the Piano roll had notes when the tool opened); on the right Count 4, Octave 5 (with the transpose arrows), Length Bar (with the rescale arrows); under them the Conventional – Adventurous slider. | [SHOT] f27, hints h1–h4; [DFM] ModePageSelect 3 pages, AnalyzeBtn hidden-by-default |
| C3 | The Chord viewer: a ruler with bar numbers, a key tab above the first chord ("♪ A Major (Ionian)", right-click: Edit › Root note / Scale (FL's reference scales) , Randomize, Delete (greyed for the first)), then the blocks across the viewer, a scrollbar, and a bottom bar: zoom, undo, redo, Names abs/rel, Chord nudge slide/swap, Snap. | [SHOT] f27, f34-keymenu, f35-keyedit |
| C4 | A block: padlock top left; "⇕ n" (the inversion) top right; the chord's notes as horizontal lines (main notes above, the bass line low); the name in a darker label in the middle ("Am ∆11", "F♯m add9", "Esus4", "A /E"); at the bottom Regenerate (arrows, left), ▾ Show alternatives (middle), a bass clef (the bass note, right). Measured: block #495c67, label #374a55, lines #9ed1a5 2 px, 1 px dark gap between blocks. | [SHOT] f27, f31a |
| C5 | Selection: a 2 px yellow-green outline (#a8e44a); while learning from MIDI the selected block is outlined red. | [SHOT] f31a, f32-learn |
| C6 | The chord menu (right-click): Preview, Preview from this chord, Lock, Main notes ✓, Bass note ✓ · Change chord: Regenerate, Show alternatives..., Add passing chord, Select chord ›, Type in chord name..., Learn from MIDI input · Change notes: Invert ›, Transpose ›, Fixed bass ›, Extra notes ›, Voicing ›, Color › · Piano roll notes: Snap existing notes, Note linking › · Chord actions: Copy, Paste, Swap with ›, Delete. | [SHOT] f30-chordmenu; [DFM] ChordMenu |
| C7 | The spanner menu: Notes: Main ✓, Bass ✓ · Non chord notes: Replace all, Replace clashing, **Keep** (default), Snap. | [SHOT] f29-spanner; [DEC] §0.5–0.6 |
| C8 | Show alternatives: a panel under the blocks, "Select alternative chord (right click to preview)" with refresh and close, six chords in two rows (in C major on vi: I, III, vi/i, VIsus4, vi/iii, I add9). | [SHOT] f33-alts |
| C9 | The presets area: Progression (None) with dice / save / delete, Rhythm (Default), "Type a progression" with get / accept, "Learn from MIDI input" and "One note preview" checkboxes. Performance and Advanced are tabs. | [SHOT] f27, f36-perf, f36-adv |
| C10 | Closing with changes asks "Pending changes — There are unsaved changes. Use the undo button in this tool to restore them." [Discard changes] [Cancel], Remember my choice. | [SHOT] f37b |
| C11 | On the A minor line FL opened in **A Major (Ionian)** and generated E, A/E, D, Esus4; Analyze (with the first block selected) gave Am∆11, F♯m add9, A, A; Start over gave **C Major (Ionian)** Am F Em C. | [SHOT] f27, f31-analyze, f33-dice |

## 3. Generate modes, Analyze, the options for generation, Learn from MIDI input

The byte-code spec (`rea/chordtool-spec.md`) gives the laws; the main ones:

| # | Fact | Evidence |
|---|---|---|
| G1 | Rework = `call_model Generate`: the count, the times and the key stay; locked chords stay; every other chord is sampled again; if no unlocked chord changed, one is swapped for another (`ensure_change`). Single-chord Regenerate never gives the same chord back (regenerate_bias 0). | §5.1 |
| G2 | Start over = `reset`: only the locked chords stay; the key is drawn again from the opening notes and the locked chords (root weight 2, the first chord's 4, chord notes 0.3), sampled more widely as the slider goes Adventurous; with neither, at random, Major 65 %. The count and the global settings stay. | §5.3, §3.5–3.7, §3.11 |
| G3 | Analyze = `call_model Analyze`: the notes that were in the Piano roll when the tool opened, per block; FL's chord analyser (84 templates: 7 types × 12 roots, soft profiles 1 / .75 / .75 / .39 … .10, a dot product, bass ×1.14 when it is the root, a type bonus) over each block's notes (each note 1, the bass ×1.14, × (1 − (1 − overlap/length) × 0.3), a pitch class = max + 0.3 × the rest); a block without notes takes the analysis before it; locked blocks stay. FL then smooths the sequence with a 2nd-order hidden Markov model over its trained style data and samples among the best (so it varies per run). | §1, §2 |
| G4 | The key on opening: every opening note in the scale scores 1 (and 1 for the root when it is the root); at the slider's default only Ionian and Aeolian score; the best (score, root score) wins, root C … B and Major before Minor. On the session's line A Ionian and A Aeolian tie (8 / 3) and FL keeps **A Major (Ionian)** — what FL showed. | §3.9, [SHOT] f27 |
| G5 | Non chord notes: Replace all cuts the parts of the old notes inside the chord blocks; Replace clashing cuts only a note on the very key of one of the block's chord notes (so Main / Bass decide); Keep keeps them and they bias the chord choice (auto-accompany); Snap keeps them and moves them onto the block's chord: nearest key in the chord's pitch classes, an equal distance decided by a seeded coin, two snapped notes sounding together never on one key; a ±6-tick margin (FL's 96 PPQ: a sixteenth of a beat). An old note that exactly repeats a chord note gives way. | §5.5–5.14 |
| G6 | Learn from MIDI input needs exactly one chord selected ("You must have exactly one chord selected to enable MIDI learn."); the played keys are analysed as one chord (FL's single-chord settings: bass ×1.11 at a gap ≥ 7, slash bass named at a gap ≥ 12, one key is enough: its major triad); played pitch classes outside the triad become extensions; the lowest main key sets the inversion. The selection moves on only with "Auto learn next chord" (off). | §4 |

## 4. Piano roll scale shading with a chosen scale

| # | Fact | Evidence |
|---|---|---|
| S1 | View › Scale highlighting is a two-column menu: Root note (C … B, greyed until a scale is chosen) | Scale: Snap PR notes to scale, Snap incoming MIDI ›, Default, Black notes (FL's default), Automatic, then the reference scales. | [SHOT] f17-scalehl, f40-menu-minor |
| S2 | With a chosen scale the rows **out of the scale** take the dark (black-key) shade and the rows in it the light shade, black keys or not: C natural minor → dark C♯ E F♯ A B; on A → dark C♯ D♯ F♯ G♯ A♯. The grid pixel values are the same two as Black notes' (66 / 60 on the session's theme). | [SHOT] f39-minor-c, f40-minor-a (row probe x = 1200) |
| S3 | The keyboard is not tinted; its octave labels move from the Cs to the scale's root ("A3", "A4" in A minor). Automatic keeps the C labels. | [SHOT] f40-minor-a vs f14, f21 |
| S4 | Automatic on the line: dark A♯ C♯ D♯ F♯ G, G♯ light (A harmonic minor), as round 190jv built it. | [SHOT] f21-automatic |

## 5. Channel rack › Options › Channel name alignment

| # | Fact | Evidence |
|---|---|---|
| N1 | The Channel rack menu's Options part: Group playlist tracks created by Loop starter ›, Auto resize ›, Auto switch display filter, Channel button width ›, **Channel name alignment › Left / Center** (radio, **Center** ticked by default), Colorful channel controls, Focus selected channel on playback, Mute removed steps, Show mixer track selectors, Show complete piano roll preview, Show a mute button on the piano roll preview, Detached. | [SHOT] f03, f04-name-align; [DFM] rack-0113…0115 |
| N2 | Left: the names start ~5 px from the button's left edge; Center: centred in the button. | [SHOT] f05-rack-left, f06-rack-center |

## 6. Arpeggiator pattern list, Trim unused tracks

| # | Fact | Evidence |
|---|---|---|
| A1 | The arpeggiator form: Pattern (name, folder button, ▸ preset list), Options: Pattern Normal, Time mul, Range 1 / Normal, Sync Time, Gate, Levels PAN VEL REL MOD X MOD Y PITCH, Group notes, Reset / Accept. | [SHOT] f22 |
| A2 | The preset list is a popup menu, the current pattern in bold at the top, then a heading per folder ("1 note", "2 notes", "3 notes", "4 notes", "5 notes", "Riff machine") with each pattern by its own name, then the folder's own patterns (Default, Night, None, Oxygene, Simple, Trance …), in columns when tall. | [SHOT] f23-arp-presets |
| T1 | Mixer menu › Options › Trim unused tracks → "Remove unused tracks": "Select the mixer track types to be removed." ◉ Unlinked tracks only ○ Unlinked and non-empty tracks [Ok] [Cancel] → "This will delete 11 unused mixer track(s). Are you sure?" ○ Remember my choice [Yes] [No]; on the default template Inserts 6–16 went (the five 808 channels' inserts stayed); Ctrl+Z restored them. Round 190jv's law agrees. | [SHOT] f08, f09, f10, f11, f12 |

## 7. What LYNX builds (round 190kd), and the differences left

- `src/core/ChordStrip.h` (header only): the blocks and their switches (lock, main, bass, inversion, transpose, bass
  octave, fixed bass, voicing, colour), FL's Generate modes (G1, G2 with FL's key draw, G3 with FL's analyser and
  fill-blanks), FL's key guess (G4), FL's Non chord notes rules and snap (G5), FL's learn rule (G6), Count, Add passing
  chord, alternatives, typed names or numerals. LYNX's own: the generator (LYNX's table of diatonic moves, biased by the
  notes found as FL's auto-accompany is) and the Analyze sequence step (per block with FL's settings and fill-blanks;
  FL's HMM smoothing needs its trained style data, which stays in FL), so Analyze is deterministic in LYNX.
- `src/ui/ChordStripView.h`: the top bar (spanner, Generate, the three mode icons with FL's hints; Analyze only with
  notes), the ruler, the key tab and its menu, the blocks (C4, C5), FL's chord menu (C6; Note linking is shown greyed:
  not built), the spanner menu (C7), the alternatives panel (C8), Names abs/rel, Learn from MIDI input (red outline, the
  played chord when every key is up), Alt+right-click / Space preview, Ctrl+A / D / C / V / G, Delete, Shift / Alt+wheel.
  LYNX keeps its dialog's options below the strip (Key, Scale, Preset, Progression, Count, Adventurous, Length, Octave,
  Rhythm, Bass note, Smooth voicing) and adds "Learn from MIDI input" and "Auto learn next chord" there.
- Not built: FL's Performance and Advanced tabs, Play / Solo / One note preview, the strip's own undo / redo, chord drag
  (Chord nudge, Snap), Note linking, FL's "Pending changes" question (LYNX's Cancel restores), FL's reference-scale key
  tab entries beyond Major (Ionian) / Minor Natural (Aeolian).
- Scale shading (S2, S3) and Channel name alignment (N1, N2) are FL-exact; the arpeggiator list follows A2 (the
  folder's own pattern first, then a heading per folder); within a folder LYNX's natural sort differs a little from FL's
  ("Trance" after "Trance 7", "1- 2 3" after "1 3 2"). The current pattern is not drawn bold.
- Analyze: without FL's HMM style data, a block with few notes can name a different chord than FL (the session's line:
  FL Am∆11, F♯m add9, A, A; LYNX Am(maj7, 9), Amaj7, Amaj7, Amaj7). LYNX keeps the single analysis's type priority in
  the sequence as its stand-in for FL's vocabulary priorities.
- The Trim choices dialog now draws each choice once as a radio dot (it repeated each choice as a label above a tick
  box).
