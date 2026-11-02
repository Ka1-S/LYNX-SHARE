# Round 168t — Playlist selection / loop on the ruler (R-mac) + R-ux (v0.1.22)

Source: docs/analise/REA_rodada168d-video.md (the YouTube review "NEW Open Source FL Studio!", Busy Works Beats,
LYNX 0.1.21 on macOS), moments #4, #5, #9, #10, #15 and the rows R-mac / R-ux of its "Proposed fix rounds".

FL Studio was NOT launched in this round: every FL point below comes from FL's own binary (REA/Ghidra on
FLEngine_x64.dll of FL Studio 2026, read-only use of the analysed project /mnt/e/lynx-work/r167g, scripts and raw
decompiles in `~/.claude/jobs/f8789b49/tmp/r168t/`: d1 = ruler handlers, d4 = their helpers, d5 = grid handlers,
d6/d7 = filter groups, f1/r1/r2 = string search and references), FL's DFM (`tmp/rea165/dfm/TEventEditForm.dfm.txt`),
FL's demo projects (FL Data) and FL's public manual / forum for the Mac key mapping (no Mac here).

## Per item

| # | reporter's words (168d) | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 R-mac | #4 "the double click to highlight this bar is ... hard to do"; #15 "Kind of weird trying to create a loop in here" (status bar said `Ctrl+drag or ×2 drag: time selection`) | [MANUAL] FL manual "Shortcuts": "Ctrl → command (Cmd), Alt → option (Opt)" (image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_shortcuts.htm). [FORUM] Image-Line forum p=2066400 (2026-07): "FL Studio 2026 does not respect [macOS] CTRL+CLICK to right click, and instead left clicks" | JUCE on macOS: `isPopupMenu()` = right button **or Control**, `isCtrlDown()` = Control only, Cmd never read. `PlaylistComponent::mouseDown` asked `isPopupMenu()` first on the ruler → Control+drag opened the ruler menu; Cmd+drag did nothing (plain scrub); only the double-click drag remained | One map for every editor: `core/ModifierMap.h` (JUCE-free, tested) + `ui/FlMods.h`. FL's Ctrl role = Ctrl (Windows/Linux) / Cmd or Control (macOS); popup = the right button only (Control+click is a left click, as in FL 2026 for Mac). Every `isCtrlDown()` / `isPopupMenu()` of the Playlist, Piano Roll (+ .h), Channel Rack, Mixer, event editor, automation UI and editor tools now goes through it (a test fails if one comes back). Hints, quick tips, tooltips' plain hints, menu shortcut texts and the shortcut editor show ⌘ / ⌥ on macOS (`modmap::keyNames`, also the German "Strg"); the saved keymap is unchanged |
| 2 ruler | #4/#15 selection feel, the brief: "plain drag on the ruler where FL does" | [DECOMP] `TimePanelMouseDown` 0xe56bf0 (Button: 0 left / 1 right / 2 middle; Shift: 1 Shift, 2 Alt, 4 Ctrl, 0x40 double click), `TimePanelMouseMove` 0xe57670, `TimePanelMouseUp` 0xe58d10, helpers 0xe43d30 (set the selection, select the events in it), 0xe5a9d0 (deselect), 0xe7ded0 (zoom to the range); tool numbers from the DFM (`SelectBtn` Tag 4, `ZoomBtn` Tag 5). Table below | right-click = ruler menu; Ctrl+drag / double-click drag = selection; Shift+drag inside the selection moved it **with its clips**; Select tool: scrub | `core/RulerGestures.h` (`ruler::press`) implements FL's table; `PlaylistComponent::mouseDown` asks it |
| 3 grid marquee | #5 "selecting stuff is a little weird" | [DECOMP] `PianoPanelMouseDown` 0xe5cd20 (the Piano roll's and the Playlist's grid, both `TEventPanel_PR`): the box selection starts for the Select / Zoom tools or `Ctrl` pressed off an item (`(Shift & 4) && !onItem` → LAB_00e5d9de) | same rule (Ctrl+drag on empty space, Select tool), but on a Mac only with Control (Cmd ignored) | same rule; on a Mac Cmd or Control |
| 4 loop from the selection, Ctrl+B / Ctrl+C | #5 "That worked … the same shortcuts so far as FL Studio" | REA_rodada165-issue11c (time selection = loop in Song mode), REA_rodada167k (Ctrl+B) | worked | unchanged (Cmd+B / Cmd+C on a Mac were already Command via ShortcutManager) |
| 5 R-ux Playlist | #9 "How do we get back to the playlist? Where is the playlist? ... these buttons are kind of like weird" | [DFM `TToolbarForm`] FL's view buttons are icon-only too: `ViewPLBtn` 30x33 px, Caption = icon glyph `\uf001`, Hint `^^F5^^dPlaylist / Maximize Play list` (named only by the hint bar on hover); keys F5..F9 | icon-only buttons, a quick tip on hover | the first three starts of a profile (fresh profile included) post a hint for 15 s naming the window buttons with their keys: "Windows: [F5] Playlist  [F6] Channel Rack  [F7] Piano Roll  [F9] Mixer  [Alt+F8] Browser (or the window buttons in the toolbar; again: hide)". 167v's first-run layout untouched |
| 6 R-ux rack | #10 "the channel rack stuff ... it's hard to tell what is what" (7 of 13 rows = FL audio clip channels) | [DECOMP] 0xfff0b0: a channel whose type becomes 4 (audio clip) / 5 (automation clip) goes to the filter group "Audio clips" / "Automation clips" (created if missing); 0x1003d80: a new channel joins the group shown, or "Unsorted" when the view is one of those two; 0xf46490 (.flp event 0xE7): the two names are translated on load. [FLP] all 80 FL demo projects carry the "Audio clips" group | filter menu = All + the project's groups (an imported FL project listed FL's "Audio clips" / "Unsorted" groups; LYNX's own projects had nothing) | `core/RackFilter.h`: the filter menu always offers FL's two views, **Unsorted** (channels in no group / FL's "Unsorted": hides the audio clip channels) and **Audio clips** (audio clip channels, or FL's "Audio clips" group), without changing the song; FL's same-named groups are not listed twice; new channels join only real groups |

## FL's ruler (Playlist and Piano roll share TimePanelMouseDown) — what each press does

| press | FL 2026 | LYNX before | LYNX now |
|---|---|---|---|
| left | marker tag: drag the marker; elsewhere: song position, scrub (`+0xd9c = 10`) | same (marker: jump) | same |
| left + Ctrl / Shift / double click, or the **Select tool** | time selection from the press (`+0xa78 = 4`), the clips / notes in it selected as it grows (0xe43d30 → 0xe903e0), everything deselected at the press (0xe5a9d0); same start and end = no selection | Ctrl / double click only; Shift = scrub; Select tool = scrub | FL (`ruler::Action::select`) |
| left + Shift with a selection | the selection moves by the drag, its length kept, not before 0 (`+0xd9c = 2`, MouseMove case 2) | Shift inside it: moved **with its clips** | FL (`moveSelection`); LYNX-own: **Ctrl+Shift+drag inside** = with its clips (the old gesture) |
| left with Ctrl/Shift/double on a marker tag | time selection from that marker to the next one (or the song's end), released at once | – | FL (`markerSpan`) |
| right | time selection (`+0xa78 = 4`); with a selection, the press moves its **nearer edge** (anchor = farther edge, `+0xd9c = 3`); markers present and the press on the marker lane (Y ≥ +0xa40), not Select / Zoom tool: marker menu | always the ruler menu | FL (`select` / `adjust` / `markerMenu`). LYNX-own: a right click released in place with no selection opens LYNX's ruler menu (FL: nothing) |
| right + Ctrl, or the Zoom tool | a range (`+0xa78 = 5`, coloured differently) and the view zooms to it on release (0xe7ded0; a tiny range zooms in at the point) | menu / scrub | FL (`zoomRange`, reuses the grid's zoom range) |
| middle | the time scroller's zoom (REA_rodada167t) | same | same |
| Piano roll ruler | the same handler | Ctrl+drag selected the notes in the range | + right-drag, double-click drag and the Select tool select the notes in the range (FL's right press deselects first, as LYNX's range select does) |

Double-click tolerance: FL uses Windows' double-click rectangle (SM_CXDOUBLECLK 4 px) and time; JUCE uses the
system time and an 8 px box, so LYNX is already the more tolerant of the two — no change. The Mac review problem was
the Control+click menu, not the double click.

## What to verify on a Mac (no Mac here)

The CI macOS build must stay green; unit tests cover the mapping (`sel168t_*`). On a Mac with 0.1.22:
1. Playlist ruler: Cmd+drag and Control+drag → time selection (no menu); two-finger drag (right) → time selection;
   Cmd+two-finger drag → zoom range; two-finger click in place with no selection → ruler menu.
2. Playlist grid: Cmd+drag on empty space → box selection; Cmd+click a clip → toggles it; two-finger drag → deletes.
3. Piano Roll: Cmd+drag on the ruler / on empty grid → selection; Cmd+wheel → zoom.
4. Channel Rack / Mixer: Cmd+click where the hint says Ctrl (e.g. a mixer LED: solo).
5. Hints and menus read ⌘ / ⌥ (e.g. ruler hint "[⌘][LMB]", Edit menu "⌘+Z").

## Tests

`tests/Sel168tTests.inc`: modifier map on Windows / Linux / macOS (Control+click on a Mac = FL Ctrl, not popup; Cmd =
FL Ctrl), Mac key names (whole words only, German "Strg"), FL's ruler table (left / right / tools / markers), the Mac
review gesture end to end (Control or Cmd+drag on the ruler → time selection), adjust / move maths, the rack's built-in
views, and a source scan that the editors no longer call `isPopupMenu()` / `isCtrlDown()`. On main they fail to
compile (the headers are new).

## Where I stopped

- Done: REA (ruler, grid, filter groups), ModifierMap + FlMods sweep, ruler gestures (Playlist + Piano Roll), Mac key
  names in hints / tips / menus, rack views, windows tip, 15 languages, tests.
- In progress: build-local with FL Data (`BUILDSLOT_PRIO=1 tmp/coord/buildslot.sh tmp/coord/bl-wt.sh sel168t`, log
  tmp/coord/bl-sel168t.log), then Windows build + GUI check, notes, landing.
- Rerun Ghidra: `~/.claude/jobs/f8789b49/tmp/r168t/g.sh <out> 0 <hex addr>...` (SCRIPT=Refs.java / Find.java for xrefs /
  byte search).
