# Round 190fm, slice mt: Live 12's MIDI Tools generators in LYNX

Rows: miditools-002, -005, -007, -066…-079 (LIVE-FACTS §4 of round 190fm). Clean-room: Live's facts in our words (set
schema 12.4 build 12402, Live's default set values, the Generate panels' picture `ableton190o/shots/live-04-generate-panels.png`,
preset names from the binary's strings). No Live code, presets or chord bank went into LYNX.

## What Live has (facts)
- The Clip View has a **Generate** panel beside Transform: a chooser (Rhythm, Seed, Shape, Stacks, Euclidean — the last
  one a Max for Live device), an **Auto** switch and a **Generate** button. [SHOT]
- Each tool's settings are saved in the set's `NoteAlgorithms`, one element per tool. [DATA]
- Rhythm (`RhythmAlgorithm`): Density 4, Repetitions 1, Pattern 14, PatternLength (Steps) 8, Pitch 36, Velocity 100,
  Accent 127, Period ("Freq") 4, Offset 0 (the arrows), Shift 0, StepDuration 7 ("1/8" in the picture), Split 0.
- Seed (`SeedAlgorithm`): NotesDensity 0.5, pitch 60…84, duration −6…−3 (log2 of a whole note: 1/64…1/8), velocity
  30…100, VerticalLimit ("Voices") 4.
- Shape (`ShapeAlgorithm`): 21 ShapeLevels 0…1, ShapePresets 2 ("Up" in the picture), Rate 0 ("Grid"), Tie 0, Density
  1, pitch 60…84, PitchVariation ("Jitter") 0. Preset names: Flat, Arc Up, Arc Down, Bounce Up, Bounce Down (+ Up, Down).
- Stacks (`StacksAlgorithm`): a sequence of chords, each RootDegree, Octave 3, RootPitch 60, Inversion 0, RuleNumber 0,
  Duration 1, Offset 0; chord banks are `.stacks` JSON files (`chord_rulesets[]` with `scale_cardinality` and 16 rules
  of scale-degree offsets).

## What LYNX does (core/MidiGenerate.{h,cpp}, core/MidiGenerateOptions.h, ui/MidiToolsPanel)
- The MIDI Tools panel has two tabs, **Transform** (round 190s) and **Generate**. Generate's chooser: Rhythm, Seed,
  Shape, Stacks, Euclidean (LYNX's one-voice version on `notetools::euclideanHit`). Reset (↺), Auto, Generate.
- A generator writes into the Piano roll's time selection, else the clip's part of the pattern (Clip-launch) or the
  whole pattern, and its notes replace the edited channel's notes that start in that range (Live: Generate replaces the
  notes in the time range). Generate draws new random choices; Auto rewrites with the same choices on every change.
  Every write is one undo step; the new notes become the selection.
- The settings live in `Song::midiGenerate` and are saved in the .lynx as `midiGenerate` (key=value text) only when they
  differ from Live's defaults, so older projects load and save unchanged.

### Exact (Live's facts)
Every default value above; the tool list and order; Shape's 21 levels; Seed's duration as log2 of a whole note; Stacks'
per-chord fields and the `.stacks` layout (LYNX reads a user's own file at run time; it ships none).

### LYNX's own (approximate, needs live check)
- **Rhythm Pattern order**: Live's table is unknown. LYNX ranks the steps (first the Euclidean hits, then the others,
  each in time order) and Pattern = the n-th `density`-subset of that ranking in lexicographic order. Pattern 0 is the
  Euclidean rhythm; Live's default 14 gives steps 0 2 5 7 of 8 (x.x..x.x). Range 0…C(steps, density) − 1.
- **Step Duration list**: 1/64T 1/64 1/32T 1/32 1/16T 1/16 1/8T 1/8 1/4T 1/4 1/2T 1/2 1/1 (index 7 = 1/8 as in the picture;
  the other entries are inferred). Shape's Rate uses 0 = Grid, then the same list.
- **Accent**: counted in hits (every Period-th hit from Offset); **Split**: an even share of the hits played as two
  half notes; **Repetitions** (not in the picture): each hit as that many equal notes in its step.
- **Seed's law**: on each grid step the k-th simultaneous note starts with chance density^(k+1), up to Voices; pitches
  drawn evenly from the range (the project key's notes when it has one), no key twice at once; durations drawn evenly
  in log2; velocities evenly. Durations step in powers of two in the panel (the saved values stay floats).
- **Shape**: the presets' formulas (Up/Down lines, Arc = sine, Bounce = a dropped ball (1 − x)|cos 2.5πx|), Custom when
  drawn; Tie = chance a note lasts over the next step; Density = chance a step gets a note; Jitter = up to ± half the
  pitch range; velocity 100 (Live's panel shows none); the key's notes when the project has one.
- **Stacks**: LYNX's own chord bank (16 rules each for 5-, 6- and 7-note scales, from triads, sevenths, sus, power,
  quartal, added-note and open voicings); without a project key the rules read the major scale on the root; Duration
  in bars, Offset in sixteenths (units inferred), the sequence repeats to fill the range; velocity 100. The chord
  display is LYNX's (a strip of chord names + a 16-cell shape grid); Live's chord map is not copied.
- **Pitch defaults vs the picture**: Live's saved defaults are Rhythm 36 and Seed / Shape 60…84 while the picture
  shows C3 and C3…C4: the picture was taken with other values or Live shows these differently (needs live check).
- **Auto on by default** in Generate as the picture shows it lit.

### Left out
- Live's Euclidean (Max for Live, four voices, its Pattern / Voices pages): LYNX has one voice.
- The `.als` side (NoteAlgorithms import / export): settings are saved in .lynx only.
- MIDI mapping of the generator controls (Live's NamedKeyMidiRemoteables).

## Best way?
Live replaces the notes of the whole range and has no per-channel notion; LYNX patterns hold several channels, so a
generator replaces only the edited channel's notes. Live's Rhythm Pattern is an opaque index; LYNX's starts at the
Euclidean rhythm so low numbers are the even rhythms — easier to explore. An option for later: show the rhythm's hits
as a step row under the knobs (Live shows none).
