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
