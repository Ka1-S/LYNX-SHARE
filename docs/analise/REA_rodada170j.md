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

## The split (after the user's approval, 2026-11-04)

The art is data now, so two rounds can add themes without touching the same lines:
- `src/ui/PluginArt.inc`: the framework — `artkit::Theme` (palette, base style, knob family `Cap`, `Pointer`, scale
  ticks, flags `thin` / `twoTone` / `buttTrack` / `thumbPointer`, arc glow, knob scale, mote motion) and optional hooks
  (`canvas`, `headerDeco`, `sectionDeco`, `titleEmblem`, `drawCap`, `tipDeco`, `paintMotion`); shared motifs; the
  themed surfaces and controls read the theme's fields (no `switch (art)` left); `ArtLayer` moves motes generically
  (`Drift::still/fall/rise`) and calls the theme's painter; the self-test walks every id.
- `src/ui/PluginArtGenerators.inc` + `PluginArtGeneratorIds.inc` (ids 1..127, `artForInstrument`) — this round.
- `src/ui/PluginArtEffects.inc` + `PluginArtEffectIds.inc` (ids 128..255, `artForEffect`) — round 170k.
- `skin::Art` is built from the two id files (X-macro), `plainStyleFor()` gives an effect's style without its art.
- `--artshot` / `--artgui` list every plugin with art from the registries (optional `name,name` filter): nobody edits
  a list in Main.cpp to shoot a new theme.
- Verified: the six pilots' shots are pixel-identical to the approved ones (only the motes' positions differ in Toxic and
  Nebula: the generic drift), self-test plugin_art=OK / result=OK.
- Style guide for every theme: the tracker's plugin-art-0.1.24.md ("Reply from 170j").

## Where I stopped

- 2026-11-04: split done (feature/art170j), Windows build + shots + self-test OK; landing via the train. Generator
  batches G1 (Harmor, Harmless, GMS, Morphine, Ogun, PoiZone, Kepler, Kepler Exo) and G2 (Transistor Bass, Sawer,
  BeepMap, FL Keys, Granulizer, Drumaxx, Drumpad, BassDrum) written in worktree ~/projetos/lynx-wt-art170jb
  (feature/art170jb); next: build, shots, review, land; then G3 (BooBass, DrumSynth, DX10, MiniSynth, Plucked!,
  SimSynth, Autogun, FL Slayer, Fruit Kick, Wasp) and G4 (own-editor ones: DirectWave, Wave Traveller, Fruity Dance,
  sampler, SoundFont, LYNX's synth and drums, Video Player...).
- Commands: `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh`;
  `LYNX.exe --artshot <dir> before|after [name,name]`; `/mnt/e/lynx-work/art170j/artgui.sh before|after|still` under
  `flock ~/.cache/lynx-wingui.lock`; page: `python3 -I /mnt/e/lynx-work/art170j/mkpage.py`.
