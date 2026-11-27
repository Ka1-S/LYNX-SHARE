// Issue #324 (round 190se, reported on 0.1.31.2): a one-shot sample in the Playlist and a time selection longer than the
// song (bars 1-2), played: the hit came back every song length inside the selection (a 0.22 s kick at 130 BPM four times
// a bar) instead of once, then silence until the selection starts over, as FL 2026 plays it. The transport looped the
// selection right; the scheduler, the automation and the tempo wrapped the position at the song's end
// (ScheduleIndex::songLength), which lies inside the selection.

namespace issue324_190se
{
// FL's default song with a 0.22 s audio clip at `start` (130 BPM: 457 ticks), the time selection [from, to) looping
inline lynx::Song oneShotSong (std::int64_t start, std::int64_t from, std::int64_t to)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 130.0;
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    audio.name = "Kick";
    s.channels.push_back (audio);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = (int) s.channels.size() - 1;
    c.start = start;
    c.length = 457;
    s.playlist.push_back (c);
    s.mode = PlayMode::song;
    s.loopStart = from;
    s.loopEnd = to;
    return s;
}

// note-ons of the audio clip's channel in [from, to), played once block by block (`rate`, `block`)
inline int hits (const lynx::Song& s, double from, double to, double rate, int block)
{
    using namespace lynx;
    const auto tps = ticksPerSample (s.bpm, rate);
    const auto index = makeScheduleIndex (s);
    const auto channel = s.playlist.front().channel;
    int count = 0;
    for (double t = from; t < to; t += block * tps)
    {
        const auto n = (int) std::min<double> (block, std::ceil ((to - t) / tps - 1.0e-6));
        EventBuffer ev;
        scheduleSong (s, t, n, tps, ev, t == from, &index, true);
        for (const auto& e : ev) // (the pass's last block ends up to a sample past `to`: what falls there is the next pass's)
            if (e.type == NoteEvent::Type::on && e.channel == channel && t + e.sampleOffset * tps < to - 0.5 * tps)
                ++count;
    }
    return count;
}

// as the engine's transport plays the loop region: each pass's blocks end where it reaches the region's end (the last
// block rounded up to a whole sample, transportSamplesUntil), then the position goes back to its start; note-ons heard
// in `passes` passes
inline int loopedHits (const lynx::Song& s, int passes, double rate, int block)
{
    using namespace lynx;
    const auto tps = ticksPerSample (s.bpm, rate);
    const auto index = makeScheduleIndex (s);
    const auto channel = s.playlist.front().channel;
    const auto from = (double) s.loopStart, to = (double) s.loopEnd;
    int count = 0;
    auto position = from;
    for (int pass = 0; pass < passes;)
    {
        if (transportReached (position, to))
        {
            position = from;
            ++pass;
            continue;
        }
        const auto n = transportSamplesUntil (position, to, tps, block);
        EventBuffer ev;
        scheduleSong (s, position, n, tps, ev, position == from && pass == 0, &index, true);
        for (const auto& e : ev)
            if (e.type == NoteEvent::Type::on && e.channel == channel)
                ++count;
        position += n * tps;
    }
    return count;
}
} // namespace issue324_190se

TEST (issue324_190se_a_selection_longer_than_the_song_plays_the_one_shot_once)
{
    using namespace issue324_190se;
    const auto bar = (std::int64_t) 4 * lynx::ppq;
    // the report: the clip at the selection's start (and where the drop put it here, half a beat in), bars 1-2, 1-3, 2-4
    for (const auto start : { (std::int64_t) 0, (std::int64_t) 480 })
        for (const auto rate : { 44100.0, 48000.0 })
            for (const auto block : { 256, 512 })
            {
                auto s = oneShotSong (start, 0, bar);
                CHECK (lynx::makeScheduleIndex (s).songLength == bar);
                CHECK (hits (s, 0.0, (double) bar, rate, block) == 1); // 0.1.31.2: 4 (at start 480: every 937 ticks)
                s.loopEnd = 2 * bar;
                CHECK (hits (s, 0.0, (double) (2 * bar), rate, block) == 1); // 0.1.31.2: 8
            }
    // the engine's way: three passes of bars 1-2, the hit once a pass (0.1.31.2: 12)
    for (const auto rate : { 44100.0, 48000.0 })
        CHECK (loopedHits (oneShotSong (480, 0, bar), 3, rate, 256) == 3);
    // a selection that starts after the song's end: silence (FL plays the empty range)
    auto late = oneShotSong (0, bar, 3 * bar);
    CHECK (hits (late, (double) bar, (double) (3 * bar), 48000.0, 256) == 0);
    // a selection inside the song, the loop switched off, no selection: the song's own length, as before
    auto inside = oneShotSong (0, 0, 240);
    inside.playlist.front().length = 2 * bar;
    CHECK (lynx::makeScheduleIndex (inside).songLength == inside.songLength());
    auto off = oneShotSong (0, 0, bar);
    off.loopOff = true;
    CHECK (lynx::makeScheduleIndex (off).songLength == off.songLength());
    auto none = oneShotSong (0, 0, 0);
    CHECK (lynx::makeScheduleIndex (none).songLength == none.songLength());
    // without a selection the song still starts over at its end (FL's song loop, issue #4): two passes, two hits
    CHECK (hits (none, 0.0, (double) (2 * none.songLength()), 48000.0, 256) == 2);
}

// Issue #323 (round 190se): FL 2026's double click on a sample in the Browser previews it and adds nothing; LYNX's new
// channel from it is the LYNX extra "browser.doubleClickAdds", off by default, always on in the Clip-launch layout
// (Live loads a double-clicked sample into the selected track), following the switch in the Simple layout.
#include "core/BrowserDoubleClick.h"

TEST (issue323_190se_double_click_adds_only_with_the_extra_or_in_clip_launch)
{
    using lynx::layoutstyle::Style;
    using lynx::browserclick::addsOnDoubleClick;
    CHECK (! lynx::browserclick::doubleClickAdds()); // off in a fresh profile, as FL
    CHECK (! addsOnDoubleClick (false, Style::pattern));
    CHECK (! addsOnDoubleClick (false, Style::simple));
    CHECK (addsOnDoubleClick (false, Style::clipLaunch));
    CHECK (addsOnDoubleClick (true, Style::pattern) && addsOnDoubleClick (true, Style::simple) && addsOnDoubleClick (true, Style::clipLaunch));
}
