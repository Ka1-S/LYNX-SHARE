// Round 190gq: render-ahead (core/RenderAhead.h) — the next blocks' positions as the device callback works them out,
// the partition (what stays real-time), bit-exact output against the real-time path (whole mix and split, with
// worker threads, loops, sends, a pre-fader send tap, a side-chain, master effects), the meters, edit latency, Stop /
// a jump leaving no note behind, live notes into the part rendered ahead, offline renders untouched.

namespace ra190gq
{
using namespace lynx;

Note note (int channel, double beat, double beats, int key, float velocity)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) (beat * ppq);
    n.length = (std::int64_t) (beats * ppq);
    n.key = key;
    n.velocity = velocity;
    return n;
}

// six synths into four inserts and the Master: a reverb, a compressor side-chained from insert 3, a delay with a post
// send to the bus (4) and a pre-fader send to insert 2, the bus, a limiter on the Master, one channel straight to it
Song makeSong (int bars)
{
    Song song;
    song.bpm = 128.0;
    song.mixer = Song::defaultMixer (6);
    song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::reverb));
    auto comp = makeEffectSlot (EffectKind::compressor);
    comp.sidechain = 3;
    song.mixer[2].effects.push_back (comp);
    song.mixer[3].effects.push_back (makeEffectSlot (EffectKind::delay));
    song.mixer[3].sends.push_back ({ 4, 0.6f, false });
    song.mixer[3].sends.push_back ({ 2, 0.3f, true });
    song.mixer[4].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    song.mixer[4].effects.push_back (makeEffectSlot (EffectKind::chorus));
    song.mixer[0].effects.push_back (makeEffectSlot (EffectKind::limiter));
    const int tracks[] = { 1, 2, 3, 4, 0, 1 };
    for (int c = 0; c < 6; ++c)
    {
        ChannelDef ch;
        ch.name = "Synth " + std::to_string (c + 1);
        ch.instrument = InstrumentKind::synth;
        ch.volume = 0.4f;
        ch.mixerTrack = tracks[c];
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        for (int e = 0; e < 8; ++e)
            p.notes.push_back (note (c, e * 0.5 + 0.125 * (c % 3), 0.3 + 0.1 * (c % 2), 48 + 5 * c + (e * 7) % 12, 0.8f));
        for (int b = 0; b < bars; ++b)
        {
            Clip clip;
            clip.pattern = c;
            clip.track = c;
            clip.start = (std::int64_t) b * 4 * ppq;
            clip.length = 4 * ppq;
            song.playlist.push_back (clip);
        }
    }
    song.mode = PlayMode::song;
    return song;
}

// The device callback, as AudioEngine runs it while playing: each device block split at the loop's end, the position
// moving with the tempo the rack rendered with
struct Callback
{
    renderahead::TransportModel model;
    MarkerPlan markers;
    double position = 0.0, bpm = 128.0, rate = 44100.0;
    int block = 64;
    std::function<void (int)> beforeBlock; // (a test's edit, jump or note at the start of device block k)
    Callback (const Song& song, int deviceBlock, double loopEnd) : bpm (song.bpm), block (deviceBlock)
    {
        model.sampleRate = rate;
        model.deviceBlock = deviceBlock;
        model.songMode = loopEnd > 0.0;
        model.loopEnd = loopEnd;
        model.markers = &markers;
    }
    // render `blocks` device blocks; fn (start, samples, l, r, model)
    template <typename Fn>
    void run (int blocks, std::vector<float>& left, std::vector<float>& right, Fn&& fn)
    {
        left.assign ((std::size_t) blocks * (std::size_t) block, 0.0f);
        right.assign (left.size(), 0.0f);
        for (int k = 0; k < blocks; ++k)
        {
            if (beforeBlock)
                beforeBlock (k);
            for (int done = 0; done < block;)
            {
                const auto tps = renderahead::transportTps (bpm, rate);
                const auto sb = renderahead::planSubBlock (model, position, done, tps);
                model.offset = done;
                const auto at = (std::size_t) k * (std::size_t) block + (std::size_t) done;
                fn (sb.start, sb.samples, tps, left.data() + at, right.data() + at, k);
                position = sb.next;
                done += sb.samples;
            }
        }
    }
};

// the real-time path: one renderer, as before round 190gq
void renderPlain (const Song& song, int blocks, int deviceBlock, double loopEnd, int workers, std::vector<float>& l, std::vector<float>& r,
                  std::vector<float>* meterPeaks = nullptr)
{
    auto state = RackState::build (song, nullptr, 44100.0);
    std::unique_ptr<WorkerPool> pool = workers > 0 ? std::make_unique<WorkerPool> (workers) : nullptr;
    RackRenderer renderer;
    renderer.setWorkerPool (pool.get());
    renderer.prepare (44100.0, 2048);
    MeterBank meters;
    LiveEventQueue live;
    Callback cb (song, deviceBlock, loopEnd);
    cb.run (blocks, l, r, [&] (double start, int n, double, float* a, float* b, int) {
        renderer.process (state.get(), true, start, n, a, b, &live, &meters, nullptr);
        if (pool != nullptr)
            pool->park();
        if (meterPeaks != nullptr)
            for (int t = 0; t < (int) song.mixer.size(); ++t)
                meterPeaks->push_back (meters.takePeak (t, 0) + 2.0f * meters.takePeak (t, 1));
    });
}

struct AheadRig
{
    // (declared in this order: the render-ahead thread stops before the snapshots, the renderer and the workers go)
    std::unique_ptr<WorkerPool> pool;
    RackRenderer renderer;
    SnapshotExchange<RackState> exchange;
    renderahead::Targets targets;
    MeterBank meters;
    LiveEventQueue live, midi;
    RenderAhead ahead { renderer };
    AheadRig (const Song& song, int workers, const std::vector<int>& liveChannels, int windowSamples = 0)
    {
        if (workers > 0)
            pool = std::make_unique<WorkerPool> (workers);
        renderer.setWorkerPool (pool.get());
        renderer.prepare (44100.0, 2048);
        if (windowSamples > 0)
            ahead.setWindowSamples (windowSamples);
        ahead.setWorkerPool (pool.get());
        ahead.prepare (44100.0, 2048);
        ahead.setTargets (&targets);
        targets.publish (liveChannels.data(), (int) liveChannels.size(), nullptr, 0);
        exchange.publish (RackState::build (song, nullptr, 44100.0));
    }
    void block (Callback& cb, double start, int n, double tps, float* a, float* b, bool playing = true, std::vector<float>* meterPeaks = nullptr,
                int tracks = 0)
    {
        renderahead::Block blk;
        blk.playing = playing;
        blk.startTick = start;
        blk.samples = n;
        blk.tps = tps;
        blk.model = &cb.model;
        ahead.process (exchange, blk, a, b, &live, &meters, &midi);
        if (pool != nullptr && ! ahead.ownsWorkers())
            pool->park();
        const auto* used = ahead.currentState();
        cb.bpm = ahead.pinnedBpm() > 0.0 ? ahead.pinnedBpm() : (used != nullptr && used->tempoMap ? renderer.currentBpm() : cb.bpm);
        if (meterPeaks != nullptr)
            for (int t = 0; t < tracks; ++t)
                meterPeaks->push_back (meters.takePeak (t, 0) + 2.0f * meters.takePeak (t, 1));
    }
};

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

double energy (const float* x, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
        e += (double) x[i] * x[i];
    return e;
}
} // namespace ra190gq

TEST (render_ahead_plans_blocks_as_the_callback_does)
{
    using namespace lynx::renderahead;
    TransportModel m;
    m.sampleRate = 44100.0;
    m.deviceBlock = 64;
    m.songMode = true;
    m.loopStart = 960.0;
    m.loopEnd = 1920.0;
    const auto tps = transportTps (120.0, 44100.0);
    // inside the loop: one sub-block of the whole device block
    auto a = planSubBlock (m, 1000.0, 0, tps);
    CHECK (a.start == 1000.0 && a.samples == 64 && a.next == 1000.0 + 64 * tps && ! a.last);
    // the loop's end inside the block: split there, the position back to the loop's start
    const auto before = 1920.0 - 10.5 * tps;
    auto b = planSubBlock (m, before, 0, tps);
    CHECK (b.samples == 11);
    CHECK (b.next == 960.0);
    auto c = planSubBlock (m, b.next, b.samples, tps);
    CHECK (c.start == 960.0 && c.samples == 53);
    // already past the end (a seek there): wrapped first
    auto d = planSubBlock (m, 1925.0, 0, tps);
    CHECK (d.start == 960.0 && d.samples == 64);
    // no loop region: the song's end
    m.loopStart = m.loopEnd = 0.0;
    m.songEnd = 3840.0;
    auto e = planSubBlock (m, 3840.0 - 5.2 * tps, 0, tps);
    CHECK (e.samples == 6 && e.next == 0.0);
    // Pattern mode: the position runs on
    m.songMode = false;
    auto f = planSubBlock (m, 5000.0, 0, tps);
    CHECK (f.samples == 64 && f.next == 5000.0 + 64 * tps);
    // a marker jump inside the block: nothing after it is foreseen
    m.songMode = true;
    MarkerPlan plan;
    plan.count = 1;
    plan.jumps[0] = { 3000, MarkerJump::Kind::jump, 100 };
    m.markers = &plan;
    m.songEnd = 0.0;
    auto g = planSubBlock (m, 3000.0 - 20.2 * tps, 0, tps);
    CHECK (g.samples == 21 && g.last);
    // the user's quantized jump waiting: the same
    plan.count = 0;
    m.pendingAt = 2000.0;
    auto h = planSubBlock (m, 2000.0 - 3.5 * tps, 0, tps);
    CHECK (h.samples == 4 && h.last);
}

TEST (render_ahead_partition_keeps_the_live_path_real_time)
{
    using namespace lynx;
    auto song = ra190gq::makeSong (2);
    song.mixer[5].inputChannel = 0; // an insert with an audio input: live
    auto state = RackState::build (song, nullptr, 44100.0);
    std::vector<char> ch, tr;
    int boundary = 0;
    // nothing played live: the Master and the input insert only
    CHECK (RenderAhead::partitionOf (*state, nullptr, 0, nullptr, 0, nullptr, ch, tr, boundary));
    CHECK (tr[0] == 0 && tr[5] == 0);                          // the Master, the input insert: real-time
    CHECK (tr[1] == 1 && tr[2] == 1 && tr[3] == 1 && tr[4] == 1); // the rest ahead
    CHECK (ch[4] == 1);                                         // a channel straight to the Master: ahead, on the boundary
    CHECK (boundary >= 5);                                      // inserts 1-4 and channel 4 into the Master
    // channel 2 played live: it, insert 3 and all it feeds (the bus 4 by its send, insert 2 by its pre-fader send and
    // its side-chain) stay real-time; insert 1 and its channels do not
    const int live[] = { 2 };
    CHECK (RenderAhead::partitionOf (*state, live, 1, nullptr, 0, nullptr, ch, tr, boundary));
    CHECK (ch[2] == 0 && tr[3] == 0 && tr[4] == 0 && tr[2] == 0);
    CHECK (ch[0] == 1 && ch[5] == 1 && tr[1] == 1);
    CHECK (ch[1] == 0 || ch[1] == 1); // (channel 1 into insert 2: ahead, a boundary channel)
    CHECK (ch[1] == 1);
    // a cut group ties channels together: channel 0 cut by channel 2 follows it into the live part
    song.channels[0].cutGroup = 7;
    song.channels[2].cutBy = 7;
    auto cut = RackState::build (song, nullptr, 44100.0);
    CHECK (RenderAhead::partitionOf (*cut, live, 1, nullptr, 0, nullptr, ch, tr, boundary));
    CHECK (ch[0] == 0 && tr[1] == 0);
    // everything live: nothing to render ahead
    const int all[] = { 0, 1, 2, 3, 4, 5 };
    CHECK (! RenderAhead::partitionOf (*state, all, 6, nullptr, 0, nullptr, ch, tr, boundary));
}

TEST (render_ahead_is_bit_exact_with_the_real_time_path)
{
    using namespace lynx;
    const auto song = ra190gq::makeSong (8);
    const int blocks = 900; // ~1.3 s at 64 samples
    for (const auto loopEnd : { 0.0, (double) (2 * 4 * ppq) + 37.0 }) // linear, and a loop region that wraps mid-block
        for (const auto workers : { 0, 2 })
        {
            std::vector<float> l0, r0;
            ra190gq::renderPlain (song, blocks, 64, loopEnd, workers, l0, r0);
            for (const auto& liveSet : std::vector<std::vector<int>> { {}, { 2 }, { 0, 4 } })
                for (const auto paced : { false, true })
                {
                    ra190gq::AheadRig rig (song, workers, liveSet);
                    ra190gq::Callback cb (song, 64, loopEnd);
                    std::vector<float> l1, r1;
                    cb.run (blocks, l1, r1, [&] (double start, int n, double tps, float* a, float* b, int) {
                        if (paced) // (the device's pace, roughly: the thread gets ahead)
                            std::this_thread::sleep_for (std::chrono::microseconds (150));
                        rig.block (cb, start, n, tps, a, b);
                    });
                    const auto& st = rig.ahead.stats();
                    CHECK (ra190gq::same (l0, l1) && ra190gq::same (r0, r1));
                    CHECK (st.engagements.load() >= 1);
                    CHECK (st.flushes.load() == 0);
                    CHECK (st.splitMismatches.load() == 0);
                    if (paced)
                        CHECK (st.fromRing.load() > (std::uint64_t) blocks / 2);
                    if (! ra190gq::same (l0, l1))
                        std::printf ("  render-ahead null: loop %.0f, %d workers, %d live, paced %d: from ring %llu inline %llu waited %llu\n", loopEnd,
                                     workers, (int) liveSet.size(), paced ? 1 : 0, (unsigned long long) st.fromRing.load(),
                                     (unsigned long long) st.inline_.load(), (unsigned long long) st.waited.load());
                }
        }
}

TEST (render_ahead_meters_show_what_is_heard)
{
    using namespace lynx;
    const auto song = ra190gq::makeSong (4);
    const int blocks = 400;
    std::vector<float> l0, r0, m0, l1, r1, m1;
    ra190gq::renderPlain (song, blocks, 64, 0.0, 0, l0, r0, &m0);
    ra190gq::AheadRig rig (song, 0, { 2 });
    ra190gq::Callback cb (song, 64, 0.0);
    cb.run (blocks, l1, r1, [&] (double start, int n, double tps, float* a, float* b, int) {
        std::this_thread::sleep_for (std::chrono::microseconds (100));
        rig.block (cb, start, n, tps, a, b, true, &m1, (int) song.mixer.size());
    });
    CHECK (m0 == m1); // every insert's peak in the very block it is heard
    CHECK (rig.ahead.stats().fromRing.load() > 0);
}

TEST (render_ahead_edits_reach_the_part_rendered_ahead_within_the_window)
{
    using namespace lynx;
    auto song = ra190gq::makeSong (8);
    for (auto& c : song.channels)
        c.volume = 0.0f;
    song.channels[0].volume = 0.5f; // only channel 0 sounds (insert 1, rendered ahead), one held chord per bar
    song.patterns[0].notes.clear();
    for (const auto key : { 48, 55, 60 })
        song.patterns[0].notes.push_back (ra190gq::note (0, 0.0, 4.0, key, 0.8f));
    const int blocks = 600, window = 1024, editAt = 200, againAt = 260;
    ra190gq::AheadRig rig (song, 0, { 3 }, window);
    ra190gq::Callback cb (song, 64, 0.0);
    std::vector<float> l, r;
    auto silentFrom = -1, backFrom = -1;
    // (each edit's snapshot is built from the running one, as the engine builds it: the same instruments and effects)
    cb.beforeBlock = [&] (int k) {
        if (k == editAt) // insert 1 muted: a new snapshot
        {
            auto muted = song;
            muted.mixer[1].muted = true;
            rig.exchange.publish (RackState::build (muted, rig.ahead.currentState(), 44100.0));
        }
        if (k == againAt) // and back again soon after (the tweak hold keeps the window short)
            rig.exchange.publish (RackState::build (song, rig.ahead.currentState(), 44100.0));
    };
    cb.run (blocks, l, r, [&] (double start, int n, double tps, float* a, float* b, int k) {
        std::this_thread::sleep_for (std::chrono::microseconds (100));
        rig.block (cb, start, n, tps, a, b);
        const auto e = ra190gq::energy (a, n) + ra190gq::energy (b, n);
        if (k > editAt && k < againAt && silentFrom < 0 && e == 0.0)
            silentFrom = k;
        if (k > againAt && backFrom < 0 && e > 0.0)
            backFrom = k;
    });
    // the real-time path, the same edits: how late they are heard there (the Master limiter's look-ahead, the ramps)
    auto plainSilent = -1, plainBack = -1;
    {
        auto a = RackState::build (song, nullptr, 44100.0);
        auto muted = song;
        muted.mixer[1].muted = true;
        auto b = RackState::build (muted, a.get(), 44100.0);
        auto c = RackState::build (song, b.get(), 44100.0);
        RackRenderer renderer;
        renderer.prepare (44100.0, 2048);
        LiveEventQueue live;
        MeterBank meters;
        ra190gq::Callback plain (song, 64, 0.0);
        std::vector<float> pl, pr;
        plain.run (blocks, pl, pr, [&] (double start, int n, double, float* x, float* y, int k) {
            renderer.process (k < editAt ? a.get() : k < againAt ? b.get() : c.get(), true, start, n, x, y, &live, &meters, nullptr);
            const auto e = ra190gq::energy (x, n) + ra190gq::energy (y, n);
            if (k > editAt && k < againAt && plainSilent < 0 && e == 0.0)
                plainSilent = k;
            if (k > againAt && plainBack < 0 && e > 0.0)
                plainBack = k;
        });
    }
    std::printf ("  real-time path: first edit %d blocks, the next one %d blocks\n", plainSilent - editAt, plainBack - againAt);
    // the mute is heard within the window (+ the fader's one-block ramp), the second edit within about two blocks
    // against the real-time path: the first edit at most one window later, the next one (the tweak hold) at most two blocks
    CHECK (plainSilent > editAt && plainBack > againAt);
    CHECK (silentFrom > editAt && silentFrom - plainSilent <= window / 64 + 1);
    CHECK (backFrom > againAt && backFrom - plainBack <= 2);
    CHECK (rig.ahead.stats().flushes.load() == 0);
    std::printf ("  render-ahead edit latency: first edit %d blocks, the next one %d blocks (64 samples)\n", silentFrom - editAt, backFrom - againAt);
    {
        const auto& st = rig.ahead.stats();
        std::printf ("  (blocks %llu, ring %llu, inline %llu, waited %llu, flushes %llu, drains %llu, swapped %llu, engagements %llu, partition changes %llu)\n",
                     (unsigned long long) st.blocks.load(), (unsigned long long) st.fromRing.load(), (unsigned long long) st.inline_.load(),
                     (unsigned long long) st.waited.load(), (unsigned long long) st.flushes.load(), (unsigned long long) st.drains.load(),
                     (unsigned long long) st.statesSwapped.load(), (unsigned long long) st.engagements.load(), (unsigned long long) st.partitionChanges.load());
    }
}

TEST (render_ahead_live_edits_and_master_stay_exact)
{
    using namespace lynx;
    // an edit of the live part (the Master's fader) is taken by the live part at once: the output stays identical to the
    // real-time path's through the edit
    auto song = ra190gq::makeSong (6);
    auto quieter = song;
    quieter.mixer[0].volume = 0.4f;
    const int blocks = 500, editAt = 150;
    std::vector<float> l0, r0, l1, r1;
    {
        auto a = RackState::build (song, nullptr, 44100.0);
        auto b = RackState::build (quieter, a.get(), 44100.0);
        RackRenderer renderer;
        renderer.prepare (44100.0, 2048);
        LiveEventQueue live;
        MeterBank meters;
        ra190gq::Callback cb (song, 64, 0.0);
        cb.run (blocks, l0, r0, [&] (double start, int n, double, float* x, float* y, int k) {
            renderer.process (k < editAt ? a.get() : b.get(), true, start, n, x, y, &live, &meters, nullptr);
        });
    }
    {
        ra190gq::AheadRig rig (song, 0, {});
        ra190gq::Callback cb (song, 64, 0.0);
        cb.beforeBlock = [&] (int k) {
            if (k == editAt)
            {
                // (built from the running state, as the engine does: the same instruments and effects)
                const auto* running = rig.ahead.currentState();
                RackState shape;
                shape.song.channels = running->song.channels;
                shape.song.mixer = running->song.mixer;
                shape.song.bpm = running->song.bpm;
                shape.instruments = running->instruments;
                shape.effects = running->effects;
                shape.sampleRate = running->sampleRate;
                shape.latencies = running->latencies;
                shape.pdc = running->pdc;
                shape.channelDelays = running->channelDelays;
                shape.sendDelays = running->sendDelays;
                shape.serial = running->serial;
                rig.exchange.publish (RackState::build (quieter, &shape, 44100.0));
            }
        };
        cb.run (blocks, l1, r1, [&] (double start, int n, double tps, float* x, float* y, int) {
            std::this_thread::sleep_for (std::chrono::microseconds (100));
            rig.block (cb, start, n, tps, x, y);
        });
        CHECK (rig.ahead.stats().statesSwapped.load() == 1);
        CHECK (rig.ahead.stats().flushes.load() == 0);
    }
    CHECK (ra190gq::same (l0, l1) && ra190gq::same (r0, r1));
}

TEST (render_ahead_stop_and_jumps_leave_no_note_sounding)
{
    using namespace lynx;
    auto song = ra190gq::makeSong (8);
    for (auto& t : song.mixer)
        t.effects.clear(); // (no reverb or delay tail: after Stop the output must fall to silence)
    ra190gq::AheadRig rig (song, 2, { 3 });
    ra190gq::Callback cb (song, 64, 0.0);
    std::vector<float> l, r;
    double tail = 0.0;
    cb.beforeBlock = [&] (int k) {
        if (k == 300) // a jump the window did not foresee
            cb.position = 4.0 * ppq + 13.0;
    };
    cb.run (1600, l, r, [&] (double start, int n, double tps, float* a, float* b, int k) {
        std::this_thread::sleep_for (std::chrono::microseconds (60));
        const auto playing = k < 700;
        rig.block (cb, start, n, tps, a, b, playing);
        if (k >= 1300) // ~8 s after Stop
            tail += ra190gq::energy (a, n) + ra190gq::energy (b, n);
    });
    CHECK (rig.ahead.stats().engagements.load() >= 1);
    CHECK (tail < 1.0e-12);
    CHECK (! rig.ahead.stats().engaged.load());
    CHECK (rig.renderer.scheduledNotesSounding() == 0);
}

TEST (render_ahead_live_note_into_the_part_rendered_ahead)
{
    using namespace lynx;
    auto song = ra190gq::makeSong (8);
    for (auto& c : song.channels)
        c.volume = 0.0f;
    song.channels[5].volume = 0.6f;
    for (auto& p : song.patterns)
        p.notes.clear(); // nothing plays but the live note
    ra190gq::AheadRig rig (song, 0, { 3 });
    ra190gq::Callback cb (song, 64, 0.0);
    std::vector<float> l, r;
    auto heardAt = -1;
    cb.beforeBlock = [&] (int k) {
        if (k == 100)
        {
            NoteEvent e;
            e.type = NoteEvent::Type::on;
            e.channel = 5; // rendered ahead (insert 1)
            e.key = 60;
            e.velocity = 0.9f;
            rig.live.push (e);
        }
    };
    cb.run (400, l, r, [&] (double start, int n, double tps, float* a, float* b, int k) {
        std::this_thread::sleep_for (std::chrono::microseconds (60));
        rig.block (cb, start, n, tps, a, b);
        if (heardAt < 0 && ra190gq::energy (a, n) > 0.0)
            heardAt = k;
    });
    CHECK (heardAt >= 100 && heardAt <= 100 + 1024 / 64 + 2); // played there, a window late at most
    CHECK (rig.ahead.stats().promoted.load() == 1);           // and the channel joined the live part
    CHECK (rig.ahead.stats().drains.load() >= 1);
}

TEST (render_ahead_offline_renders_never_use_it)
{
    using namespace lynx;
    const auto song = ra190gq::makeSong (4);
    std::vector<float> l0, r0, l1, r1;
    ra190gq::renderPlain (song, 300, 256, 0.0, 0, l0, r0);
    ra190gq::AheadRig rig (song, 0, {});
    ra190gq::Callback cb (song, 256, 0.0);
    cb.run (300, l1, r1, [&] (double start, int n, double tps, float* a, float* b, int) {
        renderahead::Block blk; // (no device model: an export, a warm-up)
        blk.playing = true;
        blk.startTick = start;
        blk.samples = n;
        blk.tps = tps;
        rig.ahead.process (rig.exchange, blk, a, b, &rig.live, &rig.meters, &rig.midi);
    });
    CHECK (rig.ahead.stats().engagements.load() == 0);
    CHECK (ra190gq::same (l0, l1) && ra190gq::same (r0, r1));
    // and with the option off
    renderAheadOption().store (false);
    ra190gq::AheadRig off (song, 0, {});
    ra190gq::Callback cb2 (song, 256, 0.0);
    cb2.run (300, l1, r1, [&] (double start, int n, double tps, float* a, float* b, int) { off.block (cb2, start, n, tps, a, b); });
    renderAheadOption().store (true);
    CHECK (off.ahead.stats().engagements.load() == 0);
    CHECK (ra190gq::same (l0, l1) && ra190gq::same (r0, r1));
}
