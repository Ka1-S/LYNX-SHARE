# Manual test script

What the automated tests don't cover: hearing the sound on a real audio device and using mouse/keyboard.

## 1. Import FL projects (5 min)
1. *File → Open* and pick a demo in
   `C:\Program Files\Image-Line\FL Studio 2026\Data\Demo projects\Demo songs` (e.g. *LeriQ - SHINE*,
   *Bombs Away - I'm Awake*, *Sid De La Cruz - Secret Agent Rendezvous*).
2. Play in SONG mode and check:
   - 3x Osc channels (built-in synth) sound close to FL (tuning, filter, plucks, kicks made in 3x Osc);
   - parametric EQs, reverbs, delays and limiters in the mixer are active (opening a parametric EQ slot shows
     the imported curve);
   - the Playlist ruler shows the markers and time-signature changes (*Secret Agent* has 7/4 and 6/4);
   - the metronome accents the first beat of every bar, including the 7/4 sections;
   - sidechained tracks duck with the kick.
3. Export (`Ctrl+R`) and compare with FL's render of the same project.

## 2. Synth and sampler (5 min)
- In a synth channel: *FL presets…* lists the 3x Osc presets and, per generator, those of Sytrus, Harmor,
  GMS, Harmless, MiniSynth, DX10, Fruit Kick, Ogun and Sakura (~2800, on the built-in synth); try a few and
  play with the PC keyboard (`Z`..`M`, `Q`..`P`).
- *Modulation (envelopes and LFOs)…*: turn on a pitch LFO (vibrato) and a volume LFO (tremolo).
- Mono / Porta / AM / Ring buttons, the porta time knob, keytrack, sweep and the 3 tone bands.
- In the Browser (F8), double-click an FL preset (.fst or .gmsynth): it creates a new channel.
- In a sampler: filter (type, cutoff, resonance), Porta + Cut itself (808 glide).
- Piano Roll: `Ctrl+P` marks notes as porta.

## 3. Mixer (5 min)
- Track menu (right-click the name) → *Track EQ…*: the 9 knobs of the built-in EQ.
- Add a parametric EQ: drag the handles, mouse wheel = Q, right-click = type and slope.
- Slot menu → *FL presets* (EQ, Limiter, Reeverb 2, Delays, Compressor, Stereo Shaper).
- Volume gate: draw the 32 steps with the mouse; right-click the bars for patterns; check the gate follows the bar.
- Sidechain: right-click a compressor slot → *Sidechain from* → pick the kick track.

## 4. Third-party plugins
- *Options → Scan for plugins*, then open an `.flp` that uses VST3/CLAP through the Fruity Wrapper: the plugin
  should load with the same preset as in FL. Missing plugins are listed in a notice.
- *Options → Plugin processes*: pick "Each plugin in its own process", reopen the project and play. Each plugin becomes a
  child `LYNX.exe` in Task Manager; the plugin window opens separately. Compare CPU/clicks with the option off.
  If a plugin hangs, it goes silent with a notice and LYNX keeps playing. "One process per plugin vendor" / "All plugins
  in one process" (round 190jg): fewer child processes; a crash mutes every plugin of that process (the notice names
  them) and Reload brings them all back. Measure with `LYNX --sandboxbench <plugin> report.txt` and
  `LYNX --sandboxgrouptest <plugin> report.txt --crash "LYNX Test Crash.clap"`.

## 5. Audio
- *Audio…*: "Windows Audio", "Windows Audio (Exclusive Mode)" and "Low Latency Mode" are listed; check latency
  and clicks with a heavy project.

## 6. Automatic comparison with FL (optional, very helpful)
FL ships 82 short test projects, one per plugin, in
`C:\Program Files\Image-Line\FL Studio 2026\Data\System\Render tests`. Exporting each of them from FL
(24-bit WAV, 44.1 kHz, same name as the project) lets `tools/comparewav` compare them with LYNX's render
(level, spectrum, envelope).
