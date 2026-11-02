# Round 168m — Gross Beat usable without FL (v0.1.22)

Source: the YouTube review of 0.1.21 on macOS (docs/analise/REA_rodada168d-video.md, moments #19 and #20; "Proposed fix
rounds" row **R-gross**). Clean-room: behaviour read from FL's own `Gross Beat_x64.dll` (Image-Line's plugin, allowed) and
written here in our words; LYNX is implemented from this document. No FL preset or state file is copied into the repo:
LYNX's factory slots are shapes made for LYNX from the behaviour described below.

- Target: `Plugins/Fruity/Effects/Gross Beat/Gross Beat_x64.dll`, SHA-256 `9025d938…8f1a` (= rounds 14 and 53).
- Tools: REA (Ghidra 12.1.4) — decompilation, string and byte searches; a small DFM reader of our own
  (`scratchpad/py/dfm.py`) for the editor form `TSynthEditorForm` (resource at file offset 5 828 916).
- No FL session was needed: every item below was settled from the binary.

## Items

| # | reporter's words (video) | FL evidence | LYNX before (0.1.21 / main e6e36fdd) | LYNX now |
|---|---|---|---|---|
| 19 | *"one thing I would like as a beginner is presets cuz there's no way I'm drawing every single Gross Beat"* (14:40, f1/s0905.png: "(no preset)", 72 empty slots) | §1: a new FL instance builds 72 flat slots, then loads its default bank from the plugin's Data folder (constructor `FUN_0083fcf0` → `FUN_008273c0(…"Data"…)`), so FL users start with filled slots | `grossbeat::make({})`: a new instance had no state and **passed the signal**; slots showed "Slot 1…36"; presets only when an FL install provides `.fst` files | A new Gross Beat starts with **LYNX's factory bank** (17 time slots + 22 volume slots, slot 1 empty on both sides as FL's); an old project's Gross Beat without a state plays the same bank; **17 LYNX SHARE presets** (Time / Volume / Both folders) that carry the whole bank and select a pair of slots; the user's presets now save the slots too; FL's `.fst` presets still listed under "FL Studio presets" when FL is installed |
| 20 | *"it doesn't do anything. I'm clicking in here and it's not doing anything … Why would you trick us like that?"* (15:02–15:13, crops/k880.png) | §2–§4: FL's envelope editor (`MEnvEditor`, mouse-down `FUN_00821760`): right-click adds a point and drags it, left-drag moves a point / a tension handle, right-click a point = its menu, Alt+right-click deletes, right-click a tension handle = straight; Snap selector; Envelope selector; slot settings under the editor | the envelope area was paint-only (no mouse handling); only an imported FL state was drawn | The editor edits the selected slot with FL's gestures; tool column (Options, Snap, time / volume); slot bar (name, Link, Key-held, One-shot, HOLD, TRIG SYNC, POS SYNC); every edit is **written back into the LXGB container in FL's own layout** and the running effect takes it in place (no rebuild, no click) |

## 1. A new instance [DECOMP `FUN_0083fcf0`]

For each of the two kinds and each of the 36 slots the constructor: clears the name, sets trigger sync **5** and position
sync **5** (4 beats), the slot's MIDI key (60 + index for time, 24 + index for volume), link / key-held / one-shot off,
a length of 4.0 beats, and two points (0, 1.0) and (4.0, 1.0), both of curve type **2 (hold)**. Options: bytes 6 (high
quality), 7 and 8 (declick) = 1. Then it points the envelope editor at the registry key
`Software\Image-Line\Shared\Plugins\Gross Beat`, loads the plugin's `Data` bank, and sets the editor's **new-point type to
2** (`editor+0x15e0 = 2`): points added in Gross Beat are hold points (steps). Colours: Time `$A8FF70`, Volume `$FFB870`
(Delphi BGR).

LYNX: `grossbeat::emptyState()` = this instance; `factoryState()` = the empty instance with LYNX's own slots
(`fill (…)` in GrossBeat.cpp: stutters/repeats built as hold steps of `level = 1 − delay/8 beats`, half speed and tape
stop as lines, reverses as lines with a jump back, gates/patterns/pumps/fades/tremolos on the volume side).
`factoryContainer()` is what `makeEffectSlot (grossBeat)` stores.

## 2. Mouse on the envelope [DECOMP `FUN_00821760` (MouseDown), `FUN_0082df80`, `FUN_00816a60`, `FUN_00816940`]

Delphi arguments: button (0 left, 1 right, 2 middle), shift state (1 Shift, 2 Alt, 4 Ctrl, 0x40 double). Over a point or
its tension handle (`hover index ≥ 0`, handle when `0x10b4 > 0`):

| gesture | FL (undo name) |
|---|---|
| right-click a tension handle | "default tension" (tension 0) |
| right-click a point | the point's popup menu (`FUN_0069c260`) |
| Alt+right-click a point | "delete point" |
| left-drag a point | "move point" (x snapped, `FUN_00816a60` with the shift state) |
| left-drag a tension handle | "move tension" |
| Alt+left on a handle | "default tension"; Alt+left on a point deletes only with a global option (`FUN_0072c400(0xa5)`) — not on by default, not done in LYNX |
| middle-click a point | toggles its selection — not done in LYNX |

On empty space: right-click (or the double-click flag pair 0xC0 with the left button) = **"add point"**: x snapped
when snapping, y snapped only when the level axis has snap (not set by Gross Beat); without snap the new level sticks to
the curve when within **8 px**; **Shift** = level on the curve; the point gets the editor's point type (2 in Gross Beat)
and the last used tension; then the drag continues while the button is held. Ctrl+left (option `0x2c4`) starts a
rubber-band selection — not done in LYNX (single-point editing only). The first point cannot be removed ("The first point
of the envelope can't be removed.").

Snap: `snapping = editorSnapFlag XOR Alt` (`FUN_0082df80`).

## 3. The Gross Beat window [DFM `TSynthEditorForm`, `FUN_0084b8f0` (SnapSelChange), `FUN_0084c4d0` (TimeSlotSelChange), `FUN_00844500`]

Client 824 × 535. Left: Time mix wheel, Options, HOLD (bar repeat), the 4 × 9 time and volume grids, ATT / REL / TNS, Volume
mix. Envelope editor at (298, 5, 520 × 501); a 23 px tool column at x = 270: **Options menu** (y 4), **Snap** selector
(y 40–229, 7 cells: off, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3 beat; table `0x8b6ae8` = 4, 16, 12, 8, 6, 4, 3 per beat — "off"
draws a quarter-beat grid without snapping), Step editing / Slide / Freeze buttons, **Envelope** selector (y 470: time
travel / volume). At creation the snap selector is set to *editor snap flag × 3* = **1/8 beat**. Under the editor
(y 510): **NameLabel** (click = rename), **Link volume slot**, **Key-held** (back to previous slot on key release),
**One-shot** (back to first slot once done), **HOLD:** combo *Off / Last / Next* ("Force hold delay (bar repeat)"),
**TRIG SYNC:** and **POS SYNC:** combos *Off, 1/4 beat, 1/2 beat, 1 beat, 2 beats, 4 beats*.
Selecting a slot in either grid calls `FUN_0084cf00(editor, kind, slot)`: the editor shows that kind's slot.

LYNX: `PluginLayout.cpp` grossBeat() — displays `gbtools`, `grossbeat`, `gbslot`; `ui/GrossBeatEditor.inc`.

## 4. State written back [REA_rodada14 §2.1, REA_rodada53 §1]

`grossbeat::writeState` writes FL's layout: version `0x00080003`, 9 parameters, 9 option bytes, then per slot the
shortstring name (0xff + i32 length when longer), trigger sync, position sync, hold, link, key-held, one-shot, and the
map stream (kind 3, version 3, n, n × {Δx f64, level f64, tension f32, type, 0, 0, direction}, flags 0, −1 × 4). Edited
points keep Σ Δx = 4 beats exactly so reading normalises nothing (write → read → write is bit-identical). Every Gross
Beat state of FL's demo projects is read, written and read again with the same sound (test `gb168_fl_demo_states_write_back`).

The running effect takes an edit in place (`acceptState` → `Engine::update`, no allocation on the audio thread; each
slot keeps room for 512 points, more = the effect is rebuilt).

## 4b. Design and a richer factory bank (user's follow-up: "está sem nenhum design, tem que fazer uns presets legais")

FL's layout is the reference (two 4 × 9 slot grids on the left, the envelope on the right with its tool column and the
slot bar), drawn in LYNX's own style:

- **Slot grids** (`gbtimegrid`, `gbvolumegrid`, replacing the plain "Slot 1…36" radio lists): each cell shows the slot's
  name over a filled thumbnail of its envelope (time in the plugin's accent, volume in the info blue — FL also tells
  the two kinds apart by colour, `$A8FF70` / `$FFB870`); empty slots show only their number. Left-click selects (FL),
  right-click opens the slot's menu (copy, paste, flip, reset, rename, **Factory slots ▸**).
- **Tab bar** over the envelope (`gbtabs`): TIME / VOLUME tabs, the selected slot's number and name, and a **Factory
  slots ▾** browser that puts one of LYNX SHARE's slots into the selected slot (LYNX-own: FL browses its slot presets
  from the Options menu).
- **Envelope**: gradient fill, a soft glow under the line, beat numbers along the top, the level scale on the left
  (time: now / 1 bar back / 2 bars back; volume: 100 / 50 / 0 %).

Factory bank grown to **25 time + 30 volume slots** (clean-room shapes, written as steps and lines in `factoryState`):
new time slots Vinyl brake (the last two beats slow to a stop), Spin back (the last beat rewinds at 3× speed), Tape
start, Repeat 3-3-2, Lo-fi wobble (a slight slow pitch wobble), Stutter into reverse, Triplet roll, Half speed last
beat; new volume slots Sidechain hard / soft / 1/2 beat, Gate 3-3-2, Reverse swell, Half-bar swell, Mute last beat,
Stutter gate 1/16. **29 factory presets** (Time / Volume / Both, e.g. Lo-fi tape, Trap stutter, Spin back pump).

## 5. Tests

`tests/GrossBeat168mTests.inc`: the video's two cases first (a new instance sounds with its factory slots; a right-click
point heard after the write-back — both fail on main: no state / no editing API), exact round trip, FL demo states,
in-place edit, presets with the bank.

## Where I stopped

- 2026-11-02 after the crash/reboot: commits intact (pushed to wip/gross168m), origin/main efb8344d merged (i18n
  tables union-merged and regenerated, test includes kept from both sides).
- Done: REA (§1–§4), core, presets with state, editor UI, design (§4b), tests, 15-language strings, notes-0.1.22.
- In progress: build-local + Windows build (release lane), then plugui after-shot and the Windows GUI check of the
  video's flow (before: /mnt/e/lynx-work/gross168m/before/plugui/Gross Beat.png; after: …/after/).
- Next: land via `flock ~/projetos/lynx/.git/lynx-merge.lock coord/land.sh gross168m` (BUILDSLOT_PRIO=1).
- Rerun: `BUILDSLOT_PRIO=1 coord/buildslot.sh coord/bl-wt.sh gross168m`; Windows:
  `OUT=build-win JOBS=3 BUILDSLOT_PRIO=1 coord/winbuild.sh scripts/build-windows-zig.sh`.
