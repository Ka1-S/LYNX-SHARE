# Round 170j — Plugin art pilot ("Themed" plugin style)

User (2026-11-04): the plugin editors all look alike ("designs bem semelhantes, coloridos"); each plugin should be more
beautiful and more visual, with the aesthetic / mood of its original (Sakura with a cherry-blossom tree...). Own art
only (procedural vector drawing), never the originals' bitmaps / logos / skins; control placement and readability kept;
the LYNX logo unchanged. PILOT of six flagships first; nothing lands on main before the user approves.

LYNX-own item (look only): no FL behaviour to prove; layouts and gestures (the parity work) are untouched.

## What was built

- `skin::Style::themed` (5th value of Settings › General › Plugin style, "Themed") and `skin::Art` (none, sakura,
  toxic, sytrus, threeOsc, facet, nebula). Themed is the automatic style of the six pilots; a plugin without art never
  draws Themed (forced "Themed for every plugin" keeps their automatic style). Effect header menus offer Themed only
  for effects with art.
- `src/ui/PluginArt.inc`: per-art palettes, illustrations (canvases), themed sections / titles / wells / knobs /
  faders / buttons / pills / plates; everything else (title text, LEDs, meters, curves) uses the art's base style.
- `skin::ArtLayer`: the moving part (Sakura petals, Toxic trefoil pulse + bubbles, Sytrus Lissajous, Nebula
  twinkles), a click-through overlay that repaints only small areas, ≤ 30 Hz, only while shown and Themed; off with
  Settings › General › "Animate the plugin art", "Reduced effects", or Windows' "Show animations" off.
- Wired into the generator page (Sakura, Toxic Biohazard, Sytrus, 3x Osc), the FL-arranged effect panel (Nebula) and
  Facet EQ (analyser in ice, sum glow in gold when Themed).
- Tools: `LYNX.exe --artshot <dir> [before|after]` (off-screen), `LYNX.exe --artgui <dir> [before|after|still]`
  (real windows, screenshot handshake, process CPU per editor). Self-test: `plugin_art=` line.
- Art directions: /mnt/e/lynx-work/tracker/plugin-art-0.1.24.md. Review page: /mnt/e/lynx-work/tracker/plugin-art/index.html.

## Where I stopped

- 2026-11-04 ~01:00: code written (PluginArt.inc, skin, editors, settings, i18n 14 tables, artshot/artgui, selftest);
  syntax check running; next: Windows build, --artshot before/after, --artgui shots + CPU, review page.
- Commands: `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh`, then
  `LYNX.exe --artshot <dir> before|after`.
