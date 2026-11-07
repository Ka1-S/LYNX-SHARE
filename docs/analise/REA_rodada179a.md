# Round 179a: Linux feedback (slide notes, font, animations, UI lag)

A Linux user on Reddit (9800X3D, 5070 Ti, 32 GB) reported no slide notes, awkward text, no UI animations and UI lag.
Evidence markers: [RUN] run here, [CODE] read in the source, [REA] FL's or Live's own binary, [WEB] with URL.
Data and shots: `/mnt/e/lynx-work/179a/`. Linux runs used a private Xvfb, so no window or input reached the desktop.

**What this machine can't prove:** Xvfb has no GPU, compositor or real refresh rate, the CPU is a Ryzen 3600 with other
builds running (load 12-17), and the runs had no audio device, so meters and the playhead were mostly idle. Relative
findings hold; absolute times are not the reporter's.

## A. Font

[CODE] JUCE's Linux default sans is the first of Verdana, Bitstream Vera Sans, Luxi Sans, Liberation Sans, DejaVu Sans,
Sans (`juce_Fonts_linux.cpp`). JUCE sizes a font so ascent + descent equals the height, so each font gets a different em.

[RUN] Width over 5317 UI strings at the same JUCE height, computed from the font tables (Segoe UI = 1.000):

| Font | Width | x-height | Cyrillic | Regular file | Licence |
|---|---|---|---|---|---|
| DejaVu Sans (today, here) | 1.315 | not read | yes | system | n/a |
| Selawik 1.01 as released | 1.108 | 1.108 | no | 44 KB | OFL, reserved name "Selawik" |
| Selawik, hhea set to its win metrics | 1.000 (calculated) | 1.000 | no | 44 KB | modified: must be renamed |
| Open Sans | 1.034 | 1.045 | yes | 147 KB | OFL, no reserved name in the copyright line |
| Noto Sans | 1.045 | 1.047 | yes | 431 KB | OFL, no reserved name in the copyright line |
| Source Sans 3 | 0.950 | 0.975 | yes | 431 KB | OFL, reserved name "Source" |
| Inter | 1.190 | 1.200 | yes | 412 KB | OFL, no reserved name in the copyright line |

[RUN] Selawik lacks 69 of the characters the Russian UI uses, which then fall back per glyph (shot `font-ru.png`).
All candidates lack `⇄`; Open Sans and Noto Sans also lack `→`, and `←` in pt-BR.

[CODE] JUCE 8 draws Linux glyphs as unhinted outlines from HarfBuzz through its own edge tables, so small text looks
lighter than Direct2D's. A different font narrows this but does not remove it.

Shots: `/mnt/e/lynx-work/179a/shots/font-*.png`, from `--snapshot` with the candidates renamed locally so the existing
binary picks them.

[RUN] Startup cost of the bundled faces: `perf record -F 20000` over a short `--devices` run of the Linux build found
1 of 130 samples in the face registration or the LookAndFeel constructor, about 0.05 ms. The two `.ttf` files add
294,792 bytes of data to the binary.

**User's decision:** Open Sans, unmodified, is the Linux default, plus an interface font setting on every platform.

### Best way? Interface font setting

- **FL:** [REA] `FLEngine_x64.dll` has "Initialize fonts", `Image-Line\Shared\Artwork\Fonts`, "Fruity fonts" and the
  scaling text "Based on current Windows font size", and I found no string for choosing the interface font. [WEB] The
  General settings page in the manual lists scaling and no font option
  (https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_general.htm); a forum user
  reports text not following GUI scaling (https://forum.image-line.com/viewtopic.php?p=1987235). I did not open FL's dialogs for this.
- **Ableton:** [REA] the matching strings in Live's executable are internal ("FontName", "_ZoomDisplay.ZoomDisplay").
  [WEB] Users get zoom and themes; the known way to change the font is an unofficial swap of the bundled files
  (https://forum.ableton.com/viewtopic.php?p=1740141), and users complain about thin text
  (https://forum.ableton.com/viewtopic.php?p=1735218).
- **Users:** requests for readable or changeable text recur in both forums, but this is anecdotal and I did not count them.
- **Verdict:** neither DAW appears to offer this, so it is a LYNX-own addition. The default stays each platform's font.

Design: Settings › General › Interface font lists the default and the installed families, with a sample line in the chosen
font and a reset. It saves `ui-font.txt` in the profile and applies at the next start, like Language, because labels and
caches hold the old font. A saved font that is gone means the default, silently (`core/UiFontChoice.h`, tested).
`--uifont "Name"` overrides it for one run. With a chosen font, CJK glyphs come from JUCE's per-glyph fallback.

**No size control:** the UI zoom in the same section already scales text and layout together. A text-only size is the
reporter's problem by another name: DejaVu's extra width is what made the layouts awkward.

## B. Slide notes

[RUN] On the Linux build in Xvfb: the toolbar icon toggles and lights up; hover shows the tip "Slide notes" and the hint
line; a drawn note carries the arrow mark; the project file stores `slide = true`.

[RUN] Native synth, `--render`, C4 held, slide note to G4 at beat 3 lasting one beat: pitch 60.01 at 0.80 s, 62.61 at
1.05 s, 66.39 at 1.30 s, 67.00 at 1.55 s. Control without the slide stays at 60.01. A slide note alone renders digital
silence, as FL's manual says ("played on its own ... will not produce sound").

[WEB] FL's manual: Slide toggle shortcut **S**, Portamento **O**; slide and porta "only work with native FL Studio
instruments and not VST instruments" (https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll.htm).
LYNX's default key is also S (`KeyMap.cpp`). LYNX also bends plugins (`PluginInstrument::slideTo`), which goes beyond the manual.

**Not verified:** a slide through a plugin. My Nekobi VST3 project did not render (`result=ERROR`, empty message), and I
did not establish why. [CODE] The bend assumes a range of 12 semitones (`PluginHost.h`), so a plugin with another bend
range would glide a different interval. Not measured.

**Finding:** no bug on the native path. The feature is there behind an icon whose tip does not name the S key.

[RUN] FL's own strings (`FLEngine_x64.dll`) name the piano roll button `forms.eventeditform.tlpianopanel.slidebtn`,
with the hint text `|Slide`. Whether FL's hint shows the key was not checked on screen.

**Fix (small, text only):** the slide toggle's tooltip and hint now end with its bound key, `(S)`, the same way the tool
buttons already show theirs (`ToolStrip::refreshKeys`). It follows a remapped key and shows nothing when the key is unbound.

## C. UI lag

[RUN] `--uiperf` on thief.flp, Xvfb 32 bpp, window 1689x950, scale 1 (`perf/b1080a.txt`):

| Case | Painted mean / p95 ms | Frames over 16 ms | Window fps |
|---|---|---|---|
| Fader drag by mouse | 3.4 / 7.7 | 0 of 88 | 93.5 |
| Mixer scroll | 36.9 / 56.4 | 50 of 51 | 34.7 |
| Mixer fader drag, live levels | 35.0 / 59.4 | 57 of 57 | 19.9 |
| Playlist scroll down/up | 19.8 / 33.5 | 12 of 16 | 45.7 |
| Whole window repainted | 22.8 / 35.1 | 38 of 38 | 35.6 |

Fader drag input to paint: p50 7.6, p95 17.9, p99 25.7 ms.

[RUN] Flat profile of a whole `--uiperf` run, all threads including load: `RectangleListRegion::fillRectWithColour`
20.6 %, libc 18.1 %, `EdgeTable` constructor 6.3 %, solid and image edge-table fills about 9 %. libX11 is 1.3 %, so the
X calls themselves are not where the time goes.

[CODE] `juce_Windowing_linux.cpp`: X11 repaints happen only on a message-thread timer at the display's rate (100 Hz when
the server reports none, as Xvfb does), not on a real vertical blank. A repaint can wait a full frame, up to 16.7 ms at
60 Hz, and is skipped while the previous XShm image is pending.

[RUN] A JUCE patch that paints as soon as input arrives (when the last paint is a frame old) gave no repeatable gain in
two A/B pairs at a forced 60 Hz. It is not landed; the patch is kept in `E:\lynx-work\179a\exp-lag-cmake.patch`.

[RUN] OpenGL path (a `juce::OpenGLContext` with component painting on the main window, experiment build only). On Xvfb
it falls back to software GL and is much slower (mixer scroll 66.9 ms mean, 8 fps). On WSLg on the real Windows desktop,
under the GUI lock, window 1689x950, two runs each (`perf/wslg-*.txt`):

| Case | Software (2 runs) | OpenGL (2 runs) |
|---|---|---|
| Mixer scroll, window fps | 44.2 / 48.0 | 15.3 / 15.6 |
| Mixer fader drag, live levels, fps | 26.9 / 28.9 | 17.4 / 17.0 |
| Whole window repainted, fps | 53.8 / 50.3 | 21.8 / 22.0 |
| Fader drag input to paint p50 / p95 ms | 7.2 / 13.0, 7.3 / 12.7 | no frames, 15.8 / 27.4 |

The GL renderer string was not logged, so whether WSLg's GL went through the GPU (d3d12) or llvmpipe is not proven.
Either way JUCE's GL path is slower here, and an optional GL mode is not worth building on this evidence.

What WSLg proves: the same build is faster on WSLg (a real compositor at the display's rate) than on Xvfb, so part of the
Xvfb numbers is the test server. What it does not prove: a native Linux desktop with a real GPU driver (Mesa, NVIDIA)
was not available; GL there could behave differently. Large window and 200 % scale (`LYNX_UIPERF_SIZE`,
`LYNX_UIPERF_SCALE`, kept as diagnostics) were not measured in this round.

**Lag verdict:** no cheap, proven win was found; nothing lands for lag. The time goes to JUCE's software rasteriser
(rectangle fills, edge tables), not to X.

## D. Animations

**User's decision:** build them, based on FL's three settings, light, and zero cost when off.

[REA] FL 2026's General settings form (`FLEngine_x64.dll` DFM, `forms.options.generalpage.disppanel`) has three controls.
Handlers were resolved through the Delphi method tables and disassembled with objdump:

- **Animations** (`FancyAnimBox`, a combo): "Don't distract me", "Keep it sober", "Make it pretty", "Entertain me".
  `FancyAnimBoxChange` (0x13588b0) stores the index 0-3 in one global and writes it as the `Fancy animations` value in
  `HKCU\Software\Image-Line\Shared\General`. 65 code sites read that global. The branches show the levels are thresholds:
  level >= 2 gates the event panel hover (`EventPanelMouseEnter/Leave`), the splitter hover, the automation tracks panel
  paint and the startup sound (`StartSnd.wv`); level 3 alone gates the form focus effect (`FormFocusFX`), the mixer cables
  paint, a touch gesture in the plugin picker and a piano roll mouse-down effect. Level 0 checks appear 10 times.
- **Smoothing** (`SmoothFactorSlider`, hint "Smoothing factor for controls and scrollers"): saved as `Smoothing factor` in
  the same key; 9 code sites read it. It smooths control and scroller motion, not the frame rate.
- **Animation refresh rate** (`AnimRefreshRateCombo`, hint "Choose how smooth animations in the program will be"):
  "Less smooth", "Smooth", "Ultrasmooth". `AnimRefreshRateComboChange` (0x135cdf0) picks a target of 30 Hz, 60 Hz or
  no cap, reads the monitor rate (DWM composition timing, else `EnumDisplaySettings` when it reports at least 50 Hz),
  halves that rate while it is at least twice the target, and sets the animation timer to `round(1000 / rate)` ms.
  It logs "Animation timer set to N ms, vsync enabled/disabled". The "Force refreshes" box calls the same handler.
  Worked example, 144 Hz monitor: Less smooth 36 Hz (28 ms), Smooth 72 Hz (14 ms), Ultrasmooth 144 Hz (7 ms).
  [INFERRED] The rate is always the monitor rate divided by a power of two, so animation frames stay in step with vsync.

[REA] Defaults in the same form resource (`DefaultValue`, DFM int8): Animations = 2 ("Make it pretty"), Animation
refresh rate = 1 ("Smooth"). So FL ships with its hover and panel motion on.

[REA] Does FL fade a docked panel? In code, no path was found:
- the only window-animation API FL resolves is user32 `AnimateWindow` (by name at runtime, global 0x13dfd78). Its single
  caller (0x61b932) is the hint window: it asks Windows for `SPI_GETTOOLTIPANIMATION` (0x1016) and `SPI_GETTOOLTIPFADE`
  (0x1018) and animates for 100 ms, so it follows the Windows tooltip setting, not FL's Animations level;
- the "AnimateAlpha" / "AnimateSize" strings belong to a particle system's properties (Spread, ParticleLifetime, Damping...);
- none of the 65 level checks sits in a named panel show/hide handler (the named ones are hover, paint and focus effects).
This is absence of evidence from code, not proof, so FL was also watched:

[RUN] FL 2026 on Windows, `Fancy animations` = 2 ("Make it pretty", read from the registry, not changed), under the GUI
lock with the idle gate and `safeinput.ps1` before each key. F9 was sent to FL, then the FL window was captured 14 to 24
times at about 36 ms per frame (`E:\lynx-work\179a\flf\close*`). First F9 (the Mixer not focused): the Mixer came forward
and was at its final size and place in the first frame, 91 ms after the key. Second F9: the Mixer was gone in the first
frame, 82 ms after the key; the frames after that differ by at most 0.02 grey levels. So FL shows no panel fade of 80 ms
or more. A shorter one cannot be excluded at this capture rate. The reopen was not captured (the user became active).

**Result:** LYNX's 110 ms panel fade is a LYNX-own extra, not FL behaviour. It is kept and marked as LYNX-own; the user judges it.

Not observed on screen in FL: these are read from code, not watched.

[CODE] LYNX already has FL's third setting: Settings › General › Animation rate (round 174e2), Display / 60 Hz / 30 Hz,
paced on the display's vertical blanks with the same "every other frame" division (`core/FramePacing.h`). FL's
"Ultrasmooth" = LYNX "Display". The FrameTicker detaches after ~0.3 s of no motion, so the playhead and meters already
cost no repaint when idle. What LYNX lacks is FL's first setting (the motion level) and the transitions it gates.

[REA] `FLEngine_x64.dll` holds "Animations", "Entertain me", "Fancy animations", "MenuAnimation", "HintAnimation",
"Animation refresh rate", "Force refreshes" and "Smoothing factor for controls and scrollers".
[WEB] FL's manual: Animations covers "menu, mouse related, overlay and pop-up animations" from "Don't distract me" (off)
to "Entertain me"; "Smoothing" and "Animation refresh rate" are separate
(https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_general.htm).
Not yet observed running in FL.

[REA] `Ableton Live 12 Trial.exe` and `Ableton Live Engine.dll`: the animation strings are widget classes
(`AAnimationView`, `ASyncAnimationModel`, `AAnimatedBitmapView`) and no setting labels. Earlier pass: only 16 strings match; relevant ones are "Drawing::Disable Insert Marker Animation", "AnimateWhen" and a progress
animation dialog. No user-facing animation setting found among these strings. That is weak evidence, not proof.

[WEB] Direct user opinion on DAW UI animation is thin. A Wine bug report ties FL's context menu freeze to the Animations
setting (https://list.winehq.org/archives/list/wine-bugs@list.winehq.org/thread/ERMCOEYX4AQJDRJBQXNAOBMX5WCTCJVH). General UI writing says
frequent actions should have little or no motion. No source found on Bitwig's transitions.

### Best way? Interface animations

- FL (REA above): a four-level motion setting (default level 2), a smoothing factor for controls and scrollers, and a
  refresh-rate cap tied to the monitor rate.
- Ableton Live 12 (REA above): animated widgets exist, but no user-facing animation setting was found in its strings.
- Users (WEB above): little direct opinion; a Wine report blames FL's Animations for a context-menu freeze, which is an
  argument for an off switch.
- LYNX before this round: hover fades (IconButton), wheel glides (Playlist, Piano Roll) and display-rate playhead and
  meters already existed, always on; Animation rate already matched FL's refresh-rate setting.
- **Verdict (coordinator OK, 2026-11-07):** one on/off switch instead of FL's four levels, because FL's levels gate
  effects LYNX lacks. On by default like FL's default, as the user asked. It gates the panel fades (new), the hover
  fades and the wheel glides. Off snaps all three and starts no timer. The playhead and meters are live views and
  stay on Animation rate. FL's four levels are not copied: levels 2 and 3 gate effects LYNX does not have (a startup
  sound has its own LYNX setting, mascot dance, focus effect). Their mapping is recorded above if the user wants levels later.
