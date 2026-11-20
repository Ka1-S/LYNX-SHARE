// Rollup 0.1.30 step 10 (round 190me): round 190lv's fix of round 190kz's batching (commit aa5b93d02, cherry-picked) — a
// block cut into more than maxPieces pieces (pattern events close together) is refused for batching AND still plays,
// piece by piece; before, it was counted and not rendered at all (an audio dropout). Round 190lv's test d34ad9fba on the
// rollup's helpers (sched190kz: the rollup has no live-controller batching yet, so only its plain-song half applies).

TEST (rollup030_step10_a_block_of_too_many_pieces_still_plays)
{
    using namespace lynx;
    const sched190kz::Guard guard;
    auto s = sched190kz::song (false);
    // pattern events every 2 ticks for a beat (about ten per 480-sample block): more than maxPieces pieces
    PatternEvents lane;
    lane.target = { AutomationTargetKind::mixerVolume, 12, 0, 0 };
    for (int i = 0; i < ppq / 2; ++i)
        lane.events.push_back ({ (std::int64_t) i * 2, i % 2 == 0 ? 0.3f : 0.9f });
    s.patterns[0].events.push_back (lane);
    const auto alone = sched190kz::render (s, 480, false, "rounds=block");
    const auto perPiece = sched190kz::render (s, 480, true, "rounds=chunk");
    const auto batched = sched190kz::render (s, 480, true, "rounds=block");
    CHECK (perPiece.samples == alone.samples);
    CHECK (batched.samples == alone.samples);
    CHECK (batched.batched > 0); // (the other blocks are still batched)
    auto state = RackState::build (s, nullptr, 48000.0);
    WorkerPool workers (3);
    RackRenderer r;
    r.prepare (48000.0, 512);
    r.setWorkerPool (&workers);
    std::vector<float> l (480), rr (480);
    const auto tps = ticksPerSample (s.bpm, 48000.0);
    float peak = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        r.process (state.get(), true, b * 480 * tps, 480, l.data(), rr.data());
        for (const auto v : l)
            peak = std::max (peak, std::abs (v));
    }
    CHECK (r.batchRefusedCount (RackRenderer::BatchRefusal::manyPieces) > 0);
    CHECK (peak > 0.0f); // (the refused blocks play)
}
