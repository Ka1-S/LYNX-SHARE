# Round 174d — video tracks and ADR (issue #83)

Request (issue #83, a post-production sound engineer): several video tracks in the Playlist and a native ADR
(automated dialogue replacement) workflow, like Nuendo, Pro Tools, Resolve's Fairlight page, and the alignment tools
those users rely on (Dialogue Match, VocAlign). User decision 2026-11-06: build all of it for 0.1.25.

## 1. What already exists (FL parity, part A)
Round 165g (docs/analise/REA_rodada165g.md §7) already rebuilt FL's **Fruity Video Player** from FL's DLL: its saved
state (version 3, flags, time display, Volume / Mute / Play, path, window size, start / end), FL's 15 time-display
modes (H:M:S:Ms plus 14 SMPTE rates, drop-frame included), the clock (slaved to the host, Ignore tempo changes), the
options menu, the detached window, "Send audio to playlist", and .flp import / export (`core/VideoPlayer`,
`ui/VideoPlayerView`, tests `video165_*`). Decoding goes through the user's own `ffmpeg` executable (one process per
stretch of playback, one frame when stopped).

Gaps left in part A, closed in this round:
- A1. Decoding: scrubbing starts one ffmpeg process per frame (slow, about 100–300 ms), and nothing plays without an
  ffmpeg install. The Video Player moves to the new decoder layer (§3), so it works out of the box on Windows and
  scrubs quickly.
- A2. FL's "Delay playback" › "Set…" dialog and the time display's right-click "type a time" are checked again in FL
  (REA on FL's own DLL only) before they are marked done.
- A3. The [HYP] clock rule in §7 (beats at the song tempo): checked against FL with a tempo-change project, in a
  single FL session under ~/.cache/lynx-fl.lock.

Everything in parts B and C is LYNX's own (FL has no video tracks and no ADR), drawn in FL's Playlist look.

## 2. Architecture

```
core/VideoTime        SMPTE / drop-frame maths, timecode parse and format, frame <-> seconds   (no JUCE, tests)
core/VideoTrack       Song model: VideoTrack { name, clips, height, muted, window }, VideoClip { path, start tick,
                      source offset (s), length, start timecode, fps override }; ProjectFile read / write
core/AdrCue           cue list model, CSV import / export, take list per cue, comp state            (no JUCE, tests)
core/DialogueAlign    offline alignment: features (log-mel energy + spectral flux), DTW, warp path ->
                      a time map; applied with the existing time-stretch (élastique-free, our stretcher)
media/VideoSource     interface: open(path) -> Info; requestFrame(t, size) async; thumbnails(n); extractAudio()
media/backends        MediaFoundationSource (Windows, built in), FfmpegLibSource (runtime-loaded LGPL libs),
                      FfmpegCliSource (the 165g pipe; last resort)
ui/VideoLane          Playlist lanes above the clip tracks: filmstrip thumbnails, clip moves / trims, offsets
ui/VideoWindow        one window per video track (or one window with a track switcher), resizable, detachable,
                      full screen, timecode burn-in overlay, ADR streamers / wipes / beeps overlay
ui/AdrWindow          cue list editor, rehearse / record / playback, takes and comping, align
```

Threading (audio first):
- The audio thread never touches video. It publishes the transport time (an atomic) as it does today
  (`video::Player::currentTime`).
- One decoder thread per open video. It reads the published time, decodes ahead into a small ring of BGRA frames at
  the shown size (8 frames), and drops frames when it falls behind (it seeks rather than decoding every frame).
- The UI pulls the frame whose time is nearest (≤) the transport time on a vblank timer
  (`juce::VBlankAttachment`) and draws it with one `drawImage` (the GPU path of the D2D / OpenGL renderer).
- Scrubbing: a seek to the nearest key frame, then decoding forward to the exact frame, cancelled by the next seek.
- Thumbnails: a low-priority pool thread, cached on disk per file (hash of path + size + mtime) in the profile cache.

Time base: video time = song seconds (tempo-map aware, `Song` already converts ticks to seconds) − clip start +
source offset. Tempo changes never change the video's speed (post-production rule; the Video Player keeps FL's own
rule). Frame accuracy: frames are picked by presentation timestamp, not by counting; drop-frame timecode only
changes the labels.

## 3. Decoding choice and licence
| Path | Platforms | Codecs | Size impact | Licence |
|---|---|---|---|---|
| Media Foundation (`IMFSourceReader`, hardware decode) | Windows 10/11 | H.264, HEVC (with the OS extension), MPEG-4, MJPEG, AAC, AC-3, WMV in MP4 / MOV / M4V / AVI / WMV | 0 | OS API |
| FFmpeg libraries, loaded at run time (`avformat`, `avcodec`, `avutil`, `swscale`, `swresample`, major 61 / FFmpeg 7.x) | all | everything pros use: ProRes, DNxHD / DNxHR, MXF, MKV, VP9, AV1 (dav1d) | 0 when found on the system / beside a user's ffmpeg; +~12–18 MB if LYNX bundles an LGPL build | LGPL-2.1 shared, dynamic: fine with AGPL |
| ffmpeg executable (165g) | all | any | 0 | separate program |

(Superseded by D1 in §6: native first, no bundle, AVFoundation on macOS.) First plan: a `VideoSource` factory tried FFmpeg libraries first (when present), then Media Foundation (Windows), then the
ffmpeg executable. Linux: the distro's `libavformat.so.61` (Ubuntu 24.10+, Debian 13, Fedora 41 ship FFmpeg 7);
other majors fall back to the executable. macOS: FFmpeg libraries (Homebrew) or the executable; an AVFoundation
backend is a later slice (no Mac here to test it). The FFmpeg headers we compile against are vendored (LGPL headers,
only the structs we touch), and the loader checks `avformat_version()` before using any struct.

Not shipped: GPL parts (x264, x265) and any encoder except FFmpeg's native AAC (only used for "export with video").
Export with video stream-copies the picture (no re-encode) and muxes the rendered mix as AAC or PCM — needs the
FFmpeg libraries or the executable.

## 4. UI sketch
Playlist (FL look): a "Video" lane group pinned at the top of the clip tracks, one row per video track with FL's
track header (name, mute, colour, size) and clips drawn as a filmstrip of thumbnails with the file name in the clip
title bar; a timecode ruler row (SMPTE at the project's frame rate) above it when a video track exists.
Video window: frame, optional burn-in timecode, frame rate / timecode read-out, track switcher, detach / full screen /
stay on top. Project settings: frame rate (23.976 … 60, drop-frame), session start timecode.
ADR window (View › ADR, dockable): cue table (number, character, in, out, line, notes, status), actor / take panel
(takes with name, rating, circle), transport strip (Rehearse, Record, Play takes, pre-roll / post-roll, cycle),
cue options (3 beeps, streamer length and colour, wipe), guide output (a hardware output pair = headphones only),
Align (choose the guide, then the take; offline, undoable, keeps the original take).

## 5. Slices
1. **174d-1 media layer**: `core/VideoTime` (SMPTE, drop-frame, parsing), `media/VideoSource` with the three backends,
   the Video Player moved onto it (fast scrub, works without ffmpeg on Windows). Tests: timecode, a decode test that
   generates a clip with a frame-number pattern and checks frame accuracy.
2. **174d-2 video tracks**: model + project file, Playlist video lanes (filmstrip, move, trim, offset, start
   timecode), video window(s), project frame rate / timecode ruler, import (drag and drop, File › Import › Video),
   audio extracted onto an audio clip.
3. **174d-3 export with video**: Export dialog option, stream copy + rendered mix muxed.
4. **174d-4 ADR cues + record cycle**: cue list, CSV, rehearse / record with pre / post roll (uses round 163's
   punch), streamers / wipes / 3 beeps on the video window, guide track to a hardware output, takes per cue,
   take naming ("<cue>_<character>_T<n>").
5. **174d-5 takes, comping, alignment**: take playlists, comp by range, DialogueAlign (DTW) as an offline process.

## 6. Decisions (answered by the user)
- D1 — **no bundled FFmpeg.** LYNX decodes through the OS: Media Foundation on Windows (`src/ui/VideoMediaWindows.cpp`)
  and AVFoundation on macOS (`src/ui/VideoMediaMac.mm`: H.264, HEVC, ProRes; 0 MB). An FFmpeg the user installed is used
  for Linux and for the pro formats (DNxHR, MXF, …): today the `ffmpeg` executable (found on PATH, in the usual
  folders, or with "Locate ffmpeg..."); loading the user's FFmpeg *libraries* is a later slice. When nothing can read a
  file, LYNX shows how to install FFmpeg (`media::ffmpegInstallHelp`: winget, Homebrew, apt, dnf with RPM Fusion,
  pacman, zypper, or ffmpeg.org), as Audacity does.
- D2 — **video tracks run in real time** (song seconds, independent of tempo changes; `videotrack::TempoMap` converts
  for drawing). The Video Player keeps FL's own rule (its tempo clock is checked against FL in A3).

## 7. What was built (wip/video174d)
- `core/VideoTime` (11 rates, drop-frame SMPTE, parsing), `core/VideoTrack` (clips in song seconds, edits, tempo map),
  `core/Adr` (cues, CSV, takes, comp, signals), `core/DialogueAlign` (log-band features, symmetric banded DTW,
  loudness-weighted smoothing, WSOLA render; worst error 14.7 ms on the test syllables). Tests: tests/Video174dTests.inc.
- `ui/VideoMedia` (reader interface, probe cache, FrameDecoder thread: decodes off the audio thread, one frame ahead,
  drops late frames and counts them), the native backends, the ffmpeg pipe fallback; the Video Player moved onto it.
- `ui/VideoTracks`: Playlist video lanes (timecode row, filmstrip thumbnails on a background pool, move / trim with
  frame snap, split, clip start timecode, extract the sound to an audio clip), resizable / detachable video windows
  (burn-in timecode, aspect, always on top, full screen, 50/100/200 %).
- `ui/AdrWindow` (Tools › ADR): cue table, CSV import / export, Rehearse / Record / Play take with pre / post-roll in
  seconds (new `RecordOptions::preRollSeconds / postRollSeconds` on round 163's punch), Cycle (loop record: one take
  per pass), beeps on a temporary channel routed to the headphone output, the guide insert sent to the headphones only
  (muted while playing takes), streamer / punch / the line over the video windows, takes (use, circle, rate, rename,
  delete), comp strip and Apply the comp (trimmed copies with 8 ms fades), Align to the guide (background thread).
- Export with video (Export dialog, single file): the user's ffmpeg muxes the rendered mix (AAC 320k) with the first
  unmuted video track's picture: stream copy when the clip starts at the export origin from its file's start, else a
  re-encode (`tpad` for the gaps, the project rate, libx264 crf 16, mpeg4 fallback).
- Not yet: the FFmpeg shared-library backend (the ffmpeg executable fallback covers Linux and pro formats).

## 8. FL findings (REA on FL 2026 "Fruity Video Player_x64.dll", sha256 59b30311…)
- A3, the clock: the player reads the host's mixing time in absolute milliseconds (host call 0x24 FHD_GetMixingTime,
  format 1 = GT_AbsoluteMS; kept at +0x1a0 in samples and +0x150 in 100 ns units). That is the song's real time, tempo
  changes included. "Ignore tempo changes" only sets the samples-per-tick fields +0x184 / +0x188, which nothing reads,
  and FL hides the item (Enabled = False, Visible = False in the menu's DFM). The 165g [HYP] (beats at the song tempo)
  was wrong. LYNX now publishes the song's tempo map to the instrument (`video::publishTempo`) and hides the item.
  Test: video174d_player_clock_follows_the_tempo.
- A2, the options menu (DFM of the module form): Enable audio, Dock control panel, Ignore tempo changes (hidden),
  Keep aspect ratio, Time display ▸ (Hours:Min:Sec:Ms, then the SMPTE rates 23.976 … 59.94 NONDROP, 72, 60, 120,
  Auto), Send audio to playlist (hidden). There is no "Delay playback" submenu in this menu. The SMPTE captions read
  "48fps NONDROP" / "50fps NONDROP", and the Auto item reads "SMPTE (Auto : x fps)", enabled for 0 < fps ≤ 120. LYNX
  now matches the order and the captions.
- A2, the "Set…" dialog: TSetTimeForm (259 × 164) has four digit wheels (hours 0–99, minutes, seconds, ms 0–999) with
  Accept / Cancel, but its menu item is Enabled = False, Visible = False: FL shows no "Set…".
- A2, the delay: the item "Delay playback by <time>" lives in the start-from-here menu (its popup handler writes the
  caption); "Delay playback until that time" negates the time and sets it. Where FL pops that menu up is checked on
  screen in FL.
- A2, typing a time: a right click on the start / end / current-time labels opens an input box captioned
  "(H:M:S:MS)", or "(H:M:S:Frame)" when the display is SMPTE, prefilled with the label. Integer fields split on ':'
  (S, S:x, M:S:x, H:M:S:x); x is ms, or frames turned into ms at the display's rate; a '-' anywhere makes the time
  negative. FL's frame-rate table is 24 for the 23.976 / 24 modes, 25, 30 for 29.97 / 30, the video's rate for Auto,
  and 1 for the others (a quirk: 48 / 50 / 59.94 / 60 / 72 / 120 treat the frame field as seconds). LYNX uses the
  display's whole rate for every mode; that difference goes to the user for a decision.
- On screen in FL 2026 (round 174d session, shots in E:\lynx-work\video174d\shots): the options menu is opened by the
  small arrow beside the folder button and reads Enable audio ✓, Dock control panel, Keep aspect ratio ✓, Time display ▸
  (no Ignore tempo changes, no Send audio to playlist, no Delay playback); the Time display submenu matches the DFM order
  and captions, with "SMPTE (Auto : 25.00 fps)" for a 25 fps video. The control panel shows while the mouse is over
  the video. A right click on a time opens FL's in-place type-in, not a dialog: a text box over the panel's left edge,
  prefilled with the label (selected), and a hint row "✓ ✕ Current time (H:M:S:MS)" under it. LYNX now opens the same
  box (`InlineTypeIn` in ui/VideoPlayerView). The ◂ beside the start time sets the start from the current time.
- Not seen on screen: FL showed no picture and its current time stayed at 0 while the song played (the video loaded:
  12 s, 25 fps), so A3 rests on the REA reading above. Where FL pops up the "Delay playback by <time>" menu was not
  found in this session.
- Media Foundation (round 174d): MF ignores the MP4 edit list, so a B-frame file written by ffmpeg / x264 reported its
  first frame at 0.08 s (two frames late); the MF reader now counts time from the first frame, as players and ffmpeg do.
- `Song::songLength()` counts the video clips (round 174d): a project with only its picture so far no longer loops
  after one bar.
- Retried for A3 with files FL surely decodes (an H.264 MP4 without B-frames and a WMV2 / WMA file, 1280 × 720 at
  25 fps), loaded both from the project and through the player's own file dialog: FL's Video Player still showed no
  picture and a 0 duration on this machine. A3 stays on the decompile (the host's mixing time in ms).
- Decisions (the user, round 174d): D3 keep LYNX's "Delay playback ▸ Set…" as a LYNX extra; D4 keep LYNX's typed-frame
  rule (frames at the display's rate in every mode), not FL's 1-fps quirk for 48 / 50 / 59.94 / 60 / 72 / 120;
  D5 A3 rests on the decompile.
- The control panel follows FL's layout (on screen): folder, ▾, the chain (Slave playback to host), [∞ | ▶ | ■], the
  volume knob and the speaker (LYNX's pop-out after them); the times under the buttons (start ◂ … now … ▸ end); the
  full-width slider with a round handle at the bottom; no status line (its text is the picture's hint).

