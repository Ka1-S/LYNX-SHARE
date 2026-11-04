# Round 170e (toolbar170e): the toolbars read at a glance — snap magnet, last tweaked, icons, trackpad zoom

Source: Busy Works Beats' review video of LYNX 0.1.22/0.1.23 on macOS with a trackpad (coord/busyworks-0.1.24.md;
frames /mnt/e/lynx-work/yt-beat/kf 00m37, 05m20, 05m27, 21m28, 22m52 + transcript). User direction (2026-11-04,
corrected): keep LYNX's own design and icon style; only make sure an FL user FINDS things quickly (recognisable
symbols for the key functions, clear tooltips / hint text). FL's toolbar order is not copied.

## Where I stopped

- Done: icons, snap magnet (toolbar toggle + editor boxes), last tweaked caption, Piano roll / Channel Rack / Mixer
  header icons, trackpad pinch + smooth scroll + Windows touchpad pieces, tests (tests/Toolbar170eTests.inc), all 14
  translation tables.
- Checks: see "Checks" below.
- Rerun: `tmp/coord/buildslot.sh tmp/coord/bl-wt.sh toolbar170e`; Windows: `OUT=~/projetos/lynx-wt-toolbar170e/build-win
  tmp/coord/winbuild.sh scripts/build-windows-zig.sh`; pictures: `LYNX.exe --snapshot <prefix> Demo.lynx` with a fresh
  LYNX_PROFILE_DIR + LYNX_TEST_MODE=1 into /mnt/e/lynx-work/toolbar170e/{before,after}.
- Ghidra (read-only, FL's own FLEngine_x64.dll, project /mnt/e/lynx-work/r167g flea): scripts and outputs in
  tmp/r170e (Gest.java, gest.txt, snaplabel*.txt, toolbarform.dfm.txt, eventeditform.dfm.txt).

## Per item

| # | reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| 5 | "Where's the snap to grid? … there needs to be some kind of more prominent icon, because right here I would never know that that was the snap to grid" (05:17, 21:21) | DFM TToolbarForm: `SnapPanel` = `SnapLabel` (a glyph label, OnMouseDown `SnapLabelMouseDown` 0xdce880 → FUN_01213530: toggles the main snap between "(none)" and the last value, like Backspace) + `SnapSelect` combo ("Main snap / quantization / step edit step"). DFM TEventEditForm: `SnapBtn` ("Snap to grid", the editors' magnet, menu on left click) and `ScaleSnapBtn` ("Snap to scale"). Reference shot of FL 2026: C:\Users\kauan\lynx-test\flref\full4.png (magnet before "Line" in the toolbar; magnet at the head of the Playlist toolbar) | toolbar: the word "SNAP" and a "Line (follows zoom)" box; Playlist / Piano roll: a text box "Main (toolbar snap)"; the Piano roll's scale snap used the only magnet icon (an open "U" that read as a letter) | a magnet icon (own drawing: a horseshoe with solid poles) before "SNAP" in the toolbar — click = snap off / back on (as FL's label and Backspace), lit red while it snaps; the Playlist and Piano roll snap boxes start with the same magnet (lit while the window snaps) and say what "Main" is now ("Main: Line", "Main: 1/2 beat"), with tooltips "Playlist snap (magnet) …" / "Piano Roll snap (magnet) …"; the scale snap got its own icon (a small magnet over keys) |
| 2 | "Last tweaked parameter … awkward" — he could not tell what the toolbar box was (00:37) | FL has no read-out: Tools › Last tweaked and a toolbar button `ParamCtrlBigBtn` "Quick tweak last tweaked parameter" (hint "Quick tweak %s", 0xdced30). The read-out is LYNX's own (round 162d, issue #8) | an automation-curve icon, the name and the value, no caption | a knob-with-arrow icon, a "LAST TWEAKED" caption over the parameter name (like SNAP / PATTERN), the value pill; the tooltip says what it is ("the control you moved last and its value") |
| audit | "algumas coisas da barra de cima, ele sentiu dificuldade" (user) | — | text-only: Piano roll "Slide" button, chord box "Note", scale box "No scale", Channel Rack length "A 1", Mixer layout "Wide"; the PATTERN caption | Piano roll: Slide = icon button (note + glide arrow), chord stamp box with a chord icon, scale box with a keyboard icon (each lit when active) and quick tips; Channel Rack length: a length icon (dim = automatic) instead of "A"/"L", quick tip "Pattern length (bars)"; Mixer layout box: the mixer icon before the layout name; toolbar: PATTERN caption with the pattern icon. Every other toolbar / header control already had an icon and a quick tip + hint text (checked on the shots) |
| 17 | "You can't just use your mouse scroll. I have a track pad. You can't just zoom in and out quickly" (22:50, macOS) | WhatsNew.htm 20.5: "Pinch and zoom from the trackpad is too slow", "Two finger pinch gesture causes window resizing" (FL Mac zooms with a pinch); FL Windows: FormMouseWheel Ctrl = time zoom at the mouse (REA round 167t), and Windows precision touchpads send a pinch as Ctrl+wheel; FLEngine imports GetGestureInfo but the editor form only handles press-and-tap on its ruler (TimePanelGesture 0xe569f0, id 0x107) | Ctrl(⌘)+wheel and Alt+wheel zoomed, but a pinch did nothing (no mouseMagnify); smooth trackpad scrolling went through the wheel glide (lag); on Windows every touchpad piece of a notch zoomed / scrolled a whole notch (JUCE marks them notched) | Playlist and Piano roll: pinch = time zoom around the pointer (Alt+pinch: track height / key height); trackpad scrolling moves the view at once in both directions; Windows touchpad pieces (and the pinch as Ctrl+wheel) scale with the fingers; the ruler hints say "[Ctrl][Wheel] or a pinch: zoom" (⌘ on macOS) |

Not changed: the "Param 110" name in his shot is the FL engine plugin's own unnamed parameter (separate issue).

## Checks

(filled in below)
