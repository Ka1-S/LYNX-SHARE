# Round 168b — FL's Speech synthesizer (part A)

Part B (Loop Starter, local) is its own document: `REA_rodada168b-loopstarter.md`.

| Words (brief) | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "Speech synthesizer form TSpeechForm, Data/Patches/Speech presets" — what the form has, the text syntax, which engine makes the audio, where the result goes; "load FL's Speech presets" | FLEngine_x64.dll (TSpeechForm, SmpLoader_Speech, .speech INI reader/writer), Speaker_x64.dll (Image-Line's wrapper), DFM of TSpeechForm, the 71 factory `.speech` files — §1–§4 | ⛔ parked in round 165f (§1.9); `.speech` files not listed, not loadable | `.speech` is a sample format: Browser lists FL's factory presets and the user's own, loading one into a channel opens "Speech properties" (FL's form, LYNX look), Accept saves a new `.speech` with FL's suggested name, the sample is spoken by LYNX's own formant synthesizer with one slice per word |

## 1. Which engine makes the audio

- `FLEngine_x64.dll` `FUN_00a65ac0` loads `Speaker_x64.dll` from FL's folder and calls `ZXSpeakInit (0, "ImageLine",
  "FruityLoops", …)` (licence arguments); exports used: `ZXSpeakInit`, `ZXSpeakUp`, `ZXFreeRam`, `ZXSpeakGetErrTxt`,
  `ZXSpeakGetDefaults`.
- `Speaker_x64.dll` = Image-Line "Speaker" 2.0.2 (Delphi; units `ZXSpeak`, `SoftVoice`). It loads
  `Shared\svctl64.dll` ("Copyright 1994-2011 SoftVoice") and `sveng64.dll` (rules and dictionary, © 1993-2005
  SoftVoice, Inc.) at run time. **The voice is a licensed third-party engine** → black box only (rules: never
  decompile third-party code). No harness of the DLL either: `ZXSpeakInit` wants FL's licence strings. So no bit-exact
  target: LYNX's voice is LYNX's own equivalent (§5), and only Image-Line's own logic (the wrapper and the form) is
  followed exactly.

## 2. Image-Line's wrapper (Speaker_x64.dll, FL's own code)

`FUN_0065e260` / `FUN_0065f270` (out/zxspeak.c):
- The text is split at spaces; in each chunk `_` becomes a space (so `big_world` is one chunk, one slice).
- A chunk with `)` that does not start with `(` and has `(` before `)` → the number inside is a pitch offset in
  semitones (`StrToIntDef (…, 0)`), the rest is the word.
- Each chunk is spoken on its own and appended; a Zero-X grid (`.ZGR`, "Positions"/"Names") marks each chunk = the
  slices (`Data/System/Speech.fst` is a Fruity Slicer "Speech" preset, `Speech.zgr`).
- Per-chunk pitch: `note = trunc ((ln p − ln 16.35) / 0.05776226505)`, +1 if the fraction > 0.5, + offset, back to Hz
  as `16.35 · exp (note · ln2/12)` (single), clamped to [10, note (5, 11) = 987.77], rounded.

## 3. FLEngine: the .speech file, the sample loader

- Reader `FUN_00a65d10`: INI section `[Speaker Settings]`, keys `Style`, `Personality`, `Pitch`, `Rate`,
  `VoicingMode`, `Text`; defaults when missing: Personality 11, Pitch 65, Rate 138, VoicingMode 0, Style 2; an empty
  Text is refused. Writer `FUN_00a66010` (same keys, that order).
- `FUN_00a66230` (SmpLoader_Speech): speaks the file into a mono 16-bit buffer and hands it to the sample like any
  audio file. `FUN_009db5b0`: speech folders `Presets\Speech` (user) and `Data\Patches\Speech` (factory).
- `FUN_00fee710` (flags & 0x0c, interactive load into a channel/plugin) → opens the form (`FUN_00fe1180`); renders go
  to the user speech folder (`FUN_0118a850`, folder id 0x40b).

## 4. The form (TSpeechForm, DFM + FUN_00fe11f0…)

- Text edit + **Listen** ("Click to preview the sample"): saves `TempSpeech\Speech.speech` and previews it.
- **Personality** combo, 20 voices in this order: Male, Female, Large male, Child, Giant male, Mellow female, Mellow
  male, Crisp male, Fly, Robotoid, Martian, Colossus, Fast fred, Old woman, Munchkin, Troll, Nerd, Milktoast, Tipsy,
  Choirboy. Choosing one asks the engine for that voice's defaults (`ZXSpeakGetDefaults`: pitch, rate, style, mode)
  and previews.
- **Style** Natural / Monotone / Sing / Random; file code = index + (index > 0) → 0, 2, 3, 4; read back as
  code − (code ≠ 0).
- **Mode** Normal / Breathy / Whispered (index stored).
- **Word rate** wheel 20..500, default 138; release → preview.
- **Keyboard** keys 12..71: `key = round (log2 (p / 440) · 12 + 57)`, stored pitch `round (440 · 2^((key − 57)/12))`.
- **Accept** (`FUN_00fe1750`), if anything changed: suggested name = text with the first letter and each letter after
  a space or `_` capitalised, only `[0-9A-Za-z]` kept, cut to 32, + `_` + personality name + `.speech`; save dialog;
  then that file is loaded. Unchanged / Cancel → the original file is loaded.

## 5. LYNX now

- `src/core/Speech.h/.cpp`: settings, `.speech` parse/write (FL's keys, defaults, CRLF, empty text refused), style
  codes, keyboard maths, `splitWords` / `wordPitchHz` (the wrapper, §2), `suggestedFileName` (§4), and `render()`:
  LYNX's own Klatt-style formant synthesizer (cascade formants, nasal pole/zero, parallel frication, glottal pulse;
  the four styles; each personality is a LYNX voice — formant scale, breath, buzz, comb for Robotoid/Colossus, ring
  modulation for Martian, vibrato for Choirboy, slur for Tipsy…). `src/core/SpeechRules.cpp`: LYNX's own
  letter-to-sound rules, small dictionary and number spelling. Nothing from SoftVoice.
- `personalityDefaults()`: LYNX's own table (FL asks the third-party engine; several values match FL's factory
  presets named after a voice).
- `SampleFiles.h` + `SampleLibrary`: `.speech` is a sample extension; decoding renders it, word starts become the
  sample's markers → "slice" uses them like FL's Zero-X grid.
- Browser: roots "Speech (FL)" (FL's `Data/Patches/Speech` when FL is installed) and "Speech" (LYNX's
  `Presets/Speech`); opening a `.speech` adds a sampler channel after the dialog.
- `src/ui/SpeechDialog.*`: "Speech properties" (text, Listen, Personality, Style, Mode, Word rate, pitch keyboard,
  Accept, Cancel); every change is heard at once.
- LYNX deviation: Accept with changes saves into LYNX's Speech folder under FL's suggested name (a free sibling name
  if taken) without a save dialog, and the Browser shows it at once.
- Tests: `tests/Speech168bTests.inc`. Strings in all 15 languages.

## Where I stopped

- Done: everything above; the 71 factory presets render (spectrograms looked like speech; checked in
  `/mnt/e/lynx-work/speech168b/wav` during the round).
- Not done / next: LYNX's voice is intelligible but plainly synthetic — the letter-to-sound rules cover common
  English only (FL's engine is English-only too). A per-item
  tooltip with FL's personality hints is not shown (JUCE combo items have no tooltips); hints are in `Speech.cpp`.
