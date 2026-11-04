# REA round 169l2 — every new generator is FL's Plugin database state; PoiZone's delay tail

Evidence markers as in REA_rodada169l: [DATA] FL's own files read byte by byte; [FL render] FL Studio 2026's
command-line render (`FL64.exe /R /Ewav`, both FL locks, one launch, FL exited by itself); [CODE] LYNX source; [TEST]
LYNX tests. Clean-room: behaviour only; the test project is built at run time from the user's own FL files and stays
under `C:\Users\kauan\lynx-test\gen169l2` (nothing of FL's is shipped).

## 1. The decision of REA_rodada169l §3 [DATA][CODE]

User decision (via the coordinator, standing rule "FL-parity items must match real FL"): the engines whose `Default.*`
preset differs from FL's Plugin database state start, like in FL, from the database entry when FL is installed;
without FL they keep LYNX's bank (round 168n).

FL's picker / "+" loads the database entry for **every** generator (REA_rodada169l §3), so `natives::presetCandidates`
now puts `Plugin database\Generators\<category>\<preset folder>.fst` first for every generator instead of a list of
eight. Byte comparison of the `0xD5` plugin state of `Default.*` vs the database entry (this round, `fststate.py`):

| Engine | Default.* vs database state | New channel now |
|---|---|---|
| 3x Osc | 44 vs 92 B, differ | DB |
| Sytrus | 1 146 vs 1 472 B, differ (DB patch "Supersaw", an FL 11.1 file) | DB |
| Harmor | 82 998 vs 85 207 B, differ (FL 11.1 file) | DB |
| Harmless, PoiZone, Sawer, BeepMap | same size, differ | DB |
| MiniSynth, FL Keys, Fruity Granulizer, Toxic Biohazard (`Default.tbio`) | differ | DB |
| Wave Traveller | 332 B (header 1000001, a version LYNX's reader refuses) vs 36 508 B (1000005: no sample, 128 key patches) | DB: no sample (silent, as FL) |
| Kepler, Kepler Exo, SimSynth, Fruity DX10, Plucked!, Fruit Kick | **same** | DB = Default (no change) |
| Drumaxx, Drumpad, BassDrum, GMS, Morphine, Transistor Bass, Sakura, Ogun | — | DB (rounds 169e / 169l) |
| FL Slayer | no database entry | first preset (unchanged) |

A database entry LYNX can't read falls through to `Default.*`, then the folder (as before). Old projects are not
touched: `presetCandidates` / `makeChannel` only make NEW channels; a project's channel keeps the state it stores.

## 2. FL render vs LYNX [FL render]

One FL launch rendered `g169l2.flp` (12 channels, each its Plugin database `.fst` events over a plain channel; keys 60 /
48 / 72, one beat each, 3 s apart; `mk169l2.py`). LYNX `renderwav` of the same project. Per-note peaks L / R (dB) and
the RMS difference relative to FL's:

| Engine | FL | LYNX | rel. diff | reading |
|---|---|---|---|---|
| 3x Osc 60 / 48 / 72 | −13.4 | −13.4 | 0.000 | identical |
| FL Keys | −19.6 −15.1 / −16.2 −16.7 / −17.8 −14.4 | identical | 0.03–0.05 | bit-close |
| Toxic Biohazard | −22.3 −20.5 / −22.2 −23.0 / −20.4 −16.3 | −22.3 −20.5 / −22.2 −22.7 / −20.4 −19.2 | 0.09–0.11 (72: 1.3) | close; key 72's R differs |
| Harmless | −5.8 | −5.5 | 0.04–0.08 | +0.3 dB |
| Sytrus, Harmor, PoiZone, Sawer, BeepMap, MiniSynth | −0.5 … −15.9 | same levels ±1–2 dB | 0.5–1.8 | unison / free-running phases: not sample-comparable |
| Fruity Granulizer, Wave Traveller | silent (no sample) | silent (after §3) | | = FL |

Tails (0.5 s windows after each note): Sytrus, Harmor, Sawer, FL Keys, Toxic, BeepMap follow FL's within 1–3 dB.

## 3. PoiZone's delay tail was cut, then replayed as a burst [FL render][CODE][TEST]

FL's PoiZone database patch has its delay on: after the note FL's render keeps echoing (−27, −34, −40, −46 dB in the
following 0.5 s windows). LYNX went silent with the last voice and, in the full project, played a 256-sample burst
(≈ −22 dB) at every later note-on of another channel (Granulizer, Wave Traveller, MiniSynth, …).
Cause [CODE]: `PoiZone::Plugin::idle()` only looked at voices and the arpeggiator, so the renderer skipped the channel
(`RackRenderer`: idle and no notes → not rendered) while its chorus / delay lines still held the echoes; whenever the
channel got an event again (a note of its cut group) one block played the stale delay line.
Fix: with the delay or chorus on, the plugin stays busy until its output has stayed under −120 dB for the delay lines'
length (2 s). Test `gen169l2_poizone_delay_tail_rings_out_before_idle`.

## 4. LYNX before / now

| | before | now |
|---|---|---|
| New 3x Osc, Sytrus, Harmor, Harmless, PoiZone, MiniSynth, Sawer, FL Keys, Granulizer, Toxic, BeepMap (FL installed) | `Default.*` preset | FL's Plugin database state (= FL's new channel) |
| New Wave Traveller (FL installed) | first readable preset `classic kick.fst` | FL's database state: no sample, silent like FL |
| Without FL | LYNX bank | unchanged |
| PoiZone with delay / chorus | tail cut at the last voice, stale burst later | echoes ring out like FL |

Code: `core/NativeEngines.cpp` (`presetCandidates`), `core/PoiZone.cpp` (`Plugin::idle`, `fxQuiet`). Tests
`tests/Gen169l2Tests.inc`.

## Left / for others

- **Ogun's release tail** (REA_rodada169l §5): from FL's and LYNX's 169l renders the three notes' tails agree within
  ≈ 2 dB for 1.7 s after the note off; below ≈ −50 dB LYNX then falls about twice as fast (key 72: −53 / −55 / −60 /
  −63 / −67 dB in FL vs −56 / −65 / −68 / −77 / −82 dB, 0.2 s windows from 1.7 s after the note off). Ogun's voice
  engine is the REA port of round 57 (release time from `p.rel`, the release velocity 0.5 like FL's notes); finding
  where the late tail departs needs a harness run against `Ogun_x64.dll` — not quick, listed.
- **Wave Traveller's `Default.fst`** (header 1000001, older than the 1000005 LYNX reads): only matters when the
  arrows reach that preset (they pass over it); a new channel no longer uses it.
- MiniSynth key 60: FL keeps a −62 … −81 dB tail LYNX doesn't have (inaudible).
- Transistor Bass −2…3 dB (REA_rodada169l §5): forwarded to round 169g.

## Where I stopped

- Done: §1 code, §2 FL render + comparison, §3 fix, tests written.
- In progress: build-local + FL-data tests, then the landing train.
