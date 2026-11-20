// Rollup 0.1.30 step 9 (round 190lu): integration checks between the step-9 branches and the rollup.
//  - round 190kz's per-block batching (one worker-pool round per device block) and round 190ks's capture of a plugin's own
//    notes (GitHub #198): the capture runs in process()'s per-piece loop only, so a block is refused while a channel is
//    marked (BatchRefusal::midiBetweenUnits) and batched again once nothing is marked;
//  - LYNX Tune reads its MIDI target channel's held notes in process(): it follows them (followsHeldNotesWith) only with
//    a MIDI channel set, so a block with it stays piece by piece then;
//  - round 190lf's L2 law on the Limiter reports its latency before the first block.

#include "core/LimiterLaw.h"
#include "core/LynxTune.h"

TEST (rollup030_step9_no_batched_block_while_plugins_own_notes_are_recorded)
{
    const sched190kz::Guard guard;
    lynx::WorkerPool::tuning().apply ("rounds=block");
    lynx::WorkerPool::tuning().helpPercent = 0;
    const auto s = sched190kz::song (false);
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::WorkerPool workers (4);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    r.setWorkerPool (&workers);
    std::vector<float> l (480), rr (480);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    double tick = 0.0;
    const auto play = [&] (int blocks) {
        for (int b = 0; b < blocks; ++b, tick += 480 * tps)
            r.process (state.get(), true, tick, 480, l.data(), rr.data());
    };
    using Refusal = lynx::RackRenderer::BatchRefusal;

    play (20); // (nothing marked: the automation grid's blocks are batched)
    const auto batchedBefore = r.batchedBlockCount();
    CHECK (batchedBefore > 0);

    std::array<std::uint64_t, lynx::RackRenderer::ownNoteWords> marks {};
    marks[0] = 1u << 6; // (a channel whose plugin window is open while recording)
    r.setOwnNoteChannels (marks);
    const auto refusedBefore = r.batchRefusedCount (Refusal::midiBetweenUnits);
    play (20);
    CHECK ((r.batchedBlockCount()) == (batchedBefore));
    CHECK ((r.batchRefusedCount (Refusal::midiBetweenUnits)) == (refusedBefore + 20));

    marks[0] = 0;
    r.setOwnNoteChannels (marks);
    play (2); // (the first block after: the capture is switched off once, piece by piece)
    const auto batchedAfterUnmark = r.batchedBlockCount();
    play (20);
    CHECK (r.batchedBlockCount() > batchedAfterUnmark);
}

TEST (rollup030_step9_lynx_tune_follows_held_notes_only_with_a_midi_channel)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    const auto fx = makeEffect (slot);
    CHECK (fx != nullptr);
    if (fx == nullptr)
        return;
    CHECK (fx->followsHeldNotes());
    slot.params[(std::size_t) lynxtune::midiChannel] = 0.0f;
    CHECK (! fx->followsHeldNotesWith (slot.params));
    slot.params[(std::size_t) lynxtune::midiChannel] = 3.0f / 128.0f; // (Channel Rack channel 3)
    CHECK (fx->followsHeldNotesWith (slot.params));
}

// round 190lf's L2 law on the Limiter: the latency PDC reads after prepare(), before any block, is already the law's
// window (issue #113's rule; tests/Pdc188eTests.inc found 66 before / 59 after at 44.1 kHz)
TEST (rollup030_step9_limiter_l2_latency_is_known_before_the_first_block)
{
    using namespace lynx;
    auto s = makeEffectSlot (EffectKind::limiter);
    s.params[(std::size_t) limiterlaw::lawParam] = limiterlaw::l2LawValue;
    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto fx = makeEffect (s);
        fx->prepare (rate);
        const auto before = fx->latencySamples();
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        fx->process (l.data(), r.data(), 512, s.params, 120.0);
        CHECK ((before) == (fx->latencySamples()));
        if (rate == 48000.0)
            CHECK ((before) == (64));
    }
    auto classic = makeEffect (makeEffectSlot (EffectKind::limiter));
    classic->prepare (48000.0);
    CHECK ((classic->latencySamples()) == (72));
}
