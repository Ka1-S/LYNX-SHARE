# REA round 190oh: FL's wrapper Settings › GUI (P3, plugin window scaling)

Clean-room: captions, hints and flag bits only (strings and saved projects); no FL code or data is copied into LYNX.

## Evidence

| Item | Kind | Source |
|---|---|---|
| The GUI group of the wrapper's Settings tab: "Check properties on display change", "Invalidate editor", "Keep focus", "DPI aware (requires reloading the plugin)" (left column); "Update when hidden", "Accept dropped files", "Scale editor dimensions" (right column, the last one under the others) | DATA | Fruity Wrapper_x64.dll (FL 2026 26.1.7.5654), the `TSynthEditorForm` DFM, `VisualPanel` (caption "GUI") with `CheckPropertiesBox`, `InvalidateEditorBox`, `NoKeyboardFocusBox`, `DPIAwareBox`, `IdleWhenHiddenBox`, `DDPriorityBox`, `ScaleDimensionsBox` |
| Hints: DPI aware "For plugins that appear too big or small"; Scale editor dimensions "Scale dimensions of plugins that appear cut-off on a high resolution screen" | DATA | the same DFM (`Hint` of each box) |
| Defaults for a hosted plugin: Check properties on, Invalidate editor on, Keep focus off, DPI aware **on**, Update when hidden off, Accept dropped files on, Scale editor dimensions **off** | UI | FL 2026, Valhalla Supermassive (VST2) wrapper, Settings tab (`raw/fl-wrapper-settings-default.png` in the round folder) |
| "DPI aware" = wrapper flags bit 0x04000000 (set = on); "Scale editor dimensions" = 0x40000000 (set = on) | DATA | the same project saved three times from FL 2026: as opened (0xB4007D8C), DPI aware unticked (0xB0007D8C), DPI aware ticked again and Scale editor dimensions ticked (0xF4007D8C); record 0x02 of the 0xD5 wrapper blob, version 12 (`fl/b0.flp`, `d1.flp`, `s1.flp`) |
| FL's default word 0xB4007D8C has DPI aware on, Scale editor dimensions off | DATA | LYNX's `.flp` export already writes 0xB4007D8C as FL 2026's default (round 151); every FL 24.2 / 25.2 / 26.1 save in the round folders on E: has 0x04000000 set and 0x40000000 clear (1,865 wrapper records), except this round's own two toggled saves; one FL 20 demo project in FL's Data folder has 0x04000000 clear |
| The wrapper's plugin menu (▸) of a VST plugin has no "Scaling" submenu (it ends Change icon..., Detached) | UI | FL 2026, the same plugin (`raw/fl-wrapper-menu-vst.png`) |
| FL's wrapper imports `SetThreadDpiAwarenessContext`, `SetThreadDpiHostingBehavior`, `GetThreadDpiHostingBehavior`, `GetDpiForMonitor`; the bridge has a `-dpiaware` switch | DATA | the import / string table of Fruity Wrapper_x64.dll |

## What LYNX does (round 190oh)

- `PluginRef::dpiAware` (on) and `PluginRef::scaleEditorDimensions` (off), saved in `.lynx` only when not the default,
  read from and written to the `.flp` wrapper word (version 10+), part of the plugin's equality; "DPI aware" also reloads
  the plugin (`samePluginProcessing`), as FL's caption says.
- Settings tab › **GUI** group with the two boxes (FL's other GUI boxes have no LYNX behaviour and are left out, as on the
  other tabs). Toggling either closes the editor (it opens with the new setting).
- Windows, "DPI aware" off: the plugin is created, and its editor and the window around it are made, in a DPI-unaware
  context (JUCE's `ScopedDPIAwarenessDisabler`, as JUCE's plugin host does): Windows draws the window at 96 dpi and
  stretches it. **Approximation:** the whole window is stretched (LYNX's wrapper header too) and it opens Detached
  (an unaware child inside LYNX's per-monitor-aware docked frames would need mixed DPI hosting for every container
  window; FL calls SetThreadDpiHostingBehavior and probably keeps its header sharp). Linux / macOS: the box is greyed.
- "Scale editor dimensions" on: the size the editor reports is taken in logical pixels (no division by the display
  scale): LYNX's CLAP and VST2 editors (`platform::editorSizeScale`), JUCE's VST3 window (JUCE fix 190oh). In a separate
  process the child does the same (the `showEditor` message carries both settings).
- Not done (LYNX extras of the P3 plan): a "Zoom" submenu for DPI-unaware editors (a bitmap-scaled host, ~1 round); each
  plugin window remembering its monitor and place (windows open centred on the main window's monitor today, so a
  monitor that is gone never hides one).

## Unknowns

- What FL does with a docked unaware editor at 150 % (sharp header or not): needs a display at 150 %, which this PC's
  monitor is not set to (not changed: it is the owner's screen).
- FL 20's meaning of a cleared 0x04000000 (one demo): read the same way as FL 2026 reads it.
