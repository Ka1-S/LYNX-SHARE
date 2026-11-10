# Round 190i: issue #116 leftovers (the .flp Detached flag, the editors macro, native panel fades) and issue #114's test

Earlier steps: `REA_rodada181c.md` (design), `REA_rodada186a.md` §2 (slice 1), `REA_rodada188a.md` (slices 2, 3),
`REA_rodada188j.md` (slice 4). Work files: `E:\lynx-work\iss190i\` (`fl\` projects and window listings, `shots\`,
`scripts\d4.py` and `craft.py`).

## 1. The plugin record's Detached flag (`.flp`) **[EXEC + DATA, FL Studio 2026 v26.1.7, 2026-11-10]**

The plugin record `0xD4` (52 bytes, 13 × u32; `REA_rodada5.md` §1.1, `REA_rodada139.md` B6) holds its flags at +16.
Known so far: `0x1` window open, `0x10` generator, `0x20` Smart disable, `0x40` threaded, `0x100`. PyFLP names bit
`0x4` "Detached" **[DOC]**. Checked here:

| Step | What was done | Result |
|---|---|---|
| a | FL's `Render tests\3OSC.flp` copied; its only `0xD4` has flags `0x51` (`a-docked.flp`) | FL opens the 3x Osc window as a **child** of the workspace (`TPluginForm`, style `54000000`, parent = the `TWPControl` desktop): docked. `wins-fl-a-docked.txt`, `shots\fl-a-docked.png` |
| b | The same file with only that u32 changed to `0x55` (`b-detached.flp`, `craft.py`) | FL opens it as a **top-level window owned by the main window** (`TPluginForm`, style `94010000`, owner = `TFruityLoopsMainForm`): detached. `wins-fl-b-detached.txt`, `shots\fl-b-detached.png` |
| c | In (a): Tools › Macros › **Switch all plugin editors to › Detached**, then Ctrl+S | The window becomes the owned top-level one (`wins-fl2-03.txt`); the saved record: `0x51` → **`0x55`**, and the rect at +36 changes from `10 80` (inside the desktop) to `e1 d2` (the screen). `a-after-detached-macro.flp` |
| d | Then **… › Attached**, Ctrl+S | Child window again (`wins-fl2-07.txt`); the record is back to `0x51`, rect `10 80`. `a-after-attached-macro.flp` |
| e | A copy of the Breakbeat template (48 plugin records, one window open): **… › Detached**, Ctrl+S | **Every** record gets `0x4`: `0x50`→`0x54` (7 samplers), `0x60`→`0x64`, `0x70`→`0x74`, `0x140`→`0x144` (7), `0x160`→`0x164` (28); the two that had it keep it. So the macro sets the flag of all plugins, open or not, generators and effects, FL's own and wrapped ones. `c-orig.flp` / `c-many.flp` |

Corpus (FL's 165 demo and template projects, `d4.py`): 310 of the plugin records carry `0x4` (`0x54`, `0x64`, `0x74`,
`0x144`, `0x154`, `0x164`, `0x165`, `0x174` …) **[DATA]**.

No FL setting was changed. FL was started by `Start-Process` under both locks and closed by its PID; input: three
menu clicks and Ctrl+S per run, each after `safeinput.ps1`; no new line in the input wall's log.

### LYNX
- Import: a hosted plugin (a "Fruity Wrapper" channel or mixer slot) takes `PluginRef::detached` from bit `0x4` of its
  own record (`core/FlWrapper.h` `flDetached`).
- Export: a hosted plugin's record gets `0x4` when `PluginRef::detached` is set and loses it when it is not, whatever
  the imported flags were (`withFlDetached`); FL's other flags of an imported channel stay as they were
  (`ChannelDef::flWrapperFlags`). Channels that are not hosted plugins keep the flags they were imported with: LYNX's
  own editors have no Detached state to set the bit from.
- Not read: the window's place and size at +36…+51 (docked: inside FL's desktop; detached: on the screen).
- Test: `issue116_flp_detached_flag_round_trips` (`tests/Issue116DetachedTests.inc`).

## 2. Best way?
- **FL:** above. Detached is per plugin, saved in the project, and travels in the `.flp`.
- **Ableton:** Live's plugin windows always float (`REA_rodada188a.md` §3); a Live set stores no such choice, so there
  is nothing to map in `.als`.
- **Users:** the requests are about detached as a default and a quick way to detach everything (forum threads in
  `REA_rodada188a.md` §3): FL's own answers are "Detach all new plugins" (slice 3) and the macro of step (c).
- **Verdict:** FL-exact. A project detached in FL opens detached in LYNX and the other way round.

## 3. Tools › Macros › Switch all plugin editors to › Attached / Detached **[STR + EXEC]**
- FL's main menu (`SwitchAllPluginsToMenu`, caption "Switch all plugin editors to", with `AttachAllPluginsMenu` and
  `DetachAllPluginsMenu` → `MacroSwitchEditorsToAttachedAction` / `MacroSwitchEditorsToDetachedAction`, captions
  "Attached" / "Detached") **[STR, FLEngine's form data]**. In FL 2026's Tools menu it sits in Macros after "Delete
  unused files and save project" and before "Set keyboard focus for all plugins to" (`shots\fl2-01-tools.png`,
  `fl2-02-switch.png`); the two items are plain actions, not ticked.
- What it does: §1 steps c, d, e — every plugin's flag is set (open or closed), and an open editor moves to the other
  host at once, at the same place on the screen.
- LYNX: the same submenu at the same place of Tools › Macros (shown while docking is on). `setAllPluginsDetached`
  (`core/Song.h`) sets every hosted plugin's flag in one edit; the open editors that are in the other host open again
  where they belong (`PluginWindows::setAllDetached`); plugins that can't dock (another process, the 32-bit bridge)
  stay detached. Core test `switch_all_plugin_editors_sets_every_hosted_plugins_detached_flag`; `--plugwintest`
  `macro_detached_the_open_editor`, `macro_detached_saved`, then `docked_again`.
- Difference from FL: FL's macro also covers its own plugins' windows; LYNX's own editors have no Detached state yet,
  so the macro reaches hosted plugins only.

## 4. Native panels fade when opened or closed (LYNX's own, round 179a)
- FL has no panel fade (`REA_rodada179a.md`: frames 82–91 ms apart, AnimateWindow only for hints), so this is LYNX's
  extra, behind Settings › General › Animations as before. Since slice 2 the Playlist, Channel rack, Piano roll and
  Mixer are native child windows and showed and hid at once (`REA_rodada188a.md` §4, "Left open").
- Now (`Workspace::NativeFader`): the window's own alpha moves over `ui::motion::panelFadeMs` (110 ms). JUCE turns an
  opaque window with alpha < 1 into a layered window with a constant alpha and back at 1, so the system blends the
  panel over what is behind it, a docked plugin editor included. An opening panel is shown at alpha 0 and rises; a
  closing one stays shown while it falls and is hidden at the end; a panel called back in the middle turns round from
  the alpha it has. A layout change without a fade (another panel moved, Animations off) ends a fade at once. The
  60 Hz timer runs only during a fade.
- Not faded, as before: docked plugin windows (FL's and LYNX's open at once), detached panels, maximising.
- `--plugwintest` dock mode (each fade stretched to half a step so its middle can be read and shot):
  `mixer_starts_fading_out`, `mixer_half_faded_out` (alpha between 0.1 and 0.9, still shown, the window layered),
  `mixer_hidden_after_fade`, `mixer_starts_fading_in`, `mixer_half_faded_in`, `mixer_opaque_after_fade` (alpha 1, not
  layered, above the plugin), `mixer_turns_round_mid_fade`, `mixer_shown_after_turning_round`,
  `no_fade_out_without_animations`, `no_fade_in_without_animations`.

### Best way? (fades)
- **FL:** no fade. **Ableton:** Live's views switch at once too. **Users:** round 179a's notes (the fade is short and
  can be switched off). **Verdict:** keep LYNX's 110 ms fade for native panels exactly as it was for plain ones; no new
  setting.

## 5. Not built: FL's "Open detached plugins with the FL Studio window"
General › Miscellaneous checkbox `DetachedPlugsOnMainScreenBox`, hint "Open detached plugins on the same screen as the
FL Studio window" **[STR]**: a multi-monitor placement rule for detached plugin windows. LYNX opens a detached plugin
window on the main window's display already (it is owned by the main window and placed from it), which is this
option switched on. FL's default and what "off" does (the plugin's last screen?) were not driven. Needs a decision:
recommended is to leave it out until someone with two monitors asks.
