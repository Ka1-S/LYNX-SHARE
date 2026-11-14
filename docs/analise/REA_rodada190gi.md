# Round 190gi: clip envelopes on audio clips (B3) and warp from the Playlist (B6)

Slices B3 and B6 of `/mnt/e/lynx-work/pain190gc/REPORT.md` §4 (complaints #21 and #7). B3 was approved by the owner
(#24 YES, roadmap "CE": "its own slice; it must cost nothing on clips that have no envelope; for .flp export, pick
whichever loses nothing", REA_rodada177d.md). No new REA session: the FL and Live facts below come from earlier rounds'
static evidence and Live's own class schema and shipped sets.

## 1. What FL and Live do

| Fact | Evidence | LYNX |
|---|---|---|
| FL 2026 has no breakpoint envelope drawn on an audio clip. Its per-clip way is the audio clip menu's Automation › Automate › Panning / Volume: an automation clip of the clip channel's Levels adjustment pan offset / volume multiplier (channel parameters 0x10 / 0x11), on the clip's track, start and length; the Volume clip's range top is unity | REA_rodada190dz §1–2 ([FORM] EEAUMenu, [H 0x1244900]); live check 190ew PL-1 / PL-2 shots | already exact (round 190dx `clipops::automateAudioClip`); the envelope's .flp export is written this way |
| FL 2026 hides the audio clip menu's "Edit envelope..." (the channel's envelope editor) | REA_rodada190dz §1 ([H 0x1237290] EEAUMenuPopup) | not shown |
| FL's Levels adjustment gain: f(x) = (11^x − 1) / 10, x = multiplier / 12800 (1 at unity) | REA_rodada190dz §2 ([H 0xfe9d20]) | the envelope's volume law below unity (`clipenv::gainOf` = `levelAdjustGain`) |
| Live's audio clips carry clip envelopes in clip time: `AudioClip/Envelopes/Envelopes/ClipEnvelope{EnvelopeTarget/PointeeId, Automation/Events/FloatEvent{Time, Value}}`, the first event "before the song" (−63072000) | [SCHEMA] realive190fm/schema-12402.txt (AudioClip, ClipEnvelope, ListAutomation, FloatEvent); [DATA] the 12 Suite demo set | `.als` in and out |
| An audio clip's "Clip Gain" envelope targets its track's `MainSequencer/VolumeModulationTarget`; the Mixer's Track Panning modulation is `Mixer/Pan/ModulationTarget` | [SCHEMA] AudioSequencer members; [STR] `mpVolumeModulationTarget`, "Clip Gain" (live-exe-strings); [DATA] Chuck Sutton – Patience: the vocal clips' VolumeModulationTarget envelopes | the two lanes LYNX keeps |
| Values: −1 where the vocal clips are silent, 1 elsewhere (both targets) | [DATA] the demo set's events (−1 … 1 only) | 0…1 ↔ −1…1 linear; **the law between the ends needs a live check** |
| Live's Clip View of an audio clip: tabs Sample / Envelopes at the right of the sample editor's title bar; envelope choosers (device, control) | [SHOT] ableton175a/live/m3-live-clipview2.png; [MAN] Live manual, clip envelopes | Clip-launch layout: the tabs and a Device (Clip / Mixer) + Control (Gain / Track Panning) chooser pair |
| Live's envelope editing: a click on the line adds a breakpoint, a click on a breakpoint deletes it, drag moves it, Alt + drag bends a segment | [MAN] Live manual (editing automation and envelopes) | the Clip View's Envelopes tab; the Playlist uses FL's in-Playlist automation gestures instead |
| FL Studio Mobile: audio clip envelope points (EVN2 on audio lanes) | coverage row audiotracks-0007 (rea190bc) | not imported yet (follow-up) |

## 2. Best way?

- **FL:** no envelope on the clip; Automate › Volume / Panning makes a separate automation clip (FL users ask for clip
  gain envelopes: IL p=2071198, p=2063890).
- **Ableton:** clip envelopes drawn in the clip (Clip Gain, Transposition, Sample Offset, mixer and device parameters),
  linked to the clip's loop or unlinked.
- **Users:** "No clip volume envelopes" is complaint #21 of 190gc (FL forums; Live has them).
- **Verdict:** LYNX-own envelope on the clip, in both layouts, as approved. FL's Automate items stay exact and the .flp
  export writes FL's own form (nothing lost). Live-exact for the .als mapping of Clip Gain / Track Panning.

## 3. What LYNX does (round 190gi)

- Model: `ClipEnvelope` (Song.h) — a volume and a pan lane of `AutomationPoint`s (FL's curve modes and tension per
  segment) in clip time (content ticks, `Clip::offset`'s units), the lane shown; `Song::clipEnvelopes`, indexed by
  `Clip::envelope` (in Clip's padding: Clip stays 144 bytes). Copy on write (`clipenv::own`); a split or copy shares it;
  trims keep every point on its audio; FL's Shift+drag stretch and Live's ÷2 / ×2 scale it.
- Sound: the Scheduler hands a clip note its envelope only when it is active; the Sampler voice reads gain and pan every
  16 samples and ramps between (`clipEnvelopeSample`); the renderer gives instruments the table only while the song has
  one. Clips without an envelope render bit-identical (test).
- .lynx: song-level `clipEnvelopes` (used entries only) and clip field 25; written only when a clip uses one.
- .flp: `clipenv::bakeToAutomation` (FL's Automate form, unity after the clip), noted in the export report.
- .als: Clip Gain / Track Panning clip envelopes both ways; other clip envelopes (devices, track volume modulation,
  transposition, sample offset) are listed as left out; curves are exported as straight segments (8 per curved one).
- FL layout (Playlist): audio clip menu › LYNX extras › Clip envelope › Show volume / Show pan / Hide / Reset volume /
  Reset pan; the lane shown is drawn on the clip and edited with FL's in-Playlist automation gestures ([RMB] adds a point,
  drag a point, hollow handles bend, the point menu with Delete / curve modes / Straight segment); a hidden active volume
  lane shows as a faint line.
- Clip-launch layout (Clip View): Sample | Envelopes tabs; the Envelopes tab with Device Clip / Mixer and Control Gain /
  Track Panning, Live's click gestures.

## 4. B6: warp from the Playlist (LYNX-own; FL warps in Newtime only)

- Clip menu › LYNX extras: Auto-warp and Quantize audio (the audio editor's commands on that clip; a clip whose channel
  other clips play gets its own channel first: `warpedit::channelForClipEdit`).
- Warp flags: dragged on the flag strip as before (round 175a); a double-click on the flag strip of a warped clip adds a
  marker; Alt + drag near a marker's line anywhere on the clip moves it. FL's plain clip drag and double-click (channel
  settings) are unchanged elsewhere; Alt + double-click stays the clip's Properties (round 153), so the slice's
  "double-click adds" lives on the flag strip.

## 5. Needs a live check

- Live's gain law between −1 and 1 of the Clip Gain modulation (and the Track Panning one).
- Live's Envelopes tab layout (chooser placement, the waveform under the envelope): no Live shot of that tab yet.
