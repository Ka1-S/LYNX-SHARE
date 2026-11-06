# Round 175b: FL's Plugin picker layout, measured

Round 171c gave LYNX's picker (F8) a grid of equal cells. FL does not do that. This round reads FL's own layout code and
checks every rule against shots of FL's picker.

Sources:
- Ghidra decompiles of FLEngine_x64.dll (FL Studio 2026), read-only copy of the analysed project. Outputs are in
  `E:\lynx-work\picker175b\re\` (lay.c, row.c). Round 171c's outputs (pl0–pl4.c) cover the input handlers.
- One FL session (FL64 PID 4444, Default template, closed by its PID). Shots are in `E:\lynx-work\picker175b\fl\`:
  - `fl-1920-*`: 1920 × 1080 screen;
  - `fl-1280-*`: the screen switched to 1280 × 720 for the session (temporary display mode, restored at once).
- FL's Plugin database bitmaps: `Data/Patches/Plugin database/**/*.png` (138 files). Only their pixel sizes and DPI were
  read; nothing of them ships.

## 1. The window

- The picker is a form of its own. It covers the **work area of the monitor**, whatever the size of FL's main window.
  - [FL fl-1280-all-win, fl-1280-all with FL at 1280 × 720 in a 1920 screen] The picker still spans 1920 × 1032 and the
    desktop shows through it.
- It is see-through ([STATIC FUN_00ec9870]): the background is a vertical blend of two dark tones, (0x23,0x20,0x28) to
  (0x17,0x22,0x28), with alpha 236/256 over what is behind. Over the top and bottom 96 px (× UI scale) the tone also
  fades towards black, by `1 − (1 − d/96)³`.
- The fade-in and the 75 % mouse rule are in round 171c.

## 2. The tiles

### Size [STATIC FUN_00ec3e10, FUN_006c0de0, FUN_006c9430]

- A tile shows the plugin's database bitmap (`Bitmap=` in its .nfo).
  - The bitmaps are 192 dpi PNGs (pHYs 7559 px/m). FL rescales a bitmap by screen dpi / bitmap dpi, so on a 100 % display
    it shows at **half its pixel size**.
  - FL's bitmaps are about half of each editor, at most 258 × 128 (e.g. Fruity Limiter 418 × 200 → 209 × 100;
    big synths 128 high). So a shown picture is about a quarter of the editor, at most 129 × 64.
- The bitmap is multiplied by 1.1 (brightened) and gets a 3 px blurred drop shadow, which grows it by 6 px.
- The tile rect is that + 2 px.
- A plugin with **no bitmap** becomes a text tile: its name in the picker font at 17 px (× UI scale).
  - The first letter is in FL's highlight colour, the rest white, with a dark blurred outline.
  - [FL fl-1920-all] "Audio Clip" and "Automation Clip".

### Order [STATIC FUN_00ec3e10]

- The Generators and Effects folders of the Plugin database are scanned for .fst files.
- Each entry is inserted at its place by name (CompareStringW), so each half is A–Z.
- An entry whose name is already listed only adds its category to that tile ("Ogun" in Drum and Synth special is one
  tile).
- Only the Plugin database is listed. "Installed" is not in the picker; its "..." ends open it in the Browser (171c).
- [FL install, fl-1920-all] `Generators/Misc/Audio Clip.fst` and `Automation Clip.fst` have no .nfo or bitmap, so
  they are text tiles, sorted with the rest ("3x Osc", "Audio Clip", …, "Automation Clip").

### Packing [STATIC FUN_00ec0d40, FUN_00ec0510, FUN_00ec0180]

- The box: the form's client area minus 32 px left and right, 56 px top and 96 px bottom.
- On the All tab (or when the other half's category is shown), each half gets `(width − 80) / 2`: Generators at the
  left, Effects at the right. When a category tab is shown, its half gets the whole width.
- Rows are filled left to right with an 8 px gap. A tile that would pass the right edge starts a new row.
- A row is as tall as its tallest tile. Every tile is centred on the row's middle line.
- A finished row is spread over the width: `dx = spare / (n − 1)` is added to every gap. Exceptions:
  - A row with one tile is centred when it is the only row. Otherwise a generator tile goes to the right edge and an
    effect tile stays at the left (the tile's type 0 / 1).
  - When the only row has room to spare, each gap gets at most 32 px extra and the row is centred.
- Rows are stacked with `min(16, spare / gaps)` px between them, and the block is centred in the box.
  - When the rows don't fit, that gap is negative and **the rows overlap**. FL never scrolls.
  - [FL fl-1280-all] At 1280 × 720 the All tab is an overlapping mosaic.

Checks against FL. `tests/Picker175bTests.inc` runs the same numbers through `pickerview::packRows`:

| Shot | FL picture left edges (px) | packRows |
|---|---|---|
| fl-1920-dynamics (one row, centred, 32 px gaps) | 312 488 664 840 992 1168 1282 1435 1519 | same |
| fl-1280-synthclassic (two rows, both spread) | 36 147 282 389 536 652 791 912 1028 1152 / 36 404 798 1170 | same |
| fl-1920-synthclassic (14 tiles, one row, edge to edge) | 36 … 1885 | same |

## 3. Titles and tabs [STATIC FUN_00ec9870, FUN_00ec6760, FUN_00ec3e10, FUN_00ec53b0]

- "Generators" and "Effects" are drawn only on the All tab, at 26 px, centred over each half of the form, in a band
  78 px high. With only effects listed (picker opened from a mixer slot), only "Effects" shows, still over the right
  half (user shot shot839).
- The tab labels of each half use the largest font size from 19 down to 12 at which (text widths + 2 × size per tab)
  fit in half the form.
  - [FL fl-1280-all] 13 px for the 9 generator tabs, 12 px for the 11 effect tabs.
- The labels of each half are one row, spread over that half like a tile row.
- "All" sits at the middle of the form, at 26 px × (the larger tab size / 19).
- "..." sits 8 px in from each edge.
- The strip's row: `base = min(form height, max(tile bottom + 2 × its row height) + 80)`, taken once from the All tab.
  The tab centre line is at `base − (tallest tab / 2 + 21)`. It stays put while tabs change.
  - [FL] Centre 998 at 1032 high; 638 at 672 high.
- The shown tab is white with a small mark above it; the others are grey.

## 4. Hover and press [STATIC FUN_00ec9870, FUN_00ec8a90]

- The highlight behind the tile pointed at:
  - horizontally, the tile ± (4 − 1) px, plus 6 % of its width + 1 px on each side;
  - vertically, the row's middle ± (row height / 2 + 14) px;
  - rounded with radius (24 − 20 × anim) + 5, so 9 px once settled;
  - a vertical gradient with a light line along the top.
- Its colour is blue-grey 0x96B7CA at alpha 80, and orange 0xFFB850 at alpha 200 while the button is held.
  - [FL fl-1920-dynamics-hover, pixels] On screen it reads as a cool cyan tint: about +22/+44/+46 over the veil under
    the top edge, +7/+20/+19 at the middle. A bright glow sits at the middle of the top edge ((179,255,255) at its
    peak, fading over about 20 px down and a third of the width across), and smaller ones at the middle of each side.
- The hovered tile is drawn a little bigger (eased; about 6 %).
- A tab pointed at gets the same box, ± 18 px vertically and not widened ([FL fl-1920-synthclassic]).
- The label is a dark box (0x080808, alpha 240) whose top is 3 px above the highlight's bottom, about 53 px high at
  1920 ([FL fl-1920-dynamics-hover]: name glyphs 19 px top to descender, category 16 px). It holds:
  - the name at 21 px;
  - the category at 17 px (15 px when it holds a "/"), white at alpha 0xB0;
  - the .nfo Tip at 11 px, if there is one.
- Its centre eases towards the mouse x (0.075 per frame), shifted towards the screen's middle by
  `(x − W/2) / (W/2) × half its width × 0.5`.
- Search, keys, wheel, click and drag: round 171c (unchanged).

## LYNX (round 175b)

- `core/PickerView.h`: `packRows`, `tabFontSize`, `tabCentreY`, `pictureSize` as above.
- `ui/PluginPickerView.cpp`: the picker is its own borderless window over the main window's monitor work area, with the
  screen captured once under its veil.
  - Tiles are packed as FL packs them, at half their picture size, with a 3 px shadow. Plugins with no picture are text
    tiles.
  - Titles, the tab strip, hover, press and label follow §3–4. The veil uses FL's two tones and the hover FL's measured
    tint and glows. LYNX's own colour is kept where FL uses its accent: the press is LYNX red (FL: orange) and so is the
    first letter of a text tile.
  - Pictures are drawn × 1.1, as FL draws them (once, when they are scaled).
  - The label's sizes give LYNX's face FL's on-screen glyph heights (name 25 px bold, category 20 px, box 53 px); the
    category is LYNX red.
  - Audio Clip and Automation Clip are text tiles in Generators › Misc. Audio Clip adds an audio clip channel;
    Automation Clip an automation linked to nothing, its clip on a free Playlist track and its editor open (LYNX has no
    automation channels in the Channel Rack). Dragged, they do the same on the Channel Rack or a mixer strip.
  - Drops checked in LYNX: Channel Rack (new channel), a mixer slot (the effect in that slot), the Playlist (nothing,
    as in FL: REA_rodada171c s17→s18).
  - A dragged tile: the picker window steps aside and the drag runs in the main window's drag container, so a tile can
    be dropped on the Channel Rack, the Playlist or a mixer slot.
  - Third-party plugins in no Plugin database folder are not listed, as in FL.
  - The timer stops once the pictures are in, so the picker does no idle work.
- `ui/PluginThumbs`:
  - Pictures use FL's budget (half the editor, ≤ 258 × 128); they go to a new cache folder, `<version>-r3`.
  - Editors that are empty when new are drawn in use, with LYNX-made content: Newtone and Newtime analyse a synthetic
    sung phrase, Control Surface has two knobs, Video Player shows a drawn still, and VFX Script has a script with
    controls; the HTML NoteBook shows a sample page (its picture was drawn from the wrong slot target and showed "No
    page").
