# Round 190jb: exports with the live plugin instances, and the rare mid-song export_parity miss

2026-11-16. Queue item 20 of 0.1.30. Owner decision (from 190hn's proposal): exports render with the plugin instances
that play, as FL does. Evidence markers: **[DECOMP]** Ghidra pseudocode of `FLEngine_x64.dll` (the round-178a86 copy of
the Ghidra project, `~/ghproj/r178a86b`, our helper scripts `StrRefs.java` / `DecAt.java`; output in
`/mnt/e/lynx-work/export190jb/rea/`), **[DOC]** Image-Line's FL Studio manual, **[CODE]** LYNX's source, **[TEST]** a
LYNX test, **[model]** our reading. Clean-room: only behaviour is described; LYNX's code is its own.

## 1. What FL does when it renders

| Question | FL | Evidence |
|---|---|---|
| Which plugin instances render | The live ones. The render start (`FUN_00d18130`, the routine round 139 found at `0xd1a568`) works on FL's global channel list (`PTR_DAT_015e2218`) and its global mixer-track array (`PTR_DAT_015e06a0 + track × 0x1478`). For "Disable maximum polyphony" it saves each live channel's Max polyphony (+0x1ec → +0x368) and changes it on that channel object. `FUN_0124d340` walks each track's 10 effect slots (+0x134c) and each channel's plugin, and calls one interface method (+0x88) on each. FL has no second engine and no copies of the plugins. | [DECOMP] |
| Playback during the render | The render start takes the audio critical section (`PTR_DAT_015e3d40`) and, inside it (`FUN_011d4800`), sets the song position and range of the one transport to the render's range. Playback cannot go on: the engine that played is the one that renders. The manual calls it a "non-real time process called 'rendering'". "Render minimized" renders "in background mode". | [DECOMP], [DOC] |
| Sample rate | The output (Mixer) sample rate, set in the Audio settings. The render window has no rate of its own: the encoders are opened with the mixer's rate (`*PTR_DAT_015e1f40`, for the OGG, FLAC, M4A and MP3 writers). | [DECOMP], [DOC] "The output (Mixer) sample rate is set in the Audio Settings window." |
| Plugins told they render | The wrapper's "Notify about rendering mode" (flag 0x4, on by default; REA_rodada134 / 149). The manual: "VST plugins may also render in HQ mode if the Wrapper setting Notify about rendering mode is selected". In LYNX this is `setNonRealtime` plus a new `prepareToPlay`, so a VST3 plugin gets `kOffline` in `setupProcessing` (round 169c). | [DOC], earlier [DECOMP] |
| PDC | "Trim PDC silence" (on by default, WAV only): the delay compensation is "removed from the start of the rendered .wav file". | [DOC]; render window field `TrimPDCDelayBox` [DATA] |
| Tail | Cut / Leave (default) / Wrap remainder: "Extends the render length to capture decaying sounds", "Wraps any decaying sound at the end of the song back onto the beginning". | [DOC] (LYNX has these since round 84) |
| Split mixer tracks | One pass: each mixer track gets its own writer (the loop over `PTR_DAT_015e06a0` tracks in `FUN_00d18130`). | [DECOMP] (REA_rodada84 / 190gj) |

Manual page: image-line.com, FL Studio online manual, "SAVE/EXPORT FILE FORMATS" (`fformats_save_export.htm`).

Not confirmed: what `+0x88` does to each plugin (the argument is the object's byte at +0xa4; it may be the
render-mode notification or a reset), and whether FL puts each plugin's state back after a render. Our reading is that it
does not: nothing in the render start saves plugin state, only the channels' polyphony and a few globals.

## 2. LYNX now

- **The hold** (`engine/Exporter.h` `LiveExportHold`, `AudioEngine::holdForExport`). The Export window opens a hold
  before the export's job is made. This applies to the mix, its stems and its marker sections, and to the Mixer's
  "Render armed tracks".
  - Opening the hold stops playback, as FL does.
  - The device callback then outputs silence without touching the rack. A flag is set while the callback is inside its
    block (both sides sequentially consistent), so `holdForExport` knows when the callback is out.
  - The render-ahead thread is halted.
  - The export engine's rack build then takes the live engine's running `PluginInstrument` / `PluginEffect` and every
    Patcher that hosts plugins, by instance tag (`RackState::Factories::liveInstrument` / `liveEffect`).
  - LYNX's own units are made new. They render the same either way ([TEST] below).
- **Offline mode, block size, rate.** The render then runs as before:
  - `setNonRealtime` and a new `prepareToPlay` for every plugin with "Notify about rendering mode" (VST3 `kOffline`);
  - "Reset plugin when FL Studio resets" applies at the start;
  - blocks of 512, inside the 4096 the plugins were prepared for.
  - Rate: an export at another rate than the device's prepares the lent plugin at that rate. This is a LYNX extra: FL
    has no export rate. A plugin that refuses the rate is not lent and gets a new instance.
  - A lent Patcher must already run at the export's rate.
- **Warm-up.** When every hosted plugin of the render was lent, the warm-up only lets the start's values settle. There
  is no 3 s boot time and no 1 s wall-time minimum, because the plugins have been running. FL has no warm-up.
- **Giving the plugins back.** When the hold and the last job that used it are gone (on the message thread), each lent
  plugin goes back to how it was:
  - its rate;
  - real-time mode;
  - its state from before the export, set again only when it changed (the export's automation and start values move
    parameters);
  - reset as FL resets a plugin ("Reset plugin when FL Studio resets").
  - The live engine is then released and publishes its song, so the units take its routes and side inputs back.
  - Owner decision for the restore: see the report. FL itself does not restore states (above, not confirmed).
- **The old way stays as the fallback.** The export makes its own instances in these cases:
  - without a hold: the command line `--export`, the remote / headless renderers, the self-tests' own engines,
    "Render to audio", Freeze, the auto-sampler;
  - when the live engine can't be held: recording, a real-time export, a count-in;
  - when another job is using the hold;
  - for a plugin that can't be lent: crashed, not running, a rate it refuses.

## 3. The rare mid-song export_parity miss

- **Seen:** 1 in about 100 `--exportparitytest` runs under load (190hn open item 1, 190ip's Linux self-test): lag 0,
  "first apart at 3.56 s". The same run in blocks of 512 matched.
- **Cause and proof:** see the round's HANDOFF (`/mnt/e/lynx-work/export190jb/HANDOFF.md`) and the self-test's new
  variants "render-ahead engaging / released mid-song" in `--exportparitytest`.
