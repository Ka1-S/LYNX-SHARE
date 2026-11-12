# REA round 190dz: FL's audio clip menu layout, the channel's Levels adjustment

Static evidence only (no FL run, no screen input): FL Studio 2026's form resources (rea190bc dfm dump,
TFRUITYLOOPSMAINFORM EEAUMenu, TPLUGINFORM OfsLevelsPanel), headless Ghidra on FLEngine_x64.dll (read-only, under the
Ghidra lock; handler addresses from rea190bc handlers.tsv), FL shots of round flgap189o. Facts in our own words.

## 1. Audio clip menu (the Playlist's right-click on an audio clip)

| FL fact | Evidence |
|---|---|
| Two columns with section bands: "Audio clip" (Preview / Muted ✓ / Rename and color..., Change color..., Random color, Change icon... / Select source channel ›, Channel settings, Make unique, Select all similar clips, Delete / Properties...), "Region" (Select region ›, Chop ›) on the left; "Sample" (Make unique as sample, Edit sample, Pitch-correct sample, Time-warp sample, Extract stems from sample, Deverb, Detect tempo, Fit to tempo), "Automation" (Automate ›, Crossfade with), "Fades" (Reset fades, Crossfade selected when shown, Fade in curve ›, Fade out curve ›) on the right | [SHOT] flgap189o FL-clipmenu-none-selected / -all-selected; [FORM] EEAUMenu order and captions (& = underlined letter) |
| "Select source channel" is set at popup time ("&Select source %s"; the form says "&Select channel") | [H 0x1237290] EEAUMenuPopup |
| Hidden in FL 2026 (set invisible by the popup, never shown): Articulator tools, Edit envelope..., Use current time signature from, the "Per clip settings" group (Set pitch..., Reset stretch factor, Reverse clip) | [H 0x1237290] |
| Rename and color..., Change color... and Change icon... share one handler: the clip channel's name dialog, Change color with its colour selector (flag 8), Change icon with the icon picker (flag 2) | [H 0x123a3d0] |
| Muted: AutoCheck item, ticked from the clip's mute flag | [FORM] MuteUnmuteClipMenu; [H 0x1237290] |
| Select region ›: "Full" (Default radio, Tag −1) + one item per sample region ("Region %d" when unnamed); a pick sets the clip's start / end to the region; the tick is the region the clip plays exactly, "Full" for the whole source, none otherwise | [H 0x1237290], [H 0x1239b70], [H 0xffa400] |
| Chop ›: Chop into regions, Dull / Medium / Sharp auto-slicing, Trim outside selection, then the groups Time based (Chop in bars … 32nd beats), Repeating, Marching, Patterns, Complex | [FORM] EEAUChopMenu; [H 0x1236260] |
| Crossfade with ›: the other channels' audio clips overlapping the clicked one in time; greyed with none. A pick makes an automation clip "<A> to <B> crossfade" over the overlap that drives both channels' Levels adjustment volume (0x11) with FL formulas "0.5*Tension(Input,10)" / "0.5-0.5*Tension(Input,-10)" | [H 0x1236f10], [H 0x1244ab0] |
| Reset fades ("Reset fade times to 0"): the fade handle's reset (Tag 1), times and tension | [H 0x1244750] -> [H 0xd01bc0] -> FUN_00cfb460 |
| Deverb: the Deverb dialog on the clip's part of the sample, the result saved as <name>_Deverb.wav; Pitch-correct / Time-warp: the sample into Newtone / Newtime | [H 0x123aed0], [H 0x1239ab0], [H 0x1244710] |

LYNX: `core/FlClipMenu.h` (the model, tested) and `PlaylistComponent::buildAudioClipMenu`: FL's order, labels,
sections and the column break (JUCE `addColumnBreak`); LYNX's own items below a line at the end. Not in LYNX yet (left
out, FL enables them): Pitch-correct sample, Time-warp sample, Deverb, Fade in / out curve, Crossfade with (shown only
greyed, where FL greys it), FL's Chop groups other than Time based.

## 2. Levels adjustment (FL Channel settings › Misc)

| FL fact | Evidence |
|---|---|
| Panel "Levels adjustment" (first on the Misc page): PAN = PanOfsWheel Tag 16 "Panning offset" −6400…6400; VOL = VolOfsWheel Tag 17 "Volume multiplier" 0…25600, default 12800; MOD X = FCutOfsWheel Tag 19 "Modulation X offset" −256…256; MOD Y = FResOfsWheel Tag 20 "Modulation Y offset" −256…256. Tag = the channel parameter (0x10, 0x11, 0x13, 0x14) | [FORM] TPLUGINFORM FuncSheet/OfsLevelsPanel |
| Stored in channel +0x184 (0xE5: pan, volume, an unnamed value, Mod X, Mod Y) | [H 0x1046a70] |
| Gain = f(volume / 12800) · f(multiplier / 12800), f(x) = 1 at x = 1 else (11^x − 1) / 10 (so 200 % = +21.6 dB) | [H 0xfe9d20] (REA_rodada3 §2); render check REA_rodada92b |
| Pan = (pan + offset − 6400) / 6400 | [H 0xfe9bf0] |
| Mod X / Y = (knob + offset) / 256 for sampler-type channels; only the offset for plugin types | [H 0xfea140] |
| Automate › Volume sets the new clip's range top to 6400 (unity of the 0…25600 travel) and its line at the top | [H 0x1244900] |

LYNX: `ChannelMisc::LevelAdjust` (saved in .lynx `misc.levels`, read / written as .flp 0xE5, no longer folded into the
channel's knobs on import), `AutomationTargetKind::channelAdjust` (FL's parameters 0x10 / 0x11 / 0x13 / 0x14 in .flp
automation), the RackRenderer applies volume and pan (automatable), the Sampler / Synth configure apply Mod X / Y. The
Misc tab's "Levels adjustment" knobs are these offsets (they showed the main Pan / Volume / cutoff / resonance before).
