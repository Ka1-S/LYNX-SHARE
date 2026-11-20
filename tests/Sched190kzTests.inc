// Round 190kz (sched, 0.1.30 perf-first): one worker-pool round per device block. A block the renderer cuts in pieces
// (the automation's 128-sample grid, pattern events) runs every piece's serial part first, then ONE task-graph round in
// which each unit renders its pieces one after another (RackRenderer::processBatched). LYNX_POOL="rounds=chunk" = one
// round per piece, as before. The samples must not change at all.

#include "core/SynthParams.h"

namespace sched190kz
{
struct Guard
{
    int rounds = lynx::WorkerPool::tuning().blockRounds.load(), help = lynx::WorkerPool::tuning().helpPercent.load();
    ~Guard()
    {
        lynx::WorkerPool::tuning().blockRounds = rounds;
        lynx::WorkerPool::tuning().helpPercent = help;
    }
};

// 35 synth / hat channels on 13 inserts with routes and a reverb (the task graph's test song), played in Song mode with
// automation clips on a channel's volume, an insert's fader, an insert's pan and the reverb's mix: the 128-sample grid
lynx::Song song (bool heldNotesEffect, bool compressor = false)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.mixer = Song::defaultMixer (16);
    for (int i = 5; i < 40; ++i)
    {
        ChannelDef c;
        c.instrument = i % 3 == 0 ? InstrumentKind::hat : InstrumentKind::synth;
        c.mixerTrack = i % 7 == 0 ? 0 : 1 + i % 13;
        c.volume = 0.25f;
        s.channels.push_back (c);
        for (int step = i % 5; step < 16; step += 2)
            s.patterns[0].notes.push_back ({ (std::int64_t) step * ticksPerStep + i * 3, 150, i, 40 + i, 0.6f });
    }
    s.patterns[0].sortNotes();
    s.setRoute (3, 7, true, 0.6f);
    s.setRoute (7, 9, true, 0.5f);
    s.setRoute (9, 11, true, 0.5f);
    s.mixer[9].effects.push_back (makeEffectSlot (EffectKind::reverb));
    if (heldNotesEffect) // (Pitcher reads every channel's held notes: the block stays piece by piece)
    {
        // (a new Pitcher takes its notes from its MIDI port (param 43 "Port routing" on, round 190ee) and reads no held
        // notes: an older project's Pitcher, Port routing off, does)
        auto pitcher = makeEffectSlot (EffectKind::pitchCorrect);
        pitcher.params[43] = 0.0f;
        s.mixer[5].effects.push_back (pitcher);
    }
    if (compressor) // (LYNX's compressor reads them only with its MIDI trigger on: at its defaults the block is batched)
        s.mixer[6].effects.push_back (makeEffectSlot (EffectKind::compressor));
    const auto bars = 4 * 4 * ppq;
    auto automate = [&s, bars] (AutomationTarget target, float from, float to) {
        Automation a;
        a.target = target;
        a.points = { { 0, from, 0.0f }, { bars / 2, to, 0.0f }, { bars, from, 0.0f } };
        s.automations.push_back (a);
        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = (int) s.automations.size() - 1;
        clip.track = (int) s.automations.size();
        clip.length = bars;
        s.playlist.push_back (clip);
    };
    automate ({ AutomationTargetKind::channelVolume, 6, 0, 0 }, 0.2f, 0.9f);
    automate ({ AutomationTargetKind::mixerVolume, 7, 0, 0 }, 0.3f, 1.0f);
    automate ({ AutomationTargetKind::mixerPan, 11, 0, 0 }, 0.1f, 0.9f);
    automate ({ AutomationTargetKind::effectParam, 9, 0, 0 }, 0.0f, 1.0f);
    // (a LYNX Synth knob: a plugin parameter binding, applied by its channel inside its task)
    automate ({ AutomationTargetKind::channelParam, 7, 0, synthparam::target (synthparam::cutoff) }, 0.2f, 0.8f);
    for (int k = 0; k < 4; ++k)
    {
        Clip clip;
        clip.pattern = 0;
        clip.track = 0;
        clip.start = (std::int64_t) k * 4 * ppq;
        clip.length = 4 * ppq;
        s.playlist.push_back (clip);
    }
    s.mode = PlayMode::song;
    return s;
}

struct Result
{
    std::vector<float> samples;
    std::uint64_t rounds = 0, batched = 0, batchedRounds = 0, heldRefusals = 0;
    int blocks = 0;
};

// the song played from its start in device blocks of `block` samples: alone (no pool) or with 4 workers
Result render (const lynx::Song& s, int block, bool pool, const char* spec)
{
    if (spec != nullptr)
        lynx::WorkerPool::tuning().apply (spec);
    lynx::WorkerPool::tuning().helpPercent = 0; // (every needed worker woken: the rounds really go to the workers)
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::WorkerPool workers (4);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    if (pool)
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
    out.heldRefusals = r.batchRefusedCount (lynx::RackRenderer::BatchRefusal::heldNotes);
    return out;
}
} // namespace sched190kz

TEST (sched190kz_tuning_parses_rounds)
{
    const sched190kz::Guard guard;
    auto& t = lynx::WorkerPool::tuning();
    CHECK (t.blockRounds.load() == 1 || t.blockRounds.load() == 0);
    t.apply ("rounds=chunk");
    CHECK (t.blockRounds.load() == 0);
    t.apply ("help=25");
    CHECK (t.blockRounds.load() == 0); // (missing key: unchanged)
    t.apply ("spin=60,rounds=block,help=30");
    CHECK (t.blockRounds.load() == 1 && t.helpPercent.load() == 30);
    t.apply ("rounds=0");
    CHECK (t.blockRounds.load() == 0);
    t.apply ("rounds=1");
    CHECK (t.blockRounds.load() == 1);
}

TEST (sched190kz_one_round_per_block_renders_the_same)
{
    // automation on the grid of 128 inside device blocks of 480 / 441 / 256 / 64 (at 64 one piece: nothing to batch):
    // without a pool, with one round per piece, and with one round per block: bit for bit
    const sched190kz::Guard guard;
    const auto s = sched190kz::song (false);
    for (const auto block : { 480, 441, 256, 64 })
    {
        const auto alone = sched190kz::render (s, block, false, nullptr);
        const auto perPiece = sched190kz::render (s, block, true, "rounds=chunk");
        const auto perBlock = sched190kz::render (s, block, true, "rounds=block");
        CHECK (perPiece.samples == alone.samples);
        CHECK (perBlock.samples == alone.samples);
        CHECK (perPiece.batched == 0);
        if (block >= 256)
        {
            CHECK (perBlock.batched > 0);        // (the blocks were batched...)
            CHECK (perBlock.batchedRounds > 0);  // (... and went to the workers in one round)
            CHECK (perBlock.rounds < perPiece.rounds); // (fewer rounds)
            CHECK ((double) perBlock.rounds <= 1.05 * perBlock.blocks);
            std::printf ("  block %d: rounds per block %.2f per piece, %.2f batched (%llu of %d blocks batched)\n", block,
                         (double) perPiece.rounds / perPiece.blocks, (double) perBlock.rounds / perBlock.blocks,
                         (unsigned long long) perBlock.batched, perBlock.blocks);
        }
        else
            CHECK (perBlock.batched == 0);
    }
    const auto heard = sched190kz::render (s, 480, false, nullptr).samples;
    CHECK (*std::max_element (heard.begin(), heard.end()) > 0.02f);
}

TEST (sched190kz_an_effect_following_held_notes_keeps_the_block_piece_by_piece)
{
    // Pitcher reads every channel's held notes at its piece: that block is not batched, and renders the same
    const sched190kz::Guard guard;
    const auto s = sched190kz::song (true);
    const auto alone = sched190kz::render (s, 480, false, nullptr);
    const auto perBlock = sched190kz::render (s, 480, true, "rounds=block");
    CHECK (perBlock.samples == alone.samples);
    CHECK (perBlock.batched == 0);
    CHECK (perBlock.heldRefusals > 0);
    // a compressor that can follow them but does not with its settings: batched, the same samples
    const auto c = sched190kz::song (false, true);
    const auto cAlone = sched190kz::render (c, 480, false, nullptr);
    const auto cBlock = sched190kz::render (c, 480, true, "rounds=block");
    CHECK (cBlock.samples == cAlone.samples);
    CHECK (cBlock.batched > 0);
    CHECK (cBlock.heldRefusals == 0);
}
