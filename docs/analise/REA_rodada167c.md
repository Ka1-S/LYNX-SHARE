# REA round 167c — Edison (Deverb, Convert to score, Run script) and Scratcher DVS / RIAA (v0.1.22)

Evidence markers: [DECOMP addr] Ghidra on FL Studio 2026's own binaries (FLEngine_x64.dll, Edison_x64.dll,
Fruity Scratcher_x64.dll, its engine_x64.dll), [DFM] form resources, [REF] FL's shipped reference text, [SHOT] driven FL /
LYNX screenshot, [TEST]. Clean-room: behaviour only, implemented from this document.

## Where I stopped
- 2026-10-31 14:00: §1 DVS landed on the branch (48edd100); §2 scripts engine + Tools › Scripting UI on the branch
  (e1d61beb, 7bae38e2, string joins). FL's 12 shipped .pyscript files all run on LYNX's engine (harness, defaults, a
  stereo sample and an empty editor). Next: Edison_x64.dll in Ghidra (E:\lynx-work\edscr167\ghidra, project "ed")
  for Convert to score and the Deverb form, then i18n, build, land.

## §1 Fruity Scratcher: DVS timecode input and RIAA preamp

Reporter's words (round brief): "Scratcher: DVS / RIAA (timecode vinyl control) as FL does it — if FL needs hardware we
lack, implement and test with a generated timecode signal."

### 1.1 FL's evidence
- The Scratcher form's TimeCodeComboBox lists, in order: Off, Serato Mk2 Side A, Serato Mk2 Side B, Serato CD, Traktor
  Skratch Mk1 Side A, Traktor Skratch Mk1 Side B, Traktor Skratch Mk2 Side A, Traktor Skratch Mk2 Side B, Timecode CD
  [DFM Fruity Scratcher_x64.dll]. The combo index maps to the decoder type: Off → −1 (off), the last entry (Timecode
  CD) → 0, the others keep their index [DECOMP Scratcher 0x733fe0]; enabling builds the decoder and shows "Could not
  enable Timecode support" on failure [DECOMP 0x72c920]. The saved state (version 0xF462A) keeps the type as an i32 and
  the RIAA checkbox as a byte.
- The decoder lives in engine_x64.dll (exports createTimecodeDecoder (type, sampleRate), destroyTimecodeDecoder,
  gotSignal, getPosition, process) [DECOMP engine_x64 export table]. Type 0 is a Timecode-CD tracker of its own
  (0x180002180); types 1–5 a generic LFSR tracker with a format table (0x1800037c0); types 6/7 a Traktor Mk2 tracker
  (0x180002980 / 0x180002ea0: 2500 Hz carrier, 23-bit register, a different scheme).
- Format table (bits, seed, taps, length, resolution in cycles/s, offset s, flags) [DECOMP 0x1800037c0]:
  Serato A 20 / 0xa5411 / 0xa786c / 710000 / 1000 / 25.7 / 0; Serato B 20 / 0xd76c6 / 0x9b0f2 / 940000 / 1000 / 8.5 / 0;
  Serato CD 20 / 0x30aef / 0x34d54 / 982000 / 1000 / 0 / 0; Traktor A 23 / 0x759c41 / 0x410410 / 1500001 / 2000 / 0 /
  0x70; Traktor B 23 / 0x53e4f7 / 0x410410 / 2109993 / 2000 / 0 / 0x70.
- The decoding [DECOMP 0x180003aa0, 0x180003c10, 0x180003f10, 0x180003e80]: a zero tracker (one-pole, k = 1 − e^(−900/fs))
  and hysteresis 0.004 find the primary channel's zero crossings; flags 0x40 = left is primary (else right), 0x10 =
  direction inverted, 0x20 = the bit is read on the negative peak. At each crossing the bit is |primary − zero| > ref,
  with ref following the peaks (ref += (m − ref)/48). Bits shift into a register; forwards the new bit enters at the top
  (LFSR step), backwards the reverse step is checked; after more than 24 consistent bits the register names the cycle
  through a table of every state → position = cycle / resolution − offset. Pitch: an alpha-beta tracker (α
  0.001953121185, β 7.62937963e-6, one step = 0.25 / resolution per quarter cycle). The wrapper moves the position by
  pitch/fs every sample and jumps to the decoded place when the error exceeds 0.1 s.
- Scratcher's render with a timecode on [DECOMP Scratcher 0x72add0]: RIAA on → a biquad on the input in place
  (coefficients 0x72cdf0: poles e^(−1/(fs·3180 µs)), e^(−1/(fs·75 µs)), zero e^(−1/(fs·318 µs)), gain 20); per sample
  the decoder runs; a valid position → the output is the loaded sample read at position·fs (wrapping on its length)
  and REPLACES the input (no volume / pan / DC blocker on this path); the same position for fs/10 samples → the last
  value holds and decays; no position → decay. Decay factor 0.1^(1/(0.05·fs)), zero below 1e−6. The motor / hold
  logic is skipped while the timecode is on.
- Serato CD's taps lack the register's top bit, so FL's backwards rule never agrees on that format: it locks only
  forwards (reproduced as is).

### 1.2 LYNX before
The Scratcher had no timecode input: its Timecode / RIAA state was dropped on import (the import report said so).

### 1.3 LYNX now
- core/Dvs.h/.cpp: the five LFSR formats with FL's tables, the zero tracker, bit reading, register lookup (tables built
  once per format in the background), alpha-beta pitch and the position wrapper, as above. Types 0 (Timecode CD) and
  6/7 (Traktor Mk2) are listed but not decoded yet: the combo marks them "(not decoded yet)", the import reports them.
- core/Scratcher: parameters Timecode type and RIAA preamp (FL's state round-trips both), the RIAA biquad, the render
  branch (replace, hold fs/10, decay), the motor skipped while the timecode is on.
- UI (Scratcher editor): the Timecode combo with FL's nine entries and the RIAA preamp toggle.
- Tests (tests/Dvs167Tests.inc, generator tests/DvsSignal.h writes a timecode record from the format description): the
  type/state mapping and saved state; the register's sequence and table lookup; every format locks forwards at 0.5×,
  1× and 1.5× with the right pitch and place, backwards within the register's length (Serato CD: speed only); a
  forward-stop-backward scratch is followed within 30 ms; the Scratcher plays the sample at the record's place and fades
  after the needle lifts; the RIAA filter's coefficients and response (+19.3 dB at 20 Hz around 1 kHz).

## §2 Edison: Run script / Edit last script

Reporter's words: "Edison: … Run script (FL's Edison scripts) — as FL 2026."

### 2.1 FL's evidence
- Edison's Tools menu has a "Scripting" section after "Synthesis": "Run script ▸" and "Edit last script" (disabled until a
  script ran) [SHOT D:\lynx-work\edison165\flshots\03-tools.png]. The caption becomes "Edit last script: %s"
  [DECOMP FLEngine 0xcd9fa0; Edison_x64.dll strings "Edit last script: %s", "Run script / run last script"].
- Scripts are .pyscript files; FL ships System\Config\Audio scripts\{Amp, FX, Generation, Regions} and Reference.txt
  ("Edison Script Reference": the enveditor module's API) [REF]. Edison's strings: "Failed to load the script file.
  Please make sure it is saved in utf8-format.", "Script message", "createDialog function needs to return a
  ScriptDialog instance", "Error|Script not found" [Edison_x64.dll strings].
- The script form (TScriptDialog) [DFM Edison_x64.dll]: caption "Configure script"; a Preview checkbox ("Automatic preview
  when controls are modified"); the parameters panel; buttons Run, Edit script, Regenerate, Reset; a wave scope.
- API (Reference.txt): globals Editor (SelectionStartS / SelectionEndS), EditorSample, Utils (ShowMessage, ProgressMsg…);
  classes Sample, Region, ScriptDialog; NF_* constants; a script either runs at the top level (form.Execute()) or
  defines createDialog() + apply(form).

### 2.2 LYNX before
No scripting in the audio editor.

### 2.3 LYNX now
- core/EdisonScript.h/.cpp: LYNX's own `enveditor` module on the embedded Python (pocketpy), written from Reference.txt
  (clean-room: names and behaviour only). Positions are frames, ends included. A script runs on a copy; the editor takes
  the result as one undo step. The UI never waits for Python: a script that asks for a form is run once to read it,
  then again with the answers (Execute() and LoadFromFile_Ask both work this way). restoreFormValues keeps the last
  values per script. Adjacent string literals are joined as Python does (FL's Limiter.pyscript needs it; pocketpy does
  not do it).
- LYNX ships its own small scripts (Amp/Gain, FX/Bit crusher, FX/Ring modulator, Generation/Sine tone, Generation/Noise,
  Regions/Markers to regions); the Run script menu also lists FL's installed folder (read in place, never copied) and
  Documents/LYNX/Audio scripts.
- UI (Audio editor › Tools › Scripting): Run script ▸, Edit last script[: name] (opens the file in the system's editor; a
  LYNX script is first saved to My scripts); the "Configure script" form with FL's controls and buttons: Preview
  (automatic), Regenerate, Reset, Edit script, Run, Cancel. Utils.ShowMessage → "Script message" boxes; errors →
  "Script error" with Python's traceback.
- Tests: tests/EdisonScript167Tests.inc (preview-style and Execute-style scripts, regions, the Sample API, errors, the
  clipboard host, the built-ins, form memory, literal joins). Harness check (not in the repo, FL's files): all 12
  scripts FL ships (Amp, Bit reduction, Destructoid, Limiter, Old school, Ring modulation, Pulse / Saw / Silence / Sine
  generator, Convert markers, Play to end) run to the end with their defaults on a stereo sample and on an empty editor.

## §3 Edison: Deverb

Reporter's words: "Edison: Deverb … as FL 2026, REA on Edison's DLL."

### 3.1 FL's evidence
- Menu: Tools › Spectral › "Deverb..." after "Vocal denoise/isolator..." [SHOT 03-tools.png].
- DeverbMenuClick (Edison_x64.dll 0x876680; FLEngine 0xce4120) checks the trial ("Deverb|This feature is not available
  in trial."), then that the optional feature is installed ("This feature needs to be downloaded before it can be
  used.", "…requires the latest version of deverb module. Please try "Repair optional features"…"); the module is
  loaded by name prefix "deverb_" ("Failed to init deverb module", 0x843e90). FL's Deverb is a separately downloaded
  neural model (not present on this PC, so its output cannot be measured).
- TDeverbForm [DFM Edison_x64.dll], three sheets: Options — Profile combo (General, Voice; default General), Block size
  combo (1–5; default 1), "Limit CPU usage" (limit separation to fewer CPU cores), "Safe mode" (troubleshooting), the
  hint "Choose a profile that best matches the source material"; Processing — "Analysing sample, please wait..." with a
  progress gauge; Result — the Mix wheel (−256 … 256, starts at 256 shown "100"), labels "Original" / "Processed",
  Preview, OK / Cancel.

### 3.2 LYNX before
No Deverb.

### 3.3 LYNX now
- core/AudioEditTools deverb(): LYNX's own DSP (published method: late-reverberation spectral subtraction, Lebart et
  al. 2001 / Habets 2007). The room's T60 is read from the free decays of the whole sample (estimateReverbTime: 10 ms
  energy frames, straight-line fits of falling runs, median slope); the late part's spectrum is predicted from the
  smoothed spectrum 50 ms (Voice: 32 ms) earlier, decayed by the T60, and subtracted with over-subtraction 2 (Voice 3),
  a gain floor of −20 dB (Voice −26 dB) and a slower fall of the gains. Block size 1–5 = an analysis block of 512 …
  8192 frames at 44.1 kHz. Limit CPU usage / Safe mode belong to FL's neural module and are left out.
- UI: Tools › Spectral › Deverb... opens the form (Profile, Block size, Mix, Preview / Accept / Cancel).
- Tests (tests/Deverb167Tests.inc): T60 read within ±30 % (0.5 s and 1 s rooms); in a live room (reverb 10 dB over the
  direct sound) the tail between bursts drops 15 dB (Voice 23 dB) while the bursts drop 1.5 dB (Voice 4.6 dB); mix 0
  leaves the audio unchanged, mix +1 and −1 add up to the original; only the selection changes.
