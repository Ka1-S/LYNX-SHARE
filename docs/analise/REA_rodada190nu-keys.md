# REA round 190nu — FL Studio 2026's key markers and Automatic chord stamp

2026-11-20, round chord2_190nu (GitHub discussion #105, part 2). Clean-room: behaviour, menu captions and file fields
only. Evidence: **[CAP]** FL Studio 2026.1.7 driven under the GUI lock (shots kept in the round folder
`/mnt/e/lynx-work/chord2_190nu/ref/fl-*.png`), **[FILE]** a project FL saved with four key markers (`ref/flkeys.flp`),
**[WN]** FL's installed `Help/WhatsNew.htm`, **[STR]** caption strings of `FLEngine_x64.dll`.

## 1. Key/Scale markers (pattern markers, type 12)
- Piano roll ▾ › Time markers › **Add key marker ▸** [CAP][STR `EEMarkerAddKeyMenu`]: a "Root note" column (C … B, radio;
  choosing one keeps the menu open) and a "Scale" column (Default, Black notes, then the reference scales). A scale
  click adds the marker at the song position, named "<root> <scale>" ("C Major (Ionian)").
- The marker shows under the ruler as "♪ C Major (Ionian)". From it to the next key marker the grid's rows are shaded for
  its key [CAP: C Major at bar 1 and C Minor Natural at bar 3; rows E♭ A♭ B♭ light only after bar 3].
- Marker menu [CAP]: heading "<name> - Key/Scale at 3:01:00", Delete, Rename…, Move content left / right, Action (None,
  Pattern length, Time signature, Start / Stop recording, **Key/scale ▸** with the same two columns). A new root renames
  the marker; notes do not move.
- .flp [FILE]: after `0x94` (type 12 << 24 | tick): `0x21` 4, `0x22` 4, `0x2E` root (0 = C), `0x2D` 0, `0xA8` reference
  scale index, `0x65` (word) out-of-scale mask with bit (11 − pitch class): C major 1322, C harmonic minor 1190, C major
  pentatonic 1387; `0xCD` name. The A-minor marker made by changing a C marker's root kept index 0 with the A-minor
  mask, so LYNX trusts the mask when the two disagree (core/KeyRegions.h `keyFromFl`). Other markers of FL 2026 files
  carry the same fields with zeros.

## 2. Automatic chord (Stamp menu)
- Stamp menu items [CAP]: Automatic chord (top down), Automatic chord (bottom up), Melodic – chords, Melodic – scales,
  Percussions, Special.
- It is the Chord Progression tool's generator [STR `auto_stamp`, `Stamps\Melodic - chords\|Chord progression\cpt.pyscript`;
  WN 25.1.5 #20702 "will now generate different notes every time", 26.1 #21521 "voice leading mode", #22155 chord markers].
- [CAP] bottom up, a click on D5: C major (E5 G5 C6) in the C Major region, C minor (D♯5 G5 C6) in the C Minor one. It
  follows the key marker but does not build the clicked degree's chord, so LYNX's "Scale degree chord" is LYNX's own.

## 3. LYNX (round 190nu)
core/KeyRegions.h (regions, FL fields, MIDI key signatures, the degree chord), PianoRollComponent (menus, shading, the
stamp's ghost and preview), ProjectFile (`markerKeys`), ProjectDowngrade (copy for 0.1.30), Flp / FlpWrite / FlpImport /
FlpExport, MidiFileIo / MidiImport. Not checked in FL: whether Snap to scale follows the key markers (LYNX: the snap
stays the Piano roll's scale), and what FL writes in `0xA8` for Default / Black notes (LYNX writes −1 / −2).
