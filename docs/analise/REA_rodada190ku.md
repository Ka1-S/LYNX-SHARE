# REA round 190ku — Soundtoys state format (clean-room), factory-preset decode via the installed plug-in

Branch `rea190ku` off `parity190js`. English-first, clean-room. **Owner permission (2026-11-17):** static analysis,
decompilation and Frida on third-party binaries are allowed for parity, provided LYNX ships only its own code and no
vendor code, table, preset or content is copied. This round touches **Soundtoys** (EchoBoy / PrimalTap / Little
PrimalTap / EchoBoy Jr / Decapitator / PanMan / Little AlterBoy). Markers: [EXEC] measured here · [STATIC] from the
binary · [model] LYNX's own law.

Evidence outside git: `/mnt/e/lynx-work/rea190ku` (`fr/` oracle host + wrappers + captures, `bin/` the copied binary +
objdump, `lab/` the decoders, `logs/`).

## 0. Why round 190is could not read the states

Round 190is found the saved Soundtoys states "unreadable black-box: changing one control rewrites the payload from its
position to the end", so PrimalTap / Little PrimalTap / EchoBoy Jr fell back to one fixed Echoform sound. This round
establishes exactly what that payload is and finds a clean way to read the states LYNX actually meets in projects.

## 1. The container [EXEC, STATIC]

A Soundtoys component state (what a DAW stores, and what `savecomponent` returns from our own VST3 host) is a VST2-style
chunk wrapping a text archive:

```
'VstW' 00000008 00000001 00000000        // VstW wrapper, version 8
'CcnK' <u32 BE total-chunk-size> 'FBCh'   // fxBank 'opaque chunk' header
00000002 'St<XX>' 00000001 00000001 ...   // fxb: numPrograms=2, fxMagic 'StEB'(EchoBoy) / 'StDc'(Decapitator) / ...
<zero pad to 0xB0>
"WIDGET = <plugin>;\r VERSION = 4;\r SAVED_BY_VERSION = 5.5.5;\r BUILDNUM = 19885;\r PRESET =<payload>;\r"
```

The factory presets on disk (`C:\Users\Public\Documents\Soundtoys\Soundtoys 5\<plugin>\**\*.toy`) are the **same text
archive** with the field named `ARCHIVE =<payload>` instead of `PRESET =` (and `SAVED_BY_VERSION = 5.0.1`). The payload
bytes are the thing of interest. Strings in the binary name the parser: `wmArchiveFile::getAssign / getBegin / getEnd`,
tokens `Number / List / String / Function / Set / Trigger / NotePattern / DelayPattern / ModPattern / DelayTime /
WaveShape / LFOShape` (`.rdata` near `0x180afab00`). The module is wrapped by PACE (`Decapitator_ProtectVST3.pdb`), so
string cross-references and the archive codec are not statically recoverable in a clean, reliable form — Ghidra returns
no xrefs to the `PRESET` / `Cannot load preset` strings, and the decrypted `.text` (dumped from the live image,
`fr/img.bin`) differs from the on-disk bytes. Reversing the codec offline was therefore judged out of scope for a safe
round; the state is read a safer way (§3).

## 2. The payload is a 6-bit-packed entropy-coded stream [EXEC]

`lab/dec6.py`. The payload alphabet is the 64 ASCII codes `0x30..0x6f` (`'0'..'o'`), i.e. 6 bits per character, packed
MSB-first into bytes. The first bytes decode to a small fixed header (`00 10 01 00 00 00 ...`) followed by what behaves
as an **entropy / range-coded** body:

- The header length scales with the plug-in's parameter count (Decapitator payload 680 chars → 510 bytes; EchoBoy 2100
  → 1575; PrimalTap 1608 → 1206).
- A one-control change rewrites the packed body from a byte boundary **to the end** (`lab/diffst.py`: Decapitator
  `AutoGain 0→1` identical for 52 of 64 blocks then all-different to the end; `HighSlope 0→1` identical for 57 then
  different). That "stable prefix, then everything downstream changes" signature is exactly a range coder (or any
  arithmetic stream): once a coded symbol changes, every subsequent coded bit changes.

This is why byte-level or per-field mapping of the raw payload is impossible, and confirms 190is's observation as a
property of the coder rather than of the data.

## 3. Reading the states LYNX meets — the installed plug-in as a decode oracle [EXEC]

The states a user's project actually contains are, overwhelmingly, the **factory presets** (optionally tweaked). Those
presets ship as `.toy` files on disk. LYNX does not need to reverse the codec to recognise them; it needs their decoded
parameter values and the hash of the payload the host will see. Both are obtained **black-box** through the installed
plug-in's public VST3 parameter API (exactly as rounds 135s / 136 read the owner's own project states):

1. `fr/mkwrap.py` puts a `.toy`'s `ARCHIVE` payload into a real EchoBoy state wrapper as `PRESET =` and fixes the
   `CcnK` size field.
2. `fr/null.ps1` runs our own hidden host (`refcap030/fxmeasure_b.exe`, no window, no input; trial lock + idle gate):
   `loadcomponent <wrapped .toy>` (the plug-in's own `setStateInformation`), `get` every parameter as the plug-in's own
   text, then `savecomponent` to capture the **canonical** PRESET payload the plug-in re-emits (what a DAW would store,
   so its FNV-1a hash is the one LYNX matches on).

Verified on EchoBoy `Default.toy`: decoded Mix 50.0, Echo1Time 250.0 ms, Feedback 0.00, Style "Studio Tape" — the known
default. Nothing of the vendor is copied: only numeric parameter values LYNX reads back, mapped onto LYNX's own engine
by the round-190is value map (`parity190is/lab/vmap.py`, EchoBoy→Echoform exact; PrimalTap / Little PrimalTap /
EchoBoy Jr mapped; Decapitator→Ember by `tp172d/mapsub.py`).


## 3b. Blocker for a bulk factory capture [EXEC]

The oracle reads one preset per instance reliably (EchoBoy `Default.toy`, `15 ips Tape Echo.toy` decoded), but a second
`loadcomponent` / `loadstate` in the same instance, and some presets even as the first load (e.g. `Space Echo.toy`),
stall our headless host indefinitely (`fr/s_ebpump.txt`, `fr/s_diag.txt`: the host prints the preset's echo line then
never reaches the first `get`). `pump` and `reset` between loads do not clear it. The plug-in has a
"Deprecated Session or Preset Format detected. Please re-save your session or preset…" modal string (`.rdata`
`0x180afabd0`); the stalling presets are the older-format ones, so the most likely cause is that `setStateInformation`
raises that modal (or an equivalent long/blocking init for the large-buffer presets such as Space Echo) that a
windowless host cannot answer. Capturing the full factory set therefore needs either one process per preset *plus*
GUI dismissal of that modal (under the input-wall / idle gate, seconds at a time — slow, and the owner uses AnyDesk),
or a Frida hook that observes the decoded parameter store directly after `setState` without driving the editor. Both
are viable next steps; neither fits safely in this round's budget, so no factory-preset table is shipped here.

## 4. What ships this round

Analysis only — no DSP or translator change, so every score is unchanged and every old project is bit-identical. The
deliverable is this document plus the validated, reusable oracle (`fr/mkwrap.py` wrap a `.toy` as a loadable state;
`fr/null.ps1` + `fr/gonull.sh` the hidden trial-locked host; `fr/geneb.py` the per-plugin capture generator) and the
decoders (`lab/dec6.py` 6-bit unpack, `lab/diffst.py` the chained-rewrite probe, `lab/stx.py` payload extractor). The
path to the score lift (recognise the factory presets a user's project actually holds, by the hash of the canonical
re-saved payload → LYNX's own Echoform / Ember parameters via the round-190is value maps, as new entries in the
existing `translateSoundtoys*` hash tables — add-only, behind a revision guard) is proven for single loads and left to
the next round once the modal is handled.

## 5. FabFilter and Valhalla (targets 2 and 3) — not opened this round

The FabFilter and Valhalla states are **not** encoded (round 190iq reads them through LYNX's own `--ff` / `--sub`
translators), so their low rows (Pro-C 3 → Crest 6.30, Pro-L 2 → Apex 7.30, Pro-MB → Prism 8.17, Pro-G → Sentry 8.21,
VintageVerb → Epoch 2.67, and the Pro-R 2 / Pro-Q 4 leftovers) are DSP-law fits, not a decode problem, and are
independent of the Soundtoys work. They were not started this round (the Soundtoys state format was the headline and
consumed the budget). REA on those binaries for the exact detector / knee / oversampling laws remains open for a
dedicated round.
