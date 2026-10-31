// Round 167e: flaky tests, performance and robustness leftovers (docs/analise/REA_rodada167e.md)

#include "core/CacheBudget.h"

TEST (perf167_cache_budget_prunes_the_least_recently_used)
{
    using lynx::cachebudget::Entry;
    // within the limit: nothing goes
    CHECK (lynx::cachebudget::toPrune ({ { 400, 1 }, { 500, 2 } }, 1000).empty());
    // above it: the oldest first, down to 3/4 of the limit (750): 1700 - 600 (t=1) - 300 (t=2) = 800 > 750, - 200 (t=3)
    const std::vector<Entry> files { { 200, 3 }, { 600, 1 }, { 400, 9 }, { 300, 2 }, { 200, 7 } };
    const auto gone = lynx::cachebudget::toPrune (files, 1000);
    CHECK (gone == (std::vector<std::size_t> { 1, 3, 0 }));
    std::int64_t left = 0;
    for (std::size_t i = 0; i < files.size(); ++i)
        if (std::find (gone.begin(), gone.end(), i) == gone.end())
            left += files[i].bytes;
    CHECK (left == 600 && left <= 750);
    // the most recently used one stays even when it alone is above the limit's 3/4
    const auto big = lynx::cachebudget::toPrune ({ { 900, 1 }, { 900, 5 } }, 1000);
    CHECK (big == (std::vector<std::size_t> { 0, 1 }) || big == (std::vector<std::size_t> { 0 }));
    CHECK (! big.empty() && big[0] == 0);
}

TEST (perf167_vfx_script_sees_the_song_stopped_in_an_export_tail)
{
    using namespace lynx::vfx;
    // FL's Tutorial 3 pulse (a note every 1/8 while the song plays): FL's render has none after the song's end
    const char* src = R"(
import flvfx as vfx
def onTick():
  if vfx.context.isPlaying and vfx.context.ticks % 48 == 1:
    v = vfx.Voice()
    v.note = 60
    v.length = 24
    v.trigger()
)";
    auto p = makeProcessor (Kind::script, vfxs165::state (src));
    const auto playing = vfxs165::play (*p, {}, 44100);
    CHECK (vfxs165::keysOn (playing).size() >= 3);
    script::setSongEnded (true);
    auto q = makeProcessor (Kind::script, vfxs165::state (src));
    const auto tail = vfxs165::play (*q, {}, 44100);
    script::setSongEnded (false);
    CHECK (vfxs165::keysOn (tail).empty());
}

TEST (perf167_stopped_vfx_script_costs_the_audio_thread_nothing)
{
    using namespace lynx::vfx;
    // a script that stopped (here: it doesn't compile) is not handed blocks: no wait for the Python thread
    auto p = makeProcessor (Kind::script, vfxs165::state ("import flvfx as vfx\ndef onTick(:\n    pass\n"));
    script::setRealtime (true);
    Transport t;
    t.sampleRate = 44100.0;
    t.playing = true;
    EventList out;
    p->process (nullptr, 0, 512, t, out); // (the first block learns that it answers nothing)
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
    p->process (nullptr, 0, 512, t, out);
    // 5000 blocks: each was a round trip to the Python thread (its wake-up: 20-60 us, up to the 2 ms budget on a busy
    // machine), at least 100 ms in all
    const auto begin = std::chrono::steady_clock::now();
    for (int b = 0; b < 5000; ++b)
    {
        auto e = vfx164::on (60);
        p->process (&e, 1, 512, t, out);
    }
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - begin).count();
    script::setRealtime (false);
    std::printf ("  stopped script: 5000 blocks in %.3f ms\n", ms);
    CHECK (out.size() == 0);
    CHECK (ms < 60.0); // (well under 1 ms in all now)
}

TEST (perf167_reeverb2_first_block_and_predelay_changes_do_not_allocate)
{
    // the heavy-project profile (renderwav LYNX_ALLOCS=1, FL's demo "Thief"): every Reeverb 2 allocated its delay lines
    // in its first audio block (prepared lazily there) and a predelay change re-allocates them (FL's FUN_006d1d00)
    auto fx = lynx::makeReeverb2();
    fx->prepare (44100.0);
    lynx::reeverb2::Settings s;
    lynx::EffectParams params {};
    lynx::reeverb2::toParams (s, params);
    std::vector<float> l (512, 0.1f), r (512, -0.1f);
    rt95::allocs = 0;
    rt95::countAllocs = true;
    fx->process (l.data(), r.data(), 512, params, 120.0);
    for (int pre = 0; pre <= 384; pre += 64) // (the predelay knob from 0 to its maximum, 1 s)
    {
        s.p[2] = pre;
        lynx::reeverb2::toParams (s, params);
        fx->process (l.data(), r.data(), 512, params, 120.0);
    }
    s.tempoPredelay = true; // (tempo-synced: 2 s at 60 BPM)
    lynx::reeverb2::toParams (s, params);
    fx->process (l.data(), r.data(), 512, params, 60.0);
    rt95::countAllocs = false;
    CHECK (rt95::allocs.load() == 0);
    if (rt95::allocs.load() != 0)
        std::printf ("  %ld allocations\n", rt95::allocs.load());
}

// Harmless: a new channel builds its effects rack on the UI thread (settleNew), so its first note and block allocate
// nothing on the audio thread (before: the rack's chorus / delay / reverb were made at the first note)
TEST (perf167_new_harmless_channel_does_not_allocate_at_its_first_note)
{
    lynx::ChannelDef channel;
    channel.instrument = lynx::InstrumentKind::harmless;
    auto inst = lynx::makeHarmlessInstrument();
    inst->prepare (44100.0);
    channel.modelParams.assign ((std::size_t) lynx::harmlessParamCount, 0.0f);
    // the rack's chorus, delay and reverb on (all other values 0)
    channel.modelParams[90] = 1.0f;
    channel.modelParams[91] = 500.0f;
    channel.modelParams[92] = 1.0f;
    channel.modelParams[96] = 1.0f;
    channel.modelParams[97] = 500.0f;
    inst->configure (channel, 120.0);
    inst->settleNew();
    std::vector<float> l (512, 0.0f), r (512, 0.0f);
    rt95::allocs = 0;
    rt95::countAllocs = true;
    inst->noteOn (60, 0.8f);
    for (int b = 0; b < 4; ++b)
        inst->render (l.data(), r.data(), 512, 1.0f, 1.0f);
    rt95::countAllocs = false;
    CHECK (rt95::allocs.load() == 0);
    if (rt95::allocs.load() != 0)
        std::printf ("  %ld allocations\n", rt95::allocs.load());
}

// Fruity Reeverb, Stereo Enhancer and Fruity Delay 2 make their buffers in prepare (UI thread): the first block, a
// delay change and a tempo change down to 60 BPM allocate nothing on the audio thread
TEST (perf167_reeverb1_stereo_enhancer_delay2_do_not_allocate_on_the_audio_thread)
{
    for (const auto kind : { lynx::EffectKind::reeverb1, lynx::EffectKind::stereoEnhancer, lynx::EffectKind::fruityDelay2 })
    {
        auto fx = lynx::makeEffect (kind);
        fx->prepare (44100.0);
        auto slot = lynx::makeEffectSlot (kind);
        std::vector<float> l (512, 0.1f), r (512, -0.1f);
        rt95::allocs = 0;
        rt95::countAllocs = true;
        fx->process (l.data(), r.data(), 512, slot.params, 120.0);
        for (int k = 0; k < 8; ++k) // every control to its maximum, one by one
        {
            slot.params[(std::size_t) k] = 1.0f;
            fx->process (l.data(), r.data(), 512, slot.params, 120.0);
        }
        fx->process (l.data(), r.data(), 512, slot.params, 60.0);
        fx->reset();
        fx->process (l.data(), r.data(), 512, slot.params, 90.0);
        rt95::countAllocs = false;
        CHECK (rt95::allocs.load() == 0);
        if (rt95::allocs.load() != 0)
            std::printf ("  effect %d: %ld allocations\n", (int) kind, rt95::allocs.load());
    }
}

// Fruity Delay 2 made in prepare and restarted at the first block sounds as one made at the first block (before)
TEST (perf167_delay2_restart_matches_a_new_engine)
{
    lynx::fldelay::Delay2State s;
    s.v = { 20, 100, 90, 80, 300, -200, 1, 90 };
    lynx::fldelay::Delay2State other;
    other.v = lynx::fldelay::delay2Default;
    lynx::fldelay::Delay2Engine fresh (s, 48000.0, 97.0, 512, true);
    lynx::fldelay::Delay2Engine reused (other, 48000.0, 120.0, 512, true);
    reused.reserve (60.0);
    std::vector<float> junkL (512, 0.3f), junkR (512, 0.2f);
    reused.process (junkL.data(), junkR.data(), 512); // (it played something before the restart)
    reused.restart (s, 97.0, true);
    double diff = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        std::vector<float> l1 (512), r1 (512);
        for (int i = 0; i < 512; ++i)
            l1[(std::size_t) i] = std::sin (0.01 * (b * 512 + i)) * (b < 4 ? 1.0f : 0.0f), r1[(std::size_t) i] = -0.5f * l1[(std::size_t) i];
        auto l2 = l1, r2 = r1;
        fresh.process (l1.data(), r1.data(), 512);
        reused.process (l2.data(), r2.data(), 512);
        for (int i = 0; i < 512; ++i)
            diff = std::max (diff, (double) std::fabs (l1[(std::size_t) i] - l2[(std::size_t) i]) + std::fabs (r1[(std::size_t) i] - r2[(std::size_t) i]));
    }
    CHECK (diff == 0.0);
}
