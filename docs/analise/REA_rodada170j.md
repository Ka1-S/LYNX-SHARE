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

## Perf (real Windows windows, `--artgui`, process CPU % of one core over 6 s, busy machine — noisy)

| editor | before (old style) | Themed, animating | Themed, motion off |
|---|---|---|---|
| Sakura (12 petals, 20 Hz, branch band only) | 2.1 | 5.7 (first run 23, warm-up) | 2.1–4.2 |
| Toxic Biohazard (trefoil pulse + bubbles) | 3.1 | 3.6–8.3 | 0.8–4.4 |
| Sytrus (Lissajous) | 3.1 | 2.1–4.2 | 0.8 (one 16 outlier) |
| 3x Osc (scope) | 1.0 | 3.4–5.5 | 2.3–2.9 |
| Facet EQ (no added motion; its analyser is live) | 14.6 | 13.0–25 | 15.6–33 |
| Nebula (twinkles) | 4.7 | 3.9–5.2 | 1.6–2.1 |

First version (petals over the whole page, 30 Hz, every item repainted) cost ~27 % for Sakura: fixed by (1) partial
repaints drawing only the layout items they touch (generator page and effect panel), (2) petals confined to the
branch's band above the controls, 20 Hz. Static art is cached (one image per size), so idle editors cost nothing extra.
GPU: plugin editors draw with GDI (software) since round 157a, so no GPU load.

## Where I stopped

- 2026-11-04 ~04:00: pilot done and pushed (feature/art170j + wip/art170j, merged with origin/main); Windows build
  clean; self-test plugin_art=OK; shots + review page in /mnt/e/lynx-work/tracker/plugin-art/. NOT landed: waiting for
  the user's approval relayed by the coordinator. Next: keep merging origin/main; after approval run build-local +
  coord/train.sh art170j; then roll out art to the other plugins.
- Commands: `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh`;
  `LYNX.exe --artshot <dir> before|after`; `/mnt/e/lynx-work/art170j/artgui.sh before|after|still` under
  `flock ~/.cache/lynx-wingui.lock`; page: `python3 -I /mnt/e/lynx-work/art170j/mkpage.py`.
