# Round 170k — the "Themed" plugin art for every effect

User (2026-11-04), after the 170j pilot ("você pegou exatamente o que era para pegar"): the themed art on EVERY
plugin. 170k owns all the effects (170j: the instruments). LYNX-own item (look only): no FL behaviour to prove; the
layouts, parameters and gestures (170c / 170i, the parity work) are untouched — only the drawing changes.

Rules kept (170j's): LYNX's own procedural vector art; it evokes each original's mood (material, era, palette) but
never copies, traces or extracts the originals' bitmaps, logos, skins or artwork; readability and control placement
unchanged; static art cached by the kit; motion ≤ 20–30 Hz in small dirty rectangles only while shown + Themed, off
with "Animate the plugin art" / Reduced effects / Windows "Show animations" off.

Art directions (one per effect): the tracker's plugin-art-0.1.24.md, section "Effects (round 170k)".
Code: `src/ui/PluginArtEffects.inc` (Theme records), ids in `src/ui/PluginArtEffectIds.inc` (130+), the Pro editors
(`ProEditorBase.h`) now draw under their art like the effect panel. Shots: `LYNX.exe --artset fx1 --artshot <dir>
before|after` and `--artset fx1 --artgui <dir> before|after|still`.

## Batches

| batch | effects | state |
|---|---|---|
| 1 | Ridge Strip, Crest Compressor, Apex Limiter, Prism Multiband, Sentry Gate, Glow Leveler, Hush De-esser | built, shot |
| 2 | Epoch Verb, Atrium, Foil Plate, Halo, Horizon, Fruity Reeverb 2, LuxeVerb, Fruity Reeverb | built, shot |
| 3 | Echoform, Spiral Echo, Orbit, Lattice Mod, Gemini Doubler, Fruity Delay 2 / 3 / Bank | built, shot |
| 4 | Ember Saturator, Aether, Blood Overdrive, Fast Dist, WaveShaper, Soft Clipper, Hardcore, Distructor | built, shot |
| 5 | Fruity Chorus, Hyper Chorus, Vintage Chorus, Flanger, Flangus, Phaser, Vintage Phaser, Frequency Shifter | building |
| 6 | Parametric EQ 2, Fruity Limiter / Compressor / Multiband, Maximus, Transient Processor, Stereo Shaper / Enhancer, Spreader | building |
| 7 | Gross Beat, Vocodex, Vocoder, Pitcher, LYNX Tune, Voice Shift, Quell, Pitch Shifter, Love Philter, Effector, Squeeze, Lo-fi | drafted |
| 8 | LYNX basics + FL's small tools (lighter themes from shared motifs) | drafted |

Design notes: the first shots showed the art hidden under opaque panels, so the illustration themes use translucent
section glass (alpha 0x9c / 0xb8, like the pilot's Nebula); the console / rack / pedal themes stay solid. The
display-first Pro editors (Crest, Apex, Prism, Sentry, Hush, Ember, Aether) are nearly all display, so their display
backdrop shows the theme's illustration dimmed (42 % black) under the readouts.

## Where I stopped

- 2026-11-04: worktree ~/projetos/lynx-wt-art170k on top of feature/art170j's split (be39f467, not yet landed).
  Batches 1-6 committed; Windows build of 1-6 running; batches 7-8 drafted in the scratchpad (batch7.inc, batch8.inc). Next: shots (artshot fx1 before/after,
  artgui), review page section, build-local + tests, merge origin/main once art170j lands, train.sh art170k.
- Commands: `OUT=build-win ~/.claude/jobs/f8789b49/tmp/coord/winbuild.sh scripts/build-windows-zig.sh`;
  `build-win/LYNX.exe --artset fx1 --artshot 'E:\lynx-work\art170k\shots' after` (fresh LYNX_PROFILE_DIR + LYNX_TEST_MODE=1).
