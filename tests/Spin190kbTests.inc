// Round spin190kb (0.1.30, perf-first): the worker pool's waiting — its statistics, the run-time tuning, the task
// graph's bounded dependency wait (await / notify: spin, then sleep until the task is pushed) and the adaptive spin.

namespace spin190kb
{
// restores the process-wide tuning after a test
struct TuningGuard
{
    int spin = lynx::WorkerPool::tuning().workerSpinMicros.load(), dry = lynx::WorkerPool::tuning().drySpinMicros.load(),
        wait = lynx::WorkerPool::tuning().waitMode.load(), waitSpin = lynx::WorkerPool::tuning().waitSpinMicros.load();
    bool adaptive = lynx::WorkerPool::tuning().adaptiveSpin.load();
    ~TuningGuard()
    {
        auto& t = lynx::WorkerPool::tuning();
        t.workerSpinMicros = spin;
        t.drySpinMicros = dry;
        t.waitMode = wait;
        t.waitSpinMicros = waitSpin;
        t.adaptiveSpin = adaptive;
    }
};

// a dependency graph run as the renderer's task graph runs it: the positions are taken in order and each waits for
// the task pushed into it; a finished task pushes the successors it completed into the next free position. Five chains
// of six tasks, then a fan-in of four and a last task after them. Returns the positions that ran other than once.
int runGraph (lynx::WorkerPool& pool, int rounds)
{
    constexpr int n = 40;
    std::vector<std::vector<int>> next (n);
    std::vector<int> indegree (n, 0);
    for (int i = 5; i < 35; ++i)
        next[(std::size_t) i - 5].push_back (i), ++indegree[(std::size_t) i];
    for (int i = 30; i < 35; ++i)
        for (int j = 35; j < 39; ++j)
            next[(std::size_t) i].push_back (j), ++indegree[(std::size_t) j];
    for (int j = 35; j < 39; ++j)
        next[(std::size_t) j].push_back (39), ++indegree[39];
    std::vector<std::atomic<int>> slots (n), pending (n), ran (n);
    std::atomic<int> pushed { 0 };
    int bad = 0;
    for (int r = 0; r < rounds; ++r)
    {
        int roots = 0;
        for (int i = 0; i < n; ++i)
        {
            pending[(std::size_t) i] = indegree[(std::size_t) i];
            slots[(std::size_t) i] = 0;
            ran[(std::size_t) i] = 0;
        }
        for (int i = 0; i < n; ++i)
            if (indegree[(std::size_t) i] == 0)
                slots[(std::size_t) roots++] = i + 1;
        pushed = roots;
        auto task = [&] (int position) {
            int id = 0;
            pool.await (position, [&] { return (id = slots[(std::size_t) position].load (std::memory_order_acquire)) != 0; });
            --id;
            volatile double x = 0.0;
            for (int k = 0; k < 100 + (id % 7) * 150; ++k)
                x = x + k * 0.5;
            ran[(std::size_t) id].fetch_add (1);
            for (const auto j : next[(std::size_t) id])
                if (pending[(std::size_t) j].fetch_sub (1) == 1)
                {
                    const auto at = pushed.fetch_add (1);
                    slots[(std::size_t) at].store (j + 1, std::memory_order_release);
                    pool.notify (at);
                }
        };
        pool.parallelFor (n, task);
        pool.park();
        for (int i = 0; i < n; ++i)
            bad += ran[(std::size_t) i].load() != 1;
        if (r % 40 == 0) // (now and then the workers fall asleep between rounds)
            std::this_thread::sleep_for (std::chrono::microseconds (300));
    }
    return bad;
}
} // namespace spin190kb

TEST (spin190kb_tuning_parses_specs_and_keeps_missing_keys)
{
    const spin190kb::TuningGuard guard;
    auto& t = lynx::WorkerPool::tuning();
    t.apply ("spin=20,dry=2,adaptive=0,wait=0,waitspin=5");
    CHECK (t.workerSpinMicros.load() == 20 && t.drySpinMicros.load() == 2 && ! t.adaptiveSpin.load() && t.waitMode.load() == 0 && t.waitSpinMicros.load() == 5);
    t.apply ("wait=1"); // the others stay
    CHECK (t.waitMode.load() == 1 && t.workerSpinMicros.load() == 20 && t.waitSpinMicros.load() == 5);
    t.apply ("wait=7,spin=-3,waitspin=99999"); // clamped
    CHECK (t.waitMode.load() == 2 && t.workerSpinMicros.load() == 0 && t.waitSpinMicros.load() == 10000);
    t.apply ("nonsense,adaptive=1,,");
    CHECK (t.adaptiveSpin.load());
    t.apply (nullptr);
    CHECK (t.adaptiveSpin.load());
}

TEST (spin190kb_task_graph_waits_run_every_position_once_in_every_mode)
{
    // the old spin-and-yield wait, the bounded spin then sleep (also sleeping at once: waitspin=0, the hardest case for
    // the wake-up handshake), with and without the adaptive spin
    const spin190kb::TuningGuard guard;
    lynx::WorkerPool pool (4);
    for (const char* spec : { "wait=0,adaptive=0", "wait=1,waitspin=0,adaptive=1,dry=0", "wait=1,waitspin=30,adaptive=1,dry=4", "wait=1,waitspin=2,adaptive=0" })
    {
        lynx::WorkerPool::tuning().apply (spec);
        const auto before = pool.stats();
        CHECK (spin190kb::runGraph (pool, 400) == 0);
        const auto after = pool.stats();
        CHECK (after.rounds - before.rounds == 400);
        if (lynx::WorkerPool::tuning().waitMode.load() == 0)
            CHECK (after.waitSleeps == before.waitSleeps); // (the old wait never sleeps)
        else
            CHECK (after.waitYields == before.waitYields); // (the new one never yields)
    }
    // waitspin=0: the waits that found no task slept (the statistics see them).
    // Round 190kz (sched): counted, not timed. A wait happens only when a worker takes a position before the thread
    // that will push its task has done so, i.e. only when two threads really run at once. On a loaded machine (load
    // 29-36: other builds on every core) the workers can be woken so late that the caller ran a whole 200-round batch
    // alone, every position already pushed when taken: no wait at all, so nothing could sleep (1 failure in 5 runs).
    // So batches run until the statistics have seen waits that slept (bounded: 40 batches of 100 rounds, about a
    // second even when loaded); every batch must still run every position exactly once.
    lynx::WorkerPool::tuning().apply ("wait=1,waitspin=0");
    const auto before = pool.stats();
    auto wrongRuns = 0;
    for (int batch = 0; batch < 40; ++batch)
    {
        wrongRuns += spin190kb::runGraph (pool, 100);
        const auto now = pool.stats();
        if (now.waits > before.waits && now.waitSleeps > before.waitSleeps)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (2)); // (give the scheduler a chance to run the workers)
    }
    CHECK (wrongRuns == 0);
    CHECK (pool.stats().waits > before.waits);
    CHECK (pool.stats().waitSleeps > before.waitSleeps);
}

TEST (spin190kb_stats_count_rounds_spins_and_sleeps)
{
    const spin190kb::TuningGuard guard;
    lynx::WorkerPool::tuning().apply ("spin=60,adaptive=1,dry=4,wait=1,waitspin=30");
    lynx::WorkerPool pool (3);
    std::atomic<int> sum { 0 };
    auto add = [&sum] (int i) { sum.fetch_add (i + 1); };
    const auto before = pool.stats();
    for (int r = 0; r < 30; ++r)
    {
        pool.parallelFor (6, add);
        std::this_thread::sleep_for (std::chrono::microseconds (500)); // (longer than any spin: the workers sleep)
    }
    pool.parallelFor (1, add); // (one index: the caller alone, not a round of the workers)
    const auto after = pool.stats();
    CHECK (sum.load() == 30 * 21 + 1);
    CHECK (after.rounds - before.rounds == 30);
    CHECK (after.spins > before.spins);
    CHECK (after.sleeps > before.sleeps);
    CHECK (after.spinFound + after.spinParked + after.spinTimedOut == after.spins);
    CHECK (after.waits == before.waits); // (no await in these rounds)
}

static std::uint64_t spin190kbHandOffs = 0;

TEST (spin190kb_task_graph_renders_the_same_in_every_wait_mode)
{
    // the renderer's task graph with the old wait, the new one (sleeping at once too) and without a pool: bit for bit
    const spin190kb::TuningGuard guard;
    // (round 190kr: every needed worker woken, as before the wake limit: this tiny graph's work would wake none, and the
    // waits and hand-offs this test checks would not happen)
    struct Help
    {
        int before = lynx::WorkerPool::tuning().helpPercent.load();
        Help() { lynx::WorkerPool::tuning().helpPercent = 0; }
        ~Help() { lynx::WorkerPool::tuning().helpPercent = before; }
    } help;
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
    auto render = [&song] (const char* spec, bool pool, bool& graph) {
        if (spec != nullptr)
            lynx::WorkerPool::tuning().apply (spec);
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        graph = state->graphOk;
        lynx::WorkerPool workers (4);
        struct Count
        {
            lynx::WorkerPool& pool;
            ~Count() { spin190kbHandOffs += pool.stats().waitSleeps; }
        } count { workers };
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        if (pool)
            r.setWorkerPool (&workers);
        std::vector<float> out, l (256), rr (256);
        const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
        for (int block = 0; block < 200; ++block)
        {
            r.process (state.get(), true, block * 256 * tps, 256, l.data(), rr.data());
            out.insert (out.end(), l.begin(), l.end());
            out.insert (out.end(), rr.begin(), rr.end());
        }
        return out;
    };
    bool graph = false, unused = false;
    const auto alone = render (nullptr, false, unused);
    const auto old = render ("wait=0,adaptive=0", true, graph);
    CHECK (graph);
    const auto bounded = render ("wait=1,waitspin=30,adaptive=1", true, unused);
    const auto sleeping = render ("wait=1,waitspin=0,adaptive=1,dry=0", true, unused);
    // mode 2: the waiting position is handed over to the thread that pushes its task (at once: waitspin=0)
    const auto handedAtOnce = render ("wait=2,waitspin=0,adaptive=0", true, unused);
    const auto handedLater = render ("wait=2,waitspin=20,adaptive=1", true, unused);
    CHECK (old == alone);
    CHECK (bounded == alone);
    CHECK (sleeping == alone);
    CHECK (handedAtOnce == alone);
    CHECK (handedLater == alone);
    CHECK (spin190kbHandOffs > 0); // (the hand-offs and sleeps did happen)
    CHECK (*std::max_element (alone.begin(), alone.end()) > 0.02f);
}
