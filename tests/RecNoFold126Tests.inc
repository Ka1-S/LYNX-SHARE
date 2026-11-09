// Issue #126 (0.1.26): a take played inside a pattern clip longer than its pattern lands where it was played and the
// pattern grows, with "Stretched pattern clips loop their pattern (LYNX)" on as well as off (FL's rule, round 168k
// [EXEC]; Rec168kTests.inc). 0.1.23 (round 167q) folded such a take into the pattern when the option was on, so arming
// and recording over an empty clip only ever filled its first bar.

namespace recnofold126
{
struct LoopFlag
{
    explicit LoopFlag (bool on) : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (on); }
    ~LoopFlag() { lynx::setStretchedClipsLoop (was); }
    bool was;
};

constexpr std::int64_t beat = lynx::ppq, bar = 4 * lynx::ppq;

// Song mode, a pattern in a clip of `length` from song tick 0 with `offset`; `kick` = a kick on beat 1 (else empty)
inline lynx::Song loopSong (std::int64_t length, std::int64_t offset = 0, bool kick = true)
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    s.patterns[0].notes.clear();
    if (kick)
    {
        lynx::Note k;
        k.length = lynx::ticksPerStep;
        k.key = 36;
        s.patterns[0].notes.push_back (k);
    }
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
} // namespace recnofold126

// The reporter's case: arm and record over an empty 4-bar clip, one note per bar. Every bar keeps its note, the pattern
// grows to the 4 bars, the clip stays, and each note plays once (0.1.25: all four folded into bar 1, looping).
TEST (recnofold126_take_over_an_empty_clip_fills_every_bar)
{
    using namespace recnofold126;
    for (const auto on : { true, false })
    {
        const LoopFlag loop (on);
        auto s = loopSong (4 * bar, 0, false);
        rec168k::Take take (s);
        take.note (0.0, beat / 2, 60);
        take.note ((double) (bar + beat), beat / 2, 62);
        take.note ((double) (2 * bar + 2 * beat), beat / 2, 64);
        take.note ((double) (3 * bar + 3 * beat), beat / 2, 65);
        take.stop ((double) (4 * bar - 1));
        CHECK ((startsAndKeys (s.patterns[0])
                == std::vector<std::pair<std::int64_t, int>> { { 0, 60 }, { bar + beat, 62 }, { 2 * bar + 2 * beat, 64 }, { 3 * bar + 3 * beat, 65 } }));
        CHECK (s.patterns[0].effectiveLength (s.beatsPerBar) == 4 * bar);
        CHECK (s.playlist.size() == 1 && s.playlist[0].length == 4 * bar && s.playlist[0].offset == 0);
        for (const auto key : { 60, 62, 64, 65 })
            CHECK (onsOfKey (s, key, 4 * bar) == 1);
    }
}

// The 168k case (FL's [EXEC]: notes at 2.1, 3.3 and 4.2 in a 1-bar pattern's clip stretched to bars 1-5) with the loop
// option on: the notes stay where they were played and the pattern grows, as with the option off.
TEST (recnofold126_take_in_a_repeat_stays_where_it_was_played)
{
    using namespace recnofold126;
    for (const auto on : { true, false })
    {
        const LoopFlag loop (on);
        auto s = loopSong (4 * bar);
        rec168k::Take take (s);
        take.note ((double) bar, beat / 2, 62);
        take.note ((double) (2 * bar + 2 * beat), beat / 2, 64);
        take.note ((double) (3 * bar + beat), beat / 2, 65);
        take.stop ((double) (3 * bar + 3 * beat));
        CHECK ((startsAndKeys (s.patterns[0])
                == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { bar, 62 }, { 2 * bar + 2 * beat, 64 }, { 3 * bar + beat, 65 } }));
        CHECK (s.patterns[0].effectiveLength (s.beatsPerBar) == 4 * bar);
        CHECK (s.playlist.size() == 1 && s.playlist[0].length == 4 * bar && s.playlist[0].offset == 0);
        CHECK (onsOfKey (s, 62, 4 * bar) == 1);
    }
}

// A clip that starts 1 beat into its pattern (offset), 8 beats long: the place is the clip's offset plus the time into
// the clip; an explicit 2-bar pattern in an 8-bar clip grows by whole bars to a take in bar 6.
TEST (recnofold126_offset_clip_and_set_length)
{
    using namespace recnofold126;
    const LoopFlag loop (true);
    auto s = loopSong (8 * beat, beat);
    rec168k::Take take (s);
    take.note ((double) beat, beat / 4, 60);
    take.note ((double) (5 * beat), beat / 4, 62);
    take.stop ((double) (7 * beat));
    CHECK ((startsAndKeys (s.patterns[0]) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { 2 * beat, 60 }, { 6 * beat, 62 } }));
    CHECK (s.playlist.size() == 1 && s.playlist[0].offset == beat && s.playlist[0].length == 8 * beat);

    auto t = loopSong (8 * bar);
    t.patterns[0].lengthTicks = 2 * bar;
    rec168k::Take t2 (t);
    t2.note ((double) (5 * bar + beat), beat / 2, 67);
    t2.stop ((double) (6 * bar));
    CHECK ((startsAndKeys (t.patterns[0]) == std::vector<std::pair<std::int64_t, int>> { { 0, 36 }, { 5 * bar + beat, 67 } }));
    CHECK (t.patterns[0].lengthTicks == 6 * bar);
    CHECK (onsOfKey (t, 67, 8 * bar) == 1);
}

// A note held past the pattern's end keeps its length (nothing is cut at the loop point any more), and a clip within its
// pattern is unchanged.
TEST (recnofold126_held_notes_keep_their_length)
{
    using namespace recnofold126;
    const LoopFlag loop (true);
    {
        auto s = loopSong (4 * bar);
        rec168k::Take take (s);
        take.note ((double) (bar + 3 * beat + beat / 2), beat, 70);
        take.stop ((double) (3 * bar));
        const auto& p = s.patterns[0];
        const auto it = std::find_if (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.key == 70; });
        CHECK (it != p.notes.end() && it->start == bar + 3 * beat + beat / 2 && it->length == beat);
    }
    {
        auto s = loopSong (bar);
        rec168k::Take take (s);
        take.note ((double) (2 * beat), beat, 72);
        take.stop ((double) (3 * beat + beat / 2));
        const auto& p = s.patterns[0];
        const auto it = std::find_if (p.notes.begin(), p.notes.end(), [] (const lynx::Note& n) { return n.key == 72; });
        CHECK (it != p.notes.end() && it->start == 2 * beat && it->length == beat);
    }
}
