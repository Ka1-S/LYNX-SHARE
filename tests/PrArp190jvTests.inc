// Round 190jv, helper "pr" (coverage prtools-0003): FL's Piano roll arpeggiator tool with its patterns (TPRArpForm;
// REA of FLEngine_x64.dll: the loader FUN_00df2680, the apply FUN_00df2bd0, the levels FUN_00df2ad0 — the facts in
// src/core/FlArpeggiator.h and docs/analise/REA_rodada190jv.md, "pr"). Expected results computed by hand from the law.
#include "core/FlArpeggiator.h"

namespace prarp190jv
{
using namespace lynx;
constexpr std::int64_t sixteenth = ppq / 4; // FL's 24 ticks at PPQ 96

// a pattern as FL's arpeggiator files hold them: keys 60 + rank, one sixteenth each (FL's Default.fsc: 1 2 3)
inline prtools::Score score (std::initializer_list<int> keys, std::int64_t length = sixteenth)
{
    prtools::Score s;
    std::int64_t t = 0;
    for (const auto k : keys)
    {
        prtools::ScoreNote n;
        n.start = t;
        n.length = length;
        n.key = k;
        s.notes.push_back (n);
        t += sixteenth;
    }
    return s;
}

inline Pattern chord (std::initializer_list<int> keys, std::int64_t start, std::int64_t length, int channel = 0, Pattern p = {})
{
    for (const auto k : keys)
    {
        Note n;
        n.channel = channel;
        n.key = k;
        n.start = start;
        n.length = length;
        p.notes.push_back (n);
    }
    p.sortNotes();
    return p;
}

inline std::vector<int> all (const Pattern& p)
{
    std::vector<int> v;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        v.push_back (i);
    return v;
}

// (start, key) of every note, in time order
inline std::vector<std::pair<std::int64_t, int>> starts (const Pattern& p)
{
    std::vector<std::pair<std::int64_t, int>> v;
    for (const auto& n : p.notes)
        v.push_back ({ n.start, n.key });
    std::sort (v.begin(), v.end());
    return v;
}

inline std::vector<int> keysInOrder (const Pattern& p)
{
    std::vector<int> v;
    for (const auto& [s, k] : starts (p))
        v.push_back (k);
    return v;
}
} // namespace prarp190jv

TEST (prarp190jv_loader_time_mul_gate_and_loop)
{
    using namespace prarp190jv;
    flarp::Options o;
    auto prepared = flarp::prepare (score ({ 60, 61, 62 }), o);
    CHECK (prepared.loop == 3 * sixteenth); // the end of the last note
    CHECK (prepared.steps.size() == 3 && prepared.steps[1].start == sixteenth && prepared.steps[2].length == sixteenth);
    // FL's Time mul table: wheel 1…7 = 1/8 1/4 1/2 1 2 3 4
    CHECK (flarp::timeMultiplier (1) == 0.125f && flarp::timeMultiplier (3) == 0.5f && flarp::timeMultiplier (4) == 1.0f
           && flarp::timeMultiplier (7) == 4.0f);
    o.timeMul = 3; // ×1/2
    o.gate = 24;   // 50 %
    prepared = flarp::prepare (score ({ 60, 61, 62 }), o);
    CHECK (prepared.loop == 3 * sixteenth / 2);
    CHECK (prepared.steps[1].start == sixteenth / 2 && prepared.steps[1].length == sixteenth / 4);
    // the gate leaves a note as long as the loop alone; FL's minimum length (4 ticks at PPQ 96)
    auto held = score ({ 60, 61, 62 });
    held.notes[0].length = 3 * sixteenth;
    o.timeMul = 4;
    o.gate = 0;
    prepared = flarp::prepare (held, o);
    CHECK (prepared.steps[0].length == 3 * sixteenth);
    CHECK (prepared.steps[1].length == flarp::minLength && flarp::minLength == ppq / 24);
    // a colour-12 note sets the loop to its own length and is dropped
    auto marked = score ({ 60, 61 });
    prtools::ScoreNote marker;
    marker.length = 8 * sixteenth;
    marker.colour = 12;
    marker.key = 72;
    marked.notes.insert (marked.notes.begin(), marker);
    prepared = flarp::prepare (marked, flarp::Options {});
    CHECK (prepared.loop == 8 * sixteenth && prepared.steps.size() == 2);
}

TEST (prarp190jv_default_pattern_on_a_chord)
{
    using namespace prarp190jv;
    // C E G held for one bar, FL's 1 2 3 pattern: C E G C E G … every sixteenth, 16 notes
    auto p = chord ({ 60, 64, 67 }, 0, 16 * sixteenth);
    const auto added = flarp::apply (p, all (p), flarp::prepare (score ({ 60, 61, 62 }), {}), {});
    CHECK (added.size() == 16 && p.notes.size() == 16);
    const auto keys = keysInOrder (p);
    for (int i = 0; i < 16; ++i)
        CHECK (keys[(std::size_t) i] == (std::array<int, 3> { 60, 64, 67 })[(std::size_t) (i % 3)]);
    CHECK (starts (p)[15].first == 15 * sixteenth && p.notes.back().length == sixteenth);
    // two held notes: rank % 2 (C E C C E C …)
    auto two = chord ({ 60, 64 }, 0, 6 * sixteenth);
    flarp::apply (two, all (two), flarp::prepare (score ({ 60, 61, 62 }), {}), {});
    CHECK ((keysInOrder (two) == std::vector<int> { 60, 64, 60, 60, 64, 60 }));
    // the pattern's octave: key 72 = one octave up (k / 12 − 5)
    auto up = chord ({ 60, 64, 67 }, 0, 2 * sixteenth);
    flarp::apply (up, all (up), flarp::prepare (score ({ 72, 49 }), {}), {});
    CHECK ((keysInOrder (up) == std::vector<int> { 72, 52 }));
}

TEST (prarp190jv_pattern_and_range_modes)
{
    using namespace prarp190jv;
    const auto run = [] (flarp::Options o, int bars16) {
        auto p = chord ({ 60, 64, 67 }, 0, bars16 * sixteenth);
        flarp::apply (p, all (p), flarp::prepare (score ({ 60, 61, 62 }), o), o);
        return keysInOrder (p);
    };
    flarp::Options o;
    o.patternMode = flarp::Mode::flip;
    CHECK ((run (o, 6) == std::vector<int> { 67, 64, 60, 67, 64, 60 }));
    o.patternMode = flarp::Mode::alternate; // every other loop pass from the top
    CHECK ((run (o, 9) == std::vector<int> { 60, 64, 67, 67, 64, 60, 60, 64, 67 }));
    o = {};
    o.range = 2; // the loop pass modulo the range adds octaves
    CHECK ((run (o, 9) == std::vector<int> { 60, 64, 67, 72, 76, 79, 60, 64, 67 }));
    o.rangeMode = flarp::Mode::flip;
    CHECK ((run (o, 6) == std::vector<int> { 72, 76, 79, 60, 64, 67 }));
    o.range = 3;
    o.rangeMode = flarp::Mode::alternate; // 0 1 2 then 2 1 0 (every other Range cycle reversed)
    CHECK ((run (o, 18) == std::vector<int> { 60, 64, 67, 72, 76, 79, 84, 88, 91, 84, 88, 91, 72, 76, 79, 60, 64, 67 }));
    // a pattern note of colour 8 keeps its octave whatever the range
    o = {};
    o.range = 2;
    auto fixed = score ({ 60, 61, 62 });
    fixed.notes[2].colour = 8;
    auto p = chord ({ 60, 64, 67 }, 0, 6 * sixteenth);
    flarp::apply (p, all (p), flarp::prepare (fixed, o), o);
    CHECK ((keysInOrder (p) == std::vector<int> { 60, 64, 67, 72, 76, 67 }));
}

TEST (prarp190jv_sync_time_block_chord)
{
    using namespace prarp190jv;
    // C E G for 8 sixteenths, then F A C for 8: the second chord starts in the middle of a loop pass (8 = 2 × 3 + 2)
    const auto run = [] (flarp::Sync sync) {
        auto p = chord ({ 60, 64, 67 }, 0, 8 * sixteenth);
        p = chord ({ 65, 69, 72 }, 8 * sixteenth, 8 * sixteenth, 0, p);
        flarp::Options o;
        o.sync = sync;
        flarp::apply (p, all (p), flarp::prepare (score ({ 60, 61, 62 }), o), o);
        return keysInOrder (p);
    };
    // Time: the loop runs on from the pattern's start — the F chord comes in on rank 3
    CHECK ((run (flarp::Sync::time) == std::vector<int> { 60, 64, 67, 60, 64, 67, 60, 64, 72, 65, 69, 72, 65, 69, 72, 65 }));
    // Block: the C chord ended where F starts (nothing held): F starts its loop
    CHECK ((run (flarp::Sync::block) == std::vector<int> { 60, 64, 67, 60, 64, 67, 60, 64, 65, 69, 72, 65, 69, 72, 65, 69 }));
    CHECK ((run (flarp::Sync::chord) == std::vector<int> { 60, 64, 67, 60, 64, 67, 60, 64, 65, 69, 72, 65, 69, 72, 65, 69 }));
    // Block vs Chord: a held bass note under both chords — Block keeps the loop running, Chord restarts it
    const auto overBass = [] (flarp::Sync sync) {
        auto p = chord ({ 48 }, 0, 8 * sixteenth);
        p = chord ({ 64 }, 0, 4 * sixteenth, 0, p);
        p = chord ({ 65 }, 4 * sixteenth, 4 * sixteenth, 0, p);
        flarp::Options o;
        o.sync = sync;
        flarp::apply (p, all (p), flarp::prepare (score ({ 60, 61, 62 }), o), o);
        return keysInOrder (p);
    };
    CHECK ((overBass (flarp::Sync::block) == std::vector<int> { 48, 64, 48, 48, 65, 48, 48, 65 }));
    CHECK ((overBass (flarp::Sync::chord) == std::vector<int> { 48, 64, 48, 48, 48, 65, 48, 48 }));
}

TEST (prarp190jv_open_notes_join_and_levels)
{
    using namespace prarp190jv;
    // "1- 2 3": the first pattern note lasts the whole loop, so it is open: the next pass's C joins it
    auto held = score ({ 60, 61, 62 });
    held.notes[0].length = 3 * sixteenth;
    auto p = chord ({ 60, 64, 67 }, 0, 6 * sixteenth);
    flarp::apply (p, all (p), flarp::prepare (held, {}), {});
    CHECK ((starts (p) == std::vector<std::pair<std::int64_t, int>> { { 0, 60 }, { sixteenth, 64 }, { 2 * sixteenth, 67 },
                                                                       { 4 * sixteenth, 64 }, { 5 * sixteenth, 67 } }));
    CHECK (p.notes.front().key == 60 && p.notes.front().length == 6 * sixteenth);

    // levels: velocity × pattern / 100, the others + (pattern − centre); by the strength (FUN_00df2ad0)
    auto lv = score ({ 60 });
    lv.notes[0].velocity = 50.0f / 128.0f; // FL byte 50
    lv.notes[0].pan = 0.5f;                // FL byte 96
    lv.notes[0].porta = true;
    lv.notes[0].slide = true;
    auto q = chord ({ 60 }, 0, sixteenth);
    q.notes[0].velocity = 100.0f / 128.0f;
    flarp::apply (q, all (q), flarp::prepare (lv, {}), {});
    CHECK (q.notes.size() == 1 && q.notes[0].velocity == 50.0f / 128.0f && q.notes[0].pan == 0.5f);
    CHECK (q.notes[0].porta && q.notes[0].slide); // the pattern's slide flag; its porta toggles the held note's
    flarp::Options half;
    half.strength = { 64, 64, 128, 128, 128, 128 }; // PAN VEL at 50 %
    auto r = chord ({ 60 }, 0, sixteenth);
    r.notes[0].velocity = 100.0f / 128.0f;
    flarp::apply (r, all (r), flarp::prepare (lv, half), half);
    CHECK (r.notes[0].velocity == 75.0f / 128.0f && r.notes[0].pan == 0.25f);
    CHECK (flarp::detail::blend (1, 100, 50, 64) == 75 && flarp::detail::blend (0, 64, 96, 128) == 96);
    CHECK (flarp::detail::blend (0, 120, 128, 128) == 128); // clamped to FL's range
    CHECK (flarp::detail::blend (5, 120, 120, 0) == 120);
}

TEST (prarp190jv_only_the_chosen_notes_and_groups)
{
    using namespace prarp190jv;
    // channel 1's chord is not chosen: it stays as it is
    auto p = chord ({ 60, 64, 67 }, 0, 3 * sixteenth);
    p = chord ({ 40 }, 0, 3 * sixteenth, 1, p);
    std::vector<int> chosen;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        if (p.notes[(std::size_t) i].channel == 0)
            chosen.push_back (i);
    flarp::Options o;
    o.groupNotes = true;
    const auto added = flarp::apply (p, chosen, flarp::prepare (score ({ 60, 61, 62 }), o), o);
    CHECK (added.size() == 3 && p.notes.size() == 4);
    CHECK (std::count_if (p.notes.begin(), p.notes.end(), [] (const Note& n) { return n.channel == 1 && n.key == 40 && n.length == 3 * sixteenth; }) == 1);
    // Group notes: each source note gets its own new group, its copies share it
    std::set<int> groups;
    for (const auto i : added)
        groups.insert (p.notes[(std::size_t) i].group);
    CHECK (groups.size() == 3 && groups.count (0) == 0);
    // no pattern / no notes: nothing happens
    auto same = chord ({ 60 }, 0, sixteenth);
    CHECK (flarp::apply (same, all (same), flarp::Prepared {}, {}).empty() && same.notes.size() == 1);
}

TEST (prarp190jv_reads_fl_arpeggiator_scores)
{
    using namespace prarp190jv;
    // an .fsc as FL writes it (prtools::writeScore), read back and prepared: FL's 1 2 3 at LYNX's PPQ
    std::vector<Note> notes;
    for (int k = 0; k < 3; ++k)
    {
        Note n;
        n.start = k * sixteenth;
        n.length = sixteenth;
        n.key = 60 + k;
        notes.push_back (n);
    }
    const auto bytes = prtools::writeScore (notes);
    const auto parsed = prtools::parseScore ({ bytes.data(), bytes.size() });
    CHECK (parsed.has_value());
    if (parsed)
    {
        const auto prepared = flarp::prepare (*parsed, {});
        CHECK (prepared.loop == 3 * sixteenth && prepared.steps.size() == 3 && prepared.steps[2].key == 62);
    }
    // the dialog lists FL's Arpeggiator folder of the user's install (read only) and runs this law for a pattern
    const auto dialogs = i18ntest::readFile ("src/ui/NoteToolDialogs.cpp");
    CHECK (dialogs.find ("scoreFolder (\"Arpeggiator\")") != std::string::npos);
    CHECK (dialogs.find ("flarp::apply (p, indices, ") != std::string::npos);
}
