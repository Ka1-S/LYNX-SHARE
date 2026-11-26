# REA round 190rv: FL 2026 effect window strip (issue #321), old Sytrus / Ogun preset states (issue #288)

Live observation of FL Studio 2026 on this PC (no binary analysis this round) and a data scan of FL's shipped presets.
Clean-room: facts in my own words; pictures are LYNX's work folder only (`/mnt/e/lynx-work/bugs032_190rv/raw/`, FL's
title bar there shows the licence owner, so those frames stay local).

## 1. An effect's window (issue #321) [EXEC, FL 2026, 2026-11-26]

Empty template, Mixer › Master › Slot 1 › Dynamics › Fruity Limiter; the window opened by itself.

- A fresh effect window shows only FL's title row (▸ menu arrow, the plugin's gear icon, "Fruity Limiter (Master)",
  Presets ◀ ▶, minimise / maximise / close) above the plugin's own panel (`fl7c.png`).
- The gear icon in the title is a toggle; its hint is **"Detailed settings"** (`fl8hc.png`). On, a strip appears between
  the title and the panel (`fl8c.png`): the plug icon (the plugin's own page) at the left and, at the right end,
  **ON** (an LED with the caption under it) and **MIX LEVEL** (a knob with an orange ring, RGB 255 168 48, caption
  under it). Measured at 100 %: ON's LED centre 65 px and MIX LEVEL's knob centre 30 px from the window's right edge.
- Hints: ON = **"Mute / solo"** (`fl9onc.png`), MIX LEVEL = **"Mix level: 100%"** (`fl9mixc.png`).
- ON is the mixer slot's LED: clicking it turned the Master's slot 1 LED off in the Mixer and the plugin's panel went
  to its paused look (`fl10offc.png`); clicking again turned both on.
- The choice is kept for that window: closed and opened again from the slot, the strip was still there (`fl11rec.png`).
- Earlier references show the same strip on FL's other effects when it is on (EQUO, Fruity Convolver:
  `/mnt/e/lynx-work/audit173d3/shots/fl-equo-main.png`, `fl-conv-main.png`) and not when it is off (Vocodex,
  `fl-vdx-main.png`).

LYNX (round 190rv): a hosted (VST3 / CLAP / LV2 / VST2) effect's wrapper strip, which LYNX always shows, gets ON and
MIX LEVEL at FL's places (`ui/ChannelSettings.cpp` EffectHeaderControls); ON is the Mixer slot LED's edit
(`pluginheader::clickLed`: click on/off, Ctrl / Alt+click solo, Shift+click lock), MIX LEVEL the slot's mix (an
automation target). LYNX's own effect windows keep their header LED as the ON switch (issue #317 made it clickable);
FL's "Detailed settings" strip for them is not built this round.

## 2. Sytrus and Ogun preset states (issue #288) [DATA]

`Data/Patches/Plugin presets/Generators/<plugin>`, the version word at the start of the plugin state (event 0xD5),
`tools` of the round (`fstver.py`, `sytold.py` in the round folder):

| Sytrus state version | 5 | 6 | 7 | 12 | 13 | 14 | 15 | 17 | 18 | 19 | 22 | 23 | 24 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| presets | 200 | 15 | 36 | 6 | 91 | 42 | 22 | 54 | 34 | 53 | 55 | 28 | 151 | 197 |

LYNX reads versions ≥ 17 (572). A parser that follows REA_rodada11 §1.2's per-version rules (main block, flags 0x4F4..,
no reverb below 10, three-int EQ bands up to 13, the RTF info text below 23, the operator block of 0x428 bytes for
8–16 and 0xE7 below 8) reads **all 161 presets of versions 12–15 exactly to the end of their stream**. Versions 5–7
(251 presets) get through the six operators and then fail at the first filter unit: no size of the filter block
(0x20–0x3F), with or without the waveshaper curve and the trailing byte, and 6 / 7 / 8 articulators, fits. Their filter
and articulator layout needs a reading of Sytrus' loader first.

Ogun state versions: 0 (18), 1 (8), 4 (7), 7 (11), 8 (14), 9 (18), 11 (13), 12 (4), 13 (2), 14 (128), 18 (6).

Size: versions 12–15 need the parse plus the operator blocks widened to the 0x828 layout (fewer harmonics, padded),
then a sound check against FL's renders; versions 5–7 and Ogun's old states need new REA first. Not a small fix:
planned for 0.1.33.
