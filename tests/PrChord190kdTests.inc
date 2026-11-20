// Round 190kd: FL's Chord progression tool — the strip of chord blocks, the Generate modes (Rework / Start over /
// Analyze), the options for generation (Main / Bass, Non chord notes), Learn from MIDI input and the chord menu's note
// changes (core/ChordStrip.h; REA in docs/analise/REA_rodada190kd.md).
#include "core/ChordStrip.h"

namespace prchord190kd
{
inline lynx::Note note (int key, std::int64_t start, std::int64_t length, int channel = 0)
{
    lynx::Note n;
    n.key = key;
    n.start = start;
    n.length = length;
    n.channel = channel;
    return n;
}
inline lynx::prtools::Chord chord (int root, std::vector<int> intervals, int bass = -1)
{
    lynx::prtools::Chord c;
    c.root = root;
    c.intervals = std::move (intervals);
    c.bass = bass;
    return c;
}
inline lynx::chordstrip::Strip strip (std::initializer_list<lynx::prtools::Chord> chords, int tonic = 0, bool minor = false)
{
    return lynx::chordstrip::fromChords (std::vector<lynx::prtools::Chord> (chords), tonic, minor, 4 * lynx::ppq, 4, false);
}
} // namespace prchord190kd

TEST (prchord190kd_block_notes_inversion_voicing_bass)
{
    using namespace lynx;
    using namespace prchord190kd;
    chordstrip::Block b;
    b.chord = chord (0, { 0, 4, 7 }); // C major, octave 4: C4 E4 G4 = 60 64 67
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 60, 64, 67 }));
    CHECK (chordstrip::bassKey (b, 4) == 48); // the root an octave under
    b.inversion = 1; // FL's Invert › Up: the lowest goes up an octave
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 64, 67, 72 }));
    b.inversion = -1; // Down: the highest goes down
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 55, 60, 64 }));
    b.inversion = 0;
    b.voicing = chordstrip::Voicing::open; // every second note up an octave
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 60, 67, 76 }));
    b.voicing = chordstrip::Voicing::octave; // the lowest again an octave above
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 60, 64, 67, 72 }));
    b.voicing = chordstrip::Voicing::stacked; // over two octaves
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 60, 64, 67, 72, 76, 79 }));
    b.voicing = chordstrip::Voicing::block;
    b.transpose = 12; // Transpose › One octave up
    CHECK ((chordstrip::mainKeys (b, 4) == std::vector<int> { 72, 76, 79 }));
    b.transpose = 0;
    b.chord.bass = 4; // C/E
    CHECK (chordstrip::bassKey (b, 4) == 52);
    b.fixedBass = 7; // Fixed bass › G wins over the slash bass
    CHECK (chordstrip::bassKey (b, 4) == 55);
    b.bassTranspose = -1; // Transpose › Bass: one octave down
    CHECK (chordstrip::bassKey (b, 4) == 43);
    // names: abs / rel (FL's Names switch)
    const auto s = strip ({ chord (9, { 0, 3, 7 }), chord (5, { 0, 4, 7 }), chord (4, { 0, 3, 7 }), chord (0, { 0, 4, 7 }) });
    CHECK (chordstrip::blockName (s, s.blocks[0], false) == "Am");
    CHECK (chordstrip::blockName (s, s.blocks[0], true) == "vi");
    CHECK (chordstrip::blockName (s, s.blocks[1], true) == "IV");
    CHECK (chordstrip::blockName (s, s.blocks[2], true) == "iii");
    CHECK (chordstrip::keyName (s) == "C Major (Ionian)");
    CHECK (chordstrip::toText (s) == "vi-IV-iii-I");
}

TEST (prchord190kd_parse_names_and_numerals)
{
    using namespace lynx;
    using namespace prchord190kd;
    // FL's Type a progression: names or numerals, separated by commas, dashes, bars or spaces
    const auto a = chordstrip::parseText ("C, G, Am, F", 0, false);
    CHECK (a.has_value() && a->size() == 4);
    if (a && a->size() == 4)
    {
        CHECK ((*a)[0].root == 0 && (*a)[0].intervals == (std::vector<int> { 0, 4, 7 }));
        CHECK ((*a)[1].root == 7);
        CHECK ((*a)[2].root == 9 && (*a)[2].intervals == (std::vector<int> { 0, 3, 7 }));
        CHECK ((*a)[3].root == 5);
    }
    const auto b = chordstrip::parseText ("I-V-vi-IV", 2, false); // in D: D A Bm G
    CHECK (b.has_value() && b->size() == 4 && (*b)[2].root == 11 && (*b)[3].root == 7);
    const auto c = chordstrip::parseText ("F#m add9 | C/E | Bb7 | Caug", 0, false);
    CHECK (c.has_value() && c->size() == 4);
    if (c && c->size() == 4)
    {
        CHECK ((*c)[0].root == 6 && (*c)[0].intervals == (std::vector<int> { 0, 3, 7, 14 }));
        CHECK ((*c)[1].root == 0 && (*c)[1].bass == 4);
        CHECK ((*c)[2].root == 10 && (*c)[2].intervals == (std::vector<int> { 0, 4, 7, 10 }));
        CHECK ((*c)[3].intervals == (std::vector<int> { 0, 4, 8 }));
    }
    CHECK (! chordstrip::parseText ("H7", 0, false).has_value());
    CHECK (! chordstrip::parseText ("", 0, false).has_value());
}

TEST (prchord190kd_generate_modes_keep_locked_blocks)
{
    using namespace lynx;
    using namespace prchord190kd;
    auto s = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }), chord (9, { 0, 3, 7 }), chord (5, { 0, 4, 7 }) });
    s.blocks[1].locked = true;
    const auto lockedBefore = s.blocks[1];
    chordstrip::GenerateOptions o;
    for (std::uint32_t seed = 1; seed < 40; ++seed)
    {
        o.seed = seed;
        o.mode = chordstrip::GenMode::rework;
        auto r = s;
        chordstrip::generate (r, {}, o);
        CHECK (r.blocks.size() == 4);
        CHECK (r.blocks[1].chord == lockedBefore.chord && r.blocks[1].locked);
        CHECK (r.tonic == 0 && ! r.minor); // Rework keeps the key
        for (const auto& b : r.blocks) // LYNX's generator stays in the key (diatonic roots)
            CHECK (chordstrip::inKey (r, b.chord.root));
    }
    // Start over: a fresh key when nothing is locked, and the blocks' own changes reset
    auto fresh = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }) });
    fresh.blocks[0].transpose = 12;
    fresh.blocks[0].voicing = chordstrip::Voicing::open;
    o.mode = chordstrip::GenMode::startOver;
    bool keyChanged = false;
    for (std::uint32_t seed = 1; seed < 20; ++seed)
    {
        o.seed = seed;
        auto r = fresh;
        chordstrip::generate (r, {}, o);
        keyChanged = keyChanged || r.tonic != 0 || r.minor;
        CHECK (r.blocks[0].transpose == 0 && r.blocks[0].voicing == chordstrip::Voicing::block);
        CHECK (r.blocks[0].length == 4 * ppq);
        for (const auto& b : r.blocks)
            CHECK (chordstrip::inKey (r, b.chord.root));
    }
    CHECK (keyChanged);
    // a locked chord stays, and Start over draws the key from it (FL: the locked chords as evidence; the G chord's root
    // weighs 4, its notes 0.3: G major scores 4.9 with root score 4.3, ahead of C major's 4.9 / 0)
    fresh.blocks[1].locked = true;
    o.seed = 3;
    auto kept = fresh;
    chordstrip::generate (kept, {}, o);
    CHECK (kept.tonic == 7 && ! kept.minor && kept.blocks[1].chord == fresh.blocks[1].chord && kept.blocks[1].locked);
}

TEST (prchord190kd_analyze_reads_the_notes_under_each_block)
{
    using namespace lynx;
    using namespace prchord190kd;
    const auto bar = 4 * ppq;
    // bar 1: A C E (A minor), bar 2: F A C (F), bar 3: G B D (G), bar 4: C G C with E two octaves down (C/E: FL names a
    // slash bass only an octave or more under the next note)
    std::vector<Note> notes {
        note (57, 0, bar), note (60, 0, bar), note (64, 0, bar),
        note (53, bar, bar), note (57, bar, bar), note (60, bar, bar),
        note (55, 2 * bar, bar), note (59, 2 * bar, bar), note (62, 2 * bar, bar),
        note (40, 3 * bar, bar), note (60, 3 * bar, bar), note (67, 3 * bar, bar),
    };
    auto s = strip ({ chord (0, { 0, 4, 7 }), chord (0, { 0, 4, 7 }), chord (0, { 0, 4, 7 }), chord (0, { 0, 4, 7 }) });
    s.blocks[2].locked = true; // a locked block is left alone
    chordstrip::GenerateOptions o;
    o.mode = chordstrip::GenMode::analyze;
    chordstrip::generate (s, notes, o);
    CHECK (s.blocks[0].chord.root == 9 && s.blocks[0].chord.intervals == (std::vector<int> { 0, 3, 7 }));
    CHECK (s.blocks[1].chord.root == 5 && s.blocks[1].chord.intervals == (std::vector<int> { 0, 4, 7 }));
    CHECK (s.blocks[2].chord.root == 0); // locked: still C
    CHECK (s.blocks[3].chord.root == 0 && s.blocks[3].chord.bass == 4);
    // a melody alone still finds a chord; a block without notes takes the one before it (FL's fill_blanks)
    std::vector<Note> melody { note (62, 0, ppq), note (65, ppq, ppq), note (69, 2 * ppq, 2 * ppq) }; // D F A
    auto m = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }) });
    chordstrip::generate (m, melody, o);
    CHECK (m.blocks[0].chord.root == 2 && m.blocks[0].chord.intervals == (std::vector<int> { 0, 3, 7 })); // Dm
    CHECK (m.blocks[1].chord.root == 2 && m.blocks[1].chord.intervals == (std::vector<int> { 0, 3, 7 }));
    // no notes at all: nothing changes
    auto none = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }) });
    chordstrip::generate (none, {}, o);
    CHECK (none.blocks[0].chord.root == 0 && none.blocks[1].chord.root == 7);
    // the played notes outside the triad become extensions (FL's get_chord)
    const auto r = chordstrip::analysis::analyse ({ note (57, 0, bar), note (60, 0, bar), note (64, 0, bar), note (67, 0, bar) }, 0, bar,
                                                  chordstrip::analysis::sequence());
    CHECK (r.type == 1 && r.root == 9 && r.chord().intervals == (std::vector<int> { 0, 3, 7, 10 })); // Am7
}

TEST (prchord190kd_learn_from_midi_and_snap)
{
    using namespace lynx;
    using namespace prchord190kd;
    // the played keys name the chord; the lowest is the bass
    auto c = chordstrip::learnChord ({ 57, 60, 64, 67 }); // A C E G = Am7
    CHECK (c.has_value() && c->root == 9 && c->intervals == (std::vector<int> { 0, 3, 7, 10 }) && c->bass == -1);
    c = chordstrip::learnChord ({ 40, 60, 67, 72 }); // E (two octaves down) C G C = C/E
    CHECK (c.has_value() && c->root == 0 && c->bass == 4 && c->intervals == (std::vector<int> { 0, 4, 7 }));
    c = chordstrip::learnChord ({ 52, 60, 67, 72 }); // E only 8 under C: no slash bass named (FL: an octave or more)
    CHECK (c.has_value() && c->root == 0 && c->bass == -1);
    c = chordstrip::learnChord ({ 60 }); // one key is enough: its major triad (FL's min_notes 1)
    CHECK (c.has_value() && c->root == 0 && c->intervals == (std::vector<int> { 0, 4, 7 }));
    CHECK (! chordstrip::learnChord ({}).has_value());
    // the played voicing: E G C (first inversion) gives the C chord with the inversion that puts E lowest
    chordstrip::Block old;
    old.chord = chord (5, { 0, 4, 7 });
    old.length = 4 * ppq;
    const auto learned = chordstrip::learnBlock (old, { 64, 67, 72 }, 4);
    CHECK (learned.has_value() && learned->chord.root == 0 && learned->inversion == 1 && chordstrip::mainKeys (*learned, 4).front() % 12 == 4);
    // Snap: the nearest chord note, a tie goes down
    chordstrip::Block b;
    b.chord = chord (0, { 0, 4, 7 });
    CHECK (chordstrip::snapToChord (61, b) == 60);
    CHECK (chordstrip::snapToChord (62, b) == 60); // D: C and E are both 2 away -> down
    CHECK (chordstrip::snapToChord (66, b) == 67);
    CHECK (chordstrip::snapToChord (64, b) == 64);
}

TEST (prchord190kd_write_options_and_non_chord_notes)
{
    using namespace lynx;
    using namespace prchord190kd;
    const auto bar = 4 * ppq;
    const auto s = strip ({ chord (0, { 0, 4, 7 }), chord (5, { 0, 4, 7 }) }); // C | F, a bar each (C4 E4 G4 + C3; C4 F4 A4 + F3)
    const auto base = [&] {
        Pattern p;
        // E4 on beat 1 (a chord key), D4 on beat 2 (not), B4 in bar 4 (after the strip), and another channel's note
        p.notes = { note (64, 0, ppq), note (62, ppq, ppq), note (71, 3 * bar, ppq), note (40, bar + ppq, ppq, 1) };
        return p;
    };
    const std::vector<int> existing { 0, 1, 2 };
    const auto has = [] (const Pattern& p, int key, std::int64_t start, std::int64_t length) {
        return std::any_of (p.notes.begin(), p.notes.end(), [&] (const Note& n) { return n.key == key && n.start == start && n.length == length; });
    };
    chordstrip::WriteOptions w;
    w.octave = 4;

    // Keep (FL's default): the old notes stay, 2 chords x (3 + bass) notes are written, one group per chord
    auto p = base();
    CHECK (chordstrip::write (p, existing, s, w) == 8);
    CHECK (p.notes.size() == 12);
    std::set<int> groups;
    for (const auto& n : p.notes)
        if (n.group != 0)
            groups.insert (n.group);
    CHECK (groups.size() == 2);
    CHECK (has (p, 64, 0, ppq) && has (p, 62, ppq, ppq) && has (p, 71, 3 * bar, ppq));

    // Replace all: the parts inside the chord blocks go (E and D), the B after the strip stays
    p = base();
    w.nonChord = chordstrip::NonChord::replaceAll;
    chordstrip::write (p, existing, s, w);
    CHECK (p.notes.size() == 10);
    CHECK (! has (p, 64, 0, ppq) && ! has (p, 62, ppq, ppq) && has (p, 71, 3 * bar, ppq));
    // a note across a block's end keeps its tail after the strip
    {
        Pattern q;
        q.notes = { note (62, bar + 2 * ppq, 2 * bar) };
        chordstrip::write (q, { 0 }, s, w);
        CHECK (has (q, 62, 2 * bar, bar + 2 * ppq));
    }
    // Replace clashing: only the note on a chord key (E4) is cut; D4 and B4 stay
    p = base();
    w.nonChord = chordstrip::NonChord::replaceClashing;
    chordstrip::write (p, existing, s, w);
    CHECK (p.notes.size() == 11);
    CHECK (! has (p, 64, 0, ppq) && has (p, 62, ppq, ppq) && has (p, 71, 3 * bar, ppq));
    // Snap: E4 stays (a chord note), D4 moves to C4 or E4 (equally near: FL's coin), the B after the strip stays
    p = base();
    w.nonChord = chordstrip::NonChord::snap;
    chordstrip::write (p, existing, s, w);
    CHECK (p.notes.size() == 12);
    CHECK (has (p, 64, 0, ppq) && ! has (p, 62, ppq, ppq) && (has (p, 60, ppq, ppq) || has (p, 64, ppq, ppq)) && has (p, 71, 3 * bar, ppq));
    // two snapped notes sounding together never land on one key
    {
        Pattern q;
        q.notes = { note (61, 0, ppq), note (62, 0, ppq), note (63, 0, ppq) }; // C# D D# over C major (a beat: not a chord note copy)
        chordstrip::write (q, { 0, 1, 2 }, s, w);
        std::set<int> keys;
        int count = 0;
        for (const auto& n : q.notes)
            if (n.group == 0)
                keys.insert (n.key), ++count;
        CHECK (count == 3 && keys.size() == 3);
        for (auto k : keys)
            CHECK (k % 12 == 0 || k % 12 == 4 || k % 12 == 7);
    }

    // Main off, Bass on: only the bass; both off: no notes (FL: chords shown, no notes)
    p = base();
    w.nonChord = chordstrip::NonChord::keep;
    w.main = false;
    CHECK (chordstrip::write (p, existing, s, w) == 2);
    w.bass = false;
    p = base();
    CHECK (chordstrip::write (p, existing, s, w) == 0);
    // a block's own switches and colour
    w.main = w.bass = true;
    auto t = s;
    t.blocks[0].main = false;
    t.blocks[1].colour = 3;
    p = base();
    CHECK (chordstrip::write (p, existing, t, w) == 5);
    CHECK (std::count_if (p.notes.begin(), p.notes.end(), [] (const Note& n) { return n.colour == 3; }) == 4);
    // Snap existing notes on one block (chord menu) with Keep
    w.snapBlocks = { 0 };
    p = base();
    chordstrip::write (p, existing, s, w);
    CHECK (! has (p, 62, ppq, ppq) && (has (p, 60, ppq, ppq) || has (p, 64, ppq, ppq)));
}

TEST (prchord190kd_count_passing_alternatives_voice_leading)
{
    using namespace lynx;
    using namespace prchord190kd;
    auto s = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }), chord (9, { 0, 3, 7 }), chord (5, { 0, 4, 7 }) });
    chordstrip::GenerateOptions o;
    // Count: chords added at the end are generated, chords removed from the end
    chordstrip::setCount (s, 6, 2 * ppq, o);
    CHECK (s.blocks.size() == 6 && s.blocks[5].length == 2 * ppq && s.length() == 4 * 4 * ppq + 2 * 2 * ppq);
    chordstrip::setCount (s, 3, 4 * ppq, o);
    CHECK (s.blocks.size() == 3 && s.blocks[2].chord.root == 9);
    // Add passing chord: the block splits in two, the first half leads to it (its dominant here: D -> G)
    auto p = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }) });
    chordstrip::addPassingChord (p, 1, o);
    CHECK (p.blocks.size() == 3 && p.blocks[1].length == 2 * ppq && p.blocks[2].length == 2 * ppq);
    CHECK (p.blocks[1].chord.root == 2 && p.blocks[2].chord.root == 7);
    CHECK (p.length() == 8 * ppq);
    // alternatives: six different chords, none the current one
    const auto alts = chordstrip::alternatives (s, 1);
    CHECK (alts.size() == 6);
    for (std::size_t i = 0; i < alts.size(); ++i)
    {
        CHECK (! (alts[i].root == s.blocks[1].chord.root && alts[i].intervals == s.blocks[1].chord.intervals));
        for (std::size_t k = i + 1; k < alts.size(); ++k)
            CHECK (! (alts[i].root == alts[k].root && alts[i].intervals == alts[k].intervals));
    }
    // smooth voicing: C -> F keeps the notes close (F/C = C F A, inversion 2)
    auto v = chordstrip::fromChords ({ chord (0, { 0, 4, 7 }), chord (5, { 0, 4, 7 }) }, 0, false, 4 * ppq, 4, true);
    CHECK (v.blocks[0].inversion == 0);
    CHECK ((chordstrip::mainKeys (v.blocks[1], 4) == std::vector<int> { 60, 65, 69 }));
    // the block at a tick
    CHECK (chordstrip::blockAt (v, 0) == 0 && chordstrip::blockAt (v, 4 * ppq) == 1 && chordstrip::blockAt (v, 8 * ppq) == -1);
}

TEST (prchord190kd_key_guess_regenerate_and_rework_change)
{
    using namespace lynx;
    using namespace prchord190kd;
    // FL's key guess on open (REA §3.9): the FL session's line A3 C4 E4 A4 B4 D5 E5 G#4 A4 — A Ionian and A Aeolian tie
    // at 8 in-scale notes and root score 3; FL's order keeps the Major: "A Major (Ionian)", as FL 2026 showed
    std::vector<Note> line;
    std::int64_t t = 0;
    for (const auto key : { 57, 60, 64, 69, 71, 74, 76, 68, 69 })
        line.push_back (note (key, t, ppq)), t += ppq / 2;
    const auto key = chordstrip::guessKey (line);
    CHECK (key.has_value() && key->tonic == 9 && ! key->minor);
    CHECK (! chordstrip::guessKey ({ note (60, 0, ppq), note (64, 0, ppq) }).has_value()); // fewer than 3 notes
    // C D E F G B: C major (root score)
    CHECK (chordstrip::guessKey ({ note (60, 0, 1), note (62, 0, 1), note (64, 0, 1), note (65, 0, 1), note (67, 0, 1), note (71, 0, 1) })->tonic == 0);

    // FL's single-chord Regenerate never gives the same chord back
    auto s = strip ({ chord (0, { 0, 4, 7 }), chord (7, { 0, 4, 7 }), chord (9, { 0, 3, 7 }), chord (5, { 0, 4, 7 }) });
    for (std::uint32_t seed = 1; seed < 60; ++seed)
    {
        const auto c = chordstrip::regenerateChord (s, 2, 0.3f, seed, false, true);
        CHECK (c.root != 9);
    }
    // Rework always changes something (FL's ensure_change), even with one unlocked chord
    for (std::uint32_t seed = 1; seed < 30; ++seed)
    {
        auto r = s;
        r.blocks[0].locked = r.blocks[1].locked = r.blocks[3].locked = true;
        chordstrip::GenerateOptions o;
        o.seed = seed;
        chordstrip::generate (r, {}, o);
        CHECK (! (r.blocks[2].chord == s.blocks[2].chord));
    }
    // the notes found guide Rework (auto-accompany): an A minor triad held under bar 1 makes Am (vi) by far the likeliest
    std::vector<Note> am { note (57, 0, 4 * ppq), note (60, 0, 4 * ppq), note (64, 0, 4 * ppq) };
    int hits = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed)
    {
        auto r = s;
        chordstrip::GenerateOptions o;
        o.seed = seed;
        chordstrip::generate (r, am, o);
        hits += r.blocks[0].chord.root == 9 ? 1 : 0;
    }
    CHECK (hits >= 30);
}

TEST (prchord190kd_analyze_sparse_block_prefers_common_chords)
{
    using namespace lynx;
    using namespace prchord190kd;
    // two notes under a bar (G#4 for half a beat, A4 for a beat): FL's per-block law would name G#m7b5 by a hair; LYNX's
    // stand-in for FL's style vocabulary (the type priority) keeps the common chord, A with its major seventh
    const auto bar = 4 * ppq;
    const auto r = chordstrip::analysis::analyse ({ note (68, 0, ppq / 2), note (69, 0, ppq) }, 0, bar, chordstrip::analysis::sequence());
    CHECK (r.type == 0 && r.root == 9);
}
