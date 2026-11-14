// Round 190fm: Live 12's MIDI Tools generators (core/MidiGenerate.h): Rhythm, Seed, Shape, Stacks and LYNX's one-voice
// Euclidean; their settings saved with the project (core/MidiGenerateOptions.h). Live's defaults are [DATA] (LIVE-FACTS
// §4); what the controls do is LYNX's reading (the header's [INFERRED] notes), tested here as LYNX defines it.

#include "core/MidiGenerate.h"

namespace mg190fm
{
inline lynx::midigen::Context bar (int bars = 1)
{
    lynx::midigen::Context c;
    c.from = 0;
    c.to = (std::int64_t) bars * 4 * lynx::ppq;
    c.channel = 0;
    c.grid = lynx::ppq / 4;
    c.seed = 7;
    return c;
}
inline lynx::midigen::ScaleRef aMinor() { return { 9, { 0, 2, 3, 5, 7, 8, 10 } }; }
inline lynx::midigen::ScaleRef cMajor() { return { 0, { 0, 2, 4, 5, 7, 9, 11 } }; }
inline bool inScale (const lynx::midigen::ScaleRef& s, int key)
{
    const auto pc = ((key - s.root) % 12 + 12) % 12;
    return std::find (s.intervals.begin(), s.intervals.end(), pc) != s.intervals.end();
}
inline std::vector<int> keysOf (const lynx::midigen::Notes& notes)
{
    std::vector<int> k;
    for (const auto& n : notes)
        k.push_back (n.key);
    return k;
}
} // namespace mg190fm

TEST (mg190fm_defaults_are_lives)
{
    using namespace lynx::midigen;
    const Settings s;
    CHECK (s.isDefault() && s.tool == Settings::rhythm);
    const auto& r = s.rhythmOptions; // [DATA] RhythmAlgorithm
    CHECK (r.density == 4 && r.repetitions == 1 && r.pattern == 14 && r.steps == 8 && r.pitch == 36 && r.velocity == 100);
    CHECK (r.accent == 127 && r.period == 4 && r.offset == 0 && r.shift == 0 && r.stepDuration == 7 && r.split == 0.0f);
    const auto& e = s.seedOptions; // [DATA] SeedAlgorithm
    CHECK (e.density == 0.5f && e.minPitch == 60 && e.maxPitch == 84 && e.minDuration == -6.0f && e.maxDuration == -3.0f);
    CHECK (e.minVelocity == 30 && e.maxVelocity == 100 && e.voices == 4);
    const auto& h = s.shapeOptions; // [DATA] ShapeAlgorithm
    CHECK (h.preset == 2 && h.preset == ShapeOptions::up && h.rate == 0 && h.tie == 0.0f && h.density == 1.0f);
    CHECK (h.minPitch == 60 && h.maxPitch == 84 && h.jitter == 0.0f && h.levels.size() == 21);
    CHECK (h.levels.front() == 0.0f && h.levels.back() == 1.0f && std::abs (h.levels[10] - 0.5f) < 1e-6f);
    const auto& k = s.stacksOptions; // [DATA] StacksAlgorithm: one chord
    CHECK (k.sequence.size() == 1);
    CHECK (k.sequence[0].rootDegree == 0 && k.sequence[0].octave == 3 && k.sequence[0].rootPitch == 60 && k.sequence[0].inversion == 0);
    CHECK (k.sequence[0].rule == 0 && k.sequence[0].duration == 1.0f && k.sequence[0].offset == 0.0f);
    CHECK (std::string (toolName (Settings::rhythm)) == "Rhythm" && std::string (toolName (Settings::euclidean)) == "Euclidean");
    // the project: never used = default (old projects are unchanged)
    CHECK (lynx::Song {}.midiGenerate.isDefault());
}

TEST (mg190fm_note_values)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    CHECK (std::string (noteValueName (7)) == "1/8" && noteValueTicks (7) == ppq / 2); // Live's default Step Duration 7 [SHOT]
    CHECK (std::string (noteValueName (5)) == "1/16" && noteValueTicks (5) == ppq / 4);
    CHECK (std::string (noteValueName (0)) == "1/64T" && noteValueTicks (0) == ppq / 16 * 2 / 3);
    CHECK (std::string (noteValueName (1)) == "1/64" && noteValueTicks (1) == ppq / 16);
    CHECK (std::string (noteValueName (6)) == "1/8T" && noteValueTicks (6) == ppq / 3);
    CHECK (std::string (noteValueName (12)) == "1/1" && noteValueTicks (12) == 4 * ppq);
    CHECK (noteValueTicks (11) == 2 * ppq && noteValueTicks (99) == 4 * ppq && noteValueTicks (-3) == noteValueTicks (0));
}

TEST (mg190fm_rhythm_patterns)
{
    using namespace lynx::midigen;
    CHECK (rhythmCount (8, 4) == 70 && rhythmCount (16, 8) == 12870 && rhythmCount (32, 16) == 601080390 && rhythmCount (8, 0) == 1);
    // pattern 0 is the Euclidean rhythm; the order wraps at C(steps, density)
    CHECK ((rhythmHits (8, 4, 0) == std::vector<int> { 0, 2, 4, 6 }));
    CHECK ((rhythmHits (8, 3, 0) == std::vector<int> { 0, 3, 6 }));
    CHECK (rhythmHits (8, 4, 70) == rhythmHits (8, 4, 0) && rhythmHits (8, 4, -1) == rhythmHits (8, 4, 69));
    // pattern 1 swaps the last Euclidean hit for the first other step
    CHECK ((rhythmHits (8, 4, 1) == std::vector<int> { 0, 1, 2, 4 }));
    CHECK ((rhythmHits (8, 4, 14) == std::vector<int> { 0, 2, 5, 7 })); // Live's default Pattern 14 in LYNX's order: x.x..x.x
    // every pattern is a different set of `density` hits
    std::set<std::vector<int>> seen;
    for (int p = 0; p < 70; ++p)
    {
        const auto h = rhythmHits (8, 4, p);
        CHECK (h.size() == 4 && std::is_sorted (h.begin(), h.end()) && h.front() >= 0 && h.back() < 8);
        seen.insert (h);
    }
    CHECK (seen.size() == 70);
    CHECK (rhythmHits (8, 0, 5).empty() && rhythmHits (8, 8, 0).size() == 8 && rhythmHits (99, 2, 0).size() == 2);
}

TEST (mg190fm_rhythm_writes_the_pattern)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    RhythmOptions o; // the defaults: 8 steps of 1/8, 4 hits, pattern 14, C1, accent every 4th hit
    auto out = rhythm (o, mg190fm::bar());
    const auto hits = rhythmHits (8, 4, 14);
    CHECK (out.size() == 4);
    for (std::size_t i = 0; i < out.size() && i < hits.size(); ++i)
    {
        CHECK (out[i].start == hits[i] * ppq / 2 && out[i].length == ppq / 2 && out[i].key == 36 && out[i].channel == 0);
        CHECK (std::abs (out[i].velocity - (i == 0 ? 127.0f : 100.0f) / 127.0f) < 1e-6f); // hit 0 accented
    }
    // two bars: the pattern repeats; the accent counts hits on (hit 4 = the next bar's first)
    out = rhythm (o, mg190fm::bar (2));
    CHECK (out.size() == 8 && out[4].start == 4 * ppq + hits[0] * ppq / 2 && out[4].velocity == 1.0f && out[5].velocity < 1.0f);
    // offset 1: the second hit of each period
    o.offset = 1;
    out = rhythm (o, mg190fm::bar());
    CHECK (out[1].velocity == 1.0f && out[0].velocity < 1.0f);
    o.offset = 0;
    o.period = 0; // no accents
    out = rhythm (o, mg190fm::bar());
    CHECK (std::all_of (out.begin(), out.end(), [] (const lynx::Note& n) { return n.velocity < 1.0f; }));
    // Shift rotates the steps later
    o = {};
    o.pattern = 0;
    o.shift = 1;
    out = rhythm (o, mg190fm::bar());
    CHECK ((mg190fm::keysOf (out).size() == 4 && out[0].start == ppq / 2 && out[3].start == 7 * ppq / 2));
    o.shift = -1; // step 0 wraps to the end
    out = rhythm (o, mg190fm::bar());
    CHECK (out.size() == 4 && out[0].start == ppq / 2 && out.back().start == 7 * ppq / 2);
    // Split 50 %: every second hit as two half notes; Repetitions 3: three notes per hit
    o = {};
    o.pattern = 0;
    o.split = 0.5f;
    out = rhythm (o, mg190fm::bar());
    CHECK (out.size() == 6 && out[0].length == ppq / 2 && out[1].length == ppq / 4 && out[2].start == ppq + ppq / 4);
    o.split = 0.0f;
    o.repetitions = 3;
    out = rhythm (o, mg190fm::bar());
    CHECK (out.size() == 12 && out[0].length == 160 && out[1].start == 160 && out[2].start == 320 && out[2].length == 160);
    // a range that is not whole patterns: notes are cut at its end
    o = {};
    o.pattern = 0;
    auto c = mg190fm::bar();
    c.from = 2 * ppq;
    c.to = 2 * ppq + ppq / 2 + 100;
    out = rhythm (o, c);
    CHECK (out.size() == 1 && out[0].start == 2 * ppq && out[0].length == ppq / 2);
    c.to = 2 * ppq + 100;
    out = rhythm (o, c);
    CHECK (out.size() == 1 && out[0].length == 100);
    // an empty range or no hits: nothing
    o.density = 0;
    CHECK (rhythm (o, mg190fm::bar()).empty());
    c.to = c.from;
    CHECK (rhythm (RhythmOptions {}, c).empty());
}

TEST (mg190fm_seed_random_notes)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    SeedOptions o;
    const auto c = mg190fm::bar (4);
    const auto a = seed (o, c), b = seed (o, c);
    CHECK (! a.empty() && a == b); // the same seed: the same notes (a preview and its Generate agree)
    auto other = c;
    other.seed = 8;
    CHECK (seed (o, other) != a);
    for (const auto& n : a)
    {
        CHECK (n.key >= 60 && n.key <= 84);
        CHECK (n.length >= 60 && n.length <= ppq / 2); // 1/64 … 1/8
        CHECK (n.velocity >= 30.0f / 127.0f - 1e-6f && n.velocity <= 100.0f / 127.0f + 1e-6f);
        CHECK (n.start % (ppq / 4) == 0 && n.start + n.length <= c.to); // on the grid, inside the range
    }
    // never more than `voices` notes at once, and no key twice at once
    o.voices = 2;
    o.density = 1.0f;
    o.minDuration = o.maxDuration = -2.0f; // quarter notes: they overlap the next grid steps
    const auto full = seed (o, c);
    int most = 0;
    bool repeated = false;
    for (std::int64_t t = 0; t < c.to; t += ppq / 8)
    {
        std::vector<int> on;
        for (const auto& n : full)
            if (n.start <= t && t < n.start + n.length)
                on.push_back (n.key);
        most = std::max (most, (int) on.size());
        std::sort (on.begin(), on.end());
        repeated = repeated || std::adjacent_find (on.begin(), on.end()) != on.end();
    }
    CHECK (most == 2 && ! repeated);
    CHECK (full.size() == 2 * 16); // at 100 % both voices are busy all the time: 2 notes per beat
    // the scale: only its keys
    auto inKey = c;
    inKey.scale = mg190fm::cMajor();
    for (const auto& n : seed (SeedOptions {}, inKey))
        CHECK (mg190fm::inScale (inKey.scale, n.key));
    // density 0: nothing; a swapped range still works
    o = {};
    o.density = 0.0f;
    CHECK (seed (o, c).empty());
    o = {};
    o.minPitch = 72;
    o.maxPitch = 70;
    for (const auto& n : seed (o, c))
        CHECK (n.key >= 70 && n.key <= 72);
}

TEST (mg190fm_shape_follows_the_levels)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    // the presets: LYNX's formulas
    const auto up = shapePreset (ShapeOptions::up), arc = shapePreset (ShapeOptions::arcUp), arcDown = shapePreset (ShapeOptions::arcDown);
    const auto bounce = shapePreset (ShapeOptions::bounceDown), bounceUp = shapePreset (ShapeOptions::bounceUp), flat = shapePreset (ShapeOptions::flat);
    CHECK (up[0] == 0.0f && up[20] == 1.0f && std::abs (up[5] - 0.25f) < 1e-6f);
    CHECK (arc[0] == 0.0f && arc[10] == 1.0f && arc[20] == 0.0f && arcDown[10] == 0.0f && arcDown[0] == 1.0f);
    CHECK (bounce[0] == 1.0f && bounce[20] == 0.0f && bounce[4] == 0.0f && bounce[8] > 0.5f && bounceUp[20] == 1.0f && bounceUp[0] == 0.0f);
    CHECK (std::all_of (flat.begin(), flat.end(), [] (float v) { return v == 0.5f; }));
    CHECK (std::string (shapePresetName (ShapeOptions::up)) == "Up" && std::string (shapePresetName (ShapeOptions::bounceDown)) == "Bounce Down");
    // the defaults (Up, Grid, C 60…84): one note per grid step, rising
    ShapeOptions o;
    CHECK (shapeAt (o, 0.0) == 0.0 && shapeAt (o, 1.0) == 1.0 && std::abs (shapeAt (o, 0.5) - 0.5) < 1e-6);
    auto out = shape (o, mg190fm::bar());
    CHECK (out.size() == 16 && out[0].key == 60 && out[0].start == 0 && out[0].length == ppq / 4);
    CHECK (out[8].key == 72 && out[15].key >= 82 && std::is_sorted (out.begin(), out.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.key < b.key; }));
    // Rate: a note value instead of the grid
    o.rate = 1 + 7; // 1/8
    out = shape (o, mg190fm::bar());
    CHECK (out.size() == 8 && out[1].start == ppq / 2 && out[1].length == ppq / 2);
    o.rate = 0;
    // Tie 100 %: one note over the whole range; Density 0: none
    o.tie = 1.0f;
    out = shape (o, mg190fm::bar());
    CHECK (out.size() == 1 && out[0].length == 4 * ppq && out[0].key == 60);
    o.tie = 0.0f;
    o.density = 0.0f;
    CHECK (shape (o, mg190fm::bar()).empty());
    // Density 50 %: some steps empty, deterministic for a seed
    o.density = 0.5f;
    out = shape (o, mg190fm::bar (4));
    CHECK (out.size() > 10 && out.size() < 54 && out == shape (o, mg190fm::bar (4)));
    // Jitter moves keys within the range; the scale snaps them
    o = {};
    o.jitter = 1.0f;
    out = shape (o, mg190fm::bar (2));
    bool moved = false;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        CHECK (out[i].key >= 60 && out[i].key <= 84);
        moved = moved || out[i].key != (int) std::lround (60 + shapeAt (o, (double) out[i].start / (8.0 * ppq)) * 24);
    }
    CHECK (moved);
    o.jitter = 0.0f;
    auto keyed = mg190fm::bar();
    keyed.scale = mg190fm::aMinor();
    for (const auto& n : shape (o, keyed))
        CHECK (mg190fm::inScale (keyed.scale, n.key) && n.key >= 60 && n.key <= 84);
    // custom levels
    o.preset = ShapeOptions::custom;
    o.levels.fill (0.0f);
    o.levels[20] = 1.0f;
    CHECK (std::abs (shapeAt (o, 0.975) - 0.5) < 1e-6 && shapeAt (o, 0.9) == 0.0);
}

TEST (mg190fm_stacks_chords)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    const auto& bank = lynxChordBank();
    CHECK (bank.sets.size() == 3);
    for (const auto& s : bank.sets)
        CHECK ((s.scaleSize == 5 || s.scaleSize == 6 || s.scaleSize == 7) && s.rules.size() == 16);
    CHECK (bank.rulesFor (7) == &bank.sets[2].rules && bank.rulesFor (8) == &bank.sets[2].rules && bank.rulesFor (5) == &bank.sets[0].rules);
    CHECK (bank.rulesFor (3) == &bank.sets[0].rules); // smaller than any: the first
    // the default chord without a key: C (60) major triad, one bar
    StacksOptions o;
    const ScaleRef none;
    CHECK ((stackKeys (o.sequence[0], none, bank) == std::vector<int> { 60, 64, 67 }));
    CHECK (stackRoot (o.sequence[0], none) == 60 && stackChordName (o.sequence[0], none, bank) == "C");
    auto out = stacks (o, mg190fm::bar (2), bank);
    CHECK (out.size() == 6 && out[0].length == 4 * ppq && out[3].start == 4 * ppq); // the sequence repeats
    // in A minor: degree 0 octave 3 = A (69), the triad A C E; rule 1 = the seventh; inversions
    const auto am = mg190fm::aMinor();
    auto chord = o.sequence[0];
    CHECK (stackRoot (chord, am) == 69 && (stackKeys (chord, am, bank) == std::vector<int> { 69, 72, 76 }));
    CHECK (stackChordName (chord, am, bank) == "Am");
    chord.rule = 1;
    CHECK ((stackKeys (chord, am, bank) == std::vector<int> { 69, 72, 76, 79 }) && stackChordName (chord, am, bank) == "Am7");
    chord.rule = 0;
    chord.inversion = 1;
    CHECK ((stackKeys (chord, am, bank) == std::vector<int> { 72, 76, 81 }) && stackChordName (chord, am, bank) == "Am/C");
    chord.inversion = -1;
    CHECK ((stackKeys (chord, am, bank) == std::vector<int> { 64, 69, 72 }));
    chord.inversion = 0;
    chord.rootDegree = 2; // C major in A minor
    CHECK ((stackKeys (chord, am, bank) == std::vector<int> { 72, 76, 79 }) && stackChordName (chord, am, bank) == "C");
    chord.rootDegree = 4; // E minor
    CHECK (stackChordName (chord, am, bank) == "Em");
    chord.rootDegree = -1; // G below A
    CHECK (stackRoot (chord, am) == 67);
    chord.rootDegree = 0;
    chord.rule = 3; // sus4
    CHECK (stackChordName (chord, am, bank) == "Asus4");
    // a sequence: two half-bar chords; offset 2 sixteenths starts the second later and keeps its end
    o.sequence = { StackChord {}, StackChord {} };
    o.sequence[0].duration = o.sequence[1].duration = 0.5f;
    o.sequence[1].rootPitch = 67;
    o.sequence[1].offset = 2.0f;
    out = stacks (o, mg190fm::bar(), bank);
    CHECK (out.size() == 6 && out[0].start == 0 && out[0].length == 2 * ppq);
    CHECK (out[3].key == 67 && out[3].start == 2 * ppq + ppq / 2 && out[3].start + out[3].length == 4 * ppq);
    CHECK (stacks (o, mg190fm::bar (2), bank).size() == 12); // two bars: the sequence again
    // a negative offset never starts before the range
    o.sequence = { StackChord {} };
    o.sequence[0].offset = -4.0f;
    out = stacks (o, mg190fm::bar(), bank);
    CHECK (out.size() == 3 && out[0].start == 0);
}

TEST (mg190fm_stacks_user_chord_bank)
{
    using namespace lynx::midigen;
    // the layout of a user's .stacks file (LYNX's own test content, not Live's bank)
    const std::string json = R"({ "name": "test", "chord_rulesets": [
        { "scale_cardinality": 7, "rules": [ [0, 2, 4, 6, 8], [0, 4] ] },
        { "scale_cardinality": 12, "other": "x", "rules": [ [0, 4, 7], [0, -12, 7] ] },
        { "scale_cardinality": 5, "rules": [] } ] })";
    std::string error;
    const auto bank = readChordBank (json, error);
    CHECK (bank.has_value());
    if (bank)
    {
        CHECK (bank->sets.size() == 2 && bank->sets[0].scaleSize == 7 && bank->sets[1].scaleSize == 12);
        CHECK ((bank->sets[0].rules == std::vector<std::vector<int>> { { 0, 2, 4, 6, 8 }, { 0, 4 } }));
        CHECK ((bank->sets[1].rules[1] == std::vector<int> { 0, -12, 7 }));
        StackChord c;
        CHECK ((stackKeys (c, ScaleRef {}, *bank) == std::vector<int> { 60, 64, 67, 71, 74 })); // C major 9
        c.rule = 1;
        CHECK ((stackKeys (c, ScaleRef {}, *bank) == std::vector<int> { 60, 67 }));
        c.rule = 2; // past the rules: wraps
        CHECK (stackKeys (c, ScaleRef {}, *bank).size() == 5);
    }
    CHECK (! readChordBank ("{}", error).has_value() && ! error.empty());
    CHECK (! readChordBank (R"({"chord_rulesets": [ {"scale_cardinality": 7, "rules": [] } ]})", error).has_value());
    CHECK (! readChordBank ("not json at all", error).has_value());
}

TEST (mg190fm_euclid_and_generate)
{
    using namespace lynx::midigen;
    using lynx::ppq;
    EuclidOptions e; // 16 steps, 8 hits of 1/16
    auto out = euclid (e, mg190fm::bar());
    CHECK (out.size() == 8 && out[1].start == ppq / 2 && out[0].length == ppq / 4 && out[0].key == 36);
    e.density = 5;
    e.rotation = 0;
    out = euclid (e, mg190fm::bar());
    CHECK (out.size() == 5);
    for (const auto& n : out)
        CHECK (lynx::notetools::euclideanHit (16, 5, 0, (int) (n.start / (ppq / 4))));
    // the chooser's tool runs
    Settings s;
    s.tool = Settings::shape;
    CHECK (generate (s, mg190fm::bar(), lynxChordBank()) == shape (s.shapeOptions, mg190fm::bar()));
    s.tool = Settings::stacks;
    CHECK (generate (s, mg190fm::bar(), lynxChordBank()).size() == 3);
    s.tool = 99;
    CHECK (generate (s, mg190fm::bar(), lynxChordBank()).empty());
}

TEST (mg190fm_generate_replaces_the_range)
{
    using namespace lynx;
    using namespace lynx::midigen;
    Pattern p;
    const auto add = [&p] (std::int64_t start, int channel, int key) {
        Note n;
        n.start = start;
        n.length = ppq / 4;
        n.channel = channel;
        n.key = key;
        p.notes.push_back (n);
    };
    add (0, 0, 50);           // in the range: replaced
    add (ppq, 0, 51);         // in the range: replaced
    add (ppq, 1, 52);         // another channel: stays
    add (4 * ppq, 0, 53);     // after the range: stays
    add (ppq / 2 - 1, 0, 54); // before the range (from = ppq / 2): stays, even though it reaches into it
    p.sortNotes();
    Context c;
    c.from = ppq / 2;
    c.to = 4 * ppq;
    c.channel = 0;
    CHECK (notesInRange (p, 0, c.from, c.to).size() == 1);
    c.from = 0;
    CHECK (notesInRange (p, 0, c.from, c.to).size() == 3);
    c.from = ppq / 2;
    RhythmOptions r;
    r.pattern = 0;
    auto made = rhythm (r, c);
    for (auto& n : made)
        n.channel = 5; // (writeGenerated gives them the context's channel)
    const auto indices = writeGenerated (p, c, made);
    CHECK (indices.size() == made.size());
    int kept = 0;
    for (const auto& n : p.notes)
        kept += n.key == 50 || n.key == 52 || n.key == 53 || n.key == 54 ? 1 : 0;
    CHECK (kept == 4 && p.notes.size() == 4 + made.size() && made.size() == 4);
    for (auto i : indices)
        CHECK (i >= 0 && i < (int) p.notes.size() && p.notes[(std::size_t) i].key == 36 && p.notes[(std::size_t) i].channel == 0);
    CHECK (std::none_of (p.notes.begin(), p.notes.end(), [] (const Note& n) { return n.key == 51; }));
}

TEST (mg190fm_settings_text_round_trip)
{
    using namespace lynx::midigen;
    Settings s;
    CHECK (decode (encode (s)) == s && decode ("") == s && decode ("nonsense\nfoo=1\n") == s);
    s.tool = Settings::seed;
    s.rhythmOptions = { 40, 12, 33, 5, 5, 0.25f, -2, 90, 120, 3, 1, 2 };
    s.seedOptions.density = 0.3f;
    s.seedOptions.minDuration = -5.5f;
    s.seedOptions.voices = 6;
    s.shapeOptions.preset = ShapeOptions::custom;
    s.shapeOptions.levels[3] = 0.123456789f;
    s.shapeOptions.rate = 4;
    s.shapeOptions.jitter = 0.4f;
    s.stacksOptions.sequence.push_back ({ 3, 2, 55, -1, 7, 0.75f, -3.0f });
    s.stacksOptions.selected = 1;
    s.stacksOptions.bankFile = "C:/Users/x/My Bank=1.stacks";
    s.euclidOptions = { 12, 5, 3, 3, 40, 80 };
    CHECK (! s.isDefault());
    const auto text = encode (s);
    CHECK (decode (text) == s);
    // CR LF line ends (a file edited elsewhere) read the same
    std::string crlf;
    for (auto ch : text)
        crlf += ch == '\n' ? std::string ("\r\n") : std::string (1, ch);
    CHECK (decode (crlf) == s);
    // values out of range are clamped; a broken chord line is skipped
    const auto odd = decode ("tool=42\nrhythm.steps=500\nseed.voices=-3\nshape.levels=2,-1,0.5\nstacks.chord=1,2\n");
    CHECK (odd.tool == Settings::toolCount - 1 && odd.rhythmOptions.steps == 32 && odd.seedOptions.voices == 1);
    CHECK (odd.shapeOptions.levels[0] == 1.0f && odd.shapeOptions.levels[1] == 0.0f && odd.shapeOptions.levels[2] == 0.5f);
    CHECK (odd.stacksOptions.sequence.size() == 1 && odd.stacksOptions.sequence[0] == StackChord {});
}
