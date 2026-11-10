# REA round 190r: Slicex region keys and layering against FL, the editor, the "add channel" entry

Continues `REA_rodada190h.md` (state, regions, key map, voice engine) and `REA_rodada190l.md` (the channel, FL's renders
of edited states). Clean-room: FL Studio 2026 was run, its windows were photographed, its files read and its renders
measured; no Image-Line code, bitmaps or fonts.

## 1. Region keys: what FL writes [EXEC]

In FL's own Slicex (project `Slicex_vel64` of round 170i2, deck A = "DL Breaker", 22 regions): right click on the
keyboard's C#5 → "Assign current region to this key" (region 1), then save. The deck's embedded WAV grew by 36 bytes:

```
… cue (532) · LIST adtl (1016) · tlst (28) · LIST INFO (36)
74 6c 73 74  1c 00 00 00   01 00 00 00   63 75 65 20  00 00 00 00  01 00 00 00  ff 3d 90 00  00 00 00 00  00 00 00 00
"tlst"       size 28       count 1       "cue "       cue id − 1   1            FF key 90 00 0            0
```

So the trigger list is **a chunk of its own**, placed after LIST/adtl, not a LIST with the type `tlst` (what
REA_rodada190h §3.1 read from the writer and round 190l built by hand — FL ignored that one, which is why its 13
`key_*` renders were identical to the unedited project). The entry layout of 190h holds: 24 bytes, the cue id minus
one, the key in byte 13. `slicex::wavRegions` now reads the chunk where FL puts it (and no longer a LIST-wrapped one);
`slicex::withRegionKeys` writes it (the editor's "Assign current region to this key").

Side observations from the same session: with "Dump deck A to piano roll after editing" on, the assignment re-dumped
the score (22 notes: region 1 and region 2 both on C#5 = 61, then 62, 63 …: an automatic region keeps `base + index`
even when a keyed region sits on that key); the keyboard no longer lights C5; the region shows "C#5" in its lower
left corner.

## 2. Layering against FL's renders [EXEC]

17 projects (`p/sxmake`, /mnt/e/lynx-work/slicex190r/r190r): regions 1, 2 on key 60 and regions 3, 4, 5 on key 61
through the trigger list as FL writes it, 10 notes of velocity 64 on keys 60…69, rendered by FL (`/R`, 16-bit) and
compared with the engine alone (`p/flcmp`, least-squares gain, residual relative to FL). FL's renders now differ from
the unedited project's and from each other, i.e. FL layers.

| layering mode | crossfade | residual | worst sample |
|---|---|---|---|
| 0 All | – | −74.8 dB | 1 LSB |
| 1 All (gain-compensated) | – | −71.0 | 1 LSB |
| 2 Velocity-mapped | none / linear / balanced / unbalanced / adding | −70.7 / −70.9 / −70.9 / −72.5 / −74.0 | 1 LSB |
| 3 Modulation X-mapped | none (Mod X 40), linear (150), adding (300) | −71.5 / −70.9 / −73.8 | 1 LSB |
| 4 Modulation Y-mapped | balanced (Mod Y 400) | −71.2 | 1 LSB |
| 6 Cycle | none, linear | −71.5 / −71.5 | 1 LSB |
| 7 Song time-synced | none, linear, unbalanced | −71.5 / −71.4 / −71.4 | 1 LSB |
| 5 Random | none | −7.8 | (FL's random numbers are its own: not comparable sample by sample) |

−71 dB with a worst sample of 3·10⁻⁵ is the 16-bit file's quantisation (as in 190l). Every deterministic mode,
including mode 7 as implemented in 190l from the REA reading and every crossfade shape, is FL's. No engine change was
needed; the only fix is where the keys are read from.

The project FL itself saved (22 dumped notes at tick positions that fall between samples) matches at −80 dB on most
notes, with −65 dB on four of them and −35 dB in the last 20 ms. Both are outside Slicex: where the host puts a note
that starts between two samples, and what the host does with the last note at the end of a render. Open (the rack
path, not the plugin).

## 3. The editor (slice 3, stage 1) [EXEC for FL's window and menus, form resource for the coordinates]

FL's form is 714 × 625 (`SynthPanel`, form resource of round 188c; FL's window photographed at 100 % in this round:
default state, LFO / velocity / envelope parts, and the Options, layering, region, envelope, File and Regions menus).
LYNX draws the same layout a quarter larger (893 × 781) with its own look (`ui/SlicexEditor.cpp`); every control sits
at the form's coordinates:

| area (form px) | FL controls | LYNX |
|---|---|---|
| top, y 0–99 | master level, randomness, LFO and pitch sliders (x 22 / 46 / 71 / 95, 20 × 75); the X / Y pad (132, 9, 77 × 77) with the Mod X / Y wheels and "Smooth up modulation"; Options button; layering mode combo (424, 9), "Layer regions of same deck only", crossfade selector; "Dump deck A to piano roll after editing"; logo | the same controls on the same spots, bound to the masters and the options block; the layering combo carries FL's eight names (All, All (gain-compensated), Velocity-mapped, Modulation X-mapped, Modulation Y-mapped, Random, Cycle, Song time-synced); the Options menu holds the four settings that live in the state (Sample start covers whole length, Link velocity to volume, Loop half of regions, HQ envelopes) |
| ArtPanel, y 99–297, left | "More region settings", current region combo, output track offset, cut group; four rows (pan / volume, cutoff / resonance, pitch, delay / start), each with its articulator group digit and amount wheel | the 17 ints of the region record (`RegionField`) |
| ArtPanel, middle | articulator group selector (1–8), filter type and mode selectors, ENV / CUT / RES wheels | the group's filter header |
| ArtPanel, right | editor target (pan, volume, cutoff, resonance, speed, start), articulator part (envelope, LFO, velocity, Mod X, Mod Y, random), the envelope editor with its knob rows (ATT / DEC / SUS / REL; SPD / TNS / SKEW / PW on the LFO part; TEMPO and GLOBAL) | the graph is drawn from the engine's own curve code (`slicex::curveLevels`, `lfoCycle`), so a segment's shape is the one the voices play; points move, are added (right click on the graph) and deleted, decay / loop / sustain marks and the 13 curve modes are in the point's menu |
| DeckPanel, y 296–362 | loop / play / stop, the Edison-style menu buttons and tools, deck selector, sample rate, tempo, title | stage 1: loop / play / stop, File (load sample), Regions (set all cut groups), deck selector, single / dual deck, the labels (title from the WAV's INAM, length and selection in bar:beat:tick) |
| decks, y 362–563 | deck A (and deck B in dual view): the wave with its regions | wave, region starts and names, the key a keyed region plays in its lower left corner (as FL shows "C#5"), click selects and plays the region, drop a file to load it; the region's menu has FL's "Trigger note" |
| KbPanel, y 571–625 | base octave, two-row pad keyboard | the same two rows; keys with regions are marked in the deck's colour; a click plays the channel's note; the key's menu is FL's ("Assign current region to this key", "Select region assigned to this key") |

The state is edited as FL's own (`slicex::parse` → change → `slicex::write` into the channel's plugin state; the
instrument takes it live). A sample loaded on a deck is referenced by its path (`setDeckFile`): deck A's in the channel's
sample path, deck B's in its one zone (round 190l). Assigning a key embeds the deck's WAV with the trigger list (§1),
as FL does when a deck is edited.

**Not in stage 1** (the buttons are not drawn rather than drawn dead): the Edison tools of the deck row (format, edit,
tools, snap, select, zoom menus; normalize, fades, script, stretch, auto-slicing, add marker, save, drag, dump to piano
roll, scrub, spectrum view) and the rest of the File / Regions menus, the display mode selector and the "view keyboard"
button (the window keeps the full layout), the Options menu's macros, region rename / delete / clone / purpose, moving
region starts in the wave, the time scroller, the envelope's time zoom and options menu, "Lock audio data" and "Leave
unedited audio data on disk".

## 4. A new Slicex ("add channel")

`natives::instrumentEngines()` lists Slicex; a new channel is `slicex::fresh()` (FL's Default preset values, both decks
empty), silent until a deck has a sample, as FL's is.

## 5. Best way?

- **FL:** one window holds everything: two decks sliced by markers, a region panel with per-region pan / volume / filter
  / pitch / delay / start, 8 articulator groups (envelope + LFO + four maps on six targets), layering of regions that
  share a key, and a keyboard map. Proof: the form resource (round 188c), the state (190h) and FL's renders (190l, §2).
- **Ableton:** Simpler's Slice mode cuts by transient, beat, region or manually, plays slices mono / poly / through, and
  shares one filter and one envelope for all slices; per-slice settings need "Slice to Drum Rack" (one Simpler per pad).
  It is quicker to reach and has less to learn; it has no per-slice articulation inside the one device.
- **Users:** Slicex is liked for depth (drop a loop, chop, replay slices in any order, per-slice treatment) and
  disliked for its window management and dated, dense panel; Fruity Slicer is seen as the quick one with beat-grid
  chops (https://ecksesontress.blogspot.com/2025/04/the-1-reason-to-use-slicer-over-slicex.html,
  https://www.audeobox.com/learn/compare/fl-studio-vs-ableton/, FL's manual pages for Slicex and Fruity Slicer).
- **Verdict:** FL-exact layout as the default (projects and habits carry over). A possible addition, not built: a
  "simple" display mode with only the wave, the region row and the keyboard (FL's own display modes already go that
  way); to be proposed to the coordinator, not switched on by a round.
