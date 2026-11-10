# Round 190s: the MIDI Tools panel and small Clip View pieces (Ableton Live study, 0.1.26)

Date: 2026-11-10. Base: wip/ableton190o-dv (7a4cd4bd9) + feature/mt190o (884f62cf0) + origin/main c400cbe22.
No new Live session this round: everything below rests on round 190o's session (Live 12 Trial 12.4.16) and its notes
(`docs/analise/REA_rodada190o.md`; shots in `/mnt/e/lynx-work/ableton190o/shots/`). Clean room: own code and art.

Tags as in 190o: **[LIVE]** seen in Live 12.4, **[MAN]** Live 12 manual, **[INFERRED]** LYNX's reading.

## 1. The Transform panel (GAPMAP-0126 D7, rank 1)
`ui/MidiToolsPanel.{h,cpp}`: Live's Transform section as the fourth panel of the Clip View's column (Clip | Launch |
Notes | Transform) and as **Tools › MIDI Tools…** in the Piano roll (a small tool window), so the FL workflows reach
it too. FL's own tools and menu are unchanged.

| Live 12.4 [LIVE, `live-03-transform-panels.png`] | LYNX |
|---|---|
| Section title, fold triangle | title "Transform", a fold button (the panel folds to a 20 px strip) |
| reset button + tool chooser | the same; chooser in Live's order with the tools LYNX has: Arpeggiate, Chop, Connect, Ornament, Quantize, Recombine, Span, Strum |
| the tool's controls, Live's defaults | the same controls and defaults (table below) |
| **Auto** (on by default) | the same: every change runs the tool again on the notes as they were before the tool [LIVE: three changes in a row gave the last settings' result only] |
| **Transform** | makes the result final (the next change starts from the notes as they are); with Auto off it is what runs the tool |

Controls (defaults are Live's, REA_rodada190o.md §1):
- **Arpeggiate**: Style (18), Steps 2, Distance +12 st, Rate 1/32, Gate 100 %.
- **Chop**: Parts 4, Gaps 0. Live's pattern toggles, emphasis row, Stretch and Variation are not in the core yet, so
  they are not shown (no dead controls).
- **Connect**: Spread 0 st, Density 100 %, Rate Grid, Tie 0.0 %.
- **Ornament**: Flam on (Position −20 %, Vel. 50 %); Grace notes off (Pitch High / Same / Low, Position −20 %,
  Vel. 50 %, Chance 100 %, Amount 3). With a project key, High / Low step by scale degrees [MAN].
- **Quantize**: Grid, 1/4, 1/8, 1/16, 1/32, Triplets, Adjust note Start / End (Start), Amount 100 %.
  New in the core (`miditools::quantize`): Start moves the note keeping its length, End moves the note's end [MAN].
- **Recombine**: dimension (Position), Shuffle, Mirror, the Rotate bars (one per chord, click one), Grid, − value +.
- **Span**: Legato / Tenuto / Staccato (Tenuto), Offset 0.0 %, Variation 0.0 %.
- **Strum**: Low 0.0 %, High 0.0 %, the curve display with its two handles (drag sideways), Tension 0.0 %.

How it edits (LYNX's own, fitted to the undo history): each control change is an ordinary undoable edit that puts
the notes back as they were when the tool started and applies the tool; the changes of one mouse press are one undo
step. The panel drops its "before" notes when the pattern no longer holds its last result (another edit, an undo,
another selection or clip), when another tool is chosen, or on Transform. The tool works on the docked Piano roll's
selected notes, or on all the notes it shows when none is selected (Live: the same), and its result is selected.
Random parts (Connect, Recombine's Shuffle, Span's Variation, the random Arpeggiate styles) keep one seed while a
tool is being tried, so a preview does not jump; Transform, or switching Shuffle on again, draws a new one.

Differences from Live, on purpose:
- LYNX's column is horizontal (approved in 183a), so Transform is a panel beside Notes, not a section under it.
- Knob / box art is LYNX's. Live shows a note icon in Span; LYNX none.
- Not there yet: Glissando and LFO (MPE), Time Warp, Velocity Shaper, the Generate section.

## 2. Clip View leftovers (GAPMAP-0126 §2.1)
| # | Live 12.4 [LIVE `live-01-clipview-notes.png`] | LYNX now |
|---|---|---|
| D15 | clip title bar: a square in the clip's colour that is also its activator | a square left of the Name field: filled in the clip's colour = active, hollow = deactivated; click switches (the clip's mute), right-click opens the usual colour menu |
| D6 | Pitch & Time: the pitch range "C3 - C4", dragged to transpose | the Notes panel's first cell: lowest - highest note (LYNX's octave numbers); drag up / down transposes every note of the clip, double-click undoes the drag's total |
| D4 | an interval size field ("0 st") + **Add Interval** | the same pair: every note gets a second note that many semitones away |
| D5 | Set Length + Grid chooser, Humanize amount, Stretch knob | **open**: the Notes panel is full at the dock's default height (six rows); needs a layout decision |
| D13 | editor header: Scale / Highlight Scale | **open** |
| D8 | loop Position / Length with Set, Duplicate (loop) | **open** (needs the loop position in the model) |

[INFERRED] that Live's "0 st" field is the interval's size and that the range display is what transposes: the 190o
shot shows one number field beside Add Interval and the range display above it; the manual names an "Interval Size"
control. Not driven in Live this round.

## 3. Best way?
- **FL:** tool dialogs from the Piano roll's Tools menu with preview and Accept (REA rounds 143a / 168g); transpose by
  keys (Ctrl+Up / Down, Shift+Up / Down); clip colour and mute in the clip menu.
- **Ableton:** everything in the Clip View beside the notes, no dialog; Auto apply.
- **Users:** see REA_rodada190o.md §6 (Live 12's tools are the most praised part of that release; FL's Piano roll
  tools the most praised part of FL).
- **Verdict:** both, as before: FL's dialogs stay the default in the FL workflows; the panel is added where Live has
  it and one menu entry away elsewhere. Nothing removed.
