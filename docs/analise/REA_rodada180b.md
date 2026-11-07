# Round 180b: LYNX's own plugins as VST3 and CLAP for other hosts

> Clean-room. FL Studio and Ableton Live were only looked at as installed products. No code or data from either was
> read into LYNX. Every point is marked **[OBS]** (observed on this PC) or **[INF]** (inferred or general knowledge,
> not checked here).

## What FL Studio and Live do

- **[OBS]** On this PC, FL Studio 2026's install folder holds `Plugins\Fruity` and `Plugins\Start` only. Neither
  `C:\Program Files\Common Files\VST3` nor `...\CLAP` has an Image-Line plugin. As installed here, FL's own plugins
  run only inside FL.
- **[INF]** Image-Line has offered some of its instruments and effects as VST plugins for other hosts, through
  separate installers. This was not checked on this PC.
- **[INF]** Live's own devices run only inside Live. Live hosts VST2, VST3 and AU, but doesn't export its devices.
  This was not checked here, and per the user's decision Live wasn't started for this round.

## Best way?

- **FL:** its own plugins stay inside FL as installed, with any versions for other hosts as a separate product (INF).
- **Ableton:** its devices are Live-only (INF).
- **Users:** a common request for any DAW's stock plugins is "let me use them in my other DAW too", so a mix done in
  LYNX could be finished elsewhere. This is general knowledge, with no thread collected for this round.
- **Verdict:** offer it, but as an add-on that is off by default. It should share one runtime with the app, so there
  is no copied DSP and a plugin sounds exactly like the slot in LYNX. Its state should be a `.lynx` song, so a plugin
  state and a LYNX project can be exchanged. CLAP can carry every plugin in one 39 MB file. For VST3 the user picks an
  option (docs/PLUGIN_EXPORT.md, "Rollout").

## Evidence for the prototype (LYNX Facet EQ, LYNX FM10)

See docs/PLUGIN_EXPORT.md for the design, the sizes and the tests. Host results are in the round's report to the
coordinator, and the logs are under `E:\lynx-work\plugex180b`.
