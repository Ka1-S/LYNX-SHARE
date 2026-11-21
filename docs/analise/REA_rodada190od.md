# REA round 190od — slide notes and note properties (#236), the Root note items (#242), theme default colours (#238), auto colours (#239), template dates (#240)

FL Studio 2026 (C:\Program Files\Image-Line\FL Studio 2026). Clean-room: behaviour only, no code or data copied.
Work folder `/mnt/e/lynx-work/issues236_190od/` (`slide/` projects, FL renders, scripts).

## 1. Slide notes move every note property (#236)

| # | Fact | Evidence |
|---|------|----------|
| S1 | FL manual, Piano roll › Slide notes: "Slide events have all the usual note properties (velocity, panning, cutoff and resonance, etc)." and "During slides all note properties move toward those set in the slide note." | [MANUAL] fl-studio-online-manual/html/pianoroll.htm |
| S2 | FL's own renders (`FL64.exe /R /Ewav`, no window, under the GUI + FL locks and the idle gate, `slide/p/flr.sh`) of six render-test generators (Sampler, 3x Osc, DirectWave, SimSynth, Harmless, MiniSynth), each with: vs = C5 vel 25 (reference); vo = C5 vel 100 + a slide note C5 at tick 48, 48 ticks, vel 25; vi = C5 vel 25 + slide vel 128; vp = C5 vel 100 + slide C5 pan 0 (full left); vk = C5 vel 100 + slide G5, 24 ticks, vel 25 (`slide/p/make.sh`). 30 renders, none failed. | [EXEC] `slide/fl/*.wav`, `slide/flr.log` |
| S3 | Velocity: Sampler, 3x Osc, DirectWave and SimSynth fade from the note's level (b) at tick 48 to the slide note's velocity's level (vs) at tick 96 (vo), and up in vi (Sampler vo per tick RMS: 0.136 → 0.090 @60 → 0.051 @80 → 0.011 @96 = vs 0.0103). The fade ends with the slide (vk: 24 ticks, quiet from tick ~72). | [EXEC] `slide/p/ticks.py` output in `slide/ticks-fl.txt` |
| S4 | Pan: all six generators move to full left over the slide (right channel 0 from tick 97). | [EXEC] same |
| S5 | MiniSynth's level does not follow a slide note's velocity (vo = b, vi = vs): it reads the velocity only at the note's start. Harmless' test preset has VEL → volume off (vs = b): no level change either; its pan follows. So the host moves the voices' FinalLevels (Vol, Pan, Mod X/Y as cutoff/resonance); each generator follows what it reads every render. | [EXEC] same |

LYNX (Song::slideNoteProps; RackRenderer::startPropSlide / stepPropSlide): a slide note moves the held notes of its
channel from their own velocity, pan, Mod X and Mod Y to its own in n + 1 equal steps on FL's ticks (the law round 190hg
measured for the pitch), through `Instrument::voiceProperty`. Engines: the Sampler (already took voice properties for
the VFX Envelope), 3x Osc and DirectWave (velocity = InitLevels.Vol with FL's law, pan), Harmless (Final.Vol when VEL →
volume is on, pan), MiniSynth and SimSynth (pan only — MiniSynth as FL; LYNX's SimSynth takes no velocity at all, see
§6). LYNX's own engines with voice properties (LYNX Synth, Wavetable, Braid, FPC) follow too; the rack splits their render
calls on the ticks only while such a slide runs. Projects saved before 0.1.31 keep the pitch-only slide (no
"slideNoteProps" key); new projects and .flp imports take FL's law. A slide note with the same properties as the note it
slides changes nothing (bit-identical to before).

## 2. Root note items wait for a scale (#242)

| # | Fact | Evidence |
|---|------|----------|
| R1 | FL manual, Piano roll › Scale highlighting: "You must first select a Scale, e.g. 'Major (ionian)', before Root notes are available." | [MANUAL] pianoroll.htm |
| R2 | `FUN_00eadb90` (called by the scale menu's builder `FUN_00eadc60` and its click handler `FUN_011f87e0`): for each Root note item (form +0x20d8 … +0x2130) sets the enabled flag (`FUN_00840520`) to `DAT_016ba828 > -1` (a reference scale chosen) and ticks the item equal to the root `DAT_016ba82c`. Default (−1) and Black notes (−2) leave every root disabled. | [DEC] flresidue190jv/notes/pr-ghidra/a/00eadb90.c |
| R3 | FL 2026 live: View › Scale highlighting with Black notes ticked shows C … B greyed. | [SHOT] live190cq/shots/curves190bw-05c-scale-highlighting-menu.png |

So the greyed Root note list of the report is FL's behaviour (kept). The menu that never opened again was LYNX's bug
(ui/PianoRollComponent.h ScaleBox: JUCE's ComboBox marks its popup active before showPopup and clears the mark only in
its own menu's callback; the box's own menu now clears it when it closes).

## 3. Theme default colours (#238)

| # | Fact | Evidence |
|---|------|----------|
| T1 | FL 2026's theme files (`Artwork/Themes/*.flstheme`, 13 presets) hold Hue … Text, the dots (Selected, Highlight, Mute, Option, StepEven/Odd, Meter0-5, Text), Lightmode, OverrideClips, NoteColor0-15, grids and the background. No track colour. | [DATA] the .flstheme files |
| T2 | FL's skin keys (the "FL Studio theme file (*.xml)" format of the theme editor's Save dialog) include `channels.colors.`, `pattern.defaultcolor`, `forms.defplugincolor` and `forms.eventeditform.playlist.track.background`: a theme can set the default colours of channels, patterns, plugins and Playlist tracks. FL 2026's what's new: "Themes: new sampler channels don't use the themed channel color" (14303). | [STR] FLEngine_x64.dll strings; [DOC] Help/WhatsNew.htm |
| T3 | "Audio and automation clips use note colors" (`ClipsOverrideBox`, hint "Use the note color list to find random colors for new clips"; .flstheme OverrideClips): new audio and automation clips take random colours from the theme's 16 note colours. | [DFM/STR] FLEngine strings; [SHOT] design190ia/shots/ref/fl-settings-theme.png |

LYNX: Settings › Theme gets a ninth dot, **Tracks** — the colour drawn for Playlist tracks, mixer tracks and channels
that have no colour of their own (FL's grey #485156 unless set; saved in .lynxtheme as `tracks`). The project keeps "no
colour", so a theme never rewrites project data. T3 is built too: Settings › Theme › Options › "Audio and automation clips
use note colors" (themes::Spec::clipsUseNoteColours, saved in .lynxtheme; core/NewClipColours.h: what an edit adds without
a colour — audio clip channels, automation clips — takes a random one of the 16 note colours).

## 4. Auto colours (#239)

FL has no name rules. It colours by content: a clip dropped on an empty track header colours it (what's new 11872),
Auto name / Lock to content take the source's name and colour, "Group with above track" and "Auto color group" colour a
group (8257), new clips can take random note colours (T3, built in this round). LYNX has the content rules already
(core/PlaylistTrackOps.h, NoteEdit colourGroup). The name rules the report asks for (REAPER's SWS Auto Color/Icon) are
LYNX's own: core/AutoLook.h, Settings › LYNX extras › Automatic colors and icons.

## 5. Templates and Project info (#240)

FL keeps a genre template's creation date in a project started from it ([DATA] REA_rodada167g: every genre template
shares one date and the demos made from them carry it). The report asks for a fresh date and working time; LYNX now
starts both for a project made from a user template, and a template is saved without them (owner decision in the
round's report; FL-exact would keep the date).

## 6. Found on the way

- LYNX's SimSynth ignores the note velocity (`SimSynthInstrument::noteOn` passes the channel volume only); FL's SimSynth
  plays velocity 25 about 22 dB below velocity 100 (S2 renders, vs against b). Not changed here (a plugin-quality-gate
  change with its own before/after scores).
