# Round 190fg: follow-ups of 0.1.27

Four small items collected while 0.1.27 landed. No new UI design; no new strings.

## 1. pocketpy: the memory test and a crash on one huge allocation

- **The flaky test.** `vfx_script_memory_runaway_is_stopped` filled LYNX's real 768 MB heap limit. That costs the Python
  thread about 0.5 s, nearly all of it kernel time for the page faults (measured with a pocketpy harness: 0.15 s user +
  0.31 s system for the old pattern). Each VFX Script call has a time limit: 3 s of the Python thread's CPU time, and
  12 s of wall time as a backstop. Under six parallel test shards plus builds, the page faults slowed down (memory
  pressure), and once the time limit came first: the script stopped as "took too long", and the memory status the test
  looks for was missing. The product behaved correctly; the test depended on load. Now the test lowers the limit to
  64 MB (`py_lynx_heap_limit`, put back at the end). The counter and the stop work the same at any limit, and the
  shipped 768 MB is checked on its own. The "memory comes back" half is stronger: a 48 MB list must fit under the same
  64 MB limit after the runaway VM is released.
- **Can a real script escape the stop under load?** No. The heap check runs synchronously on every allocation
  (`py_lynx_heap_allows`), so load cannot slip past it. Under load the time limit may stop a slowly allocating script
  first, with "took too long". That is still a stop, and it happens before the heap passes the limit plus one
  allocation.
- **What did escape (found while checking):** one C-level operation that asks for more than twice the limit in a
  single call. Examples: `'x' * 2000000000`, `[0] * 300000000`, `list(range(200000000))`. Past 2 × 768 MB,
  `py_lynx_malloc` refuses the allocation, and pocketpy then either aborts (`c11_vector__reserve(): out of memory`,
  exit 134) or writes through the null pointer (SIGSEGV, exit 139). One line of a script took LYNX down. Now str /
  list repeat check the size first, and `list()` of an iterator checks the heap as it grows. Each of them stops the
  script with the usual memory message (`/mnt/e/lynx-work/follow190fg/mini/result.txt`: before 5 crashes, after 0).
  Other single C calls that could reach 1.5 GB in one go are still possible in principle (none found in FL's factory
  scripts).

## 2. VST3 note ids of nested same-key notes (190ej follow-up)

JUCE fix 14 gave a note off the id of the **oldest** sounding note of its key. Take a LIFO pair (a long note C with a
short note D of the same key inside it): D's end carried C's id, so a VST3 that keys its voices by noteId ended C and
left D sounding. FL ends each note's own voice (190ej [EXEC]); its wrapper sends a note off with the id of the voice it
ends (REA 173a).

Fix 26 (`cmake/LynxPatchJuce.cmake`, idempotent like the others): `MidiEventList` asks `lynxNoteTagHook` for the LYNX
note of each note on and note off it converts. It keeps the tag beside the id it gave the note on, and a tagged note
off gets the id of the on with the same tag. LYNX queues one tag per note on / off written on MIDI channel 1 for a VST3
(`hostpitch::SentKeys::tag`), in the order they go out: the note's `NoteEvent::noteId` from the rack, or 0 for a live
key or a held key's release. A note off without a tag keeps fix 14's oldest-note rule. A velocity-0 note on goes out as
a note off, so it is queued as one.

Proof: `LYNX --vst3noteidtest "LYNX Test Synth.vst3" report` with the repo's test synth. The synth now keeps its voices
by noteId and logs the ids. Two renders: one with the tags (D's end carries D's id), one with the hook off (fix 14
alone: D's end carries C's id). Core test `follow190fg_vst3_note_tags_pair_each_off_with_its_own_on`.

## 3. The song meter's denominator

190em gave a 6 / 8 scene a 3/4 song bar: the same length, but three quarter beats. The song meter now has a
denominator, `Song::beatUnit` (1, 2, 4, 8 or 16). A `Meter` gives the bar and beat lengths: a bar lasts 4n/d quarters,
and a beat is a 1/d note.

- **FL:** the time signature list behind FL's grid computes beat ticks = (16 / denominator) steps and bar ticks =
  numerator × beat (REA_rodada190ax [DECOMP FUN_010628b0 / FUN_01062830 / FUN_01062880]). A 6/8 bar is 12 sixteenths,
  which is 3 quarters. FL's project time signature (0x11 / 0x12, "Set as time signature": steps per beat = 16 / d,
  REA_rodada168j E14c) is read and written now. The Playlist's signature markers already used this law in LYNX.
- **Live:** a bar of n / d at a quarter-note tempo; the metronome follows the denominator (eighths in 6/8).
- **LYNX now:** bars (`barsBetween`, `barAt`, pattern lengths, song length, launch quantization), the metronome (one
  click per beat of the meter and of each signature marker, `metronome::beatAt`), the plug-ins' play head (time
  signature n / d, last bar start), position fields (bar : beat counts the meter's beats), channel loops "next beat",
  and Piano roll / Playlist bar tools all follow the meter. A scene launch gives the song its n / d as it is.
- **Saved:** .lynx `"beatUnit"` only when it is not 4 (older files read n/4; older readers ignore it). .flp 0x12.
  .als `TimeSignature/Manual` (Live's code, both ways; a set's start signature becomes the song meter, no longer a
  marker at 0). MIDI time signature meta event (both ways).
- **Unchanged for n/4:** every formula gives the old value at d = 4 (tests check all numerators 1-32).
- **Approximate / left:** the Audio editor's warp rulers count quarter notes (a 7/8 bar is rounded to 4 there). The
  Piano roll and Playlist beat lines stay on quarters (the bar lines follow the meter). The song meter's denominator
  has no editor of its own (Settings › Project shows it in time-signature mode). A scene, an .als, .flp or MIDI file
  sets it.

## 4. Pocket effects' knob names in other languages

The mixer's knob page (`EffectsPanel`, the knobs under an insert's slots) drew `EffectInfo` names untranslated. It now
goes through the translation table, like the generated editor page and the Pocket panels do. Every Pocket control name
is in all 14 tables (test `follow190fg_pocket_knob_names_are_translated`).
