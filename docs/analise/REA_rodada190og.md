# REA round 190og: FL Studio Mobile export (.flm writer) and the last FLM format unknowns (2026-11-21)

LYNX SHARE 0.1.31. Owner (11-21): "use REA on all of them and continue the REA until it covers everything, including FL
Studio Mobile, and FL Studio Mobile project export and import." This page is the FLM part of round 190og; the coverage
re-measure of the five programs is `/mnt/e/lynx-work/coverage190og/COVERAGE-0.1.31.md`.

Clean-room: every fact below is in our words. Image-Line's code is cited by address or by its exported name only; no
decompiled code, table or data was copied into LYNX. Tags: [DEC] static reading of FLM's code, [FILE] holds in FLM's own
files, [EXEC] measured on FL's own output, [HYP] not proven.

Sources:
- **W** the FL-hosted `FL Studio Mobile_x64.dll` (FL Studio 2026), the read-only Ghidra project of rounds 188f–190fn;
  the loader 0x18076b9e0, the channel writer 0x180757d80 and reader 0x180758ac0 (raw decompiles stay in
  `/mnt/e/lynx-work/reaflm190fn/rea/`, never in the repo).
- **A** FL Studio Mobile 5.0.3 for Android, `libFLM.so` (the library keeps its C++ names), disassembled with capstone
  (`/mnt/e/lynx-work/reaflm190fn/tools/dis.py`; this round's notes in `/mnt/e/lynx-work/coverage190og/flm/rea/`).
- **F** FLM's own files in FL's copy of its resources: 11 demo songs and 9 new-song templates (`Installed/Songs`,
  `Installed/New Song Templates`), read as data.

## 1. What a file needs for FLM to open it (the writer's rules)

| Fact | Evidence |
|---|---|
| The loader takes "10LF", then reads chunks while 8 bytes are left: tag, u32 size (capped at what is left), body. Each known tag is tried in turn; **a tag it does not know is passed over without a message**. "Error when loading chunk %s" is logged only when a known chunk fails to read (its failure flag), and loading goes on. So a writer may add chunks of its own: FLM skips them, and drops them when it saves the song again | [DEC] W 0x18076b9e0 (the per-tag comparisons, the failure flag set only on a short known chunk, the log call) |
| Order FLM writes: HEAD, KEYB (4.x and later), META, TDIV, the racks (master first), then the channels in the same order — rack n belongs to channel n | [FILE] all 20 files; [DEC] W (the rack lookup per channel, "Missing rack for Channel #…") |
| HEAD of a 4.7+ file: 358 bytes + the app version as u32 length + text ("4.7.14", "4.8.3"); older songs end at 353 | [FILE] |
| The view doubles of HEAD (+296…+327) hold the playlist's scroll and zoom: (0, y, ≈0.0091, ≈0.0076) in the templates | [FILE]; LYNX writes the templates' zoom so a song opens framed |
| Containers carry their version magic: "10KR" racks, "20HC" channels, "20LC" clips, "20LS" samples ("10LS" in the oldest), "10MS" drum sets; RSMP's u32 after "10MS" is the number of kit samples | [FILE] |
| A module (RMOd): u32 type, u32 instance id, then HEAD (u32 type, u32 0, u8 1), DESC (256 + 256 + 4 bytes), PRST (two counted strings), ADD1 (3 bytes), PRMS, SMPR (3 floats), CSTM when the module has state | [FILE] every RMOd of the 5.x templates |
| An audio track's rack keeps one empty sample slot ("<empty>", no file) | [FILE] "Audio Recording" template |
| CHHD of a "20HC" channel: 1084 bytes + type name, user name and a third name as counted strings; the master holds the view doubles (0.0125, 28.0) at +1040 / +1048, the other channels zeros | [FILE] |

## 2. CHHD +1056 / +1060: input monitoring and record arm (format-0044, FLM-FACTS U2 — closed)

190fn left the two u32 after CHHD's view doubles as "INFERRED mute / solo". They are not:

| Field | Meaning | Evidence |
|---|---|---|
| +1056 | the channel's **input monitoring** switch (the rack mixes the device input into its buffer while it is on) | [DEC] A: `CSeqChannel::GetState` 0x9f9198 writes the channel's byte +0x221 here; `CChannelRack::RenderSound` 0x57fdc4 tests that byte before it takes the input (0x5802c4); `CChannelRack::ControlValueChanged` sets it |
| +1060 | the channel's **record arm** | [DEC] A: `GetState` writes byte +0x220 next; `CSeqChannel::IsArmedForRecording` 0x9fbf38 returns it when multi-track recording is possible; set by the same rack control |

Mute and solo live in the rack (RPRM [2] / [3], FLM_FORMAT §3), as before. LYNX's import leaves both switches alone
(LYNX's own arm and monitoring are per track and start off); the export writes 0, FLM's default.

## 3. The export (`core/FlmExport.h`, `ui/FlmExportDialog.h`)

File › "Save a copy as FL Studio Mobile project (.flm)..." — the same list window as "Save a copy for an older LYNX"
(what may sound different / what is left out), then a file chooser; the samples are copied into "<name> Samples" beside
the .flm (FLM's "@L@" library layout, as FLM's own share zip keeps it), and FLM's own factory content stays an "@R@"
reference (nothing of Image-Line's is copied). `LYNX --export-flm <project> <out.flm>` does the same without a window.

Every law is the .flm import's, turned around, so that LYNX → .flm → LYNX keeps the values:

| LYNX | FLM | Law |
|---|---|---|
| tempo, beats per bar | HEAD +264, TDIV (beats per bar, 4 steps per beat) | as they are; a beat unit other than 4 is listed |
| project key | HEAD key locker (root, scale index) | the FLM scale with the same degrees (none: listed) |
| Chord pad set | KEYB JSON | the import's own shape |
| title, author, comments, created, working time | HEAD title, META | dates as UTC "YYYY-MM-DD HH-MM-SS", time in seconds |
| an insert used by one channel | that channel's rack: RPRM level / pan / mute / solo, the insert's effects as modules | level v = ((log10 g + 3) / 3.3)^(1/0.6) (FLM's fader); pan (p + 1) / 2 |
| an insert several channels use, or one that receives sends | a bus channel (0x80); the users send into it | SEND words (target id, 1, min(g, 1), 1, 0.853, fader value) |
| the Master's level and effects | the master rack | as above |
| a channel's volume and pan | the instrument's wrapper: Post Gain × Mix, Wet Pan | Post Gain = (log10 g + 1) / 2 down to −20 dB, the rest on Mix |
| FLM's own engines in LYNX: Pocket SuperSaw, MiniSynth (FLM's build), GMSynth (FLM), Transistor Bass (FLM's build), DW Sampler (FLM's engine, its zones as "10WD" ZONE records + MIDI) | modules 23, 0, 2, 19, 1 | their values as stored (each import keeps FLM's own values) |
| FL Studio's MiniSynth / Transistor Bass | modules 0 / 19 | the same values; the sound differences of FLM's build are listed |
| 3x Osc (an .flm's, or an .flp's in the same state layout) | module 39 (149 values) | `threeOscState`'s laws turned around: the oscillators' raw values, the five LFO / envelope records, the filter, FL's channel settings (polyphony, porta, gate, key / velocity tracking, echo, arpeggiator); a desktop 3x Osc gets FLM's octave back on its coarse values (FLM plays one octave lower); the level on Volume × the volume envelope's amount |
| automation clips: an insert's level / pan, Pocket effects' parameters, the DW Sampler's and GMSynth's own values | automation lanes (module 0 Level / Pan, module n parameter k + 1 …) | the import's lane laws turned around; FLM's lanes hold the curve as points, so a curved LYNX segment is sampled every 1/32 beat; other targets are listed |
| sampler channels that share an insert | one drum channel: a kit pad each, a drum lane (kit[key]) | pad Level = the fader law without its silent bottom, Pan, Speed (whole semitones), Mute, reverse, polyphony, cut groups A–C; a note's pitch ±12 st as the event's pitch (2^(2p − 1)) |
| a Layer | one rack holding its instruments, each with its key range | the import's own reading (round 190bs) |
| any other instrument | MiniSynth with FLM's defaults (only Enable written; FLM fills the rest) | listed: the notes stay |
| Pocket effects (FLM's own effects in LYNX) | their modules | slot parameter k → PRMS k + 1, the slot's mix → Mix; a Pocket blend container → Mix + Post Gain; a gain-only utility after a module → its Post Gain (±20 dB) |
| pattern clips | one clip per channel lane, one content id per (pattern, channel), so a pattern placed twice stays linked | start / length / offset in beats, loop = the pattern's length; notes: tick × 128 / 960, velocity round(255 v), pan byte (127 = centre), slide flag |
| audio clips | an audio channel per Playlist track, clips with a CLSm sample | level from the clip gain and the channel's level against audio-clip unity, pan, Speed, pitch (STR1), reverse, mute; a stretched clip gets a stretch mode (FLM's own stretcher, listed) |

**The LYNX chunk.** FLM keeps one pattern per track, so a LYNX pattern used by several channels becomes several FLM
contents. The export ends the file with a chunk "LYNX" (text: which content of which channel was which LYNX pattern, the
patterns' names and colours, and a 64-bit FNV-1a checksum of every byte before the chunk). FLM's loader skips it (§1).
LYNX's import uses it only while the checksum holds and the chunk is the file's last: then the per-track patterns are
merged back into the song's patterns, their names and colours restored, and the clips the split made of one clip turned
back into one. A song FLM saved again has no chunk; a changed file has another checksum: both import as plain FLM songs.

**Import fix found on the way:** an audio track's empty slot ("<empty>", no file) made a silent sampler channel named
"<empty>" (FLM's own Audio Recording template and every song with a recorded audio track). It now makes no channel unless
a drum lane plays the kit.

## 4. FL Studio Mobile opens and plays the exported file — [EXEC]

FL's "FL Studio Mobile" generator holds a whole FLM song as its state ("SSLF" + the .flm bytes, round 190jv). FL's
command-line render (`FL64 /R`, render180d's `flrun.ps1`: locks, idle gate, no input; it stops only the FL it started —
FL does not close by itself after this generator's render, so a timeout with the wav written counts) plays the song
through FLM's own engine. Tools: `/mnt/e/lynx-work/coverage190og/flm/tools/` (`mkflp_og.py` builds the FL project,
`flrender.sh` renders, `flmx` = .flm → LYNX → .flm outside the app).

Song: FLM's own demo "Nucleon - Tokyo" (FL's copy of the demo songs: Image-Line content read as data, never in the
repo), rendered as it is, and after .flm → LYNX (the .flm import) → .flm (the export). Scorer: `scripts/flref_compare.py
pair` (FL's render of the original against FL's render of the export).

| Step | What the render showed | Change |
|---|---|---|
| noise floor | the same project rendered twice by FL: **bit-identical** (esr −226 dB): FLM's render is deterministic, so every difference below is the file's | — |
| first export | FLM **opens and plays** the LYNX-written song (all channels sound); against the original: rms +2.0 dB, highs (> 4 kHz) −12.6 dB, env_corr 0.54 | — |
| kit samples | FLM writes each kit sample's place (0, 1, 2…) in MAIN's first f64 and −1 for a clip's sample [FILE, all 20 files]; with −1 on every pad, FLM's drum set played the wrong slots: highs −12.6 → **−2.4 dB** | writer fixed |
| MiniSynth automation | the "FLT Freq" lane was dropped (listed); now written through the engine's parameter range | export fixed |
| a kit's held bend | writing the bend's whole semitones on each pad's Speed (equal in LYNX) is **not** equal in FLM: the render ran 245 samples (5.6 ms) late, env_corr 0.44; the bend kept on RPRM [4] with the pads at their own Speed: lag 0, env_corr **0.87**, esr −3.4 dB | export: the kit's bend = the pitch most pads share |
| audio clips and the rack's bend | the export with the bend added to audio clips played them shorter than the original (45.5 s vs 47.5 s): FLM's held bend does not move audio clips (the import keeps leaving it off them) | none (experiment) |
| Autoduck (legacy) as module 29, Reverb (legacy) with 21 of its 22 values, the audio racks' held bend | each swapped back to the original's bytes: renders **bit-identical** to the export's → no effect | none |

After these fixes the whole song scores rms +2.9 dB, esr −3.4 dB, gesr −7.0 dB, env_corr 0.87, lag 0. Rendering one
channel alone (the other lanes' clips removed in both files): the drums are the same sound (env_corr **0.99**, gesr
−14.1 dB) **3.6 dB louder** in the export. The one difference left in that channel is its route: the 4.x demo has no SEND
(FLM's default route; the song also holds an empty "Kick Mix" bus), the export a SEND to the master at 0 dB as FLM 5
writes it. Open point: which route FLM 4.x songs' default is (`RHED`'s third word is the next suspect: −1 in the demo,
2 on its bus). A render of the export without its SENDs wrote no file (as FL's render of the original with only its
Chords lane: FL exits "ok" without a wav), so it was not settled this round.

## 5. Other FLM facts read this round (for the next rounds)

| Row | Fact | Evidence |
|---|---|---|
| format-0050 SEND words | **[1] is the send's Enable** (on = 1.0); [3] and [4] are the initial values of Enable and Level (what an automation lane resets to); [2] the linear gain made from [5] by the fader law, capped at 1. The channel's send record is 28 bytes: target id, the two values, the two initial values, two "can automate" flags. LYNX's import now leaves a switched-off route off (before: it played) | [DEC] W 0x180757d80 (the SEND writer); A `CSeqChannel::GetSendParamValue` / `GetSendParamInitValue` / `GetSendParamCanAutomate` (record offsets +4, +12, +20) |
| audiotracks-0003 / -0004 (stretch) | FLM's time stretcher is zplane's **élastique Pro V3** (`CElastiqueV3`, `PSOLASynthForElastiqueProV3` are linked into FLM 5.0.3's library): a licensed third-party library, studied only by renders, never statically | [DEC] A (symbol names only) |
| pianoroll-0018 Guess Scale | `ScaleUtils::KeyLocker::GuessScale` takes a count per pitch class (12), gives no answer below three distinct pitch classes, and scores every root (12) × key-locker scale (1…32), summing the counts of the notes inside each; the ranked results go to `CScaleGuessResultDialog`. The ranking rule (ties, the root's weight) was not read to the end: the next Piano roll round finishes it | [DEC, partial] A 0x84c3d8, `CNotesEditorGrid::GuessScale` 0x924178 |
| effects-0035 / instruments arpeggiator | FLM 5.0.3's Arpeggiator (module 51) is a new engine (`FXArpeggiator::ArpEngine`: sort orders Up/Down, Inside-Out, Outside-In, Bass, Treble, Cascade, Anchor, Jump, Random walk / bag, a rhythm step grid, retrigger aligned to the transport), larger than the parameter list the 5.0.4 files carry. LYNX's .flm import bakes the older parameter set (round 190de); the new orders are a follow-up | [DEC] A (function names, sizes) |
| stepseq-0019 Repeat Pad | the drum pads' note repeat is `CDrumpadsControl::SetRepeatRate` with `CGamepadNoteRepeat` (rate in beats, triplet switch, a render loop of its own) | [DEC] A (names; rates not read) |

## 6. Left open (each with what it needs)

- Slicer and SoundFont Player channels export as MiniSynth (listed): their state writers (the import's `slicerState` /
  `soundFontState` turned around) are the next export slice.
- Automation of MiniSynth / SuperSaw / Transistor Bass / 3x Osc parameters (the import maps them through each engine's
  parameter laws) is listed, not written.
- Audio-clip time-stretch stays FLM's black-box stretcher (FLM coverage `audiotracks-0003`).
