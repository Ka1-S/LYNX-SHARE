// Round 190ra (0.1.31.2 hotfix). sidthenugget (Discord, 11-24): with "Stretched pattern clips loop their pattern" on, a
// clip drawn on an instrument track ("Clips own their notes": a track clip, one bar long) and stretched further kept
// looping its first bar; notes drawn, pasted or dragged past that bar were never heard. A track clip's pattern has a SET
// length (lengthTicks = the drawn bar) and the Piano roll never grew it (the Channel rack's steps did: lengthWithStep).
// Now an edit that takes the notes past a set length grows it to whole bars (growSetLengthToNotes, the Piano roll's
// editNotes and its MIDI import).

namespace hotfix190ra
{
using lynx::ppq;
constexpr std::int64_t bar = 4 * ppq;

struct LoopFlag
{
    explicit LoopFlag (bool on) : was (lynx::stretchedClipsLoop()) { lynx::setStretchedClipsLoop (on); }
    ~LoopFlag() { lynx::setStretchedClipsLoop (was); }
    bool was;
};

inline lynx::Note note (std::int64_t start, std::int64_t length, int key)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.channel = 0;
    return n;
}

// one instrument track with "Clips own their notes", a new empty track clip of one bar (as the pencil draws it) placed
// at the song's start and stretched to `clipBars` bars; the clip's first bar has key 60 on its downbeat
inline lynx::Song trackClipSong (int clipBars, int& pattern)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.channels.resize (1);
    s.channels[0].name = "Keys";
    s.channels[0].instrument = InstrumentKind::synth;
    s.playlist.clear();
    s.patterns.clear();
    s.mode = PlayMode::song;
    s.playlistTracks.resize (std::max<std::size_t> (s.playlistTracks.size(), 2));
    s.playlistTracks[0].mode = PlaylistTrackMode::instrument;
    s.playlistTracks[0].channel = 0;
    s.playlistTracks[0].ownClips = true;
    pattern = trackclips::newClip (s, 0, bar);
    s.patterns[(std::size_t) pattern].notes.push_back (note (0, ppq / 2, 60));
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = pattern;
    c.track = 0;
    c.start = 0;
    c.length = clipBars * bar;
    s.playlist.push_back (c);
    s.currentPattern = pattern;
    return s;
}

// an edit of the pattern's notes the way the Piano roll makes it (PianoRollComponent::editNotes)
template <typename Edit>
void editNotes (lynx::Song& s, int pattern, Edit&& edit)
{
    auto& p = s.patterns[(std::size_t) pattern];
    const auto before = lynx::notesEnd (p);
    edit (p);
    p.sortNotes();
    lynx::growSetLengthToNotes (p, before, s.barTicks());
}

// note-ons of `key` heard in [from, to) when the song plays once (48 kHz, 512-sample blocks)
inline int heard (const lynx::Song& s, int key, std::int64_t from, std::int64_t to)
{
    using namespace lynx;
    const auto tps = ticksPerSample (s.bpm, 48000.0);
    const auto index = makeScheduleIndex (s);
    int count = 0;
    for (double t = 0.0; t < (double) (s.songLength() + bar); t += 512 * tps)
    {
        EventBuffer ev;
        scheduleSong (s, t, 512, tps, ev, t == 0.0, &index, false);
        for (const auto& e : ev)
        {
            const auto at = (std::int64_t) std::llround (t + e.sampleOffset * tps);
            if (e.type == NoteEvent::Type::on && e.key == key && at >= from && at < to)
                ++count;
        }
    }
    return count;
}
} // namespace hotfix190ra

TEST (hotfix190ra_a_note_drawn_past_a_track_clips_bar_is_heard_in_its_stretched_clip)
{
    using namespace hotfix190ra;
    const LoopFlag loops (true);
    int pattern = -1;
    auto s = trackClipSong (4, pattern);
    CHECK (s.patterns[(std::size_t) pattern].trackClip && s.patterns[(std::size_t) pattern].lengthTicks == bar);
    // 0.1.31.1: the clip loops its one bar four times
    CHECK (heard (s, 60, 0, 4 * bar) == 4);
    // a note drawn in the clip's third bar (the Piano roll shows the clip from its start)
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes.push_back (note (2 * bar + ppq, ppq / 2, 67)); });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 3 * bar); // grown to whole bars
    CHECK (heard (s, 67, 2 * bar, 3 * bar) == 1);                   // heard where it was drawn
    CHECK (heard (s, 60, 0, 4 * bar) == 2);                         // the clip now loops three bars: bar 1, then bar 4
    CHECK (heard (s, 60, 3 * bar, 4 * bar) == 1);
    // the same song with the edit but without the growth (what 0.1.31.1 kept): the note was never heard
    auto old = s;
    old.patterns[(std::size_t) pattern].lengthTicks = bar;
    CHECK (heard (old, 67, 0, 4 * bar) == 0);
}

TEST (hotfix190ra_pasted_notes_past_a_set_length_grow_it)
{
    using namespace hotfix190ra;
    const LoopFlag loops (true);
    int pattern = -1;
    auto s = trackClipSong (4, pattern);
    // copy the bar's note and paste it two bars later (the Piano roll's Ctrl+C / Ctrl+V: noteedit::paste)
    const auto clipboard = lynx::noteedit::copy (s.patterns[(std::size_t) pattern], { 0 });
    editNotes (s, pattern, [&clipboard] (lynx::Pattern& p) { lynx::noteedit::paste (p, clipboard, 2 * bar); });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 3 * bar);
    CHECK (heard (s, 60, 2 * bar, 3 * bar) == 1);
    // a paste inside the length leaves it as it is
    editNotes (s, pattern, [&clipboard] (lynx::Pattern& p) { lynx::noteedit::paste (p, clipboard, ppq); });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 3 * bar);
}

TEST (hotfix190ra_a_note_dragged_or_stretched_past_a_set_length_grows_it)
{
    using namespace hotfix190ra;
    const LoopFlag loops (true);
    int pattern = -1;
    auto s = trackClipSong (4, pattern);
    // stretched: the bar's note made a bar and a half long (its end in the second bar)
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes[0].length = bar + bar / 2; });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 2 * bar);
    // dragged: moved to the fourth bar
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes[0].start = 3 * bar; p.notes[0].length = ppq; });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 4 * bar);
    CHECK (heard (s, 60, 3 * bar, 4 * bar) == 1);
    CHECK (heard (s, 60, 0, 3 * bar) == 0);
    // dragged back: the length the user's drag reached stays (only an automatic length follows the notes back)
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes[0].start = 0; });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 4 * bar);
}

TEST (hotfix190ra_the_clip_drag_extended_first_then_a_note_drawn_in_it)
{
    using namespace hotfix190ra;
    const LoopFlag loops (true);
    int pattern = -1;
    auto s = trackClipSong (1, pattern);
    // the clip at its pattern's length follows the pattern (followPatternLengths, AudioEngine::editSong): an edit that
    // grows the pattern grows the clip with it
    {
        auto grown = s;
        const auto before = lynx::patternLengths (grown);
        editNotes (grown, pattern, [] (lynx::Pattern& p) { p.notes.push_back (note (bar + ppq, ppq, 64)); });
        lynx::followPatternLengths (before, grown);
        CHECK (grown.playlist[0].length == 2 * bar);
        CHECK (heard (grown, 64, bar, 2 * bar) == 1);
    }
    // the clip's right edge dragged to four bars: one bar looped four times (the extra, as asked)
    s.playlist[0].length = 4 * bar;
    CHECK (heard (s, 60, 0, 4 * bar) == 4);
    // then a note drawn in its second bar: two bars looped twice, the new note heard in both passes
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes.push_back (note (bar, ppq, 62)); });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 2 * bar);
    CHECK (heard (s, 62, 0, 4 * bar) == 2);
    CHECK (heard (s, 60, 0, 4 * bar) == 2);
}

TEST (hotfix190ra_lengths_the_rule_leaves_alone)
{
    using namespace hotfix190ra;
    using lynx::Pattern;
    // an automatic length (FL's patterns): stays automatic, the notes make it
    {
        Pattern p;
        p.notes.push_back (note (0, ppq, 60));
        const auto before = lynx::notesEnd (p);
        p.notes.push_back (note (5 * bar, ppq, 60));
        CHECK (! lynx::growSetLengthToNotes (p, before, bar));
        CHECK (p.lengthTicks == 0 && p.effectiveLength ({}) == 6 * bar);
    }
    // FL's "Pattern length" marker sets the length: FL keeps it
    {
        Pattern p;
        p.lengthTicks = bar;
        lynx::Marker m;
        m.action = lynx::Marker::patternLength;
        m.tick = bar;
        p.markers.push_back (m);
        const auto before = lynx::notesEnd (p);
        p.notes.push_back (note (2 * bar, ppq, 60));
        CHECK (! lynx::growSetLengthToNotes (p, before, bar));
        CHECK (p.lengthTicks == bar);
    }
    // notes already past the length before an edit that does not take them further (an imported pattern): unchanged
    {
        Pattern p;
        p.lengthTicks = bar;
        p.notes.push_back (note (2 * bar, ppq, 60));
        const auto before = lynx::notesEnd (p);
        p.notes.push_back (note (ppq, ppq, 62));
        CHECK (! lynx::growSetLengthToNotes (p, before, bar));
        CHECK (p.lengthTicks == bar);
    }
    // automation clip notes count (the rack's automation rows, round 190ow), and a 3/4 bar rounds to its own bars
    {
        Pattern p;
        p.lengthTicks = 3 * ppq;
        const auto before = lynx::notesEnd (p);
        p.autoNotes.push_back (note (4 * ppq, ppq, 0));
        CHECK (lynx::growSetLengthToNotes (p, before, 3 * ppq));
        CHECK (p.lengthTicks == 6 * ppq);
    }
    CHECK (lynx::notesEnd (Pattern {}) == 0);
}

TEST (hotfix190ra_a_track_clip_saved_with_notes_past_its_bar_is_mended_by_the_next_edit)
{
    using namespace hotfix190ra;
    const LoopFlag loops (true);
    int pattern = -1;
    auto s = trackClipSong (4, pattern);
    // what 0.1.31.1 left: a note in the third bar of a one-bar track clip, unheard
    s.patterns[(std::size_t) pattern].notes.push_back (note (2 * bar, ppq, 65));
    CHECK (heard (s, 65, 0, 4 * bar) == 0);
    // any later edit in the Piano roll (here: a note's velocity) grows the length to the notes
    editNotes (s, pattern, [] (lynx::Pattern& p) { p.notes[0].velocity = 0.5f; });
    CHECK (s.patterns[(std::size_t) pattern].lengthTicks == 3 * bar);
    CHECK (heard (s, 65, 2 * bar, 3 * bar) == 1);
}
