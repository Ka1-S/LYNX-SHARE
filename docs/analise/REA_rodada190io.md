# Round 190io — FL's Channel rack and Mixer landmarks, Mixer details, issue #154 part 2 → 0.1.30

Slices 6 (FM-S3) and 11 (FM-S5) of the design plan (/mnt/e/lynx-work/design190ia/PLAN.md, findings fl-main.md FM-10…14,
FM-30…32), the Mixer rows of the details audit with evidence (details190if DC-13, DC-14 / DF-70), and the second part of
issue #154 (the effects list on the Mixer strips). Built on round 190ih (the rack's activity light, R4) and round 190ht
(the effects list on linked track headers).

## 1. Evidence

| # | Fact | Source |
|---|---|---|
| E1 | FL fills a channel button with the channel colour (Synth Bass #11479C, Grv Kick #892019, Saw #366389), white text, the channel's glyph at the right | `fl-channelrack.png` 317,130,400,1000 (design190ia FM-10) |
| E2 | FL's steps are raised keycaps 13 × 20 at a 16 px pitch: light top edge, dark bottom edge, a dark LED slot line a quarter down; groups of 4 alternate | `fl-channelrack.png` 425,160,690,190 (FM-11) |
| E3 | Audio clip channels show their 16 steps like any other channel | `fl-channelrack.png` 425,160,690,690 (FM-12) |
| E4 | "If desired, Audio Clips can be triggered from the Step Sequencer or the Piano roll" | FL manual, Playlist › Audio Clips (playlist_audioclip.htm, fetched 2026-11-16) |
| E5 | An audio clip channel plays from a typing-keyboard note | details190if `shots/fl-audiochannel-wave-typingkey.png` (C-15) |
| E6 | FL's selector is a 12 × 27 slot, outlined 2 px when its channel is selected | `fl-channelrack.png` 405,130,418,160 (FM-13) |
| E7 | FL drops the middle of a long name: "WA_..6BPM", "Titey..m #2", "CUTLAY..idated)"; the two parts take about half of the box each, by width | `fl-channelrack.png` 315,490,400,520; details190if `fl-mixer-meters-strips-3frames.png` (FM-14) |
| E8 | FL's Mixer header band: 38 px of the track colour, a dark number (colour × 0.3), the icon in a light tint | `fl-mixer.png` 330,526,560,580 (FM-30) |
| E9 | FL's strip names are a light tint of the track colour (red-pink on the red group, lilac on BELL) | `fl-mixer.png` 333,600,350,662; `fl-mixer-meters-strips-3frames.png` (FM-31) |
| E10 | Routing iconography: "Selected Track: the downward green arrow indicates audio is being sent from the track"; "The up-arrows indicate available Send-Tracks and also act as switches to create a Send when clicked"; a source track routed to the selected one "shows as a grayed-out send switch"; "Send in use: the small up-arrows indicate that another track is sending to the track"; right-click = route or sidechain | FL manual, Mixer (mixer.htm, fetched 2026-11-16); `fl-mixer.png` 340,940,700,965 (FM-32) |
| E11 | Peak-hold ticks are short ticks in the meter's own lime, per channel, still drawn after Stop | details190if `shots/fl-mixer-meters-strips-3frames.png` (DC-13) |
| E12 | Level fader colours: "Grey (Default), Green (Selected), Orange (Armed for recording)"; the selected Saw Bass cap is green | FL manual, Mixer › Level faders; details190if `shots/fl-fader-selected-green.png` (DC-14 / DF-70) |
| E13 | "Extra large has a special feature showing the FX in use per track": left-click opens an effect, right-click an empty slot adds one, right-click an effect for move up / down and replace; Mixer menu › "Compact plugin list" removes the empty slots | FL manual, Mixer › Mixer layout (fetched 2026-11-16) |
| E14 | FL's own render (`FL64.exe /R /Ewav`, 2026-11-16) of a project with one step on an audio clip channel (a 2 s 440 Hz sine, the pattern once at bar 1, 120 BPM): the clip sounds from 0.000 to 2.010 s at 440 Hz, the same as a Sampler channel with the same step; a step does not cut the clip at its 1/16 | `/mnt/e/lynx-work/rackmix190io/fl/` (clipstep / samplerstep .flp written by LYNX's .flp export from the round's test, FL's .wav renders) |

Not settled (left as it was): FL's peak-hold time and fall (details190if capture C3 / REA R-3 still open: the existing
captures prove only that the tick is still there 0.6 s after Stop; LYNX keeps its 1.5 s hold and ~20 dB/s fall); how the
over-0 dB state clears (DF-59: LYNX's clip latch until clicked stays); Smart-disable's look (DF-78).

## 2. What changed (Pattern layout's Channel rack and Mixer; every other screen as before)

| Item | FL | LYNX before | LYNX now |
|---|---|---|---|
| Channel button (FM-10) | channel colour fill, white text, glyph at the right | dark tint + 4 px stripe, glyph before the name | the colour with its brightness held at 0.42–0.58 (white text ≥ 4.5:1), ±6 % gradient, FL grey #485156 without a colour, muted greyed; glyph 6 px from the right; focused channel = 1.5 px light outline; 150 px kept (LYNX) |
| Steps (FM-11) | raised keycaps 13 × 20, pitch 16 | flat tiles 19 × 22, pitch 22, 1.22:1 on the rack | keycaps 15 × 20, pitch 18: bevel to the LED slot line (2.0:1), light top / dark bottom edge; lit slot line light |
| Audio clip channels (FM-12) | steps drawn (E3); triggerable (E4) | "audio clip — plays from the Playlist" | steps drawn and editable (paint, Fill); the engine already played a step as FL does (E14): the whole clip once at its own pitch (test `rackmix190io_audio_clip_channel_plays_from_a_step`: 2.13 s above −60 dBFS for the 2 s clip; FL's render file ends with the song at 2.02 s, so it cannot show a tail) |
| Selector (FM-13) | 12 × 27 slot, 2 px outline when selected | 6 × 16 pill | 10 × 24 slot, `bg1` + hairline, 2 px accent outline when selected; round 190ih's activity light fills it |
| Long names (FM-14) | middle ".." | end "…" | `theme::fitMiddle` (rack buttons, Mixer names) |
| Mixer header (FM-30) | 38 px colour band, dark number, light-tint icon | 3 px bar | 36 px band (brightness 0.45–0.6), number bold in FL's dark ink where it reads at 3:1, else white; light-tint icon |
| Strip names (FM-31) | light tint of the colour | white | light tint (≥ 4.5:1, else white), one size up |
| Route arrows (FM-32, E10) | ▼ selected, △ available, ▲ lit, greyed source, small "send in use" | "→ n" pill in the routing lane only | the arrows at every strip's foot (a click = the pill's route code; right-click = its menu); the pill stays |
| Peak tick (E11) | lime tick | white line under −6 dB | the meter's colour at its level |
| Fader cap (E12) | grey / green selected / orange armed | always light grey | light grey / `ledOn` selected / `warning` armed |

## 3. Issue #154 part 2: the effects list on the Mixer strips

The owner's choice (round 190ht): an optional Mixer view, off by default, so FL's Mixer is unchanged unless it is
switched on. Mixer menu and Settings › LYNX extras › "Insert effects on the strips (LYNX)" (ui-options
`mixerStripInserts`). Each strip in the Wide layouts and Extra large lists its insert's effects under the header band
(3–6 rows of 13 px by the Mixer's height, `core/StripInserts.h`), with the linked track headers' list and edits
(`core/TrackInserts.h`, round 190ht): dot = bypass, click = window, drag = reorder, onto another strip's list = move
(Ctrl: copy), right-click = the Mixer slot menu (on "+ Add" or below: the add menu), wheel = scroll. The Compact
layouts (20–22 px strips) have no room for names and show no list.

E13 (FL's Extra large layout lists the FX per strip) was found during this round: see the owner decision in the
round's HANDOFF.

## 4. Performance

Static paints: the rack's keycaps are plain rectangles; the band is one path per strip; the arrows are two small paths;
the effects list draws only while its view is on. No timer was added; the meters keep their FrameTicker and the rack
its 30 Hz light timer. `--paintbench` / `--uiperf` before / after: the round's HANDOFF.
