// Round 190of (0.1.31 perf study): the Plugin performance monitor's Longest / Spikes columns (core/PluginMonitor.h,
// ProcessTimes::addCall in core/RackRenderer.h), the split CPU meter's counters (P24, core/RenderAhead.h Stats
// liveNs / aheadNs / audioNs) and the allocation-free latency check (RackState::latenciesDiffer).

TEST (perf190of_monitor_longest_and_spikes)
{
    using namespace lynx;
    using namespace lynx::perfmon;
    auto song = Song::makeDefault();
    song.channels[2].instrument = InstrumentKind::plugin;
    song.channels[2].plugin.name = "Heavy Synth";
    const auto list = sources (song);
    Monitor m;
    std::vector<std::uint64_t> busy (keyCount, 0);
    CallStats calls;
    calls.longestNs.assign (keyCount, 0);
    calls.spikes.assign (keyCount, 0);
    calls.spikes[2] = 7; // (counted before the monitor opened: not shown)
    m.update (list, busy, 0, 0, &calls); // starting point
    busy[2] = 30'000'000;
    calls.longestNs[2] = 4'500'000; // one call of 4.5 ms
    calls.spikes[2] = 9;            // two more spikes
    calls.longestNs[0] = 200'000;
    m.update (list, busy, 100'000'000, 40'000'000, &calls);
    auto rows = m.rows ({}, SortBy::spikes);
    CHECK (! rows.empty() && rows[0].source.channel == 2 && rows[0].spikes == 2 && std::abs (rows[0].longestMs - 4.5) < 1e-9);
    // the longest call stays the highest since the reset; spikes add up
    calls.longestNs[2] = 1'000'000;
    calls.spikes[2] = 10;
    m.update (list, busy, 200'000'000, 80'000'000, &calls);
    rows = m.rows ("heavy");
    CHECK (rows.size() == 1 && rows[0].spikes == 3 && std::abs (rows[0].longestMs - 4.5) < 1e-9);
    rows = m.rows ({}, SortBy::longest);
    CHECK (rows[0].source.channel == 2 && rows[1].source.channel == 0 && std::abs (rows[1].longestMs - 0.2) < 1e-9);
    // reset: both start again from the next update
    m.reset();
    calls.longestNs[2] = 2'000'000;
    calls.spikes[2] = 11;
    m.update (list, busy, 300'000'000, 90'000'000, &calls);
    rows = m.rows ("heavy");
    CHECK (rows.size() == 1 && rows[0].spikes == 1 && std::abs (rows[0].longestMs - 2.0) < 1e-9);
    // an update without the call counters (older callers) keeps them
    m.update (list, busy, 400'000'000, 90'000'000);
    rows = m.rows ("heavy");
    CHECK (rows.size() == 1 && rows[0].spikes == 1 && std::abs (rows[0].longestMs - 2.0) < 1e-9);
}

TEST (perf190of_process_times_count_calls)
{
    using namespace lynx;
    auto times = std::make_unique<ProcessTimes>();
    // 1 ms of audio: a call of 0.4 ms is no spike, 0.6 ms is one
    times->addCall (true, 5, 400'000, 1'000'000);
    times->addCall (true, 5, 600'000, 1'000'000);
    times->addCall (true, 5, 100'000, 1'000'000);
    CHECK (times->channelNs[5].load() == 1'100'000);
    CHECK (times->channelLongestNs[5].load() == 600'000);
    CHECK (times->channelSpikes[5].load() == 1);
    const auto key = (std::size_t) perfmon::effectKey (3, 2) - perfmon::channelKeys;
    times->addCall (false, key, 3'000'000, 2'000'000);
    CHECK (times->effectNs[key].load() == 3'000'000 && times->effectLongestNs[key].load() == 3'000'000 && times->effectSpikes[key].load() == 1);
    // out of range keys are ignored
    times->addCall (true, ProcessTimes::channels, 1, 1);
    times->addCall (false, times->effectNs.size(), 1, 1);
    CHECK (times->channelSpikes[5].load() == 1);
}

TEST (perf190of_renderer_feeds_the_monitor_while_it_is_open)
{
    using namespace lynx;
    const auto song = ra190gq::makeSong (2);
    auto state = RackState::build (song, nullptr, 44100.0);
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    MeterBank meters;
    LiveEventQueue live;
    std::vector<float> l (256), r (256);
    double tick = 0.0;
    auto play = [&] (int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            renderer.process (state.get(), true, tick, 256, l.data(), r.data(), &live, &meters, nullptr);
            tick += 256 * renderahead::transportTps (song.bpm, 44100.0);
        }
    };
    play (20); // the monitor closed: nothing counted
    auto& times = renderer.processTimes();
    std::uint64_t longest = 0, busy = 0;
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        longest += times.channelLongestNs[c].load(), busy += times.channelNs[c].load();
    CHECK (longest == 0 && busy == 0);
    times.enabled.store (true);
    play (100);
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        longest = std::max<std::uint64_t> (longest, times.channelLongestNs[c].load()), busy += times.channelNs[c].load();
    CHECK (busy > 0 && longest > 0 && longest <= busy);
    std::uint64_t effects = 0;
    for (std::size_t k = 0; k < times.effectNs.size(); ++k)
        effects += times.effectLongestNs[k].load();
    CHECK (effects > 0); // (the song's inserts have effects)
}

TEST (perf190of_split_meter_counts_the_live_part_and_the_part_rendered_ahead)
{
    using namespace lynx;
    const auto song = ra190gq::makeSong (8);
    const int blocks = 600;
    ra190gq::AheadRig rig (song, 0, {});
    ra190gq::Callback cb (song, 64, 0.0);
    std::vector<float> l, r;
    double callbackNs = 0.0;
    cb.run (blocks, l, r, [&] (double start, int n, double tps, float* a, float* b, int) {
        std::this_thread::sleep_for (std::chrono::microseconds (150)); // (the device's pace, roughly: the thread gets ahead)
        const auto t0 = std::chrono::steady_clock::now();
        rig.block (cb, start, n, tps, a, b);
        callbackNs += (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
    });
    const auto& st = rig.ahead.stats();
    const auto audio = st.audioNs.load(), live = st.liveNs.load(), ahead = st.aheadNs.load();
    // the audio: every sample the callback rendered
    CHECK (std::abs ((double) audio - (double) blocks * 64.0 / 44100.0 * 1.0e9) < (double) blocks); // (1 ns of rounding per block)
    // the live part: the callback's own time in the renderers (within 5 % of what the caller measured around them)
    CHECK (live > 0 && (double) live <= callbackNs * 1.0001 && (double) live >= callbackNs * 0.95);
    if (! ((double) live >= callbackNs * 0.95))
        std::printf ("  split meter: live %.0f ns, measured around the callbacks %.0f ns\n", (double) live, callbackNs);
    // the part rendered ahead: the render-ahead thread's work (most blocks came from its ring)
    CHECK (st.fromRing.load() > (std::uint64_t) blocks / 2 && ahead > 0);
    renderahead::SplitLoadReader reader;
    const auto first = reader.take (st);
    CHECK (first.valid && std::abs (first.live - (double) live / (double) audio) < 1e-9 && std::abs (first.ahead - (double) ahead / (double) audio) < 1e-9);
    CHECK (! reader.take (st).valid); // nothing new since
}

TEST (perf190of_latencies_differ_without_making_the_list)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    const auto state = RackState::build (song, nullptr, 44100.0);
    const auto list = state->currentLatencies();
    CHECK (! state->latenciesDiffer (list));
    auto other = list;
    if (! other.empty())
    {
        other.back() += 1;
        CHECK (state->latenciesDiffer (other));
        other = list;
        other.pop_back();
        CHECK (state->latenciesDiffer (other));
    }
    other = list;
    other.push_back (0);
    CHECK (state->latenciesDiffer (other));
    CHECK (state->latenciesDiffer ({}) == ! list.empty());
}
