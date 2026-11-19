# REA round 190mx: knobs turned in a plugin's own window while recording (GitHub #198 follow-up)

The question came from the reporter of #198: "even continuous automation such as tweaking knobs ... inside a plugin
should be read as is for the current session". These are FL Studio's and Live's answers, and what LYNX does now.

## FL Studio 2026.1.7

### Static [STRINGS / DFM, Fruity Wrapper_x64.dll]

The wrapper's Settings page has an **Automation** panel, `AutomationPanel`, with one check box:

- the box is `RecordParamChangesBox`, a TVectorCheckBox;
- its caption is "Notify about parameter changes" and its hint "Record parameter changes as automation";
- its click handler is `RecordParamChangesBoxClick`.

Live capture `raw/f04.png` (round folder `/mnt/e/lynx-work/issues191_190mx`) shows the box checked by default for a new
VST3 instance (Surge XT). Round 190ks had already listed the hint (REA_rodada190ks-plugin-midiout.md Q3).

### Live check [EXEC]

Under the GUI lock, 14:43–14:49 on 2026-11-19. Wall log 322 → 322, no Alt+Z, no licence popup.

- **Setup.** Surge XT on a channel. The record dialog's choice was "Notes and automation". The record filter had
  Automation and Notes on (`raw/f12.png`). Surge's "A Filter 1 Cutoff" slider was dragged in its own window while
  recording. The hint bar showed "A Filter 1 Cutoff: 226.63 Hz" (`raw/f06rec-top.png`).
- **Song mode.** Two takes, one from bar 1 (inside the "Phrase" clip) and one from bar 5 (no clip there):
  - **no automation clip** was made and no clip was placed at bar 5 (`raw/f08.png`, `raw/f11.png`);
  - the "Phrase" pattern clip shows event data (`raw/f08-clip.png`, `raw/f15-clip.png`).
- **The saves.** Dumped with `s198/flpauto.cpp`, which links LYNX's own .flp parser:
  - `flp/flsongrec.flp`, the Song-mode takes: 3 pattern events (0xDF records) in pattern 1, id **0x813f** (channel 0,
    plugin parameter 0x13f). There is also one 0xD8 init-control record.
  - `flp/flpatrec.flp`, a Pattern-mode take: 7 events of the same id, ticks 6…294 (FL's 96-PPQ save), spaced like the
    drag's steps.
  - So FL writes one event per change, at the time of the change, with no fixed sampling.
- **Conclusion.** With Recording › Automation on, a generator plugin's own knob is recorded into the current pattern as
  event data in Song mode as well as in Pattern mode.
- **Not checked.**
  - An effect plugin's knob. The REA 143 model, automation clips in Song mode, is kept for it.
  - Where exactly a Song-mode take outside every clip of the pattern lands in the pattern. The saves fit LYNX's
    `patternPositionForTick`, the song position folded into the pattern loop, but 3 events are too few to prove it.

## Ableton Live (manual)

- **Arrangement Record** writes parameter changes into the track's Arrangement automation.
- **Session Record**, recording a slot or overdubbing a playing clip, writes them into that clip's envelopes, which play
  whenever the clip plays.
- **Not checked live this round.** Whether Live also records automation into playing clips on unarmed tracks.

## LYNX before this round

- **Plugin moves were already recorded.** `PluginWindows::onGesture` passes a plugin's begin and end gestures, and
  round 162d's single-change filter for plugins without gestures, to `AudioEngine::noteTweaked`. Every touched target
  was then sampled 30 times a second.
- **Song mode** made automation clips for every target, a plugin's own parameters included. This differs from FL.
- **Pattern mode** wrote the pattern's events (FL-exact).
- **Clip-launch layout.** It behaved like the Pattern layout. Session Record wrote no automation at all, and launched
  clips did not play their pattern's events.
- **Thinning.** Only repeated values were dropped, so a continuous move kept about 30 points a second.

## LYNX now (round 190mx)

- **Routing** is decided by `recordedAutomationDestination` (core/Recorder.h).
  - In Pattern mode, every layout writes the current pattern's events.
  - In Song mode, the Pattern layout puts a generator's own parameter (`recordsAsPatternEvents`: channelParam, not a
    controller's CC, pressure or pitch wheel) into the current pattern. Other targets still make clips. The Simple layout
    is unchanged (clips).
- **Clip-launch, Arrangement Record.** `commitLanePoints` writes the parameter's automation lane on the track that
  `autolanes::trackFor` names, in song time. A new lane has no flat line of its own, so it holds the first value before
  the move and the last value after it.
- **Clip-launch, Session Record.**
  - `AudioEngine::routeSessionAutomation` finds the recording MIDI take, or the overdubbing armed track, whose chain
    holds the target (`detailchain::holds`). Audio takes are skipped: LYNX's audio clip envelopes have no device
    parameters.
  - `commitSessionAutomation` writes the moves into the clip's pattern events once the clip exists, at its own position.
    For a new take that is the clock minus the take's start. For an overdub it is the offset plus the distance from the
    anchor, folded into the loop.
  - The renderer plays a launched pattern clip's events at that same position (RackRenderer::updateAutomation).
- **Thinning.** `thinRecordedCurve` runs Ramer-Douglas-Peucker along the value axis, within 1/512 of the range, on every
  batch written to a clip or a lane. Pattern events are steps, so they keep the 30 Hz sampling as FL keeps one event per
  change, minus repeats.
- **Tests.** `tests/Issue198_190mxTests.inc`.
