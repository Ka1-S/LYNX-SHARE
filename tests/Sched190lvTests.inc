// Round 190lv (sched2, 0.1.30 perf-first): batched blocks for projects with live controllers (Peak / Formula / envelope
// controllers: RackState::liveAutomations). Such a block runs every piece's serial part first, as round 190kz's batched
// blocks do, then two task-graph rounds: round A = the controllers' sources and every unit that feeds them, each source
// writing its live values into the next piece's slot right after rendering a piece; round B = the rest. Anything where a
// live value is read or written elsewhere (clips, links, lanes, modulators, a round-A unit's own controls...) stays piece
// by piece. LYNX_POOL "live=0" = piece by piece as before. The samples must not change at all.

namespace sched190lv
{
struct Guard
{
    int rounds = lynx::WorkerPool::tuning().blockRounds.load(), help = lynx::WorkerPool::tuning().helpPercent.load(),
        live = lynx::WorkerPool::tuning().liveRounds.load();
    ~Guard()
    {
        lynx::WorkerPool::tuning().blockRounds = rounds;
        lynx::WorkerPool::tuning().helpPercent = help;
        lynx::WorkerPool::tuning().liveRounds = live;
    }
};

lynx::Automation liveAutomation (lynx::AutomationTarget target, int track, int output, int channel = -1, int slot = 0)
{
    lynx::Automation a;
    a.target = target;
    a.source.track = track;
    a.source.slot = slot;
    a.source.output = output;
    a.source.channel = channel;
    if (output == 1)
        a.curve = { 0.1f, 0.8f, 0.4f }; // (a mapping curve on one of them)
    return a;
}

// round 190kz's test song (35 channels on 13 inserts with routes, automation clips on the 128 grid) with two Fruity Peak
// Controllers: insert 3's (fed by its channel, routed on to 7 → 9 → 11) and insert 2's (its LFO), driving insert 11's
// fader, the reverb's mix on insert 9, a channel's volume and a LYNX Synth knob (a binding) — all of them in round B
lynx::Song song()
{
    using namespace lynx;
    auto s = sched190kz::song (false);
    auto peak = makeEffectSlot (EffectKind::peakController);
    peak.params[1] = 0.9f; // (the peak moves its output)
    peak.params[3] = 0.3f;
    s.mixer[3].effects.insert (s.mixer[3].effects.begin(), peak);
    auto lfo = makeEffectSlot (EffectKind::peakController);
    lfo.params[5] = 0.95f; // (an LFO: it moves whatever comes in)
    lfo.params[7] = 0.0f;
    s.mixer[2].effects.push_back (lfo);
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::mixerVolume, 11, 0, 0 }, 3, 0));
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::effectMix, 9, 0, 0 }, 3, 1));
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::channelVolume, 8, 0, 0 }, 2, 2));
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::channelParam, 10, 0, synthparam::target (synthparam::cutoff) }, 2, 0));
    return s;
}

// the same with round-A units that read live values exactly: insert 3's own pan from its own controller, and a third
// controller on insert 9 (after 3 → 7 → 9: round A is now 3, 7, 9 and their channels) whose reverb mix insert 3's
// controller drives (an upstream source); the channel volume moves to a channel on insert 12 (round B)
lynx::Song songUpstream()
{
    using namespace lynx;
    auto s = song();
    auto peak = makeEffectSlot (EffectKind::peakController);
    peak.params[1] = 0.8f;
    s.mixer[9].effects.push_back (peak);
    for (auto& a : s.automations)
        if (a.isLive() && a.target.kind == AutomationTargetKind::channelVolume)
            a.target.index = 11;
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::mixerPan, 3, 0, 0 }, 3, 0));
    s.automations.push_back (liveAutomation ({ AutomationTargetKind::mixerPan, 13, 0, 0 }, 9, 1, -1, 1));
    return s;
}

struct Result
{
    std::vector<float> samples;
    std::uint64_t rounds = 0, batched = 0, batchedRounds = 0, liveRounds = 0, liveRefusals = 0;
    int blocks = 0;
};

// the song from its start in device blocks of `block` samples: alone (threads 0) or with a pool of `threads` workers
Result render (const lynx::Song& s, int block, int threads, const char* spec)
{
    if (spec != nullptr)
        lynx::WorkerPool::tuning().apply (spec);
    lynx::WorkerPool::tuning().helpPercent = 0;
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::WorkerPool workers (std::max (1, threads));
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    if (threads > 0)
        r.setWorkerPool (&workers);
    Result out;
    std::vector<float> l ((std::size_t) block), rr ((std::size_t) block);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    const auto before = workers.stats().rounds;
    double tick = 0.0;
    for (int done = 0; done < 2 * 48000; done += block, ++out.blocks)
    {
        r.process (state.get(), true, tick, block, l.data(), rr.data());
        tick += block * tps;
        out.samples.insert (out.samples.end(), l.begin(), l.end());
        out.samples.insert (out.samples.end(), rr.begin(), rr.end());
    }
    out.rounds = workers.stats().rounds - before;
    out.batched = r.batchedBlockCount();
    out.batchedRounds = r.batchedRoundCount();
    out.liveRounds = r.batchedLiveRoundCount();
    out.liveRefusals = r.batchRefusedCount (lynx::RackRenderer::BatchRefusal::liveControllers);
    return out;
}
} // namespace sched190lv

TEST (sched190lv_tuning_parses_live)
{
    const sched190lv::Guard guard;
    auto& t = lynx::WorkerPool::tuning();
    t.apply ("live=0");
    CHECK (t.liveRounds.load() == 0);
    t.apply ("rounds=block,help=10");
    CHECK (t.liveRounds.load() == 0); // (missing key: unchanged)
    t.apply ("live=1,rounds=chunk");
    CHECK (t.liveRounds.load() == 1 && t.blockRounds.load() == 0);
}

TEST (sched190lv_live_controllers_batched_render_the_same)
{
    // device blocks of 480 / 441 / 256 / 64 (at 256 two pieces per block, at 64 one: kept piece by piece, two rounds would
    // not save any): alone, one round per piece, the live switch off, and the two rounds, with 1 and 3 workers: bit for bit
    const sched190lv::Guard guard;
    for (const auto upstream : { false, true })
    {
    const auto s = upstream ? sched190lv::songUpstream() : sched190lv::song();
    {
        auto state = lynx::RackState::build (s, nullptr, 48000.0);
        CHECK (state->liveAutomations.size() == (upstream ? 6u : 4u));
    }
    for (const auto block : { 480, 441, 256, 64 })
    {
        const auto alone = sched190lv::render (s, block, 0, "rounds=block,live=1");
        CHECK (alone.batched == 0);
        for (const auto threads : { 1, 3 })
        {
            const auto perPiece = sched190lv::render (s, block, threads, "rounds=chunk,live=1");
            const auto liveOff = sched190lv::render (s, block, threads, "rounds=block,live=0");
            const auto twoRounds = sched190lv::render (s, block, threads, "rounds=block,live=1");
            CHECK (perPiece.samples == alone.samples);
            CHECK (liveOff.samples == alone.samples);
            CHECK (twoRounds.samples == alone.samples);
            CHECK (perPiece.batched == 0 && liveOff.batched == 0);
            CHECK (liveOff.liveRefusals > 0);
            if (block >= 441)
            {
                CHECK (twoRounds.batched > 0);
                CHECK (twoRounds.liveRounds > 0 && twoRounds.liveRounds == twoRounds.batchedRounds);
                CHECK (twoRounds.liveRefusals == 0);
                CHECK (twoRounds.rounds < perPiece.rounds);
                CHECK ((double) twoRounds.rounds <= 2.05 * twoRounds.blocks);
                std::printf ("  %sblock %d, %d workers: rounds per block %.2f per piece, %.2f in two rounds (%llu of %d blocks batched, "
                             "%llu on the workers)\n",
                             upstream ? "upstream readers, " : "", block, threads, (double) perPiece.rounds / perPiece.blocks, (double) twoRounds.rounds / twoRounds.blocks,
                             (unsigned long long) twoRounds.batched, twoRounds.blocks, (unsigned long long) twoRounds.liveRounds);
            }
            else
                CHECK (twoRounds.batched == 0);
        }
    }
    if (upstream)
        continue;
    // the controllers really move what they drive: without them the song sounds different
    auto plain = s;
    for (auto& a : plain.automations)
        if (a.isLive())
            a.source = {};
    CHECK (sched190lv::render (plain, 480, 0, nullptr).samples != sched190lv::render (s, 480, 0, nullptr).samples);
    }
}

TEST (sched190lv_live_controller_refusals_stay_piece_by_piece)
{
    // each case keeps every block piece by piece (counted as "live controllers") and renders what it rendered alone
    using namespace lynx;
    const sched190lv::Guard guard;
    const auto check = [] (const char* name, const Song& s) {
        const auto alone = sched190lv::render (s, 480, 0, "rounds=block,live=1");
        const auto pool = sched190lv::render (s, 480, 3, "rounds=block,live=1");
        CHECK (pool.samples == alone.samples);
        CHECK (pool.batched == 0);
        CHECK (pool.liveRefusals > 0);
        std::printf ("  %s: %llu blocks refused, batched %llu\n", name, (unsigned long long) pool.liveRefusals, (unsigned long long) pool.batched);
    };
    // a round-A unit reads a live value: the volume of insert 3's own channel (it feeds the source)
    {
        auto s = sched190lv::song();
        auto channel = -1;
        for (int c = 0; c < (int) s.channels.size(); ++c)
            if (s.channels[(std::size_t) c].mixerTrack == 3)
                channel = c;
        CHECK (channel >= 0);
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::channelVolume, channel, 0, 0 }, 2, 0));
        check ("round-A reader (channel)", s);
    }
    // ... insert 3's pan from insert 2's controller (both in round A, 2 does not feed 3)
    {
        auto s = sched190lv::song();
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::mixerPan, 3, 0, 0 }, 2, 2));
        check ("round-A reader (another source's insert)", s);
    }
    // ... insert 3's pan from a controller downstream of it (insert 9 after 3 → 7 → 9)
    {
        auto s = sched190lv::songUpstream();
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::effectParam, 3, 0, 0 }, 9, 0, -1, 1));
        check ("round-A reader (downstream source)", s);
    }
    // a live automation with an automation clip too
    {
        auto s = sched190lv::song();
        const auto a = (int) s.automations.size() - 4;
        s.automations[(std::size_t) a].points = { { 0, 0.2f, 0.0f }, { 4 * ppq, 0.9f, 0.0f } };
        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = a;
        clip.track = 20;
        clip.length = 4 * ppq;
        s.playlist.push_back (clip);
        check ("clip on a live automation", s);
    }
    // a modulator on a live automation's target (its route reads the live slot)
    {
        auto s = sched190lv::song();
        Modulator m;
        m.routes.push_back ({ { AutomationTargetKind::mixerVolume, 11, 0, 0 }, 0.3f });
        s.modulators.push_back (m);
        check ("modulator source", s);
    }
    // the Master as a source (it runs after the rounds)
    {
        auto s = sched190lv::song();
        s.mixer[0].effects.push_back (makeEffectSlot (EffectKind::peakController));
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::mixerPan, 12, 0, 0 }, 0, 0));
        check ("Master source", s);
    }
    // a live pitch bend on a LYNX instrument (applyLiveControl)
    {
        auto s = sched190lv::song();
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::channelParam, 10, 0, midictl::pitchBendBase }, 2, 2));
        check ("live pitch bend", s);
    }
    // a VCA level driven live
    {
        auto s = sched190lv::song();
        s.vcas.push_back ({});
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::vcaLevel, 0, 0, 0 }, 2, 2));
        check ("live VCA level", s);
    }
    // a source slot that holds nothing (old path: no value written)
    {
        auto s = sched190lv::song();
        s.automations.push_back (sched190lv::liveAutomation ({ AutomationTargetKind::mixerPan, 12, 0, 0 }, 6, 0));
        check ("empty source slot", s);
    }
}

TEST (sched190lv_a_block_of_too_many_pieces_still_plays)
{
    // pattern events every 2 ticks for a beat (about ten per 480-sample block): more than maxPieces pieces. Such a block
    // is not batched and plays piece by piece — before round 190lv it was counted and not rendered at all
    using namespace lynx;
    const sched190lv::Guard guard;
    for (const auto live : { false, true })
    {
        auto s = live ? sched190lv::song() : sched190kz::song (false);
        PatternEvents lane;
        lane.target = { AutomationTargetKind::mixerVolume, 12, 0, 0 };
        for (int i = 0; i < ppq / 2; ++i)
            lane.events.push_back ({ (std::int64_t) i * 2, i % 2 == 0 ? 0.3f : 0.9f });
        s.patterns[0].events.push_back (lane);
        const auto alone = sched190lv::render (s, 480, 0, "rounds=block,live=1");
        const auto perPiece = sched190lv::render (s, 480, 3, "rounds=chunk,live=1");
        const auto batched = sched190lv::render (s, 480, 3, "rounds=block,live=1");
        CHECK (perPiece.samples == alone.samples);
        CHECK (batched.samples == alone.samples);
        CHECK (batched.batched > 0);
        auto state = RackState::build (s, nullptr, 48000.0);
        WorkerPool workers (3);
        RackRenderer r;
        r.prepare (48000.0, 512);
        r.setWorkerPool (&workers);
        std::vector<float> l (480), rr (480);
        const auto tps = ticksPerSample (s.bpm, 48000.0);
        for (int b = 0; b < 100; ++b)
            r.process (state.get(), true, b * 480 * tps, 480, l.data(), rr.data());
        CHECK (r.batchRefusedCount (RackRenderer::BatchRefusal::manyPieces) > 0);
    }
}
