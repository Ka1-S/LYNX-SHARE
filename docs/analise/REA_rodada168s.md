# Round 168s — issue #32 (Majed Salih / medievilmusic, Windows 11, FL ASIO, KV331 SynthMaster 2 + Vital): 8 problems from a full track

Issue: "Problems with rendering and mixer routing." (read-only `gh issue view 32`). Worktree `lynx-wt-issue32s`,
branch `feature/issue32s`, scratch `~/.claude/jobs/f8789b49/tmp/r168s/`.

## Items

FL evidence: one FL session 2026-11-02 03:34-03:37 (FL closed by its own PID, "Save changes" -> No, the StepSeqForm
registry values put back). Shots in `C:\Users\kauan\lynx-test\r168s\` (f = full screen, c_ = crops). FL strings from
the FL binaries (clean-room, tmp/s16.txt). The reporter's "sequencer" is the Playlist (#3 "snap ... in piano roll and
sequencer").

### 1. VST synths cut notes in the render; boxed: glitches "in the background until the progress bar completed"

- FL: renders with playback stopped (REA_rodada155: "FL Studio renders from the project's live plugin instances
  (playback stops while the render runs)").
- LYNX before: the live engine kept playing and processing every live plugin while the export engine rendered its own
  instances: the background glitches are the live output starved by the render (the render runs as fast as it can on the
  same CPU). A boxed (sandboxed) plugin was never told it renders offline (RemotePlugin had no setNonRealtime: the child
  kept realtime mode) and a block it took more than 500 ms over came back silent (the realtime timeout), and a reset() was
  not passed to the child.
- LYNX now: while an export renders (AudioEngine::offlineRendersRunning) the live callback stops playback and outputs
  silence (not while recording); the sandbox passes the render mode (setNonRealtime -> the child re-prepares in that mode)
  and reset() to the child and waits up to 30 s per block while offline.
- Not verified with SynthMaster 2 / Vital (not installed; Vital needs an account). If notes still cut, a project + the
  plugin versions are needed.

### 2. Mixer: "can not rout any channel to another beside the master"; "faders can react suddenly on the mouse wheel"

- FL (c_f10): select Insert 1, right-click the route arrow under Insert 2: "Route to this track", "Route to this track
  only", separator, "Sidechain to this track", "Sidechain to this track only". Wheel (c_f07): one notch over a fader =
  100% -> 103% ("Volume: 103% 0.6dB"), 2% of the travel; the wheel over the route lane does nothing.
- LYNX before: routing worked with a left-click on the "-> n" switch of the selected insert, but the right-click menu was
  LYNX's own (send levels) without FL's four items. A Windows wheel notch (JUCE deltaY 0.234) was read as 1.95 notches
  (`/0.12`), so a fader moved 3.9% per notch, twice FL's.
- LYNX now: FL's four items (mixerops::routeSelectedTo, route / routeOnly / sidechain / sidechainOnly), plus "Remove this
  send" when one is on; controls::wheelNotches counts a notch per platform (Windows 120 units, Linux 50/256): 2% per
  notch, as FL. Test issue32_one_wheel_notch_moves_a_fader_one_fl_step.

### 3. "Snap function suddenly stops working ... works back only after relaunching"

- FL (f05): Backspace in the Playlist toggles the toolbar snap to "(none)"; a second Backspace puts "Line" back.
- LYNX before: the same toggle (EditorGlobals::toggleGrid), shown only in the toolbar box; the global snap is not saved,
  so a relaunch resets it: that is the "works after relaunch". Backspace is "delete" in many DAWs.
- LYNX now: the toggle stays FL's; the hint bar says "Snap off: notes and clips move freely. [Backspace] turns snap back
  on" / "Snap on: <grid>. [Backspace] turns it off" for 3 s.

### 4. "Playback ... reset sounds and loops when adding new sounds or patterns"

- FL: keeps playing (every channel keeps its instance).
- LYNX before: RackState::build reused the previous instrument / effect by list position. A channel inserted above
  others (FL inserts after the selected one) shifted every later channel: each got its neighbour's instrument or a new
  one (new sample, no voices), and the renderer's voice orders / held notes stayed on the old numbers.
- LYNX now: ChannelDef / EffectSlot carry an InstanceTag (identity, ignored by ==), made unique on publish
  (Song::uniqueInstances: a pasted copy gets its own); build matches by tag (position only for an item whose tag is gone)
  and the renderer remaps voice orders and held notes through RackState::channelFrom. Tests
  issue32_adding_a_channel_while_playing_keeps_every_other_channels_instrument,
  issue32_a_note_sounding_on_a_shifted_channel_keeps_sounding.

### 5. "moving non native plugins inside the mixer chain up or down they reset all their parameters"

- FL: the plugin moves with its slot, settings kept.
- LYNX before: the same position matching: after Move up/down both slots were made again from the saved state (every
  change since the last save lost).
- LYNX now: the instance moves with its slot (within an insert and to another insert). Tests
  issue32_moving_a_plugin_in_the_effect_chain_keeps_its_instance, issue32_a_pasted_copy_of_a_plugin_slot_gets_its_own_instance_once.

### 6. "The channel selector ... is very small and its not accurate"

- FL (c_f14-c_f17): the TRACK digiwheel (`forms.channelrack.controls.digiwheel.mixertrack:digiwheel`): one wheel notch
  = 1 track, drag 30 px = 4 tracks, double-click shows the Mixer at that insert, right-click = the value menu plus
  "Assign to free mixer track".
- LYNX before: a small knob, 3 px per track, a wheel notch = several tracks.
- LYNX now: ui::digitWheel specs (one step per notch, 8 px per step as FL) for TRACK and RANGE, drawn as a digit box;
  TRACK double-click opens the Mixer at the insert, right-click adds "Assign to free mixer track"; the Channel Rack's
  route control uses the same spec.

- Look (digiwheel168s polish): FL's digiwheel (c_f14, C:\Users\kauan\lynx-test\r168s\c_f14.png) is a display about
  22 px high (RANGE ~32 px, TRACK ~40 px wide), the value right-aligned before small up/down marks, in a muted
  tone of the skin. LYNX: theme::drawDigitDisplay, a dark inset bg0 box with light text and the marks; the accent only
  while hovered or dragged (edge + text). Same box for the Channel Rack insert box (no marks: 27 x 16 px). Test
  digiwheel168s_number_boxes_are_dark_displays_red_only_while_touched; shots
  C:\Users\kauan\lynx-test\dw168s\shots\beepmap-lynx-{100,150}.png and dw168s\snap\p-rack.png.

### 7. "When selecting (Fit to project tempo) sometimes its out of tempo"

- FL (c_f20): the sampler TIME knob menu "(none), Autodetect..., Project tempo, 1 beat, 2 beats, 1 bar, 2 bars, 3 bars,
  4 bars"; strings "%s (estimated)" / "%s (embedded)", tempo ranges 50-100 / 75-150 / 100-200 / 150-300.
- LYNX before: clipops::fitBeats rounded every sample to a power of two of beats (a 3-bar loop -> 16 beats, a loop with a
  tail squeezed so its beats came early); the channel's Stretch knob and the clip properties rounded the value to 0.25
  and capped it at 64 / 256, and any other edit there wrote the rounded value back; Detect tempo rounded to 1/4 beat.
- LYNX now: fitBeats takes the WAV's ACID beats, else the detected tempo in the octave nearest the project tempo, exact
  (tempodetect::fitBeatsOf); power of two only when no pulse is found. Knobs to 1024 beats in 0.01 steps, an unmoved knob
  keeps the exact value. FL's TIME menu on the Stretch knob ((none), Autodetect, Project tempo = its length at the
  project tempo, 1 beat ... 4 bars) and the same presets in the clip's Fit to tempo menu; Detect tempo uses FL's ranges.
  Test issue32_fit_to_tempo_keeps_a_loop_in_time.

### 8. "The sequencer window is not always on top. Some windows just get stuck on top until pressing F6 or F7"

- FL (f02-f04): a click on the Playlist brings it over the Channel Rack; F6 brings the covered rack back to front.
- LYNX before: the Playlist was a "background" panel: every floating panel stayed over it until hidden (F6/F7).
- LYNX now: only the Browser stays at the back (workspace::isBackground); Layout::stacking() orders the rest by last
  click. Test issue32_the_clicked_playlist_comes_to_front_like_fl. Plugin editor windows stay above the panels (owned
  top-level windows, as FL's plugin windows can be).

### Contributors: PR #37, PR #39 (HighTecno), issues #36 and #38

- PR #37 (7cf40fb2) and PR #39 (92a28224) merged byte-identical into feature/issue32s (their authorship).
- #37 on top of the instance tags: `editSongStructural()` and its call sites stay (live states copied into the Song
  first, so the undo step has them); its clearing of duplicate plugin ids in lastPublishedShape went (it forced a
  rebuild of every duplicate: new instance + teardown of the old, the #36 path). Tag matching swaps duplicates.
- #39 reconciled with FL's TRACK digiwheel: its `ValueSpec::wheelPerNotch` replaces round 168s' `stepsPerNotch`;
  `ui::digitWheel` sets it to 1 and 7.5 px of drag per step (FL c_f17: 30 px = 4); one wheel step per notch (the
  per-platform notch count makes a click one notch, a multi-notch event moves that many).
- #36 (Presswerk segfault in `~VST3PluginInstance` from `~RackState` inside the engine's Timer callback, after a slot
  move created a new instance): a move no longer creates or destroys the plugin (tags). Retired racks go through
  `SnapshotExchange::dispose` to `AudioEngine::RackDisposer` (AsyncUpdater: freed from a message of its own, app only;
  flushed in ~AudioEngine before the plugin hosts go); the orphaned plugin windows (last owner of a removed plugin) are
  closed from a callAsync, not inside the reaper's timer callback (editor first, then the plugin). Black-box: Presswerk
  not available here. Tests: issue32_moving_a_plugin_in_the_effect_chain_keeps_its_instance (no destruction, duplicate
  swap), issue36_retired_racks_go_to_the_owner_not_freed_in_the_collecting_callback.
- #38: FL uses both words (TRACK knob + "Target mixer track", "Insert N" strips, "Route to this track"): kept, reply drafted.

## Where I stopped

- 19:30 READY: origin/main aae68291 merged; build-local with FL Data 2108504 checks, 0 failures; Windows build OK.
  Windows GUI check (fresh profile, test mode, `C:\Users\kauan\lynx-test\r168s\g01-g14`):
  - a click on the Playlist brings it over the Channel Rack and F6 brings the rack back (g02, g04);
  - Backspace shows "No snap" and the hint, a second Backspace restores the snap (g03, g04);
  - the rack track box and the TRACK wheel move 1 per notch, and 30 px of drag moves 4 (c_g05, c_g07, c_g08);
  - the TRACK menu has "Assign to free mixer track" (c_g09), and a double-click opens the Mixer at Insert 6 (g10);
  - the route arrow menu shows FL's 4 items, and "Route to this track only" routes Insert 1 to 2 only (c_g11, c_g13);
  - one wheel notch on a fader is +0.6 dB (c_g12).
  Not checked in the GUI: a third-party plugin slot move (no plugins on this PC, covered by the unit tests) and the
  sampler Stretch menu. Seen: the channel settings title keeps "(Insert 1)" after TRACK changes (cosmetic, not
  touched). The coordinator lands via the merge train.

- RESUME (coordinator): PRs #37/#39 merged + reconciled, #36 teardown, #38 reply; origin/main aae68291 merged;
  build-local + Windows build queued. Next: tests 0 failures, GUI check, push feature/issue32s, tell coordinator "ready <hash>" (no land.sh).

- 16:40 PAUSED (coordinator, round 168w pipeline). State: all 8 points coded; tests/Issue32Tests.inc 7 tests pass; the
  full suite passed except the embedded i18n tables (regenerated since). Notes are in coord/notes-0.1.23.md and the #32 reply
  draft in coord/replies-0.1.23.md (done). Not landed: the merge lock waiter was cancelled for the pause. Two earlier
  landings failed on generated i18n header conflicts (src/ui/I18nData.h, src/ui/i18n/*.h). Fix:
  r168s/merge-land.sh (under the merge lock: merge origin/main, take theirs for the headers, run make_i18n.py, commit,
  then run land.sh). The Windows work build build-win-x crashed clang (139, random), so no GUI check yet.
- NEXT on RESUME: `flock ~/projetos/lynx/.git/lynx-merge.lock ~/.claude/jobs/f8789b49/tmp/r168s/merge-land.sh`, then
  run the GUI check on C:\Users\kauan\lynx-test\land-issue32s\LYNX.exe (r168s/gui-start.sh, wingui lock): Playlist z-order,
  route arrow menu, fader wheel, TRACK digiwheel, Backspace hint, plugin slot move, Stretch TIME menu. Then delete
  ~/.cache/zig-issue32s and the worktree.

- 11:10 PC crash; resumed: files intact. Issue32 tests built and passed except a float tolerance (fixed). Next: full
  test run, Windows build, GUI check, notes/reply, land.
- found: lastPublishedShape never carried the serial, so the held-note remap never ran in the app (fixed).

- 04:10: all 8 points coded + tests (tests/Issue32Tests.inc), i18n done; first test build queued (prio lane).
- started 2026-11-02 03:20: worktree from origin/main 5a9abcb8; code survey of the 8 points.
