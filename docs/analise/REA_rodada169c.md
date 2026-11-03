# Round 169c — issue #32 follow-up (medievilmusic): 5 points on "current builds"

Issue #32 was reopened on 2026-11-03 16:43 with a 90 s video and the project `House V1.lynx` (copies on E:
`/mnt/e/lynx-work/issue32b/`). Worktree `lynx-wt-issue32c`, branch `feature/issue32c`. Windows checks in
`C:\Users\kauan\lynx-test\issue32c\` (s = LYNX main build shots, f = FL Studio 2026 shots, l = LYNX live run).

**Version used by the reporter:** the title bar shows no version; the comment (16:43 UTC) came 12 h after the 0.1.22
release (04:27 UTC) and 0.1.23 is not released, so the video is 0.1.22, which has none of round 168s's fixes (render
stops playback, bridged plugins told about offline mode, Playlist no longer a background window, wheel 2 %/notch).
Everything below was reproduced on current `origin/main` (633c04e4, the Windows build `land-rel0123fix\LYNX.exe`).

**Test plugin.** SynthMaster 2 and Vital are not installed here (no third-party VST3 instrument at all), so this round
adds `tools/testplugin/lynx_test_synth_vst3.cpp`: a minimal VST3 instrument built only on the VST3 SDK interfaces,
with no `IMidiMapping` (like SynthMaster 2), one sine voice per note on, and a log of every process call (play head
position, block size, play state, process mode, voices) and every note event (`LYNX_TEST_SYNTH_LOG`).
`tools/testplugin/synthlog.py` summarises a log. The reporter's project with SynthMaster 2 (channel 11) and Vital
(channel 18) replaced by it: `house-ts.lynx`.

| # | Reporter's words | FL evidence | LYNX before (main 633c04e4) | LYNX now |
|---|---|---|---|---|
| 1 | "When moving inside sequencer while the playback is on, some synths just double sound or go out of tempo" | FL releases the notes sounding at a set-position and chases the ones crossing the new position (REA_rodada167k §2, FUN_011e7a10 → FUN_011e7470) | [LOG] `live-main.log.*`: 4 ruler clicks while playing → the Vital stand-in got 3 note ons of its 16-bar note and **0 note offs** (3 voices stacked = "double sound"), the SynthMaster stand-in 8 doubled notes, and after Stop **3 voices still sounding**. Cause: a jump / stop sent only All Notes Off + All Sound Off (CC 123 / CC 120); JUCE's VST3 host passes a controller to a VST3 plugin only as the parameter its `IMidiMapping` maps it to and drops it otherwise (`juce_VST3Common.h` `sendMappedParameter` returns true for every controller once a mapping object exists), so a VST3 synth without that mapping never stopped its notes. And the play head: a generator's chunk is split at its notes, each piece one plugin block, and every block reported the chunk's start: `synth-main.log.3` 1503 blocks repeated the previous block's position (a synced sequencer / arpeggiator / LFO saw the song stand still, then jump: "out of tempo") | `PluginInstrument::allNotesOff` (jump, stop, panic) sends each held key its own note off first (`HostedKeys`, all 16 MIDI channels), then CC 123 / 120 for the formats that read them. The play head reports where each block starts (`HostTimeline::tickAt`, `ticksPerSample`, a chunk count; `HostBlockClock` + a thread-local block offset set around each plugin block). Tests `issue32c_hosted_keys_release_each_held_key_once`, `issue32c_a_jump_while_playing_sends_the_sounding_notes_their_note_offs`, `issue32c_each_block_of_a_generator_reports_where_it_starts` |
| 2 | "If i recall a window it still down and the f6 (pattern window) is always on top" | FL 2026: the Playlist is a window like the others; clicking it brings it over the Channel Rack, F6 brings the rack back (round 168s [SHOT] r168s f02–f04) | **Already fixed on main** (round 168s, not in 0.1.22: the Playlist was a "background" panel, so the rack always covered it). [SHOT] s02 click the Playlist title → Playlist in front; s03 F6 → rack in front; s04 F5 → Playlist; s05 F9 → Mixer; s07 click a Playlist track name while the rack covers it → Playlist in front | unchanged |
| 3 | "Render results still contains cuts in notes … the synthmaster2 sound is cutting after render (randomly cuts the notes)" | FL renders with playback stopped (REA_rodada155) and, with the wrapper's "Notify about rendering mode" (on by default), tells the plugin it renders offline | 0.1.22 rendered while the live engine kept playing (fixed by 168s). On main: [LOG] `synth-main.log.3/4` — a whole `--export` of the project: every note on has its note off and every note is exactly its length (7751–7752 samples at 44.1 kHz for the 0.375-beat notes; 756 of 756), so LYNX's note scheduling does not cut notes; **but the plugin ran in real-time mode through the whole export** (`rt` on all 90 179 playing blocks). `setPluginRenderMode` called `setNonRealtime` + `prepareToPlay`, and JUCE's VST3 `prepareToPlay` returns at once when the plugin is active at the same rate and block size, so `setupProcessing` never got `kOffline`. A synth in real-time mode during a faster-than-real-time render may drop or shorten voices it cannot compute in time (SynthMaster 2's multi-core voice rendering is the likely case) — the random cuts. Plus point 1's play-head repeats, which reach the render too | the plugin is released and prepared again (`releaseResources` + `prepareToPlay`) when the render mode changes, so VST3 `setupProcessing` gets `kOffline` (and `kRealtime` after). [LOG] `synth-fix.log.3/4` (branch build, same `--export`): `off` on all 90 179 playing blocks, 0 play-head repeats, 756/756 notes exact |
| 4 | "Cursor place is not accurate in the sequencer … creating markers or pasting some patterns (sometimes it pasts patterns in the same place by accident…)" | [SHOT] f11 → f12: FL 2026, clip on Track 1 at 3.2, song position at bar 9, mouse at bar 12 / Track 6, Ctrl+C / Ctrl+V → the copy lands at **bar 9 on Track 1** (the song position, same track; not the mouse); f13 → f14 the same at bar 7. Markers: Alt+T at the song position (FUN_01229d20, REA in `addMarkerByKey`), right-click ruler "Add marker" at the click | LYNX does the same (`pasteClips`: the song position snapped to the grid; ruler clicks land on the clicked bar: s11 click at bar ~19 → 19:1:1, s12 → 45:1:1). So when the song position is where the copied clips are, Ctrl+V pastes on top of them, in FL too: that is the "pasted in the same place, overloaded" | no change (FL-exact). Not reproduced as an inaccuracy: asked the reporter for the exact steps (which marker action, which snap, where the song position was) |
| 5 | "All the faders on the mixer panel can react accidently on the mouse wheel … (Including the 3 band eq faders)" | [SHOT] f03–f05: FL 2026's mixer does **not** scroll with the wheel over the strips' blank parts (names, bottom row, meters): nothing moves; the wheel over a fader moves it 2 % per notch (r168s c_f07) | [SHOT] s08 → s09 → s10: LYNX scrolls the strips with the wheel over a blank part (LYNX's own); 3 notches there scrolled, then the same spin passing over Insert 12's fader moved it from −8.0 to **−9.3 dB** | a wheel that is scrolling the mixer keeps scrolling while it keeps turning (`controls::WheelScrollLatch`, events < 400 ms apart; `ui::noteWheelScroll` / `ui::wheelScrolling`), over the faders, pan / stereo / send knobs, the insert EQ, the effect knobs and the slot names (which the wheel otherwise moves up/down). After a pause the wheel over a fader moves it as in FL. LYNX-own (FL has no wheel scroll there): documented. Test `issue32c_a_wheel_scrolling_the_mixer_keeps_scrolling_over_its_controls` |

## Windows check (branch build `build-win\LYNX.exe` = `LYNXfix.exe`, fresh profiles from `proftpl`)

| Check | main 633c04e4 | branch |
|---|---|---|
| `--export house-ts.lynx`, test synth log | mode `rt` on every block; 1503 blocks repeat the previous play-head position | mode `off` on every block (90 179 + 88 682); 0 repeats; 756 + 3 notes, every one with its note off and exact length |
| live: Play, 4 ruler clicks while playing, Stop (`live-*.log.*`) | long note: 3 note ons, 0 offs, 3 voices after Stop; short notes: 8 doubled | 4 jumps; 33/33 and 3/3 notes on/off, 0 doubled, 0 left sounding; 0 play-head repeats ([SHOT] l02) |
| mixer: 3 notches on blank space, the same spin over a fader | Insert 12 −8.0 → −9.3 dB ([SHOT] m2.png) | strips scroll, no value changes ([SHOT] m4.png rows 1–3) |
| mixer: a pause, then the wheel on a fader | — | Insert 16 0.0 → −0.6 → 0.0 dB (m5.png), as in FL |

## Notes

- CLAP plugins were never affected by point 1: LYNX's CLAP host already turns All Notes Off into one note off per held
  note (`ClapFormat.cpp addMidi`). CLAP plugins get no transport at all (no `clap_event_transport`): not in this round.
- Bridged (sandboxed) plugins: the proxy passes MIDI as it gets it, so they get the note offs too; their child has no
  play head (unchanged).
- The VST3 process mode is also in every `ProcessData` (`processMode`); plugins built on the SDK's `AudioEffect` read it
  from `setupProcessing` only, as the test synth does.

## Where I stopped

- Done: code + core tests (commit 0620af30 on feature/issue32c, pushed as wip/issue32c); FL session (one, closed with
  "No" by its own window); main-build reproductions (s01–s12, l01, synth logs).
- Done too: build-local (all tests) + Windows build, the export, live jump and mixer wheel checks above.
- Open: point 4 (cursor accuracy) not reproduced: waiting for the reporter's steps or a clip. SynthMaster 2 itself is not
  installed here: the render fix is proven with the test synth (offline flag, no repeats), not with SynthMaster 2.
- Commands: `python3 -I tools/testplugin/synthlog.py <log>.<n>`; test synth build line at the top of
  `tools/testplugin/lynx_test_synth_vst3.cpp`.
