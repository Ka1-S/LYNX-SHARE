# Roadmap

Ordered to get something usable as early as possible without ever breaking the audio-engine rule
(no allocation, locks or I/O on the audio thread).

## Phase 1: a playable base ✅
- [x] Transport, audio engine, JUCE graph
- [x] Song model (channels, patterns, notes, 960 PPQ)
- [x] Sample-accurate scheduler
- [x] Built-in synthesized instruments
- [x] Channel Rack with step sequencer
- [x] `.flp` reader + channel/pattern/note import
- [x] Windows build without Visual Studio (Zig) and core tests

## Phase 2: real composing ✅
- [x] Sampler: `.wav`/`.flac`/`.ogg`/`.mp3`/`.aiff` per channel, including FL's Ogg-in-WAV files
- [x] Imported `.flp` projects play with the real samples from the FL installation
- [x] Piano Roll (create, move, resize, delete, snap, zoom, preview, ghost notes)
- [x] Save/open in LYNX's own format (`.lynx`, versioned JSON)
- [x] Per-channel volume, pan and mute; PC keyboard as a musical keyboard
- [x] Render to WAV (`LYNX.exe --render`)
- [x] Undo/redo (Ctrl+Z / Ctrl+Y), one step per mouse gesture
- [x] Velocity lane, multi-selection, copy/paste/duplicate, quantize, snap
- [x] Channel solo, rename/colour/clone patterns (colours imported from `.flp`)
- [x] MIDI input and note recording (MIDI and PC keyboard)
- [x] Export from the UI: WAV 16/24/32f, FLAC, OGG, with tail and progress (Ctrl+R)

## Phase 3: arrangement ✅
- [x] Playlist: pattern clips in time, tracks, PAT/SONG mode, seek from the ruler
- [x] Import the `.flp` playlist (pattern clips with start/end slices)
- [x] Audio clips (waveform, drag a file in, trimming, resume mid-clip after a seek)
- [x] Import audio clips from `.flp` (offsets in beats or ms, detected per clip)
- [x] Disk streaming for long audio (memory-mapped cache + prefetch)
- [x] Time-stretch/resample of audio clips (pitch-preserving WSOLA or resampling)
- [x] Loop region, multi-selection, copy/paste/duplicate clips, "make unique"
- [x] Multiple arrangements (selector in the Playlist; imported from `.flp`)
- [x] Automation clips with a point/curve editor; volume/pan, effects and plugin parameters
- [x] Import automation from `.flp` (points, curves, targets)

## Phase 4: mixer and plugins ✅
- [x] Mixer: Master + up to 127 inserts, FL fader curve (0.8 = 0 dB), pan, mute, solo, stereo meters, click-free ramps
- [x] Channel → insert routing; imported from `.flp`
- [x] Sends between inserts with levels, "route to this track only", solo along the signal path
- [x] 10 effect slots per insert (and on the Master) with bypass, replace, move, remove
- [x] Built-in effects (EQ, parametric EQ, compressor, limiter, delay, reverb, chorus, filter, distortion, ...)
- [x] VST3 hosting: instruments and effects, plugin window, state saved in the project
- [x] Plugin scan with progress window, list saved in %APPDATA%\LYNX, "dead man's pedal"
- [x] Scanning in separate processes, with a blacklist
- [x] CLAP (own format on top of JUCE: parameters, state, latency, editor)
- [x] Per-edge latency compensation (PDC), like FL; export drops the initial latency
- [x] Multithreaded processing: channels in parallel and the mixer in layers (lock-free)
- [x] Optional separate-process plugins (sandbox, in Options), ilbridge-style
- [x] Sidechain inputs (compressor), configurable per slot

## Phase 5: scale
- [x] Render an insert to audio (freeze)
- [x] Render/export to WAV/FLAC/OGG (no MP3: JUCE has no MP3 encoder)
- [x] WASAPI exclusive and "Low Latency" (from JUCE; listed in Audio settings)
- [x] ASIO via the Steinberg ASIO SDK's GPLv3 option (2025), compatible with LYNX's AGPL-3.0
- [x] GPU UI: JUCE 8 draws with Direct2D on Windows
- [x] Idle channels skipped by the renderer
- [x] FL workflow P2 (`docs/WORKFLOW_FL.md`): step graph lanes, mixer gestures, slot drag, Browser search, quick-add
  palette, undo history, recording options, note tools, scale snap, zoom presets, ripple editing

## Phase 6: FL fidelity (with REA)
- [x] Volume/fader curves, swing, envelopes, time-stretch, cut groups (round 3)
- [x] 3x Osc → built-in synth (round 4); channel filter, envelopes, LFOs, AM, porta (round 6); exact engine with the HQ oscillators and the channel processor (round 47)
- [x] FL native effects with their saved settings (rounds 7–8, 14–15, 18–20, 22)
- [x] Third-party VST3/CLAP plugins in the Fruity Wrapper, with state (rounds 5, 22)
- [x] VST3 parameter map (record 4: slot k → ParamID) for automation of wrapped plugins (checked with Surge XT via `--wrappertest`; no demo uses it)
- [x] Playlist markers and time-signature changes; metronome following them
- [x] FPC, DirectWave, FL Keys, Fruity Slicer/Slicex as samplers (rounds 8–9)
- [x] Harmless, MiniSynth, DX10, Fruit Kick (rounds 10, 21, 24)
- [x] Sytrus and Harmor with exact timbres on the harmonics oscillator (rounds 11, 18, 19, 23, 24)
- [x] GMS, Ogun/Autogun, Sakura, DrumSynth Live (round 12), Kepler, Wave Traveller (round 13)
- [x] GMS and Morphine on their exact engines, effects included (round 39)
- [x] Gross Beat, Frequency Splitter, Effector (round 14); Effector rebuilt with all 12 algorithms (rounds 18–19)
- [x] Controller links: Peak Controller, Envelope/Keyboard Controller, X-Y/Control Surface
- [x] Issue #3 (round 133): MIDI CC / aftertouch / pitch bend automation of hosted plugins, Formula Controller (exact
      language), editable Envelope Controller, MIDI Out → MIDI input port, "Link to internal controller" on every knob
- [ ] Patcher with audio and MIDI inputs/outputs (user-editable)
- [x] Patcher graphs of any shape (round 20)
- [x] Love Philter: filters, Mod X/Y, pattern envelopes and LFOs, cutoff/resonance modulation in every unit (rounds 16–20); exact engine (round 66)
- [x] Pitcher (pitch correction) and Fruity Vocoder (rounds 15, 19, 20; Fruity Vocoder exact engine: round 70)
- [x] Sidechain links (round 22)
- [x] Vocodex as the vocoder with two side inputs (round 20); exact engine (round 35), SG stage and internal Sytrus carrier (round 40)
- [x] Maximus and Soundgoodizer on FL's exact Maximus engine (rounds 36, 40)
- [x] Gross Beat on FL's exact engine: time-travel buffer and volume slots, sync, hold, smoothing (round 53)
- [x] Ogun's FFT spectral engine: initial spectrum and per-partial decay (round 24) on an evolving spectrum
- [x] Ogun on its exact engine from the plugin state: inverse-FFT timbre, articulators, filter, EQ/chorus/delay/reverb,
  qualities 0–4 (round 37)
- [x] Autogun on Ogun's exact engine with its Magic Dust (Soundgoodizer) stage, and Transistor Bass on its exact engine
  with the sequencer and effects (round 57)
- [x] Harmor resonance: types, mask and offset (round 25) as an SVF peak
- [x] FLEX by preset name only (its packs are encrypted and never opened)

## Phase 7: Ableton Live interop (round 175a)
- [x] Import Live 11/12 sets (.als): tracks, clips, warp, mixer, devices (`docs/analise/ALS_FORMAT.md`)
- [x] Export Live 12 and Live 11 sets, with samples collected (N2)
- [x] Warp modes Beats, Tones, Texture and Complex as LYNX stretch methods (N4)
- [x] Beats transposition: each piece resampled offline by the clip's pitch, as Live does
- [ ] Complex Pro formants (Live keeps formants when transposing; LYNX's +5 st is 3 dB louder with a different tilt)
- [ ] Excess level at 60–95 Hz in the stretched renders
- [ ] Beats and Texture parameters (Transient Loop Mode/Envelope, Grain Size, Flux) beyond the defaults
- [x] Clips with several warp markers at different rates: kept, rendered segment by segment (round 175a, M1; editing them = M3)
- [ ] LYNX's built-in instruments and effects in exported sets (N3; listed in the export report for now)
- [x] Session view ↔ LYNX's clip grid in .als import and export: scenes, launch modes, quantisation, follow actions (round 181c)
- [x] Optional Clip-launch workflow layout style beside the default Pattern workflow (round 181a)
- [x] Live-style Clip View and Device View, racks and note chance (0.1.26)
- [x] Simple workflow layout style for beginners (0.1.26)
- [x] Racks (round 183c, `docs/analise/REA_rodada183c.md`): Audio Effect and Instrument Racks as a Patcher in rack mode,
      with chains, key / velocity / chain-select zones (Live's measured fade laws), chain volume / pan / solo, 16 macros
      with Map mode, variations, Randomize; LYNX's optional variation morph and launch glide; .als import / export
- [x] Live's own devices onto LYNX's in .als import, with automation and macros (0.1.27, three lots)
- [x] Drum Rack, Session mixer, scenes with tempo, Draw Mode, MIDI and Key Map Modes, Groove Pool (0.1.27)
- [ ] FM Operator, Hybrid Reverb, Grain Delay, Spectral devices, Amp, Roar, Cabinet (dropped for now)
- [ ] Standalone MIDI Effect Rack playing its child channels (decision R1: the engine routing is still open)
- [ ] VST3 parameter mappings in .als racks (Live stores plain units and parameter ids)

## Phase 8: FL Studio Mobile import (round 182c)
- [x] `.flm` reader: tempo, time signature, arrangement (note, drum and audio lanes), samples, drum kits (`docs/analise/FLM_FORMAT.md`)
- [x] DirectWave single-sample programs, rack level / pan / routes on the inserts
- [x] Mute / solo, automation lanes and the effects as Pocket effects (0.1.26)
- [x] MiniSynth, Transistor Bass, 3x Osc and GMSynth instruments (0.1.26)
- [x] DW Sampler and Slicer on FLM's own engines, SuperSaw, more effects on FLM's engines, chord sets and key locker (0.1.27)
- [ ] FL Studio Mobile's remaining instruments (they play LYNX's built-in synth for now)
