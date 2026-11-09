# Round 185b — issues #124, #125, #126 (FL 2026 26.1.7, [EXEC])

## #124: how FL 2026 writes sample paths
An FL-saved project's six sampler channels were pointed at copies of one kick in different folders, opened in FL and
saved with Ctrl+S. FL wrote (event 0xC4):

| Sample folder | Written as |
| --- | --- |
| `Documents\Image-Line\FL Studio\Audio\...` (user data) | `%FLStudioUserData%\Audio\...` |
| `Documents\Image-Line\Downloads\...` (FL Cloud sounds, downloads) | `%FLStudioDownloadsData%\...` |
| `Documents\Image-Line\...` (Shared data) | `%ILSharedData%...` (no separator after the variable) |
| next to the project / any other folder | the full path (FL keeps no project-relative path) |
| `C:\Program Files\Image-Line\FL Studio 2026\...` | `%FLStudioFactoryData%\...` |

The Shared data folder is FL's `HKCU\Software\Image-Line\Shared\Paths\Shared data` (here under OneDrive). LYNX did not
know the Downloads and Shared data variables (fixed: `FlPathRoots::downloadsData` / `sharedData`).

## #125: a moved control and its automation clip
`z125_full.flp`: a flat automation clip (0.2) over the whole song on channel 0's volume; `z125_ramp.flp`: a 0 -> 1 ramp
over 16 bars. The channel volume knob was moved with the mouse (shots in /mnt/e/lynx-work/issues185b/shots):

- Opening the project (stopped): the knob shows the clip's value at the song position.
- Stopped: a moved knob keeps its value (3 s later still 36 %, not 20 %); moving it again also sticks.
- Play: the knob jumps to the clip's value at the position (20 %; on the ramp 13 % at bar 3).
- Moved while playing: the value stays for the rest of that playback, on the flat clip and on the ramp (2 s later
  still 32 % while the ramp went on); stopping keeps it; the next start puts the clip back in charge.

LYNX: `AudioEngine::noteTweaked` (knob drags, plugin gestures, linked controllers) releases the target from its
automation (`RackRenderer::setReleasedSlots`) until the transport starts again. While recording automation the touch
rule of round 146 applies instead. A controller is taken to behave as the mouse (no hardware controller here).

## #126: recording into a looping stretched clip
FL plays a stretched clip once and a take in it grows the pattern (round 168k). LYNX's loop option had folded the take
into the pattern's length (281a06893), so a take over an empty clip only filled its first bar; the take now lands where
it was played in both modes.
