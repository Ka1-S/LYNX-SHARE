# Round 168a — Online Sessions, phase 1 (packages, stand-ins, part merge)

LYNX-own feature (FL Studio 2026 has no collaboration: evidence in [REA_rodada167r.md](REA_rodada167r.md)). Design:
[docs/design/ONLINE_SESSIONS.md](../design/ONLINE_SESSIONS.md) (the user's decisions added at the top, "P1 as built"
in §9). User steps: [docs/USER_GUIDE.md](../USER_GUIDE.md). No FL reverse engineering was needed: there is nothing in
FL to match.

| Asked (brief 168a) | LYNX before | LYNX now |
|---|---|---|
| File › Send project…: project + samples + stand-ins + manifest with hashes + version + keep policy | Export project with samples (.zip) only | *File › Online session › Send project...* writes a `.lynxpack` (zip: `manifest.json`, `project.lynx`, `media/<sha256>.<ext>`); one file used twice is stored once; the guest's tag, inserts, rows, colour and keep choice in the manifest and the song |
| Open on another profile plays identically; missing plugins show the stand-in + badge | — | the engine plays a projection of the song where a third-party plugin is missing: pattern stems at the clips (Song mode), the loop stem (Pattern mode), insert stems in place of the chain; amber **R** badge in the Channel Rack and Mixer, hint bar text |
| Guest "Send my part back" (his area only) | — | `parts::extract` (his channels, notes slices, patterns, automations, clips, inserts, rows + stubs) with his own stand-ins; his copy is reduced to his part when the host did not allow a full copy (decision 1) |
| Host "Merge a part…" (his colour / name, one undo step, no clobbering) | — | `parts::merge` by uid, in one `editSong`; media copied to `<project>/Session media/<guest>/`; moved inserts / rows reported |
| Stable IDs survive save / load / .flp import | channels and patterns had no identity | uids (tag << 40 \| counter) on channels, patterns, automations, clips, arrangements; `uids::normalize` on every publish (also after an .flp import), saved in the .lynx |
| Tests | — | `tests/Session168aTests.inc` (7 tests, 95 checks) and `--selftest` `online_session=` |

## How it works (short)

- **Identity**: `core/SessionIds` — `uids::normalize` gives every entity without a uid (or with a duplicate: a copied
  channel / arrangement) a new one with the editing participant's tag (`song.session.me`). `refs::forEach*Ref` visit
  every field that holds a channel / pattern / automation / insert / row index (a test sets each field and fails when
  one is missed: 10 channel fields, 13 insert fields today).
- **Areas**: `parts::invite` reserves 20 inserts and 16 Playlist rows per guest after what the project uses (max 4
  people). New inserts / free-insert picks in a guest copy prefer his range and skip the others' (`Song::nextFreeInsert`).
- **Stand-ins**: `core/StandIns` decides what is rendered (channels / inserts with a hosted VST3 / VST2 / CLAP plugin,
  directly or inside Patcher / parallel containers; LYNX's own engines never), builds the render song (each pattern
  once + 4 bars tail, then twice: the second pass is the loop stem, so the first pass's tail is in it like when the
  pattern loops) and the projection. `engine/SessionPack::render` runs it through an `ExportJob` (32-bit float WAV,
  44.1 kHz, playback quality, no extra WAV chunks) and slices the result; the source hash (the channel's JSON, its
  notes in that pattern, tempo, swing, rate) skips unchanged parts next time.
- **Plugins on the other PC**: the manifest lists the used plugins' portable identity (format, unique id, name,
  vendor, version, file name — never the whole plugin list). `resolvePlugins` re-points a PluginRef to this PC's id
  when the same plugin is installed (another version: the user chooses renders or his version).
- **Substitutes**: an insert with stand-ins does not get LYNX's plugin substitutes (the stand-in replaces the chain).
- **Test builds** no longer register Windows file types (`LYNX_TEST_MODE`), so a test run never takes over `.lynx` /
  `.lynxpack` on the user's PC. Normal builds also register `.lynxpack`.

## Verification

- Core tests (build-local): see "Where I stopped" for the run of this round.
- `tests/Session168aTests.inc`: stand-ins rendered the way SessionPack cuts them and played through the projection
  match the channel itself within 3e-8 (Song mode with a clip that starts inside its pattern and a tail into the next
  clip; Pattern mode from the second pass); insert stand-in: exact.
- `--selftest`: a real `ExportJob` render, the package written (3 media, 1 duplicate), opened into another folder
  (hashes verified, uids and stand-ins kept, media inside the folder), the guest's channel saved / loaded with its
  uid, his part written, opened, adopted and merged (his channel in his tag, the host's untouched).

## Windows GUI check (two fresh profiles, real mouse and keyboard)

Test build, `LYNX_TEST_MODE=1`, profiles `%TEMP%\lynx168a\host` (FabFilter Twin 3 and Simplon known through a
`--scan-one` of those two files only) and `%TEMP%\lynx168a\guest` (no plugins). Project: Kick (sampler, insert 1 with
Simplon), Twin 3 (chords), Kick hi (same sample), pattern "Beat" on 3 Playlist clips. Screenshots in
`E:\lynx-shots\168a\`.

1. Host: File › Online session (Send my part back greyed) › Send project... → names, "Only his own part" →
   native save dialog → "host - for Ana.lynxpack is ready… 4 files (5.2 MB), 3 plugin renders" (`send-dialog.png`,
   `send-done.png`). Stand-ins checked: Twin 3 once 6.7 s / loop exactly one bar (2.000 s), peak 0.33-0.38; insert 1
   timeline stem peak 0.37.
2. Guest opens the package: summary (Carlos, inserts from 4, Playlist tracks from 2, keeps his own part)
   (`open-summary.png`), then "Not installed here (their renders play, marked R): Twin 3, Simplon" (`open-plugins.png`);
   amber R on Twin 3 in the Channel Rack and on insert 1 in the Mixer (`guest-rack.png`, `guest-mixer.png`).
3. Guest adds a Synth channel with 4 steps (routed to his insert 4 automatically), Send my part back... →
   "0 files, 0 plugin renders … Carlos asked that you keep only your own part: the rest was removed" — the copy keeps
   only the Synth (`guest-menu.png`, `guest-own.png`, `part-sent.png`).
4. Host: Merge a part... → "Ana's part is in the project: 1 channels, 0 patterns, 4 notes, 0 Playlist clips, 1
   inserts" — Synth in Ana's blue, the host's channels untouched (`merge-done.png`, `host-merged.png`); Edit › Undo
   takes the whole merge out (`host-undone.png`). Desktop clean afterwards (`clean1.png`).

Found and fixed during the check: the R badge's bold font leaked into the channel name; the Mixer badge covered the
arm button (now left of the FX count); the received project was called "project.lynx" (now named after the project).

## Where I stopped

- **State (2026-11-02, paused for round 168w)**: branch `feature/online168a` = P1 code + docs + 15 languages, merged
  with origin/main efb8344d after the reboot; NOT landed yet. Before the reboot: Windows build OK, `--selftest` all OK
  (`online_session=OK`), core tests 1 890 962 checks with only the stale embedded-i18n check failing (the tables were
  regenerated after that test binary was compiled; `make_i18n.py` now gives no diff), Windows GUI check passed.
  After the merge: a Windows build was running when the pause came; core tests not re-run yet.
- **Exact next step on RESUME**: rebuild Windows (`winbuild.sh scripts/build-windows-zig.sh`) and core tests
  (`bl-wt.sh online168a`) on the merged branch; re-run `--selftest` (fresh profile); optionally re-shoot
  `guest-rack.png` / `guest-mixer.png` with the badge fixes (open `E:` test package `%TEMP%\lynx168a\projects\host -
  for Ana.lynxpack` in a fresh guest profile); then `land.sh online168a`.
- P1 complete as described above (round 168a), merged with origin/main after the 2026-11-02 reboot (Loop
  Starter's `loopStarterPattern` added to the pattern-reference visitor).
- Not done in P1 (next): "Send my part back" offers no choice of which Playlist row / take to include (his whole
  area goes); the guest's channels get his colour only at merge time (not while he records); stand-ins do not follow
  note edits of a plugin part on a PC without the plugin (by design: the owner re-sends); no per-guest colour picker.
- P2 (live session) starts from `parts::merge` / `uids` / `refs` / `StandIns` here; the wire diff, transport and
  presence UI are still design only (ONLINE_SESSIONS.md §1, §6, §7).
