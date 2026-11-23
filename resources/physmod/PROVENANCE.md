# LYNX Model: provenance of the parameter tables

LYNX Model (`src/core/physmod/LynxModel.h`, rounds 190ni, 190nl and 190nm) ships **fitted parameters only**: one table per instrument
(`<name>.lpm`, format in `tools/physmod/lpm.py`), compiled in as `src/core/physmod/tables/<name>.inc`. No audio from
any recording is shipped or played. The strike is synthesised: a contact force `t e^(-t / tau)` and band-pass noise,
whose parameters are fitted. No recorded transient or attack table is used, by owner decision (design study 190nd §3
#16, Casio US11094307B2).

## Data policy

CC0 first; never NC or SA data. Every table here comes from one CC0 source only:

| | |
|---|---|
| Library | Versilian Community Sample Library (VCSL), https://github.com/sgossner/VCSL |
| Commit | `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e` (2026-01-14), fetched 2026-11-19 |
| Licence | CC0 1.0 Universal: the repository's `LICENSE` (SHA-256 `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499`) and README ("This collection is under a Creative Commons 0 license"). The repository tree has no other licence or notice file in the six instrument folders, so every file used is under that CC0 grant. |
| Integrity | Each file's git blob SHA-1 was checked against the commit's tree, and its SHA-256 recorded |

## Per table

Each `<name>.provenance.json` lists every recording the table was fitted on: its repository path, git blob SHA-1,
SHA-256, size, velocity layer and sounding key. The table's header carries the first 8 bytes of that record's SHA-256,
so a table and its record belong together (`physmodrender info` prints them).

| Table | VCSL folder | Takes used | Layers |
|---|---|---|---|
| `marimba.lpm` | Idiophones/Struck Idiophones/Marimba | 30 (10 keys) | soft, med, loud |
| `vibraphone.lpm` | Idiophones/Struck Idiophones/Vibraphone/Soft Mallets | 22 (11 keys) | v1, v2 |
| `xylophone.lpm` | Idiophones/Struck Idiophones/Xylophone/Medium Mallets | 16 (8 keys) | pp, ff |
| `glockenspiel.lpm` | Idiophones/Struck Idiophones/Glockenspiel | 18 (7 keys) | soft, medium, loud |
| `tubularbells.lpm` | Idiophones/Struck Idiophones/Tubular Bells 2 | 22 (11 keys) | v2, v4 |
| `kalimba.lpm` | Idiophones/Plucked Idiophones/Kalimba, Tanzania | 25 (25 keys) | one |

Takes of a key a table already has (a second take, a second tine on the same note) are listed under
`not_used_extra_takes` and serve as extra test notes.

## Reproducing

```
curl -o tree.json "https://api.github.com/repos/sgossner/VCSL/git/trees/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e?recursive=1"
curl -o LICENSE https://raw.githubusercontent.com/sgossner/VCSL/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/LICENSE
python3 -I tools/physmod/fetch_vcsl.py tree.json LICENSE <notes> c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e
# 1. modes, tuning, a first strike (and the provenance records) for every instrument
python3 -I tools/physmod/calibrate.py fit all <notes> <notes>/manifest.json <fit>
cp <fit>/*.provenance.json resources/physmod/
# 2. the strike refined through the engine (round 190nl): the shipped table and its include, per instrument
python3 -I tools/physmod/refine.py build/physmodrender <notes> <fit>/<name>.fit.json <fit>/<name>.lpm resources/physmod/<name>.lpm \
        --inc-dir src/core/physmod/tables --report <fit>/<name>.refine.json [--level]
# (round 190nm: the tubular bells and the kalimba add --level, each anchor's level matched through the engine; their
# fitting settings - the bells' lower analysis floor, the kalimba's window-exact levels, two-component modes, onset and
# fade - are in calibrate.py's INSTRUMENTS)
# held-out tables (three folds: every inner recorded key held out once), refined the same way, then the report
python3 -I tools/physmod/calibrate.py fit all <notes> <notes>/manifest.json <fit> --holdout 3 [--fold 1|2]
python3 -I tools/physmod/refine.py build/physmodrender <notes> <fit>/<name>-holdout3[f1|f2].fit.json <fit>/<name>-holdout3[f1|f2].lpm <eval-fit>/...
python3 -I tools/physmod/evaluate.py build/physmodrender <notes> <eval-fit> <evaluation> --tables resources/physmod
```

Keep the downloaded notes outside the repository. The fit is deterministic: the same notes give the same parameters
(the header's provenance hash also covers the record's date).
