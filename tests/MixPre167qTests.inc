// Round 167q: issue #25 (a pattern clip stretched past its pattern repeats the pattern, as in FL) and FL mixer presets.
#include "core/ExportOptions.h"
#include "core/TransportEdit.h"

namespace mixpre167q
{
// The "on" events of the song in [0, ticks), scheduled in blocks of `block` samples (with or without the index):
// (song tick, key), sorted.
inline std::vector<std::pair<long, int>> songOns (const lynx::Song& s, std::int64_t ticks, int block, bool withIndex)
{
    const auto tps = lynx::ticksPerSample (120.0, 48000.0); // 1 beat = 24000 samples, 250 samples per tick
    const auto index = lynx::makeScheduleIndex (s);
    std::vector<std::pair<long, int>> out;
    const auto total = (long) std::llround ((double) ticks / tps);
    for (long at = 0; at < total; at += block)
    {
        lynx::EventBuffer ev;
        const auto n = (int) std::min<long> (block, total - at);
        lynx::scheduleSong (s, (double) at * tps, n, tps, ev, false, withIndex ? &index : nullptr, false);
        CHECK (ev.droppedCount() == 0);
        for (const auto& e : ev)
            if (e.type == lynx::NoteEvent::Type::on)
                out.emplace_back (std::lround ((double) (at + e.sampleOffset) * tps), e.key);
    }
    std::sort (out.begin(), out.end());
    return out;
}

inline lynx::Song oneBarSong()
{
    lynx::Song s;
    s.channels.resize (1);
    auto& p = s.patterns.emplace_back(); // 1 bar: C5 on beat 0, C6 on beat 2
    for (auto [beat, key] : { std::pair { 0, 60 }, std::pair { 2, 72 } })
    {
        lynx::Note n;
        n.start = beat * lynx::ppq;
        n.length = lynx::ppq / 4;
        n.key = key;
        p.notes.push_back (n);
    }
    return s;
}
} // namespace mixpre167q

// Issue #25 (Disar): "create a clip, extend it beyond its length to repeat, play" — the Playlist drew the repeats but
// the song played the pattern once. FL repeats it: its own demo projects place 4- and 8-bar clips of a 1-bar pattern
// back to back (Exoplanet.flp, pattern 14), and clips split from such a clip keep offsets past the pattern (LIFE.flp,
// 8448..9216 of a 384-tick pattern).
TEST (mixpre167q_pattern_clip_repeats_its_pattern)
{
    auto s = mixpre167q::oneBarSong();
    lynx::Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 16 * lynx::ppq; // 4 bars of a 1-bar pattern
    s.playlist.push_back (c);
    lynx::Clip o = c; // a clip that starts 1 beat into the pattern, 8 beats long, at song beat 20
    o.start = 20 * lynx::ppq;
    o.offset = lynx::ppq;
    o.length = 8 * lynx::ppq;
    s.playlist.push_back (o);

    const auto ppq = (long) lynx::ppq;
    const std::vector<std::pair<long, int>> expected {
        { 0, 60 }, { 2 * ppq, 72 }, { 4 * ppq, 60 }, { 6 * ppq, 72 }, { 8 * ppq, 60 }, { 10 * ppq, 72 }, { 12 * ppq, 60 }, { 14 * ppq, 72 },
        // the offset clip: pattern beat 2 at song beat 21, then the pattern again from song beat 23
        { 21 * ppq, 72 }, { 23 * ppq, 60 }, { 25 * ppq, 72 }, { 27 * ppq, 60 },
    };
    for (const auto block : { 64, 480, 512, 4096 })
        for (const auto withIndex : { false, true })
        {
            const auto ons = mixpre167q::songOns (s, 32 * lynx::ppq, block, withIndex);
            CHECK (ons == expected);
        }

    // a clip within its pattern plays it once, as before
    s.playlist.resize (1);
    s.playlist[0].length = 3 * lynx::ppq;
    CHECK ((mixpre167q::songOns (s, 8 * lynx::ppq, 512, true) == std::vector<std::pair<long, int>> { { 0, 60 }, { 2 * ppq, 72 } }));

    // an offset past the pattern's length (a piece split from a long clip): the pass it falls in
    s.playlist[0].offset = 6 * lynx::ppq; // pattern beat 2 of the second pass
    s.playlist[0].length = 4 * lynx::ppq;
    CHECK ((mixpre167q::songOns (s, 4 * lynx::ppq, 512, true) == std::vector<std::pair<long, int>> { { 0, 72 }, { 2 * ppq, 60 } }));

    // MIDI export takes the repeats too
    s.mode = lynx::PlayMode::song;
    s.playlist[0].offset = 0;
    s.playlist[0].length = 8 * lynx::ppq;
    const auto notes = lynx::exportopts::midiExportNotes (s);
    CHECK (notes.size() == 4);
    if (notes.size() == 4)
        CHECK (notes[2].start == 4 * lynx::ppq && notes[2].key == 60 && notes[3].start == 6 * lynx::ppq && notes[3].key == 72);

    // the Piano roll's playhead and recording wrap with the passes
    CHECK (lynx::patternTickInClip (s.playlist[0], 5.0 * lynx::ppq, 4 * lynx::ppq) == 1.0 * lynx::ppq);
    CHECK (lynx::transportedit::patternPlayhead (s, 0, 5.0 * lynx::ppq) == 1.0 * lynx::ppq);
    CHECK (lynx::patternPositionForTick (s, 5.0 * lynx::ppq, 4 * lynx::ppq) == lynx::ppq);
}

// Many clips of many shapes, scheduled block by block: the same events with and without the index, nothing dropped,
// every event inside its block, no crash (issue #25 was reported as a crash).
TEST (mixpre167q_repeats_stress)
{
    std::mt19937 rng (25);
    auto pick = [&rng] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    for (int round = 0; round < 40; ++round)
    {
        lynx::Song s;
        s.channels.resize (3);
        for (int p = 0; p < 3; ++p)
        {
            auto& pat = s.patterns.emplace_back();
            if (p == 2 && round % 3 == 0)
                pat.lengthTicks = lynx::ppq * pick (1, 3); // an explicit length shorter than its notes
            for (int k = pick (0, 12); k > 0; --k)
            {
                lynx::Note n;
                n.start = pick (0, 8 * lynx::ppq);
                n.length = pick (1, 3 * lynx::ppq);
                n.key = pick (36, 84);
                n.channel = pick (0, 2);
                n.slide = pick (0, 9) == 0;
                pat.notes.push_back (n);
            }
            pat.sortNotes();
        }
        s.playTruncatedNotes = round % 2 == 0;
        for (int k = pick (1, 8); k > 0; --k)
        {
            lynx::Clip c;
            c.pattern = pick (0, 2);
            c.track = pick (0, 3);
            c.start = pick (0, 32) * lynx::ppq / 4;
            c.length = pick (1, 64 * lynx::ppq);
            c.offset = pick (0, 3) == 0 ? pick (0, 20 * lynx::ppq) : 0;
            s.playlist.push_back (c);
        }
        const auto length = s.songLength() + 2 * lynx::ppq;
        const auto block = pick (16, 2048);
        const auto a = mixpre167q::songOns (s, length, block, false), b = mixpre167q::songOns (s, length, block, true);
        CHECK (a == b);
        for (const auto& [tick, key] : a)
            CHECK (tick >= 0 && tick <= length && key >= 36 && key <= 84);
    }
}
