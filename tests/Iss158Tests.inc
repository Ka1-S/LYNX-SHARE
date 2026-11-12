// Issue #158 (round 190eh): overlapping pattern clips. FL 2026, 25 command-line renders (/mnt/e/lynx-work/iss158/,
// HANDOFF.md): a pattern plays from one Playlist clip at a time — a later clip of the SAME pattern (any track; the same
// start: the one later in the list) takes over at its start, the earlier clip starts no more notes from there (also after
// the later clip has ended), the notes it already started play to their ends. Clips of different patterns never cut each
// other; muted clips and clips on muted tracks take nothing over (Song::patternClipTakeover, Scheduler).

#include "core/ExportOptions.h"

namespace iss158
{
constexpr std::int64_t bar = 4 * lynx::ppq;

// Patterns as in the FL renders: P1 = channel 0 key 60 held 8 bars + channel 1 key 72 a short note on every beat; P2 = the
// same channels with keys 67 / 79; P3 = channels 2 / 3, keys 64 / 76; P4 = channel 1 key 72 on every beat + channel 2 key
// 76 on every beat of bars 4-8 only.
inline lynx::Song song (bool takeover = true)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.channels.resize (4);
    s.patterns.resize (4);
    s.playlistTracks.resize (4);
    s.patternClipTakeover = takeover;
    const auto make = [] (Pattern& p, int heldChannel, int heldKey, int pulseChannel, int pulseKey) {
        Note h;
        h.start = 0;
        h.length = 8 * bar;
        h.channel = heldChannel;
        h.key = heldKey;
        p.notes.push_back (h);
        for (int b = 0; b < 32; ++b)
        {
            Note n;
            n.start = b * ppq;
            n.length = ppq / 4;
            n.channel = pulseChannel;
            n.key = pulseKey;
            p.notes.push_back (n);
        }
    };
    make (s.patterns[0], 0, 60, 1, 72);
    make (s.patterns[1], 0, 67, 1, 79);
    make (s.patterns[2], 2, 64, 3, 76);
    for (int b = 0; b < 32; ++b)
    {
        Note n;
        n.start = b * ppq;
        n.length = ppq / 4;
        n.channel = 1;
        n.key = 72;
        s.patterns[3].notes.push_back (n);
        if (b >= 16)
        {
            n.channel = 2;
            n.key = 76;
            s.patterns[3].notes.push_back (n);
        }
    }
    for (auto& p : s.patterns)
        std::stable_sort (p.notes.begin(), p.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return s;
}

inline lynx::Clip clip (int pattern, std::int64_t startBar, std::int64_t bars, int track = 0, std::int64_t offset = 0)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = pattern;
    c.start = startBar * bar;
    c.length = bars * bar;
    c.track = track;
    c.offset = offset;
    return c;
}

struct Played
{
    lynx::NoteEvent::Type type;
    int channel, key;
    std::int64_t tick;
};

// The whole song played once in 512-sample blocks at 48 kHz; each event at its tick. With or without the index.
inline std::vector<Played> play (const lynx::Song& s, bool withIndex = true)
{
    using namespace lynx;
    const auto tps = ticksPerSample (s.bpm, 48000.0);
    const auto index = makeScheduleIndex (s);
    std::vector<Played> out;
    const auto end = (double) (s.songLength() + bar);
    for (double t = 0.0; t < end; t += 512 * tps)
    {
        EventBuffer ev;
        scheduleSong (s, t, 512, tps, ev, t == 0.0, withIndex ? &index : nullptr, false);
        for (const auto& e : ev)
            out.push_back ({ e.type, e.channel, e.key, (std::int64_t) std::llround (t + e.sampleOffset * tps) });
    }
    return out;
}

inline int count (const std::vector<Played>& p, lynx::NoteEvent::Type type, int key, std::int64_t from = 0,
                  std::int64_t to = std::numeric_limits<std::int64_t>::max())
{
    return (int) std::count_if (p.begin(), p.end(), [&] (const Played& e) { return e.type == type && e.key == key && e.tick >= from && e.tick < to; });
}
inline int ons (const std::vector<Played>& p, int key, std::int64_t from = 0, std::int64_t to = std::numeric_limits<std::int64_t>::max())
{
    return count (p, lynx::NoteEvent::Type::on, key, from, to);
}
inline std::vector<std::int64_t> offsOf (const std::vector<Played>& p, int key)
{
    std::vector<std::int64_t> t;
    for (const auto& e : p)
        if (e.type == lynx::NoteEvent::Type::off && e.key == key)
            t.push_back (e.tick);
    std::sort (t.begin(), t.end());
    return t;
}
} // namespace iss158

TEST (iss158_a_later_clip_of_the_same_pattern_takes_over_on_its_track)
{
    // FL e_same_pat_same_track: P1 0-4 bars and P1 2-6 bars on one track. The beat notes play once per beat (-14.2 dB, not
    // +6 dB), from bar 2 the later clip's; the earlier clip's held note goes on to its own end (bar 4) beside the later one's
    using namespace iss158;
    auto s = song();
    s.playlist = { clip (0, 0, 4), clip (0, 2, 4) };
    const auto p = play (s);
    CHECK (ons (p, 72) == 8 + 16);                    // the earlier clip's beats 0-7, the later clip's 16
    CHECK (ons (p, 72, 2 * bar, 4 * bar) == 8);       // one note per beat where both clips lie
    CHECK (ons (p, 60) == 2);
    CHECK ((offsOf (p, 60) == std::vector<std::int64_t> { 4 * bar, 6 * bar }));
    // LYNX before (and projects saved before, the option off): both clips play every note
    auto legacy = song (false);
    legacy.playlist = s.playlist;
    CHECK (ons (play (legacy), 72) == 32);
}

TEST (iss158_the_earlier_clip_does_not_come_back_after_the_later_one_ends)
{
    // FL h_inside_same_pat / m (other track) / v (offset): P1 0-8 bars with P1 2-4 bars inside it — after bar 4 nothing of
    // P1 starts any more (silence where the outer clip still lies); the outer held note ends at its own end (bar 8)
    using namespace iss158;
    for (const auto track : { 0, 1 })
        for (const auto offset : { (std::int64_t) 0, bar })
        {
            auto s = song();
            s.playlist = { clip (0, 0, 8), clip (0, 2, 2, track, offset) };
            const auto p = play (s);
            CHECK (ons (p, 72, 0, 2 * bar) == 8);
            CHECK (ons (p, 72, 2 * bar, 4 * bar) == 8);
            CHECK (ons (p, 72, 4 * bar) == 0);
            CHECK (ons (p, 60) == (offset == 0 ? 2 : 1)); // (the inner clip with an offset starts after the held note)
            CHECK (offsOf (p, 60).back() == 8 * bar);
        }
}

TEST (iss158_different_patterns_never_cut_each_other)
{
    // FL a / b / c / d / g / i / j / k / l: overlapping clips of different patterns (same or other channels, same or other
    // track, inside, the same start, either list order) all play every note
    using namespace iss158;
    const std::vector<std::vector<lynx::Clip>> cases {
        { clip (0, 0, 4), clip (1, 2, 4) },    { clip (0, 0, 4), clip (1, 2, 4, 1) }, { clip (0, 0, 4), clip (2, 2, 4) },
        { clip (0, 0, 8), clip (1, 2, 2) },    { clip (1, 2, 4), clip (0, 0, 4) },    { clip (0, 0, 4), clip (1, 0, 4) },
        { clip (0, 0, 4, 1), clip (1, 2, 4) }, { clip (0, 0, 8), clip (2, 2, 2) },
    };
    for (const auto& clips : cases)
    {
        auto s = song();
        s.playlist = clips;
        auto legacy = song (false);
        legacy.playlist = clips;
        const auto p = play (s);
        const auto q = play (legacy);
        for (const auto key : { 60, 67, 64, 72, 79, 76 })
        {
            CHECK (ons (p, key) == ons (q, key));
            CHECK (offsOf (p, key) == offsOf (q, key));
        }
        CHECK (ons (p, 72) == (int) (clips[clips[0].pattern == 0 ? 0 : 1].length / lynx::ppq));
    }
}

TEST (iss158_the_same_start_goes_to_the_clip_later_in_the_list)
{
    // FL n1-n6: two clips of P1 at the same start (4 and 8 bars, same or other tracks): only the one later in the item list
    // plays (n1 / n3 / n6: the 8-bar one, beats to 16 s; n2 / n4 / n5: the 4-bar one, all silent from 8 s), one held note
    using namespace iss158;
    for (const auto tracks : { std::pair { 0, 0 }, std::pair { 0, 1 }, std::pair { 1, 0 } })
    {
        auto s = song();
        s.playlist = { clip (0, 0, 4, tracks.first), clip (0, 0, 8, tracks.second) };
        auto p = play (s);
        CHECK (ons (p, 72) == 32);
        CHECK (ons (p, 60) == 1);
        CHECK ((offsOf (p, 60) == std::vector<std::int64_t> { 8 * bar }));
        std::swap (s.playlist[0], s.playlist[1]);
        p = play (s);
        CHECK (ons (p, 72) == 16);
        CHECK (ons (p, 60) == 1);
        CHECK ((offsOf (p, 60) == std::vector<std::int64_t> { 4 * bar }));
    }
}

TEST (iss158_a_muted_clip_or_track_takes_nothing_over)
{
    // FL q_inner_on_muted_track / x_inner_clip_muted: the outer clip plays all its beats
    using namespace iss158;
    for (const auto viaTrack : { false, true })
    {
        auto s = song();
        s.playlist = { clip (0, 0, 8), clip (0, 2, 2, 1) };
        if (viaTrack)
            s.playlistTracks[1].muted = true;
        else
            s.playlist[1].muted = true;
        const auto p = play (s);
        CHECK (ons (p, 72) == 32);
        CHECK (ons (p, 60) == 1);
        // unmuted again: the inner clip takes over (the index keeps the chain, the mute is read live)
        const auto index = lynx::makeScheduleIndex (s);
        s.playlistTracks[1].muted = false;
        s.playlist[1].muted = false;
        CHECK (lynx::patternClipStopsAt (s, 0, &index) == 2 * bar);
        CHECK (lynx::patternClipStopsAt (s, 0) == 2 * bar);
    }
}

TEST (iss158_takeover_is_per_pattern_and_the_boundary_is_inclusive)
{
    using namespace iss158;
    // FL u_per_channel: P4's channel-2 notes (bars 4-8 of the pattern) never play: the outer clip stopped at bar 2, the
    // inner clip (pattern bars 0-2) has none
    {
        auto s = song();
        s.playlist = { clip (3, 0, 8), clip (3, 2, 2) };
        const auto p = play (s);
        CHECK (ons (p, 76) == 0);
        CHECK (ons (p, 72) == 16);
    }
    // FL y_boundary: the inner clip starts at bar 2 half a beat into the pattern (its first note half a beat later); the
    // outer clip's note exactly at bar 2 does not play
    {
        auto s = song();
        s.playlist = { clip (0, 0, 8), clip (0, 2, 2, 0, lynx::ppq / 2) };
        const auto p = play (s);
        CHECK (ons (p, 72, 2 * bar, 2 * bar + 1) == 0);
        CHECK (ons (p, 72, 2 * bar - lynx::ppq, 2 * bar) == 1);
        CHECK (ons (p, 72, 2 * bar + lynx::ppq / 2, 2 * bar + lynx::ppq / 2 + 1) == 1);
    }
    // FL w_earlier_ends_first: P1 0-3 bars, P1 2-6 bars: the earlier held note ends at bar 3, the later one at bar 6
    {
        auto s = song();
        s.playlist = { clip (0, 0, 3), clip (0, 2, 4) };
        const auto p = play (s);
        CHECK ((offsOf (p, 60) == std::vector<std::int64_t> { 3 * bar, 6 * bar }));
        CHECK (ons (p, 72) == 8 + 16);
    }
}

TEST (iss158_the_index_and_the_scan_agree)
{
    using namespace iss158;
    auto s = song();
    s.playlist = { clip (0, 0, 8), clip (1, 1, 4, 1), clip (0, 2, 2, 2), clip (0, 2, 4, 1), clip (0, 6, 2), clip (2, 0, 8) };
    const auto index = lynx::makeScheduleIndex (s);
    for (std::size_t i = 0; i < s.playlist.size(); ++i)
        CHECK (lynx::patternClipStopsAt (s, i, &index) == lynx::patternClipStopsAt (s, i));
    CHECK (lynx::patternClipStopsAt (s, 0, &index) == 2 * iss158::bar);
    CHECK (lynx::patternClipStopsAt (s, 2, &index) == 2 * iss158::bar); // (the same start: the clip later in the list)
    CHECK (lynx::patternClipStopsAt (s, 3, &index) == 6 * iss158::bar);
    CHECK (lynx::patternClipStopsAt (s, 4, &index) == std::numeric_limits<std::int64_t>::max());
    CHECK (lynx::patternClipStopsAt (s, 1, &index) == std::numeric_limits<std::int64_t>::max());
    const auto a = play (s, true), b = play (s, false);
    CHECK (a.size() == b.size());
    for (const auto key : { 60, 67, 64, 72, 79, 76 })
        CHECK (ons (a, key) == ons (b, key) && offsOf (a, key) == offsOf (b, key));
    s.patternClipTakeover = false;
    CHECK (lynx::patternClipStopsAt (s, 0, &index) == std::numeric_limits<std::int64_t>::max());
}

TEST (iss158_midi_export_writes_what_plays)
{
    using namespace iss158;
    auto s = song();
    s.playlist = { clip (0, 0, 8), clip (0, 2, 2) };
    const auto notes = lynx::exportopts::midiExportNotes (s);
    const auto beats = std::count_if (notes.begin(), notes.end(), [] (const lynx::Note& n) { return n.key == 72; });
    CHECK (beats == 16);
    s.patternClipTakeover = false;
    const auto all = lynx::exportopts::midiExportNotes (s);
    CHECK (std::count_if (all.begin(), all.end(), [] (const lynx::Note& n) { return n.key == 72; }) == 32 + 8);
}

TEST (iss158_flp_import_turns_it_on_and_keeps_the_file_order)
{
    // an .flp plays as in FL: the option is on, and two clips of a pattern at the same start keep the file's order (the
    // later one wins)
    using namespace lynx;
    flp::Project p;
    p.tempo = 120.0;
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::sampler;
    c.name = "Tone";
    c.samplePath = "C:/t.wav";
    p.channels.push_back (c);
    flp::Pattern pat;
    pat.index = 1;
    for (int k = 0; k < 16; ++k)
    {
        flp::Note n;
        n.position = 96 * k;
        n.length = 24;
        n.key = 72;
        n.velocity = 100;
        pat.notes.push_back (n);
    }
    p.patterns.push_back (pat);
    flp::Arrangement a;
    for (const auto bars : { 4u, 2u, 1u })
    {
        flp::PlaylistItem item;
        item.position = 0;
        item.length = 384 * bars;
        item.index = 1;
        item.track = (int) bars;
        a.items.push_back (item);
    }
    p.arrangements.push_back (a);
    FlpImportOptions fo;
    fo.fileExists = [] (const std::string&) { return true; };
    const auto s = importFlp (flp::parseProject (flp::writeProject (p)), fo);
    CHECK (s.patternClipTakeover);
    CHECK (s.playlist.size() == 3);
    if (s.playlist.size() == 3)
    {
        CHECK (s.playlist[0].length > s.playlist[1].length && s.playlist[1].length > s.playlist[2].length);
        CHECK (patternClipStopsAt (s, 0) == 0 && patternClipStopsAt (s, 1) == 0);
        CHECK (patternClipStopsAt (s, 2) == std::numeric_limits<std::int64_t>::max());
    }
}
