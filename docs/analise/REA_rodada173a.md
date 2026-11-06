# REA round 173a: issues #70, #72 and #73 (reported on 0.1.23)

Per item: the reporter's words | evidence (FL, the video, LYNX) | LYNX before | LYNX now.

## #70 "Notes keep on cutting off when two or more play" (Windows 10, LABS VST3)

- **Reporter:** a new project, LABS (Spitfire, VST3) on a channel, a piano part with 20+ notes; "when 2 or more notes
  play it will cut off the other notes playing". "Use any instruments".
- **The video** (`70.mp4`, 112 BPM, ffmpeg frames + spectrogram of its audio, `/mnt/e/lynx-work/issues173a/vid`): the
  piano roll holds G3 for the whole bar, D4 from beat 0.5, G4 / B4 / A5 from beat 1, F#5 from beat 1.5 (chords, see
  `pr.png`). In the audio each new note on ends every note that sounds: G3 (196 Hz) stops exactly where D4 (294 Hz)
  starts, D4 stops where the G4 / B4 / A5 chord starts, and so on (`spec2.png`). The audio (`a.wav`, 21.9 s) has two
  plays. 1.2–10.0 s (Song mode, the reference audio clip "Soft Piano 112" on track 1): G3 sustains through the bar
  (108–134 dB in its band). 11.7–20.4 s (Pattern mode, LABS): G3 falls from 138 to 98 dB within 0.12 s of D4's start
  (11.94 s); D4 falls 128 → 93 when the G4 / B4 chord starts (12.18 s); B4 falls when F#5 starts (12.42 s); F#5 then
  sounds alone until the next note (13.20 s), which takes it 109 → 46 dB. So each new note on ends every note that
  sounds, long before its note off: one voice, the newest note.
- **LYNX's notes:** the scheduler is right. A dense overlapping part (the video's, two passes, the loop too) reaches the
  instrument as one note on at each note's start and one note off at its end, nothing early and no All Notes Off
  (test `issues173a_overlapping_notes_each_end_at_their_own_end`, the rack's real renderer). The whole chain with a
  real VST3: the Linux build exported (`--export p70.lynx --pattern`) a 32-note version of the video's part (16 notes
  a bar, up to 13 held together, same-key notes end to start) through a JUCE-built polyphonic VST3 (32 voices, logs
  every event it gets, `/mnt/e/lynx-work/issues173a/jplug`, `probe/`): all 32 note ons and 32 note offs arrived, each
  on its note's own sample (worst 0.5 sample), and up to 14 voices sounded at once (13 held + a release tail).
  JUCE-built plugins ignore noteIds, so the probe plays chords before and after the fix alike; LABS does not. Channel Mono, Max
  polyphony and Cut itself are off on a new plugin channel.
- **FL evidence (Fruity Wrapper, FruityWrapper_x64.dll of FL 2026):** the VST3 note on is built at 0x84d0e0: an Event
  of 0x30 bytes with busIndex, sampleOffset, ppqPosition, the channel, the pitch, tuning = (pitch − whole pitch) × 100
  cents, velocity = byte × 1/127, and `noteId = param_3`, the id of FL's voice; `noteId = -1` only when `param_3` is
  the wrapper's "no voice" sentinel (`*PTR_DAT_008f6c98`, which only the live MIDI input path 0x84cbd0 passes). The
  note off (≈0x84d000) carries the same voice id. So FL gives each note its own noteId, and its note off the same one.
- **LYNX before:** a hosted VST3 got its notes through JUCE's host (juce_VST3Common.h `MidiEventList`), which writes
  `noteId = -1` into every note on and note off. In VST3 a plugin may tell its voices apart by noteId; one that does
  takes a note on whose id already sounds (−1, always) as that note again and moves its voice to the new pitch: one
  voice for the whole channel, the newest note — the video's sound. (JUCE-built plugins ignore noteIds, which is why
  LYNX's own test plugins never showed it. LABS is not installed here and no installed VST3 instrument keys its voices
  by noteId, so the model below stands in for it.)
- **LYNX now:** cmake/LynxPatchJuce.cmake fix 14 (CMake builds and the Windows build's patched copy of
  juce_audio_processors): `MidiEventList::hostToPluginEventList` numbers the host's note ons (per plugin instance,
  counting up from 0) and gives each note off the id of the oldest sounding note on its channel and key (poly
  pressure too; a note off without a note on: −1 as before). Same-key notes end to start (off and on at one sample)
  take two ids.
- **Check:** `/mnt/e/lynx-work/issues173a/noteid` (check.cpp): the video's first bar through JUCE's real
  `hostToPluginEventList`, before (JUCE 8.0.4 as shipped) and after (patched), and a model plugin that keys its voices
  by noteId (a note on with an id that already sounds takes that voice over; a note off ends the voice with its id).
  Before: 8 note ons, 1 distinct noteId (−1), 0 note offs matched; the model plays at most 1 note at once and 6 notes
  are taken over by the next one, the video's sound. After: 8 note ons with ids 0…7, every note off carries its note
  on's id (D4 ending at 75857 gets id 1, the D4 starting on the same sample id 6); the model holds the bar's 7 notes
  together, none taken over. (Printed by the check: before.txt / after.txt.)

## #72 "File chooser fails to open or load the selected file" (Arch Linux, PipeWire, tar.gz)

- **Reporter:** opening an audio file (Edison-style audio editor), choosing a sample (Sampler, Wave Traveller, Patcher):
  "the file chooser is not working", failure not characterised.
- **JUCE's Linux chooser (juce_FileChooser_linux.cpp, 8.0.4):** the native dialog runs `zenity --file-selection` (or
  `kdialog` in a KDE session or without zenity) as a child process and reads the path from its stdout. It never checks
  that the dialog opened: a zenity that cannot start (missing GTK / library, a display it cannot open) exits at once
  with nothing printed, which JUCE reports as "cancelled" — nothing opens, nothing loads, no message. zenity and
  kdialog filters also match case-sensitively (JUCE's built-in chooser does not): `*.wav` hides `LOOP.WAV`, so a
  supported file cannot be picked.
- **LYNX before:** 61 choosers in 26 UI files, each `std::make_unique/make_shared<juce::FileChooser>` with JUCE's
  default (native when zenity or kdialog is found by `which`).
- **LYNX now (src/ui/FileChoosers.h, every chooser of the UI):**
  - Linux: the native dialog only when the program JUCE would run is on PATH (searched directly) and answers
    `--version` with exit code 0 within 5 s (checked once per run); otherwise JUCE's built-in chooser.
  - A native dialog that ends without a file in less than a second (it never showed) switches the session to the
    built-in chooser and opens it at once in its place, with the same title, folder, filters and callback.
  - zenity / kdialog get each filter pattern in upper case too.
  - `LYNX_FILE_DIALOG=builtin` / `native` forces one.
  - Lifetime: the chooser stays owned by its editor (`launchChooser (holder, …)`); a fallback chooser replaces it in the
    same holder and the old one is deleted after its callback returns.
- **Check (Linux build, WSLg):** `LYNX --choosertest out.txt file` (opens a chooser through makeFileChooser +
  launchChooser like the UI; a built-in dialog is shot, the file typed in and confirmed), LOOP.WAV in a folder with
  tone.wav, fake zenity programs on PATH (`/mnt/e/lynx-work/issues173a/chooser`):
  - no zenity / kdialog: built-in dialog after 233 ms, LOOP.WAV reaches the callback — OK;
  - a working zenity (answers after 1.5 s): native, LOOP.WAV reaches the callback; zenity got
    `--file-filter=*.wav *.WAV *.flac *.FLAC` — OK;
  - a zenity that exits at once (cannot show): the built-in dialog opens in its place after 206 ms (shot
    `broken.txt.png`: both files listed), LOOP.WAV reaches the callback, the session stays built-in — OK;
  - a zenity that cannot start (`--version` fails): built-in from the start — OK;
  - the same broken zenity with `LYNX_FILE_DIALOG=native` (= 0.1.23's behaviour): the callback gets no file after
    110 ms, nothing opened — the report.

## #73 "SoundFont Player detune not being imported correctly"

- **Reporter:** an FL 12 project with a SoundFont Player detuned by −400 cents plays at its normal pitch in LYNX.
- **FL evidence:** FL 2026 rendered (`FL64.exe /R /Ewav`) four projects written by issues173a_write_fl_detune_probes:
  FL's SoundFont Player (state v6, FL's own `Default.sf2`) on one long C5, the channel PITCH knob (0xDB levels +8) at 0
  or −400 cents with its range 12, Misc › Fine tune (0x8E) at 0 or −40. Pitch of the strongest partial, 0.5–1.5 s:
  knob 0 = 130.04 Hz; knob −400 = 103.21 Hz (**−400.0 cents**); fine −40 = 127.07 Hz (−40.0); both = 100.86 Hz
  (−440.0). So FL's SoundFont Player follows the channel's PITCH knob, the Misc fine tune and both together, to the
  cent.
- **LYNX before:** the import was right: FL's channel PITCH knob (.flp 0xDB levels, offset 8, cents) became the
  channel's pitch (−4 semitones; range 0xD7 + 112), Misc › Fine tune (0x8E) the channel's fine tune. But the LYNX
  Soundfont engine that plays FL's SoundFont Player ignored the channel's PITCH knob: the rack hands it to the
  instrument as `setChannelPitchNow (knob cents, fine + main pitch)` before each block, and the Soundfont instrument
  overrode neither it nor `setLivePitch` (the default `setLivePitch` does nothing). Only the per-note part (fine tune, main pitch) reached it.
- **LYNX now:** the Soundfont instrument takes the knob (applied to every sounding voice, live) and the rest (each voice
  keeps the rest it started with, so a live main pitch move counts once). Other generators: the knob reached the
  sampler, the LYNX Synth, FL's engines and hosted plugins already (issue #14 and round 167a audits).
- **The same FL projects through LYNX (Windows build, `--export`):** main before this round (4f46c244): knob −400 =
  130.04 Hz, **+0 cents** (the report). This round: 130.04 / 103.21 / 127.07 / 100.86 Hz = 0 / −400.0 / −40.0 / −440.0
  cents, FL's numbers to the hundredth of a hertz.
- **Tests:** issues173a_soundfont_player_follows_the_channel_pitch_knob (−400 → −400 cents, fine −40, both −440, a
  live knob move +200, a live main pitch move +100), issues173a_write_fl_detune_probes (FL's renders).
