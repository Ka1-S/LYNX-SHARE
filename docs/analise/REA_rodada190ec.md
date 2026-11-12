# Round 190ec: Ableton Live's Drum Rack (Clip-launch layout option and .als interop)

FL workflow stays the default: the Drum Rack is one more instrument in the plugin picker (beside Instrument Rack), and
the .als import uses it for Live's Drum Racks. Clean-room: facts below come from Live's own set XML (format only), the
member names in Live's binary strings and the public Live manual; no Live sample, preset or artwork is in the repo.

## 1. Evidence

- [DATA] Drum Racks of the shipped sets (`/mnt/e/lynx-work/ableton/xml`: Quick Start Beat / Song, Drums Lesson, APC demos,
  the four Live 11 template sets, Chuck Sutton demo; 12 Drum Racks, 196 pads, 27 return chains):
  - `DrumBranch/BranchInfo {ReceivingNote = 128 − key, 0 = all notes; SendingNote (60 in every kit but Drums Lesson's
    tuned pads 53 / 54 / 56 / 64); ChokeGroup 0 / 1 / 2 seen (Live: None, 1…16)}`;
  - sends `MixerDevice/SendInfos/AudioBranchSendInfo {Send (linear gain, floor 0.0003162277571), EnabledByUser, Index}`;
  - a chain's `RoutingHelper/Routable/Target` `AudioOut/Track.<id>/DeviceIn.<device id>.R<k>,ChainIn` (into return k),
    `AudioOut/None` (the rack's output); Drums Lesson routes all 16 pads into "a Mix Bus" and switches their send to it off;
  - views `ArePadsVisible`, `IsMidiSectionVisible`, `AreSendsVisible`, `IsReturnBranchesListVisible`, `PadScrollPosition`
    (the top visible row from the top of 32 rows of 4; 19 = C1 at the bottom left).
- [DATA] Core Library `Defaults/Slicing/*.adg` (Live's Slice to Drum Rack / Slice to New MIDI Track templates): one
  `DrumBranchPreset` each. "Default": Classic Simpler, 50 ms release, one voice, ChokeGroup 1; "Drums Gritty": One-Shot,
  5 voices, no choke. `Defaults/Dropping Samples/On Drum Rack`: a One-Shot Simpler at −12 dB, 2 voices.
- [BIN] `live-exe-strings.txt`: `ChokeGroup`, `ChokeGroupChooser`, `sInfoTextHeaderSliceToDrumRack`,
  `sMenuSliceToNewMidiTrack`, `SlicingDialog`, `sInfoTextDrumBranchTriggerButton`, `sInfoTextSavePadDefault`.
- [DOC] Live manual ch. 24 (Instrument, Drum and Effect Racks): Receive / Play (default C3 plays a sample at its own
  pitch), choke groups silence each other, up to six return chains with a send per chain, the I-O / S / R buttons at the
  lower left of the chain list. Forum (Ableton product team): return chains may route to the Set's return tracks
  (not saved with presets) — LYNX keeps returns inside the rack.

## 2. What LYNX does (core/Rack.h `drum`, patcher::Graph)

| Live | LYNX | Exact? |
|---|---|---|
| Pad = chain receiving one key (or all keys), playing its Play note into its devices | zone key = Receive, `PatchZone::playKey` turns note-on and note-off into Play | exact (data) |
| Choke groups 1…16 | a note into a zone of group g cuts the instrument of every other zone of g (`Instrument::cut`, the declick of an FL cut group) | behaviour exact; the fade length is LYNX's (live check) |
| Device chain per pad (MIDI fx → instrument → audio fx) | the Instrument Rack's chain rules | exact |
| Return chains, sends, Audio To | return chains of audio effects + mixer into the rack's output; one cable per return after the chain's mixer (post volume/pan); a chain's output into a return | sends post-fader: live check |
| Pad view 4 × 4, overview of 128, M / ▶ / S, drag onto another = swap | RackView drum mode (see HANDOFF) | layout from the manual and the view flags; no Live shot of a Drum Rack yet |
| Pad names on the note editor's key column | Piano roll key labels from `rack::padNames` (a user key label wins) | |
| Slice to Drum Rack / Slice to New MIDI Track | one pad per slice from C1 (max 92), LYNX templates "Default (choke, gated)" / "One-shot" after the Slicing defaults' facts | Live's own presets not loaded (Live content) |
| .als | import as a Drum Rack (old split import kept as `drumPadsAsChannels`), export as `DrumGroupDevice` with Simplers for the pads' Samplers | live check: Live opens the export |

Saved projects: the rack is written as an Instrument Rack plus `drum`, `pad`, `sends`, `returns`, `drumView`; each pad
chain's key zone is its Receive note, so a LYNX without Drum Racks opens it as a key-split Instrument Rack. Older
projects have no such keys and load unchanged.

## 3. Best way?

- **FL:** FPC (pads of layered samples, per-pad mixer output offset, cut groups via the channel's cut / cut-by), and
  for effects per pad: one channel per sound routed to its own mixer insert. No device chain inside a pad.
- **Ableton:** the Drum Rack: a full device chain per pad, choke groups, internal returns, Slice to Drum Rack.
- **Users:** producers cite per-pad processing in one device, choke groups for hats and "Slice to New MIDI Track" for
  chopping breaks as Drum Rack strengths (edmprod.com/ableton-drum-rack-tips, soundonsound.com "Drum Racks in Ableton
  Live"); the common complaint is that chains can't route to the set's returns (forum.ableton.com p=1499200).
- **Verdict:** FL-exact default unchanged (FPC and the Channel Rack); the Drum Rack is an added instrument, Live-exact
  where the data shows it. LYNX extra kept small: none beyond Live's model.

## 4. Open (needs a live check)

See `/mnt/e/lynx-work/drumrack190ec/HANDOFF.md` §Live checks.
