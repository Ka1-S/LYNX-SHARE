# REA round 190ac: Slicex editor stage 2 (region edits, zoom / scroll bar, menus), two stage-1 proofs, DirectWave status and plan

Continues `REA_rodada190r.md` (editor stage 1). Clean-room: FL Studio 2026 was run, its windows photographed, its saved
files read and its renders measured; no Image-Line code, bitmaps or fonts. Marks: **[EXEC]** seen in FL / measured,
**[DATA]** read from a file FL wrote, **[DOC]** FL's manual, **[HYP]** not confirmed.

## 1. FL's deck menus, listed [EXEC: every menu opened and photographed, `slicex190ac/fl/shots`, round 190r's `menus-sheet.png`]

| menu (deck tool row) | entries in FL | in LYNX after this round | left out, and why |
|---|---|---|---|
| **File** | New · Load sample… (Ctrl+O) · Save sample as… (Ctrl+S) · Save sample as 24bit .wav file… · Export regions ▸ · Export display as… | New, Load sample…, Save sample as… | 24-bit save and Export regions (one file per region: needs the audio rewritten, LYNX keeps the deck's bytes as they are); Export display (a picture of the wave) |
| **Regions** | Add region… (Alt+M) · Add marker… (M) · Set loop (Alt+L) · Set first downbeat · Delete (Shift+Ctrl+Del) · Rename all… (Ctrl+F2) · Quick rename all… (Alt+F2) · Auto rename all · Assign trigger notes to all… · Assign all to ▸ · Set all cut groups to… · *Detection:* Auto-slice ▸ · Detect beats · Detect pitch regions · Zero-cross check all regions · Freeze all | Add marker…, Delete, Rename all…, Set all cut groups to…, Freeze all | Add region and Set loop (need FL's drag selection in the wave, not built); Set first downbeat (beat-grid editing); Quick rename all / Auto rename all (FL's name list and its drum classifier); Assign trigger notes to all / Assign all to (the per-region "Trigger note" and the keyboard menu cover one region at a time); **Auto-slice, Detect beats, Detect pitch regions, Zero-cross check**: FL's detection algorithms are not decoded — see §4 |
| **Zoom** | Zoom in (PgUp) · Zoom out (PgDn) · Zoom out full (Home) · Zoom on selection (End) · Zoom on left / right of selection (Ctrl+Left / Right) | the first four ("selection" = the region shown) | left / right of selection (no selection yet) |
| **Snap** | Snap to grid (1, the default) · regions (2) · samples (3) · zero-crossing (4) · pitch period (5) | none | acts on selections and envelope points, not on region tags (§2) |
| **Select** | Deselect · Select before / after current selection · Select zoomed part (Ctrl+A) · Select previous / next region (Left / Right) | none | no selection yet |
| **View** | Spectrum (S) · Dual view · Display settings ▸ · Regions ✓ · Loop · Time format ▸ · Scroller above ✓ · Background gradient ✓ · Invert grid ✓ | none (the layout is FL's default: regions shown, scroller above) | view options |
| Format, Edit, Tools | Edison's sample format, cut / paste and processing tools | none | they rewrite the audio; LYNX's own audio editor does that work |
| **Region tab** (right click; the same from "More region settings") | *name* · Delete · Clone as region · Turn into marker · Rename… · Quick rename ▸ (Kick, Snare, HiHat, Perc + the GM drum names) · Set info… · Purpose ▸ (Basic, Beat, Downbeat, CD track, CD index, Single cycle) · Trigger note ▸ · Select · Zoom on | Delete, Clone as region, Turn into marker, Rename…, Trigger note, Zoom on | Quick rename (a name list), Set info (the text is kept in the file, there is no box for it), Purpose (kept as saved; a new region is "rgn "), Select (no selection) |

## 2. Region edits: what FL writes [DATA: `fl/ac_base.flp` → edits in FL → `fl/ac_edit1.flp`, deck A's embedded WAV before and after]

Done in FL on "DL Breaker" (22 regions): Rename… of region 3 to "Zed" (a small inline box, hint "Region #3 name");
Clone as region on region 9; Delete on region 11; three tag drags (regions 2, 4, 8), the first with "Snap to grid"
on, the last with "Snap to zero-crossing". FL's saved chunks (`p/riff.py`):

- The pack's regions are not bare markers: every cue has a length entry with the purpose **"beat"** and the distance
  to the next cue. FL draws such a region with a start tag and an end tag.
- **Rename**: only the label changes. **Delete**: the cue goes; the region before it keeps its length (22050), so the
  deleted stretch belongs to no region. **Clone as region**: a second cue at the same start, same length and purpose,
  named "Snare #2", right after its source. Cue ids are renumbered 1…n in sample order; the trigger list follows.
- **Dragging a start tag** moves that start only: region 2 went 22050 → 28968 with its end where it was (length 22050
  → 15132) and region 1's end untouched, which leaves 22050…28967 without a region. The distance is the mouse's own
  (14 px × 494.1 frames = 6918; 17 px → 8400; 7 px → 3459): **no snapping**, with grid snap or zero-crossing snap on.
  The Snap menu is for selections and envelope points.
- `slicex::withRegions` writes the list this way; run on FL's own two files with the regions it reads from them, it
  gives the files back byte for byte (`p/rt`). `wavRegions` now also keeps each region's purpose and info text.
- Seen but not explained: the same save holds a beat-grid end of 6.845 instead of 8 (one of the drags or wheel turns
  changed the deck's grid). Not reproduced, not built.

## 3. Zoom / scroll bar and the wave [EXEC + DOC]

FL's deck has the zoom / scroll bar on top (View ▸ Scroller above, on by default): the whole sample small, the part
shown lit, an arrow at each end. Seen: a wheel notch over the wave zooms by about a fifth (frames per pixel 494 → 401
→ 367); Shift + wheel scrolls; Home shows everything. FL's manual (Edison, "Sample edit window"): drag the lit part
to scroll, drag one of its edges to zoom, middle button drag zooms and scrolls. A left drag in the wave makes a
selection (red, "from – to" in LENGTH / SEL); "Add marker…" asks for the name ("New region name") and puts the marker
at the selection's start, at the sample's start without one.

LYNX: the bar (drag, edge drag, double click = all), wheel = zoom at the mouse (×0.8 / ×1.25), Shift + wheel = scroll,
the Zoom menu; region tags with names on a row above the wave, end tags where a region stops before the next one; a
tag drag moves the start (unsnapped, as FL), a tag's right click opens the region menu. Not built: the drag selection
(a click still selects and plays the region, as in stage 1; "Add marker…" uses the place last clicked), dragging an
end tag, the bar's arrows, middle-button zoom.

## 4. A WAV without a beat count [EXEC: nine files loaded in FL at project tempo 120, `fl/u1…u9.wav`]

Stage 1 guessed "the project's tempo". FL does something else: it gives the sample a **power-of-two beat count that
puts its tempo in 80 < BPM ≤ 160** and shows that tempo.

| file | length | FL shows | beats |
|---|---|---|---|
| u1 (five hits 0.6 s apart = 100 BPM) | 3.000 s | 160BPM | 8 (4 would be exactly 80) |
| u2 (one swell) | 1.370 s | 88BPM | 2 |
| u8 / u6 / u9 / u5 / u7 / u3 / u4 | 2.981 … 2.791 s | 81 / 81 / 82 / 82 / 83 / 84 / 86 BPM | 4 |

The content plays no part (u1's hits are at 100 BPM), nor does the project's 120 (u1 could have been 6 beats). FL's
own save of u9 has the grid 0…4 and writes an ACID chunk with 4 beats. `slicex::guessBeats`; the editor's load uses
it. [HYP] below one beat at 160 BPM the count is halved (no such file was tried); other project tempos not tried.

The same loads showed FL **slicing the file by itself**: u1 got five markers "Marker #1…#5" on its hits, u2 one
marker where the swell rises. LYNX loads such a file as one region. FL's detection (the Regions menu's Auto-slice /
Detect beats family) is not decoded; **needs a decision**: decode FL's slicer in a later round (recommended), or use
LYNX's own onset detector meanwhile as a marked stand-in.

## 5. A key click sounds [EXEC, LYNX]

`LYNX.exe --slicexkeytest report.txt` (new): the real engine behind a stand-in device, one Slicex channel with four
0.25 s regions (200 / 400 / 600 / 800 Hz), and the call the editor's keyboard makes
(`sendLiveNote (channel, key, 100/128, on)`). Windows build of this round:

```
ok   idle: rms 0.000000000
ok   key 60 down: rms 0.142180, 200.0 Hz
ok   key 60 plays region 1 (200 Hz)
ok   mixer track 1 meter peak 0.201073
ok   key 60 up: the end is silent, rms 0.000000000
ok   key 62 plays region 3 (600 Hz): rms 0.142179, 600.0 Hz
ok   key 72 (no region): silent, rms 0.000000000
result=OK
```


## 6. DirectWave: where it stands, and the plan

### 6.1 What already exists (rounds 9, 72, 77, 80 — on main since 0.1.x)

The brief asked for a first slice "codec + playback of a DirectWave channel from .flp against FL renders". That slice
is already in the tree:

- **State codec** `dw::parseState` (`core/DirectWave.cpp`, REA_rodada72 §1–§4): the 0xD5 stream's programs, zones
  (header 0x1F4, format 0x1F7, tuning 0x1F8, stretch 0x1F9, sends, two filters, amp and two modulation envelopes, two
  LFOs, the 16-slot modulation matrix, sample path or embedded audio 0x205), program effects (delay, chorus, reverb,
  two waveshapers), voice mode, glide, 100 alternation groups.
- **Engine** `dw::Engine`: one voice per matching zone, the matrix per control block, 32.32 linear reader with
  forward / sustain / bounce loops, both filters (serial / parallel), limiter, amp envelope, pan law, the four buses and
  the program effects. Round 72: all 57 demo programs bit-identical to the DLL with its own decoded samples.
- **Channel** `InstrumentKind::directwave` (`dw::importChannel`): the played program's zones as the channel's zones
  (file paths resolved with FL's variables, or the embedded audio saved beside the project), each zone's sample format.
  FL Mobile's "DW Sampler" lands on the same engine (round 182c).

**Measured again in this round [EXEC]** (`renderwav` of the Slicex stack, FL's own 16-bit renders):

| project | program | LYNX against FL (residual) |
|---|---|---|
| FL's `System/Render tests/Directwave.flp` | Rhodes, 7 zones | −76.7 dB |
| the same project with the state of a demo's channel (`p/dwmake.py`): Piezo (10 zones) | | −76.2 dB |
| Strat Mute (12) | | −73.1 dB |
| Close Grand (17) | | −76.2 dB |
| Jazz Guitar (6) | | −75.9 dB |
| FL Piano (45) | | −74.7 dB |
| Stage Grand (88) | | **−52.8 dB** |
| Strings (11) | | **−27.9 dB** |
| Choir Ahh (12) | | **−33.4 dB** |

Six of nine are at the 16-bit file's floor. The three that are not are the programs with long releases (FL's renders
run 3.0…3.5 s where the others stop at 2.0…2.3 s); gain and level are right (rms difference 0.0 dB), so the difference
is in the tails. Not investigated in this round — first item for the DirectWave round (candidates: the program reverb
across LYNX's own control-block boundaries, REA_rodada72 §8, and the host's handling of the last notes at the render
end, the open item of round 190r).

−71…−77 dB is the 16-bit file's quantisation (the same floor as Slicex in §2 of round 190r).

### 6.2 What an .flp DirectWave channel still lacks

| gap | where | size |
|---|---|---|
| No editor: the channel opens the generic channel settings only (FL's form is 990 × 627: program / zone grid, sample view with loop points, zone pages, matrix, program FX, browser / options side panel; `slicex188c/rea/dfm-dw-sum.txt`) | UI | large |
| Not addable: DirectWave is missing from "add channel"; a new one would be FL's empty program | `natives::instrumentEngines`, a `dw::fresh()` | small |
| No state **writer**: the state is read, never written, so nothing can be edited or saved back as FL's (the .flp export keeps the imported bytes) | `core/DirectWave` | medium (the reader's chunk walk in reverse; byte-exact round trip on the demo states as the test, as Slicex has) |
| Only the first program with zones plays; MIDI slots 1–15 / program changes are not followed | engine + channel | small |
| Time-stretch modes (chunk 0x1F9), HQ (sinc) interpolation, 8-sample control blocks, the polyphony limit, alternation probabilities < 1 (operating-system randomness: not reproducible) | engine | medium; the first three are plugin settings that are off in every demo |
| .dwp files: loaded for FL Mobile projects only; no "open .dwp" on a desktop channel; no drop of samples to make zones | UI + import | small once the writer exists |

### 6.3 Plan (in this order)

1. **State writer + `dw::fresh()` + addable channel** (non-UI): byte-exact rewrite of every demo state; a new channel
   with one empty program; "add channel → DirectWave". Test: round trip of the 57 states, render unchanged.
2. **Editor stage 1** (UI, FL | LYNX shots first): the program selector, the zone list / key-velocity grid with
   selection, the sample view (root, ranges, loop points), drop a sample = a new zone on the dropped key.
3. **Editor stage 2**: the zone pages (tuning, amp / mod envelopes, LFOs, filters, matrix) and the program page
   (voice mode, glide, effects), all bound to the state through the writer.
4. **Programs and slots**: program list, MIDI-slot → program, .dwp load / save.
5. **The plugin settings left** (stretch modes, HQ interpolation) with FL renders per mode.

## 7. Best way?

- **FL:** regions are edited on the wave itself: tags to drag, a right-click menu per region, Edison's menus above the
  deck, a zoom bar that is also the overview (§1–§3, FL's own save in §2).
- **Ableton:** Simpler's Slice mode shows slice markers on the sample: drag to move, and in Manual mode a double
  click adds or deletes one; a sensitivity control re-slices by transient; slices have no names and no menu of their
  own (Live's manual, Simpler; /mnt/e/lynx-work/ableton/STUDY.md).
- **Users:** no new search in this round; round 190r's sources stand (Slicex liked for depth, disliked for its dense
  panel).
- **Verdict:** FL-exact gestures and menus as the default. A possible addition for the coordinator / user, not
  built: double click in the wave = a marker there (Live's gesture in Manual mode).
