# REA round 190dx: Automate / Preview on audio clips, new instrument and audio tracks, one-click audio recording, Disk recording latency switch

Static REA of FL Studio 2026 (FLEngine_x64.dll, form resources) and existing FL screenshots; no FL run. Full fact table
with addresses: `/mnt/e/lynx-work/flpl190dx/FL-FACTS.md`. Facts in our own words.

## What FL does and what LYNX now does

| FL item | FL (evidence) | LYNX |
|---|---|---|
| Audio clip menu › Preview (first item) | plays the clip through its channel as the Playlist plays it (EEAUPreviewMenuClick → FUN_01239e40 → FUN_00ffa0a0) | `PlaylistComponent::previewClipAudio`: the clip's own audio (renderClipAudio) at clip × channel volume through the preview output (insert effects not heard) |
| Audio clip menu › Automation › Automate › Panning / Volume (Volume = default) | one handler (0x1244900): an automation clip of channel parameter 0x10 / 0x11 (FL's Level adjustments pan / volume) of the clip's channel, flat at the current value from 0 to the clip's length, placed at the clip's own start, length and track; Volume's range top set to unity (6400 of 12800) with the line at max(2v, 1) | `clipops::automateAudioClip`: same placement and flat shape; target = the clip channel's Pan / Volume (LYNX has no Level adjustments) |
| Channel menu › Assign to new instrument track / Unassign from instrument track | LineAssignInstrTrackMenuClick (Tag 0 / 1, caption chosen in ChannelMenuPopup) → FUN_013036e0 | `trackmodes::assignToNewInstrumentTrack` (first free track), `unassignFromInstrumentTracks` |
| Track mode › Instrument track › Use existing channel ›, More plugins..., generator list | EEPLTrackMorePlugMenuClick: plugin dialog; on an instrument track the generator is replaced, else a new channel | LYNX's plugin picker; `trackmodes::addInstrumentTrack`, `replaceGenerator` |
| Playlist "+" (AddInstrBtn): menu Audio track › Add audio track, Instrument track › More plugins..., generators; right click = Add audio track | FUN_00e711d0 / FUN_012e3640: first track without clips and unassigned, a mixer track nothing uses | `AddTrackButton` (21 × 18 at x 7 on the ruler row), `trackmodes::addAudioTrack` |
| Tools › One-click audio recording › I want to record audio...: Into Edison audio editor/recorder (default), Into playlist as an audio clip | FUN_0120d380 mode 3 / 2: Edison (AudioLoggerTrack) on the selected mixer track + Play; or Song mode, arm the selected mixer track (ask its input), Record + Play | `MainComponent::oneClickRecord`, `core/OneClickRec.h` |
| Mixer › Disk recording › Latency compensation (checked) | FUN_0126c040: take offset = switch ? latency : 0 | `RecordOptions::latencyCompensation`, `takes::compensation` |

Menu models for the tests: `core/FlTrackMenus.h`. Tests: `tests/FlPl190dxTests.inc`. Offscreen checks: `LYNX --flplshot <prefix>`.
