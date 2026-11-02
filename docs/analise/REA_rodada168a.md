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

## Where I stopped

(filled at the end of the round)
