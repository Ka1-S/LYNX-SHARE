// Round 190gp (A5): the scheduler visits only the Playlist clips a block can reach (ScheduleIndex's buckets by time).
// Random songs (pattern and audio clips, long clips over many bars, muted clips and tracks, overlapping clips of one
// pattern, clips before 0), every block of a song played twice through its loop, with resumes and chases: the same
// events, in the same order, as the walk over every clip (the same index without its buckets).

namespace schedindex190gp
{
struct Rng
{
    std::uint32_t s = 1;
    std::uint32_t next() noexcept { return s = s * 1664525u + 1013904223u; }
    int below (int n) noexcept { return (int) ((next() >> 8) % (std::uint32_t) n); }
    bool chance (int percent) noexcept { return below (100) < percent; }
};

inline lynx::Song randomSong (Rng& g)
{
    using namespace lynx;
    Song song;
    song.bpm = 90.0 + g.below (80);
    const auto channels = 1 + g.below (6);
    for (int c = 0; c < channels; ++c)
    {
        ChannelDef ch;
        ch.instrument = c % 3 == 2 ? InstrumentKind::audioClip : InstrumentKind::synth;
        ch.samplePath = "/x.wav";
        song.channels.push_back (ch);
    }
    const auto patterns = 1 + g.below (5);
    for (int p = 0; p < patterns; ++p)
    {
        auto& pat = song.patterns.emplace_back();
        const auto notes = g.below (40);
        for (int k = 0; k < notes; ++k)
        {
            Note n;
            n.channel = g.below (channels);
            n.start = (std::int64_t) g.below (16) * ppq / 4 + (g.chance (20) ? g.below (50) : 0);
            n.length = 1 + g.below (4 * ppq);
            n.key = 40 + g.below (40);
            n.velocity = 0.8f;
            pat.notes.push_back (n);
        }
        std::sort (pat.notes.begin(), pat.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    }
    const auto clips = g.below (120);
    for (int k = 0; k < clips; ++k)
    {
        Clip c;
        const auto audio = g.chance (30);
        c.kind = audio ? ClipKind::audio : ClipKind::pattern;
        c.pattern = g.below (patterns);
        c.channel = g.below (channels);
        c.track = g.below (8);
        c.start = (std::int64_t) g.below (64) * ppq + (g.chance (10) ? -g.below (2 * ppq) : 0);
        c.length = (std::int64_t) (g.chance (15) ? 1 + g.below (40 * 4 * ppq) : 1 + g.below (8 * ppq));
        c.offset = g.chance (20) ? g.below (2 * ppq) : 0;
        c.muted = g.chance (8);
        song.playlist.push_back (c);
    }
    song.patternClipTakeover = g.chance (50);
    song.mode = PlayMode::song;
    return song;
}

inline bool sameEvents (const lynx::EventBuffer& a, const lynx::EventBuffer& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.sampleOffset != y.sampleOffset || x.type != y.type || x.channel != y.channel || x.key != y.key || x.velocity != y.velocity
            || x.startSeconds != y.startSeconds || x.clipElapsed != y.clipElapsed || x.clipRemaining != y.clipRemaining
            || x.lengthTicks != y.lengthTicks || x.slideSamples != y.slideSamples || x.pan != y.pan || x.cents != y.cents)
            return false;
    }
    return true;
}
} // namespace schedindex190gp

TEST (s190gp_schedule_buckets_same_events)
{
    using namespace lynx;
    int blocks = 0, withEvents = 0, mismatches = 0;
    for (std::uint32_t seed = 1; seed <= 120; ++seed)
    {
        schedindex190gp::Rng g { seed * 40503u };
        auto song = schedindex190gp::randomSong (g);
        if (g.chance (20))
            song.playlistTracks.resize (8), song.playlistTracks[(std::size_t) g.below (8)].muted = true;
        const auto index = makeScheduleIndex (song);
        auto walk = index;
        walk.bucketTicks = 0; // (every clip, as before)
        CHECK (song.playlist.empty() || index.bucketTicks > 0);
        const auto tps = ticksPerSample (song.bpm, 44100.0);
        const auto length = (double) std::max<std::int64_t> (1, index.songLength);
        auto events = std::make_unique<EventBuffer>(), expected = std::make_unique<EventBuffer>();
        double tick = 0.0;
        while (tick < 2.0 * length && blocks < 4000000)
        {
            const int sizes[] = { 64, 256, 512, 1024, 4096 };
            const auto n = sizes[g.below (5)];
            const auto resume = g.chance (3), chase = g.chance (3), wrap = ! g.chance (5);
            events->clear();
            expected->clear();
            scheduleSong (song, tick, n, tps, *events, resume, &index, wrap, chase);
            scheduleSong (song, tick, n, tps, *expected, resume, &walk, wrap, chase);
            if (! schedindex190gp::sameEvents (*events, *expected))
                ++mismatches;
            withEvents += expected->size() > 0 ? 1 : 0;
            ++blocks;
            tick += n * tps * (g.chance (2) ? 37.0 : 1.0); // (now and then a jump)
        }
    }
    std::printf ("  %d blocks (%d with events), %d differ\n", blocks, withEvents, mismatches);
    CHECK (withEvents > 1000);
    CHECK (mismatches == 0);
}
