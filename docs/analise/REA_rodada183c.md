# Round 183c: Ableton-style Racks (Instrument, Audio Effect, MIDI Effect) with chains, zones, 16 macros and variations

Date: 2026-11-08. Base: `origin/main` bbb948055. Gap map item #3 of round 182f
(`/mnt/e/lynx-work/ableton182f/GAPMAP.md` §3 #3, §2.5 D1–D2, §2.9, decisions §5 D6).

Clean room: Live's behaviour comes from its published manual, the XML of Live's own sets and rack presets (read on this
machine, never copied into LYNX or shipped), the class names in Live's binaries, and measured renders. No Live code,
art or preset goes into LYNX. Third-party plugins were not touched.

## 0. Sources and tags
| Tag | Meaning |
|---|---|
| **[MAN]** | Live 12 manual, chapter 25 "Instrument, Drum and Effect Racks" (https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/, read 2026-11-08; text kept locally in `/mnt/e/lynx-work/rack183c/web/racks.txt`) |
| **[DATA]** | XML of Live 12.4's own files on this machine: the demo set `Chuck Sutton - Patience (Live 12 Intro Demo).als` (23 Audio Effect Racks, 8 Instrument Racks, 1 MIDI Effect Rack, 1 Drum Rack) and the Core Library rack presets (`Core Library/Racks`: 148 Audio Effect, 737 Instrument, 19 MIDI Effect, 145 Drum Racks, 1072 `.adg` files). Scripts: `/mnt/e/lynx-work/rack183c/scripts/` |
| **[BIN]** | Class and string names in `Ableton Live 12 Trial.exe` (12.4.16; round 175a's string tables) and in `Ableton Live Engine.dll` (28 MB, the audio engine; RTTI names read with `strings` and REA, see §7) |
| **[SHOT]** | Screenshots of Live 12.4.16 Trial on this machine (`/mnt/e/lynx-work/rack183c/live/`; also round 183a's `/mnt/e/lynx-work/detail183a/live/`) |
| **[RENDER]** | Live renders measured against LYNX (planned, §8) |
| **[CODE]** | LYNX source at bbb948055 (`src/…:line`) |
| **[WEB]** | User opinion, with the URL |

Every claim is marked **PROVEN** (seen in the sources above) or **INFERRED** (reasoned, not yet seen).

## 1. What a Rack is in Live
| Claim | Source | Status |
|---|---|---|
| Four rack kinds: Instrument, Drum, Audio Effect, MIDI Effect. MIDI Effect Racks hold only MIDI effects and sit only on MIDI tracks; Audio Effect Racks hold only audio effects; an Instrument Rack chain is MIDI effects, then one instrument, then audio effects, in that order | [MAN] 25.2 | PROVEN |
| In Instrument and Effect Racks every chain gets the same input at the same time, processes it serially, and the chains' outputs are summed into the rack's output | [MAN] 25.1.1 | PROVEN |
| A rack behaves as a single device in the chain; racks nest without limit ("a Rack within a Rack") | [MAN] 25.1.1, 25.2 | PROVEN |
| Group (selected devices' title bars → context menu Group / Group to Drum Rack) creates a rack; Ungroup dismantles it | [MAN] 25.2 | PROVEN |
| Views of a rack, from a view column on its left: Macro Controls, Chain List, Devices (Drum Racks also Pad View); all hidden = the rack folds | [MAN] 25.3; [DATA] `AreMacroControlsVisible`, `IsBranchesListVisible`, `AreDevicesVisible`, `IsRangesEditorVisible`, `AreMacroVariationsControlsVisible`, `IsFolded` | PROVEN |
| Engine classes: `OBranchInProcessor`, `OBranchOutProcessor`, `OBranchOutSelectorRangeProcessor`, `OCrossFadeVolumeProcessor`, `OCrossfadeState` (Engine DLL); `AGroupDeviceBranch`, `AMacroVariations`, `AMacroSnapshot`, `AGroupMacroPoti`, `ABranchMixerDevice`, `AAudioBranchMixerDevice`, `MacroMapping`, `MapModeMacro` (exe) | [BIN] | PROVEN (names only) |
| A chain's input filter (zones) and its output gain stage (selector fade) are separate engine steps (`BranchIn` / `BranchOutSelectorRange` processors) | [BIN] class names | INFERRED |

## 2. Chains
| Claim | Source | Status |
|---|---|---|
| Each chain has an Activator (on/off), Solo and Hot-Swap; Instrument, Drum and Audio Effect Rack chains also have volume and pan | [MAN] 25.4 | PROVEN |
| MIDI Effect Rack chains have no volume and pan (they carry MIDI) | [MAN] 25.4 ("Chains in Instrument, Drum and Audio Effect Racks also have…"); [DATA] `MidiEffectBranch` has a `MidiBranchMixerDevice` | INFERRED from the manual's wording; to check in the shot |
| Chain XML: `Branches/<Kind>Branch` with `Name`, `IsSoloed`, `Color`, `DeviceChain/<…>DeviceChain/Devices`, `MixerDevice` (`Speaker` = activator, `Volume` linear gain 0.000316…1.995 = −70…+6 dB, `Panorama` −1…1), `BranchSelectorRange`, and for Instrument / MIDI branches `ZoneSettings/KeyRange`, `ZoneSettings/VelocityRange` | [DATA] demo set, 29 audio branches, 8 instrument branches | PROVEN |
| In `.adg` presets the chains are stored apart, as `BranchPresets/<Kind>BranchPreset/DevicePresets` (a different layout from the inline `.als`) | [DATA] 1072 presets | PROVEN |
| Chain list: selection picks the chain shown in the Devices view; multi-selection; drop area below adds chains; rename, info text, colour; chains save as presets | [MAN] 25.4 | PROVEN |
| Auto Select: the chains currently processing signal become selected (with zones) | [MAN] 25.4.1; [DATA] `IsAutoSelectEnabled` false in 1632 of 1636 Instrument Racks, true in 167 of 168 Drum Racks | PROVEN |
| Solo: soloing one chain silences the non-soloed chains; several chains can be soloed | INFERRED (common mixer behaviour; the manual names the button only). To check in the session | INFERRED |

## 3. Zones
| Claim | Source | Status |
|---|---|---|
| Zones are filters at each chain's input. Instrument and MIDI Effect Racks: key, velocity and chain-select zones. Audio Effect Racks: chain-select only. Drum Racks: none (Receive note instead) | [MAN] 25.5 | PROVEN |
| A note reaches a chain only if it passes all three zones | [MAN] 25.5.1 | PROVEN |
| Ranges: key 0–127, velocity 1–127, chain select 0–127; default chain-select zone is the single value 0 (length 1); default key 0–127, velocity 1–127 | [MAN] 25.5.2–4; [DATA] defaults `KeyRange 0 127 0 127`, `VelocityRange 1 127 1 127`, `BranchSelectorRange 0 0 0 0` | PROVEN |
| **Fade layout:** each range is `Min`, `Max`, `CrossfadeMin`, `CrossfadeMax`, always with `Min ≤ CrossfadeMin ≤ CrossfadeMax ≤ Max`. The chain is at full level on `[CrossfadeMin, CrossfadeMax]`, fades in over `[Min, CrossfadeMin]` and out over `[CrossfadeMax, Max]`, and is closed outside `[Min, Max]`. Example: a two-layer velocity crossfade is `1 80 1 56` + `56 127 80 127` | [DATA] every one of the ~600 non-default ranges in the Core Library satisfies the ordering (`scripts/` table, §7) | PROVEN |
| Key and velocity fades attenuate the **velocity** of notes entering the chain | [MAN] 25.5.2–3 | PROVEN |
| Chain-select fades: in MIDI Effect Racks they attenuate note velocity; in Instrument and Audio Effect Racks they attenuate the **chain's output volume** | [MAN] 25.5.4 | PROVEN |
| In Instrument and Audio Effect Racks, a selector outside a zone that **ends in a fade** silences the chain's output; outside a zone with **no fade** the output is not attenuated (tails ring out), only new notes are filtered | [MAN] 25.5.4 | PROVEN |
| Chain-select zones filter only notes by default; "Chain Selector Filters MIDI Ctrl" filters all MIDI events | [MAN] 25.5.4; [DATA] `ChainSelectorFilterMidiCtrl` (false in all 1839 library racks) | PROVEN |
| The chain selector is a rack parameter `ChainSelector/Manual` (0–127) and can itself be mapped to a macro (44 Core Library Audio Effect Racks do so) | [DATA] | PROVEN |
| Fade **curve shape** (linear gain, equal power, dB-linear; linear or not in velocity) | — | **unknown**: measured in a Live render before slice (a) lands (§8) |
| A note-off goes to the chain that got the note-on even if the selector moved in between | INFERRED (otherwise notes would hang). To check with the render set | INFERRED |
| `RangeTypeIndex` 0 / 1 / 2 = which zone editor is shown (key / velocity / chain) | [DATA] values {0: 16, 1: 1797, 2: 26} | INFERRED (meaning) |

## 4. Macro Controls
| Claim | Source | Status |
|---|---|---|
| Up to 16 macros per rack; 8 shown on a new rack; the shown count is saved | [MAN] 25.7 | PROVEN |
| Shown counts in the library are 1, 2, 4, 6, 8, 10, 12, 14, 16 | [DATA] `NumVisibleMacroControls` | PROVEN (values); the +/− step of the selector buttons: INFERRED (2) |
| A macro value is a float 0–127 (`MacroControls.N/Manual`, e.g. 92.067688) | [DATA] | PROVEN |
| Per macro: display name (`MacroDisplayNames.N`, default "Macro N"), colour (`MacroColor.N`, −1 = default), default value (`MacroDefaults.N`, −1 = none), info text, MIDI map | [DATA]; [MAN] 25.7.1 | PROVEN |
| **Mapping storage:** the mapping lives on the *target* parameter, as `KeyMidi` with `Channel = 16`, `NoteOrController = macro index` (0-based), plus `MidiControllerRange Min / Max` in the target's own units. Min > Max = an inverted mapping | [DATA] e.g. `Knob 1 Fade to Grey.adg`: EQ Three GainMid 1→0.49, FreqLo 50→2034 Hz, Delay Feedback 0→0.95; [MAN] 25.7.1 "Inverted mappings… Min… greater than… Max" | PROVEN |
| **Mapping law:** the macro moves the target **linearly in the target's own normalised control range** between Min and Max (not linearly in the displayed unit) | [DATA] 12,465 mid-position mappings in the Core Library whose stored parameter values were compared with the macro values: linear-scale parameters (Amp Bass 0–10, pan, 0–1 sends) sit exactly at `Min + (Max − Min)·m/127`; tapered ones (gate threshold, release ms, gain) don't, consistently with a taper (§7) | PROVEN for linear parameters; the taper for tapered ones is each device's own |
| A macro can drive many parameters; a parameter belongs to at most one macro (one `KeyMidi`) | [MAN] 25.7.1; [DATA] | PROVEN |
| With one mapping, the macro takes the parameter's name and units; with several, the generic name and a 0–127 scale (unless all share unit and range) | [MAN] 25.7.1 | PROVEN |
| A mapped parameter appears disabled in its device; clip envelopes can still modulate it | [MAN] 25.7.1 | PROVEN |
| Map mode: Map button → mappable parameters highlighted, a Map button under each macro, the Mapping Browser (Min/Max sliders, Invert Range, delete) | [MAN] 25.7.1; [BIN] `sMenuEditMacroMap`, `sMenuRemoveMacroMapping`, `MacroRangeRuler` | PROVEN |
| A macro of a nested rack can be mapped to a macro of the outer rack | [DATA] `MacroControls.N` carrying `KeyMidi Channel 16` inside an inner rack (Basic Heavy Guitar Amp, Cabinet Mic Mixer) | PROVEN |
| **Randomize** ("Rand" in the title bar) randomises the values of all *mapped* macros; "Exclude Macro from Randomization" per macro; macros mapped to Volume in Instrument Rack presets are excluded by default | [MAN] 25.7.2; [DATA] `ExcludeMacroFromRandomization.N` true in 283 Instrument Racks (one macro) | PROVEN |
| The random distribution (uniform over 0–127?) | — | INFERRED (uniform); check in the session (§8) |

## 5. Macro Variations
| Claim | Source | Status |
|---|---|---|
| A variation is a stored set of macro values; New stores the current state, named "Variation 1", "Variation 2"…; rename, duplicate, delete; Launch recalls; Overwrite replaces | [MAN] 25.7.3; [BIN] `sManeuverRenameVariation`, `sManeuverDuplicateVariation`, `sManeuverReorderVariation` | PROVEN |
| "Exclude Macro From Variations" keeps a macro unchanged when a variation launches | [MAN] 25.7.3; [DATA] `ExcludeMacroFromSnapshots.N` | PROVEN |
| XML: `MacroVariations/MacroSnapshots/MacroSnapshot Id` with `SnapshotName`, `AutogeneratedNameIndex`, `MacroValues.0–15` and `MacroHasValue.0–15`; a macro without a value has `MacroValues = −1` and `MacroHasValue = false` (11,375 of 11,375 cases) | [DATA] 379 racks with variations, up to 10 each | PROVEN |
| Launching a variation leaves macros with `MacroHasValue = false` (and excluded macros) unchanged | INFERRED from the format; check in the session | INFERRED |
| Which macros get a value at store time (mapped only, or all shown) | [DATA] the stored count follows the mapped macros in the cases read (Arp Variations: 4 mapped, 4 stored) | INFERRED |
| Variations are recalled instantly (no glide, no morph) | [MAN] 25.7.3 ("make instant jumps"); [WEB] users ask for morphing (GAPMAP #3) | PROVEN (no morph in Live) |

## 6. LYNX today (the reuse check)
| LYNX piece | What it gives a Rack | Source |
|---|---|---|
| **Patcher graph** `patcher::Graph` | Plugin and built-in effect nodes, generator (instrument) nodes, VFX note nodes, a Layer node, audio cables with gain, MIDI cables, control cables with a value curve, the Surface node (control outputs from knobs), published parameters (automation targets), **delay compensation across parallel branches**, nested Patchers, value-only updates without rebuilding (`sameStructure` / `takeValues`), running nodes kept across rebuilds | [CODE] `src/core/Patcher.h:1–301`, `Patcher.cpp:176` |
| Patcher in a mixer slot and as a Channel Rack generator | The two homes a Rack needs (`EffectKind::patcher`, `InstrumentKind::patcher`), with project save/load, undo, FL import/export, plugin state capture, editors | [CODE] `Song.h:563`, `Patcher.h:271–276` |
| `parallel` container (`EffectBranch`, `EffectLink`) | Parallel effect branches, but **no plugins** (`Effects.cpp:2171`), no latency alignment (`:2228`), no instruments | [CODE] `Effects.cpp:2159` |
| Layer channel | Key split, random, crossfade, round robin for whole channels; no zones with fades | [CODE] `Song.h:660`, `src/core/Layer.h` |
| Patcher Surface / Control Surface / Dashboard | FL's way to build macro panels; no variations, no randomise, no map mode | [CODE] `Song.h:502`, `src/core/ControlSurface.h` |
| .als import | An Instrument Rack keeps only its first chain's instrument; Audio Effect Racks and MIDI Effect Racks are dropped and listed; plugins on a track are carried over | [CODE] `AlsImport.cpp:401–412`, `:420–444` |
| .als export | Plugins export as `PluginDevice`; LYNX built-ins are listed as not exported | [CODE] `AlsExport.cpp:414–430` |

**Conclusion:** the Patcher graph already is a parallel-chain engine with plugins, instruments, MIDI effects, control
mapping and PDC. A Rack does not need a new graph: it is a fixed, simpler model that **compiles to a Patcher document**
and runs on `patcher::Graph`. Four small graph additions are needed (§9.2).

## 7. Method log
- **XML.** `scripts/rackschema.py` (element paths of each rack kind), `macromaps.py` / `mapcurve.py` (macro mappings
  and the mapping law), `scanracks.py` / `rackstats.py` (zones, variations, field statistics: `rea/rackstats.txt`).
  The XML was read from Live's own files in place; local copies are in `/mnt/e/lynx-work/rack183c/xml/` (not shipped).
- **REA.** 04:36–04:56 under `~/.cache/lynx-ghidra.lock`: `open_binary` on `Ableton Live Engine.dll` (sha256
  a2bf3e05…); one `search_strings` call found `OCrossFadeVolumeProcessor` (0x1819d1a48), `OBranchInProcessor`
  (0x1819e1b00), `OBranchOutSelectorRangeProcessor` (0x1819e1b30). Ghidra's auto-analysis took ~25 min, longer than the
  20-min hold; the binary was closed at 05:06 (reported to the coordinator). Further REA on this DLL only if the renders
  can't answer a question, with a ~45-min hold agreed first.
- **Live session.** §8.

## 8. Live session and renders
**Session 1 (shots), 2026-11-08 05:18–05:26**, under `lynx-wingui.lock` then `lynx-fl.lock` (holder pid 146550), idle
gate passed at 05:19:20 and 05:22:06. Set: a local copy of Live's own demo set with track 10's two-chain Audio Effect
Rack ("Articulator") made visible in every view (`scripts/mkshotset.py`; never shipped). Inputs, each after a fresh shot
and `safeinput.ps1` on Live's PID, wall log clean after each: "No" on Live's crash-recovery question (the crashed set
was round 182b's test export `T2-asher`), OK on the trial info box ("Saving and exporting were successfully
activated"), a click on the "Vocal Main" track header, Map on, Map off, and "No" to "Save changes". Live exited by
itself at 05:25:55. Live's `Preferences.cfg` differs after the session (Live rewrites it on exit: recent sets and window
state); no setting was changed through Live's UI, nothing restored. Shots: `live/ref-live-rack-deviceview.png`,
`live/ref-live-rack-mapmode.png`, `live/ref-live-mapping-browser.png`, full desktops `s05`–`s10`.

| Claim (seen in the shots) | Status |
|---|---|
| Rack title bar: on/off LED, name, **Rand** and **Map** buttons (Map lit green while on), then hot-swap, save-preset and menu icons | PROVEN [SHOT] |
| View column on the rack's left: icons for macros, chain list, devices and variations, plus **+ / −** for the number of shown macros | PROVEN [SHOT] |
| Variations panel (left of the macros): **New** button; one row per variation with a store (overwrite) icon on the left, the name, and a launch ▶ on the right | PROVEN [SHOT] |
| Macros: a grid of 2 rows × 4 for 8 shown; a mapped macro shows its name on a coloured plate and its value in the target's units ("−9.637 dB", "3.43", "86"); unmapped ones are grey "Macro 5…8" with 0 | PROVEN [SHOT] |
| Chain list row: name on the chain colour, volume (dB field), pan ("C"), activator (speaker), **S** (solo), hot-swap; "Drop Audio Effects Here" below the rows | PROVEN [SHOT] |
| Zone editor right of the chain list, opened by **Chain** (Audio Effect Rack: the only zone type) with **Hide**; ruler 0–127 every 8; the chain selector is a cyan marker on the ruler; each chain's zone is a blue bar, its fades drawn as a thin line ramping to the bar | PROVEN [SHOT] |
| The selected chain's devices follow the rack body, inside the rack's closing bracket | PROVEN [SHOT] |
| Map mode: a **Map** button under every macro; the browser area becomes "Macro Mappings" with columns Macro, Path ("Vocoder \| Mixer"), Name ("Chain Volume", "Lower Filter Band"), Min, Max; a chain's own volume is mappable ("Sibilance" → Chain Volume, −∞…0 dB) | PROVEN [SHOT] |

**Session 2 (renders), 2026-11-08 05:38–05:57**, same locks and gates (holders 179036, 233529). Set:
`scripts/mkmeasure.py` → `live/rackmeasure Project/rackmeasure.als` (MIDI tracks: Simpler playing a 1 kHz sine, −12 dBFS,
into the rack under test; built from Live-saved sets on this machine, never shipped). Inputs: OK on the trial info box,
Ctrl+Shift+R, "All Individual Tracks", Export, the file name, Enter (each after a fresh shot; wall log clean). Exported
48 kHz / 32-bit float, no normalise. Pass 1 (05:40–05:45): a first try crashed Live on load (an automation envelope of
the base set pointed at a renumbered id: my set's error, fixed); the pass that rendered had the demo rack's own macro
mapping (macro → chain 1 volume) left in, which silenced chain 1, so the Audio Effect Rack tracks were void (kept in
`live/renders1/`). Pass 2 (05:51–05:53) with the mappings stripped: `live/renders/`, numbers in
`live/renders/results.txt` (`scripts/analyse.py`). Live closed by itself (no changes to save).

| Claim | Evidence | Status |
|---|---|---|
| **Key and velocity fades scale the note's velocity linearly, counting both ends**: in a fade-in, `v' = v · (x − lo + 1) / (fadeLo − lo + 1)`; in a fade-out, `v' = v · (hi − x + 1) / (hi − fadeHi + 1)` (x = the key or the velocity) | velocity zone 1–127 full from 80: v 10/20/40/60/70 → 1.25 / 5.00 / 20.00 / 45.00 / 61.25 (formula: identical); fade-out full to 40: v 50/100/127 → 44.3 / 31.8 / 1.4 (formula 44.32 / 31.82 / 1.44); key zone 48–127 full from 60: keys 48…58 → 9.72 / 29.15 / 48.60 / 68.06 / 87.92 / 107.45 (formula 9.77 / 29.31 / 48.85 / 68.38 / 87.92 / 107.46) | PROVEN [RENDER] |
| The scaled velocity is not rounded, and a note inside the zone always plays, at least at velocity 1 | v 10 → 1.25; v 1 at the zone's first step → 1 | PROVEN [RENDER] |
| **Chain-select output fade** (Audio Effect and Instrument Racks): `gain = sin (π/2 · u)` with u the same linear fade as above | zone 20–127 full from 60, selector 20/25/30/40/50/55 → 0.0383 / 0.2279 / 0.4091 / 0.7205 / 0.9275 / 0.9817 (formula 0.0383 / 0.2279 / 0.4091 / 0.7205 / 0.9275 / 0.9817); fade-out 0–100 full to 40, selector 55/70/100 → 0.9263 / 0.7162 / 0.0257 (formula 0.9263 / 0.7162 / 0.0257); the Instrument Rack gives the same ratios (0.0383 / 0.4091 / 0.7205 / 0.9275) | PROVEN [RENDER] |
| Outside a zone the chain is silent (selector outside a zone with no fade: no output from a steady input; Instrument Rack: a note started there never sounds) | A-nofade-out, D-irsel-out-nofade: silence | PROVEN [RENDER] |
| That a zone with no fade lets a chain's tails ring out | [MAN] 25.5.4 | INFERRED (manual; not measurable with a steady input) |
| **Chain volume** `MixerDevice/Volume` is a linear gain (0.5012 → −6.00 dB, 0.1 → −20.00 dB) | E-vol | PROVEN [RENDER] |
| **Chain pan** is equal power with unity at the centre: `L = √2 · cos ((p + 1) π/4)`, `R = √2 · sin ((p + 1) π/4)` (−1: +3.01 dB / silent; −0.5: +2.32 / −5.33 dB) | E-pan ±0.5, ±1 | PROVEN [RENDER] (a mono-identical source; per-side gains on a stereo source: INFERRED) |
| Chains sum (two dry chains: +6.02 dB); a soloed chain is the only one heard | F-nosolo, F-solo | PROVEN [RENDER] |
| The chain volume's control taper (the 0..1 a macro moves): 40 dB per unit above −18 dB, 0 dB at 0.85, +6 dB at 1; below −18 dB a power law | [DATA] 440 + 60 stored mapping values (§4) | PROVEN above −18 dB; the fit below (within 1 dB): INFERRED |

All three laws are in `core/Rack.cpp` (`fadeGain`, `selectOutputGain`, `panGains`, `volumeGain`) with these values as tests.

## 9. Design (approved by the coordinator 2026-11-08 with R1 and R2 as recommended)
### 9.1 Where a Rack lives (add, never remove)
- **Audio Effect Rack** = a mixer insert slot whose effect is a Patcher in *rack mode* (`EffectKind::patcher` with
  `PatcherDoc::rack`). It shows in the Mixer slot list like any effect, opens in its own window in Pattern workflow, and
  is a card in round 183a's Device View (`DetailView::effectCardFactory`).
- **Instrument Rack** = a Channel Rack generator, `InstrumentKind::patcher` in rack mode. It plays through its mixer
  insert like any channel; in the Device View it takes the instrument card's place (`instrumentCardFactory`).
- **MIDI Effect Rack**: (a) the MIDI section at the head of an Instrument Rack chain (VFX nodes and MIDI-effect plugins),
  and (b) **decision R1 (approved)**: standalone, as a generator that plays its child channels, like FL's Layer ("Set
  children"). The model holds the children (`RackDoc::children`) and the compiled chains end on "To LYNX" MIDI port 0;
  routing that port to the children is the engine step still open, so the add menus don't offer the standalone MIDI
  Effect Rack yet.
- No new `EffectKind` / `InstrumentKind`: save/load, undo, plugin state, automation, FL import/export and the Mixer
  already handle Patchers. A plain Patcher stays FL's Patcher; rack mode only exists when created as a Rack.

### 9.2 Engine: a Rack compiles to a Patcher graph
`core/Rack.h` (model, no JUCE) + `core/Rack.cpp` (`rack::compile (const RackDoc&) → PatcherDoc`), derived on every edit,
never saved (the RackDoc is saved). The running graph is the existing `patcher::Graph`:
- chain *k*: "From LYNX" (audio, or notes through the chain's zone) → the chain's devices in series → a Utility node
  (pan = Balance, chain-select fade = Gain driven by a control cable) → "To LYNX" with the chain volume as the cable gain;
  mute = cable off; solo = the other chains' cables off. Parallel chains are delay-compensated by the graph.
- macros = 16 Surface knobs; each mapping = a control cable from the macro's output to the target parameter with the
  curve `{min, max}` (Live's law, §4: linear in the target's normalised range; min > max inverts). The chain selector
  is Surface output 16; mapped to a macro, its curve is composed into the cables that read it.
- automation: macros 1–16 and the selector are the Patcher's published parameters 0–16 (automation clips, Event editor,
  controllers, the Device View's knobs: all existing paths).
Graph additions (small, each with tests): (1) a published parameter may target a Surface control (one automation lane
fans out to all of a macro's mappings); (2) a MIDI cable may carry a note zone (key, velocity, chain select, each with
fades; velocity scaled in fades; a note-off follows its note-on); (3) a control cable into a nested Patcher node calls
its `setParameter` (an outer macro drives an inner rack's macro); (4) nothing else: PDC, plugins, VFX, nesting exist.

### 9.3 Rack model (slice a)
`RackDoc { kind (instrument / audioEffect / midiEffect); chains[]; macros[16]; shownMacros (1,2,4…16); selector 0–127;
selectorFiltersCtrl; variations[]; morph (LYNX extra, off); view flags }`.
`RackChain { name, colour, active, solo, volume (linear, −70…+6 dB), pan, keyZone, velocityZone, selectZone (each
lo/hi/fadeLo/fadeHi, Live's layout §3), midiDevices[], instrument (Instrument Rack), devices[] }`.
`RackMacro { name, colour, value 0–127 (float), defaultValue, excludeFromRandom, excludeFromVariations,
mappings[] { chain, device path, param, min, max } }`. `RackVariation { name, values[16], has[16] }`.
Commands (core, tested): add/remove/move/duplicate chain, group slots into a rack, ungroup, map/unmap, invert range,
Randomize (mapped, not excluded macros; uniform until the render shows otherwise), store / launch / overwrite /
rename / duplicate / delete variation (launch skips excluded and `has = false` macros).

### 9.4 UI (slice b; LYNX's own drawing, Live's layout from the shots, theme colours, red accent)
One `RackComponent` used in three places: the Device View card body, the Mixer slot's window, and the Channel Rack
channel's window. Left to right, as in Live [SHOT]:
```
[● Name    Rand Map ⧉][view column: ▤ macros ☰ chains ▭ devices ◈ variations + −]
 | Variations: New | ▣ Verse ▶ | ▣ Drop ▶ |  Macros 2×4…2×8 knobs, name plates, values in units  |
 | Chain list: name | vol dB | pan | ● | S | ⇄ |  Key / Vel / Chain | Hide  ruler 0–127, zone bars with fade ramps |
 | ⟦ the selected chain's devices as nested cards ⟧
```
- Map mode: Map buttons under the macros; mappable knobs anywhere in the rack get a highlight; the mapping list (Macro,
  Path, Name, Min, Max) opens as a side panel in the rack window and as a pop-up list in the Device View (LYNX has no
  browser slot to take over in Pattern workflow; **decision R2 (approved)**). Picking a target: a macro's Map button
  opens a menu of the rack's chains › devices › parameters, the chains' volume / pan and the chain selector (Live
  highlights the parameters themselves; LYNX's devices have their own windows, so the menu is the way in).
- Pattern workflow: nothing changes unless a Rack is added (Mixer slot menu › "Audio Effect Rack", Channel Rack
  › Add › "Instrument Rack"; "Group into Audio Effect Rack" on a multi-selection of slots, Ctrl+G in the Device View).
- Strings in every language file; the rack window keeps FL-style window behaviour (detach, close, remember position).

### 9.5 Variations and the morph (slice c)
Live-exact variations (§5). The LYNX extras (D6, both off by default, in the variations' right-click menu): "Variation
morph (LYNX)" shows a **Morph** slider between variation A and B ("Morph from / to this variation") that moves every
macro both hold and that isn't excluded linearly from A's value to B's; in the graph the morphing macros' cables come
from the Morph control (automatable: published parameter 17). "Launch glide (LYNX)" (Off, 100 ms … 4 s) moves the
macros to a launched variation over that time instead of Live's instant jump. Off = Live's panel exactly.

### 9.6 .als (slice d)
Import: `AudioEffectGroupDevice` / `InstrumentGroupDevice` / `MidiEffectGroupDevice` → racks with chains, zones,
volume/pan/activator/solo, selector, macros (names, values, colours, defaults, exclusions), variations, nested racks,
and the mappings onto plugin parameters (`PluginFloatParameter`: Live keeps their Min/Max in 0–1); Live's native
devices inside chains are dropped and listed as today, with their mappings. Export: the reverse, written by
generators in Live's member order (round 175a's method), plugins as `PluginDevice`. Round-trip tests:
LYNX → .als → LYNX equal; Live-saved fixtures (made in session 2) → LYNX → .als → Live opens it.
Bonus later: FL export writes a rack as FL's Patcher (the compiled graph, macros as Surface knobs).

### 9.7 Round 183c-2 additions
- **Ungroup** (Live's Ungroup on a rack): mixer slot menu › "Ungroup" on a one-chain Audio Effect Rack (`rack::ungroupSlot`).
  The devices take the rack's slot; each native parameter a macro moves keeps the macro's current value. LYNX extra: a macro
  with exactly one full-range mapping hands its automation back to that parameter, so Group → Ungroup restores the old
  slots and their automation (Live deletes macro automation with the macros). Other macro / selector automation goes.
- **.als zone editor**: Live stores only `IsRangesEditorVisible` [DATA: rackshot.als]; which editor shows is not in the set.
  Import: visible → the Chain editor for an Audio Effect Rack (its only one), Key for the others; export writes it back.
- **Macro names on dropped devices**: Live stores "Macro 3" and shows "Formant Shift" (the one mapped parameter of its
  Vocoder) [DATA + SHOT s08]. When LYNX drops the device, such a macro keeps the shown name (from the parameter's member name).

## 10. Best way?
- **FL:** FL has no rack-with-macros device. Its pieces are the Patcher (a free graph with a Surface tab of controls),
  the Layer channel (stacks whole channels, with key split, crossfade and round robin) and the Control Surface /
  Dashboard. Mapping a knob to many parameters means cabling it in the Patcher's Map; there is no map mode, no
  range ruler per mapping, no variations and no randomise (REA_rodada141, REA_rodada151, REA_rodada164a).
- **Ableton:** the Rack (§1–5): a fixed chain list with zones, 16 macros with a map mode, variations and Rand.
- **Users:** FL users ask for Ableton-style instrument racks and are told to switch DAWs or to use Patcher
  ([IL forum, "Making Racks in FL studio"](https://forum.image-line.com/viewtopic.php?p=1978659)); Patcher's preset
  knobs are already called "macros" by users, who want parameter linking across FL
  ([IL forum](https://forum.image-line.com/viewtopic.php?p=740403)); creators sell Patcher remakes of Ableton racks
  ([Gumroad example](https://darkmajikmusic.gumroad.com/l/captivating-patcher-plugins-and-ableton-racks-vol-1)).
  Computer Music called the Live 11 rack update "outstanding" (16 macros and variations,
  [Thomann reprint](https://www.thomann.de/ro/prod_review_76083_AR_514567.html)); users wish variations could morph
  (GAPMAP #3).
- **Verdict:** add Racks as an Ableton-exact (ALX) device on top of the Patcher engine; FL's Patcher, Layer and
  Dashboard stay as they are. Morphing between variations is LYNX's own extra, off by default (D6).
