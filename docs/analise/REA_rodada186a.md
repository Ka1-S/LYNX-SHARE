# Round 186a: default template (#123) and docked plugin windows, slice 1 (#116)

Evidence tags: **[STR]** strings in FL 2026's `FLEngine_x64.dll` (`strings`, ANSI and UTF-16). **[DFM]** form resources
inside that DLL. **[UI]** FL 2026 screenshots (`E:\lynx-work\dock186a\shots\fl-*.png`, and round 181c's `f116*.png`).

## 1. Issue #123: the default template

### What FL does
- Options › General settings › Miscellaneous holds **"Default template"**: `CurrentTemplateLabel` with the caption
  `Default template` and `CurrentTemplateBtn`, a combo-style button (`FS_SoberCombo`, `tbDownArrow`, a popup on
  mouse down) whose hint is `|Select default project template` [DFM].
- File › **New from template** (`New from &template`) lists the templates (`TemplatesMenu`, `TemplatesMenuClick`); the
  code that builds it reads each template's `.NFO` (`Title`, `Description`, `MenuIconIndex`) and the list ends with
  **`Change default template...`** (UTF-16, right after `TemplateAction`) [STR].
- Templates live in `Data\Templates\` and the user's `Projects\Templates` [STR]; File › Save as template...
  (`FileSaveAsTemplateAction`) adds one ("The project and its data have been saved as a new project template.") [STR].
- FL 2026 driven (2026-11-09, `fl-02c`…`fl-07c`): File › New is labelled with the default template
  ("New (Basic with limiter)"). New from template shows the categories (Advanced, Minimal, Other, Performance, Power
  User, Utility), Empty, then **Change default template...**, which opens Settings › General with Miscellaneous
  expanded: a **Default template** combo (the same template menu as a popup) and a **Startup project** combo with
  *Empty project / Default template / Last used project* (FL's default: Default template) [UI].
- The same panel holds "Detach all new plugins" and "Open detached plugins with the FL Studio window" (for #116 later).

### What LYNX does now
- Settings › General › Startup › **Default template**: "Default (LYNX SHARE)", the built-in templates, then the user's
  `.lynx` files in Documents\LYNX\Templates. Kept in `new-project.txt` as `defaultTemplate=` (`core/Templates.h`
  `TemplateChoice`: `builtin:<name>` / `user:<file name>`; paths and unknown names read as LYNX's default).
- Settings › General › Startup › **Startup project** under it: Empty project (LYNX's built-in "Empty"), Default
  template (default), Last used project (the newest recent project, opened as itself; gone → the default template).
- File › New reads "New (<template>)" when a default template is chosen (FL always names it; LYNX's own default keeps
  the plain "New").
- At start-up without a file to open (with Startup project = Default template), and with File › New when the New project
  window is turned off, the default template opens as a new untitled project (`MainComponent::startFromDefaultTemplate`, `makeDefaultTemplateProject`),
  so Ctrl+S asks for a name. A template whose file is gone: LYNX's default opens, with a hint.
- The New project window selects the default template first and marks it; a right-click on a template there offers
  "Use as default template" (the reporter's suggestion; FL has no such item).
- File › New from template lists the user's templates after LYNX's own, and ends with "Change default template..."
  (opens Settings › General).
- Nothing changes until a default is chosen: the default stays LYNX's own project.

### Best way?
- **FL:** above.
- **Ableton:** not covered by `ableton/STUDY.md` or the research files. From Live's public manual (not checked by REA):
  Settings › File & Folder › "Save Current Set as Default" makes the set Live opens at start-up and with File › New.
  Same model as FL.
- **Users:** the reporter of #123 (start-up template that opens untitled, so Ctrl+S never overwrites it). No web
  research this round.
- **Verdict:** FL-exact (setting in General, the menu item), plus the reporter's right-click in the New project window
  as a small addition.

## 2. Issue #116: docked plugin windows, slice 1

Design: `REA_rodada181c.md` (approved). This slice is step (a) of its suggested order: the native child frame host behind
a developer switch, with the Channel rack and plugin windows only.

### What LYNX does now (with `LYNX_DOCKED_PLUGINS=1`; without it nothing changes)
- `ui/NativeChild.h`: hosts a component as a native child window of the main window, placed in the Workspace's
  coordinates; follows the main window's native window when it is made again and the desktop's place in it.
- The Channel rack's frame is such a child window (`Workspace::isNativePanel`). Dragging and snapping work in the
  desktop's coordinates, keys nothing in it used go to the main window's shortcuts.
- A plugin's editor opens in a `DockedPluginFrame` (`ui/PluginWindow.cpp`): FL's title strip (plugin menu at the left,
  title, minimise, close), dragged by the strip inside the desktop, a corner resizer for resizable editors, Esc closes
  it. A click on the Channel rack brings the rack in front of it; a click on the plugin brings the plugin back (f116e/f).
- FL's plugin menu › **Detached** (the frame's menu, and the end of the wrapper's presets menu in both hosts) closes
  the editor and opens it again in the other host, never re-parenting it. Saved per plugin (`PluginRef::detached`,
  `.lynx` key `detached`, only when true). Not read from or written to `.flp` yet (the bit is not found).
- Forced detached: plugins in another process (`isSandboxedPlugin`: Options' separate process, "Make bridged", the
  32-bit bridge).
- `EditorHost` holds what both hosts share (keep-alive, gestures, Last tweaked, thumbnails, orphan closing). The
  editor crash guard and the plugin key hook find a docked editor through its parent windows.
- `--plugwintest <plugin> <report> 20` with `LYNX_DOCKED_PLUGINS=1 LYNX_PLUGWIN_DOCK=1`: the test window gets a
  Workspace; it checks the native parents, the stacking order after a rack click and a plugin click, and the Detached
  round trip (owned top-level window, saved flag, docked again).

### Known limits of slice 1 (why the switch stays off)
- Only the Channel rack is native: a docked plugin stays above the Playlist, Piano roll and Mixer whatever is clicked.
- Lightweight overlays drawn over the workspace (setup overlay, drag highlights, value tips drawn in the main window)
  stay under native frames. The hint bar does not read controls inside native frames.
- No fades for native panels; DPI changes are not passed to docked editors yet.
- Next: (c) all floating panels native, (b) the f116 comparison in the real app, (d) the .flp flag if REA finds it.
