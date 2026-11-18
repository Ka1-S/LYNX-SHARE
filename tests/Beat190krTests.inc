// Round 190kr (0.1.30, perf-first: beat FL on CPU while playing Thief): the worker pool's wake limit. A chunk wakes only
// as many sleeping workers as its measured work needs (WorkerPool::Tuning::helpPercent, RackRenderer::processChunk);
// who computes what must change no sample.

namespace beat190kr
{
struct HelpGuard
{
    int help = lynx::WorkerPool::tuning().helpPercent.load();
    ~HelpGuard() { lynx::WorkerPool::tuning().helpPercent = help; }
};
} // namespace beat190kr

TEST (beat190kr_tuning_parses_help)
{
    const beat190kr::HelpGuard guard;
    auto& t = lynx::WorkerPool::tuning();
    t.apply ("help=40");
    CHECK (t.helpPercent.load() == 40);
    t.apply ("spin=60");
    CHECK (t.helpPercent.load() == 40); // (missing key: unchanged)
    t.apply ("help=-5");
    CHECK (t.helpPercent.load() == 0);
    t.apply ("help=99999");
    CHECK (t.helpPercent.load() == 1000);
}

TEST (beat190kr_wake_limit_renders_the_same)
{
    // the renderer's task graph with every wake limit (none, a few workers, one, none at all) and without a pool: bit for bit
    const beat190kr::HelpGuard guard;
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (16);
    for (int i = 5; i < 40; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = i % 3 == 0 ? lynx::InstrumentKind::hat : lynx::InstrumentKind::synth;
        c.mixerTrack = i % 7 == 0 ? 0 : 1 + i % 13;
        c.volume = 0.25f;
        song.channels.push_back (c);
        for (int step = i % 5; step < 16; step += 2)
            song.patterns[0].notes.push_back ({ (std::int64_t) step * lynx::ticksPerStep, 150, i, 40 + i, 0.6f });
    }
    song.patterns[0].sortNotes();
    song.setRoute (3, 7, true, 0.6f);
    song.setRoute (7, 9, true, 0.5f);
    song.setRoute (9, 11, true, 0.5f);
    song.mixer[9].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));
    std::uint64_t sleeps = 0;
    auto render = [&song, &sleeps] (int help, bool pool) {
        lynx::WorkerPool::tuning().helpPercent = help;
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        lynx::WorkerPool workers (4);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        if (pool)
            r.setWorkerPool (&workers);
        std::vector<float> out, l (480), rr (480);
        const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
        for (int block = 0; block < 120; ++block)
        {
            r.process (state.get(), true, block * 480 * tps, 480, l.data(), rr.data());
            out.insert (out.end(), l.begin(), l.end());
            out.insert (out.end(), rr.begin(), rr.end());
        }
        sleeps = workers.stats().sleeps;
        return out;
    };
    const auto alone = render (0, false);
    for (const auto help : { 0, 5, 25, 50, 100, 1000 })
        CHECK (render (help, true) == alone);
    CHECK (*std::max_element (alone.begin(), alone.end()) > 0.02f);
    // (a limit that wakes nobody: the workers that went to sleep stay asleep; the calling thread does the work)
    render (1000, true);
    const auto fewest = sleeps;
    render (0, true);
    CHECK (fewest <= sleeps);
}
