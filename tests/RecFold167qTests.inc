// Round 167q (0.1.23, the user's decision): with "Stretched pattern clips loop their pattern (LYNX)" on, a take played
// inside a pattern clip longer than its pattern folds into the pattern (its place modulo the pattern's length, from the
// clip's offset): it plays in every repeat and the pattern keeps its length. Off: FL's rule, the pattern grows
// (Rec168kTests.inc, rec168k::FlClipRule).

namespace recfold167q
{
struct LoopFlag
{
    explicit LoopFlag (bool on) : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (on); }
    ~LoopFlag() { lynx::setStretchedClipsLoop (was); }
    bool was;
};

constexpr std::int64_t beat = lynx::ppq, bar = 4 * lynx::ppq;

// Song mode, a 1-bar pattern (a kick on beat 1) in a clip of `length` from song tick 0 with `offset`
inline lynx::Song loopSong (std::int64_t length, std::int64_t offset = 0)
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes.clear();
    lynx::Note kick;
    kick.length = lynx::ticksPerStep;
    kick.key = 36;
    s.patterns[0].notes.push_back (kick);
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.length = length;
    c.offset = offset;
    s.playlist = { c };
    return s;
}

inline std::vector<std::pair<std::int64_t, int>> startsAndKeys (const lynx::Pattern& p)
{
    std::vector<std::pair<std::int64_t, int>> out;
    for (const auto& n : p.notes)
        out.emplace_back (n.start, n.key);
    std::sort (out.begin(), out.end());
    return out;
}

inline int onsOfKey (const lynx::Song& s, int key, std::int64_t ticks)
{
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    const auto index = lynx::makeScheduleIndex (s);
    int ons = 0;
    for (double t = 0.0; t < (double) ticks; t += 512 * tps)
    {
        lynx::EventBuffer ev;
        lynx::scheduleSong (s, t, 512, tps, ev, t == 0.0, &index, false);
        for (const auto& e : ev)
            ons += e.type == lynx::NoteEvent::Type::on && e.key == key;
    }
    return ons;
}
} // namespace recfold167q

// The 168k case (FL's [EXEC]: notes at 2.1, 3.3 and 4.2 in a 1-bar pattern's clip stretched to bars 1-5), option on:
// they fold into bar 1 and play in all four repeats; the pattern stays 1 bar, the clip 4 bars.
TEST (recfold167q_take_in_a_repeat_folds_into_the_pattern)
{
    using namespace recfold167q;
    const LoopFlag loop (true);
    auto s = loopSong (4 * bar);
    rec168k::Take take (s);
    take.note ((double) bar, beat / 2, 62);
    take.note ((double) (2 * bar + 2 * beat), beat / 2, 64);
    take.note ((double) (3 * bar + beat), beat / 2, 65);
    take.stop ((double) (3 * bar + 3 * beat));
    const auto& p = s.patterns[0];
    CHECK ((startsAndKeys (p) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { 0, 62 }, { beat, 65 }, { 2 * beat, 64 } }));
    CHECK (p.effectiveLength (s.beatsPerBar) == bar);
    CHECK (s.playlist.size() == 1 && s.playlist[0].length == 4 * bar && s.playlist[0].offset == 0);
    for (const auto key : { 62, 64, 65 })
        CHECK (onsOfKey (s, key, 4 * bar) == 4); // in every repeat

    // the same take with the option off: FL's rule, the notes stay where they were played and the pattern grows
    const LoopFlag fl (false);
    auto f = loopSong (4 * bar);
    rec168k::Take flTake (f);
    flTake.note ((double) bar, beat / 2, 62);
    flTake.note ((double) (2 * bar + 2 * beat), beat / 2, 64);
    flTake.note ((double) (3 * bar + beat), beat / 2, 65);
    flTake.stop ((double) (3 * bar + 3 * beat));
    CHECK ((startsAndKeys (f.patterns[0]) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { bar, 62 }, { 2 * bar + 2 * beat, 64 }, { 3 * bar + beat, 65 } }));
    CHECK (f.patterns[0].effectiveLength (f.beatsPerBar) == 4 * bar);
    CHECK (onsOfKey (f, 62, 4 * bar) == 1);
}

// A clip that starts 1 beat into its pattern (offset), 8 beats long: the place is relative to the clip's offset.
TEST (recfold167q_offset_clip_folds_from_its_offset)
{
    using namespace recfold167q;
    const LoopFlag loop (true);
    auto s = loopSong (8 * beat, beat);
    rec168k::Take take (s);
    take.note ((double) beat, beat / 4, 60);     // pattern beat 2, first pass: as played
    take.note ((double) (5 * beat), beat / 4, 62); // pattern beat 6 = beat 2 of the second pass
    take.note ((double) (3 * beat), beat / 4, 64); // pattern beat 4 = beat 0 of the second pass
    take.stop ((double) (7 * beat));
    CHECK ((startsAndKeys (s.patterns[0]) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { 0, 64 }, { 2 * beat, 60 }, { 2 * beat, 62 } }));
    CHECK (s.patterns[0].effectiveLength (s.beatsPerBar) == bar);
    CHECK (s.playlist.size() == 1 && s.playlist[0].offset == beat && s.playlist[0].length == 8 * beat);
    // an explicit 2-bar pattern in an 8-bar clip: a take in bar 6 folds into bar 2, the set length stays
    auto t = loopSong (8 * bar);
    t.patterns[0].lengthTicks = 2 * bar;
    rec168k::Take t2 (t);
    t2.note ((double) (5 * bar + beat), beat / 2, 67);
    t2.stop ((double) (6 * bar));
    CHECK ((startsAndKeys (t.patterns[0]) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { bar + beat, 67 } }));
    CHECK (t.patterns[0].lengthTicks == 2 * bar);
    CHECK (onsOfKey (t, 67, 8 * bar) == 4);
}

// A note held across the pattern's end is cut there (the pattern keeps its length); a note quantized onto the end goes
// to the start (the next repeat's first beat). Pattern-mode takes and takes outside any clip are not touched.
TEST (recfold167q_notes_crossing_the_loop_end)
{
    using namespace recfold167q;
    const LoopFlag loop (true);
    {
        auto s = loopSong (4 * bar);
        rec168k::Take take (s);
        take.note ((double) (bar + 3 * beat + beat / 2), beat, 70); // pattern 3.5 beats, held 1 beat
        take.stop ((double) (3 * bar));
        const auto& p = s.patterns[0];
        CHECK (p.effectiveLength (s.beatsPerBar) == bar);
        const auto it = std::find_if (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.key == 70; });
        CHECK (it != p.notes.end() && it->start == 3 * beat + beat / 2 && it->length == beat / 2);
    }
    {
        auto s = loopSong (4 * bar);
        rec168k::Take take (s);
        take.q.grid = beat;
        take.note ((double) (2 * bar + 4 * beat - 10), beat / 4, 71); // just before bar 4: quantized onto the pattern's end
        take.stop ((double) (3 * bar + beat));
        const auto& p = s.patterns[0];
        CHECK (p.effectiveLength (s.beatsPerBar) == bar);
        const auto it = std::find_if (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.key == 71; });
        CHECK (it != p.notes.end() && it->start == 0 && it->length >= 1 && it->start + it->length <= bar);
    }
    {
        // a clip within its pattern (no loop): a note held past the clip keeps its length, as before
        auto s = loopSong (bar);
        rec168k::Take take (s);
        take.note ((double) (2 * beat), beat, 72);
        take.stop ((double) (3 * beat + beat / 2));
        const auto& p = s.patterns[0];
        const auto it = std::find_if (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.key == 72; });
        CHECK (it != p.notes.end() && it->start == 2 * beat && it->length == beat);
    }
}
