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
