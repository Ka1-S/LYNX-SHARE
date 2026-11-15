// Round 190hq, slice B13 (community complaint #27 "delay compensation: automation and MIDI out early, sync off"): the
// side inputs of a slot (side-chain, second side-chain, a hosted effect's Connections) line up with the slot's own input,
// the automation of faders, pans, route levels and VCA faders follows the audio it acts on (Song::compensateMixerLevels),
// and render-ahead stays bit-exact with all of it. The clocks LYNX sends / follows, Link and played notes count the
// Master's PDC in the app (engine/AudioEngine.cpp heardLatencyMs; checked by --selftest).

namespace pdc190hq
{
using namespace lynx;

// the slot's own input and its side input as they reach it: the first sample index (from the first process) where each
// is non-silent
struct Probe final : Effect
{
    struct Seen
    {
        long main = -1, side = -1, side2 = -1, connectedMain = -1;
    };
    explicit Probe (std::shared_ptr<Seen> s) : seen (std::move (s)) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void setSidechain (const float* l, const float*) noexcept override { side = l; }
    void setSidechain2 (const float* l, const float*) noexcept override { side2 = l; }
    void setMainInput (const float* l, const float*) noexcept override { connected = l; }
    void process (float* l, float*, int n, const EffectParams&, double) noexcept override
    {
        auto onset = [&] (const float* x, long& at) {
            if (x != nullptr && at < 0)
                for (int i = 0; i < n; ++i)
                    if (std::abs (x[i]) > 0.01f)
                    {
                        at = position + i;
                        break;
                    }
        };
        onset (l, seen->main);
        onset (side, seen->side);
        onset (side2, seen->side2);
        onset (connected, seen->connectedMain);
        side = side2 = connected = nullptr;
        position += n;
    }

    std::shared_ptr<Seen> seen;
    const float *side = nullptr, *side2 = nullptr, *connected = nullptr;
    long position = 0;
};

class Latent final : public Effect
{
public:
    explicit Latent (int samples) : latency (samples), line (samples) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* l, float* r, int n, const EffectParams&, double) noexcept override { line.process (l, r, n); }
    int latencySamples() const noexcept override { return latency; }

private:
    int latency;
    DelayLine line;
};

EffectSlot plugin (const std::string& id, int sidechain = -1, int sidechain2 = -1)
{
    EffectSlot fx;
    fx.kind = EffectKind::plugin;
    fx.plugin.id = id;
    fx.sidechain = sidechain;
    fx.sidechain2 = sidechain2;
    return fx;
}

// channel c → insert c + 1 (an impulse each), all to the Master
Song impulses (int channels)
{
    Song s;
    s.mixer = Song::defaultMixer (channels + 1);
    for (int i = 0; i < channels; ++i)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = "impulse";
        c.mixerTrack = i + 1;
        s.channels.push_back (c);
    }
    s.patterns.emplace_back();
    return s;
}

// "latent<N>" = N samples of latency, "probe" = Probe
RackState::Factories factories (std::shared_ptr<Probe::Seen> seen)
{
    RackState::Factories f;
    f.samples = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (64, 0.0f);
        d->left[0] = 1.0f;
        return std::shared_ptr<const SampleData> (d);
    };
    f.pluginEffects = [seen] (const PluginRef& ref, double) -> std::shared_ptr<Effect> {
        if (ref.id == "probe")
            return std::make_shared<Probe> (seen);
        return std::make_shared<Latent> (std::stoi (ref.id.substr (6)));
    };
    return f;
}

// every channel plays its impulse at sample 0; what the probe saw
Probe::Seen run (const Song& s)
{
    auto seen = std::make_shared<Probe::Seen>();
    auto state = RackState::build (s, nullptr, 48000.0, factories (seen));
    RackRenderer r;
    r.prepare (48000.0, 256);
    LiveEventQueue live;
    for (int c = 0; c < (int) s.channels.size(); ++c)
    {
        NoteEvent on;
        on.channel = c;
        on.key = 60;
        on.velocity = 1.0f;
        live.push (on);
    }
    std::vector<float> l (256), rr (256);
    for (int b = 0; b < 4; ++b)
        r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), b == 0 ? &live : nullptr);
    return *seen;
}
} // namespace pdc190hq

TEST (pdc190hq_side_inputs_in_the_plan)
{
    using namespace lynx;
    // the kick (insert 1) through a 100-sample limiter, side-chaining the bass's compressor (insert 2): the bass waits for it
    auto s = pdc190hq::impulses (2);
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent100"));
    s.mixer[2].effects.push_back (pdc190hq::plugin ("probe", 1));
    auto plan = computePdc (s, { 0, 0 }, { {}, { 100 }, { 0 } }, 48000.0);
    CHECK (plan.inAlign[2] == 100 && plan.slotInput[2][0] == 100 && plan.sideDelay[2][0][0] == 0);
    CHECK (plan.channelDelay[1] == 100 && plan.total == 100 && plan.sendDelay[2][0] == 0);

    // a 30-sample EQ before the compressor, a dry kick: the side-chain waits 30 samples
    s.mixer[1].effects.clear();
    s.mixer[2].effects.insert (s.mixer[2].effects.begin(), pdc190hq::plugin ("latent30"));
    plan = computePdc (s, { 0, 0 }, { {}, {}, { 30, 0 } }, 48000.0);
    CHECK (plan.inAlign[2] == 0 && plan.slotInput[2][1] == 30 && plan.sideDelay[2][1][0] == 30 && plan.sideDelay[2][0][0] == 0);
    CHECK (plan.any());

    // both: the bass's input waits 70, the slot's input (100) and the kick (100) meet
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent100"));
    plan = computePdc (s, { 0, 0 }, { {}, { 100 }, { 30, 0 } }, 48000.0);
    CHECK (plan.inAlign[2] == 70 && plan.slotInput[2][1] == 100 && plan.sideDelay[2][1][0] == 0 && plan.channelDelay[1] == 70);

    // a source bypassed "Always" is not delayed (as its routes); the second side-chain gets its own delay
    s.mixer[1].effects.clear();
    s.mixer[2].effects[1].sidechain2 = 1;
    plan = computePdc (s, { 0, 0 }, { {}, {}, { 30, 0 } }, 48000.0);
    CHECK (plan.sideDelay[2][1][1] == 30);
    s.mixer[1].latencyBypass = LatencyBypass::always;
    plan = computePdc (s, { 0, 0 }, { {}, {}, { 30, 0 } }, 48000.0);
    CHECK (plan.sideDelay[2][1][0] == 0 && plan.sideDelay[2][1][1] == 0);
    s.mixer[1].latencyBypass = LatencyBypass::never;

    // FL's Automatic off: no plugin latency counts, nothing waits
    s.autoPdc = false;
    plan = computePdc (s, { 0, 0 }, { {}, {}, { 30, 0 } }, 48000.0);
    CHECK (plan.sideDelay[2][1][0] == 0 && ! plan.any());
    s.autoPdc = true;

    // on the Master: a side-chain from insert 1 behind a 50-sample slot of the Master
    auto m = pdc190hq::impulses (1);
    m.mixer[0].effects.push_back (pdc190hq::plugin ("latent50"));
    m.mixer[0].effects.push_back (pdc190hq::plugin ("probe", 1));
    plan = computePdc (m, { 0 }, { { 50, 0 } }, 48000.0);
    CHECK (plan.sideDelay[0][1][0] == 50 && plan.total == 50);

    // the fader's lag: the insert's alignment plus its effects (the manual latency delays nothing inside it)
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent100"));
    s.mixer[1].latencyMs = 1.0f; // 48 samples
    plan = computePdc (s, { 0, 0 }, { {}, { 100 }, { 30, 0 } }, 48000.0);
    CHECK (plan.outLat[1] == 148 && plan.levelLag[1] == 100 && plan.levelLag[2] == plan.outLat[2]);
}

TEST (pdc190hq_side_chain_lines_up_when_rendered)
{
    using namespace lynx;
    // the compressor sees the kick when it sees the bass's own hit: a latent source…
    auto s = pdc190hq::impulses (2);
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent100"));
    s.mixer[2].effects.push_back (pdc190hq::plugin ("probe", 1));
    auto seen = pdc190hq::run (s);
    CHECK (seen.main >= 100 && seen.side == seen.main);

    // …a latent slot before the compressor…
    s.mixer[1].effects.clear();
    s.mixer[2].effects.insert (s.mixer[2].effects.begin(), pdc190hq::plugin ("latent30"));
    seen = pdc190hq::run (s);
    CHECK (seen.main >= 30 && seen.side == seen.main);

    // …the two, the second side input as well, and a side delay (230 samples) across the 256-sample blocks
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent100"));
    s.mixer[2].effects.insert (s.mixer[2].effects.begin(), pdc190hq::plugin ("latent300"));
    s.mixer[2].effects.back().sidechain2 = 1;
    seen = pdc190hq::run (s);
    CHECK (seen.main >= 330 && seen.side == seen.main && seen.side2 == seen.main);

    // FL's Automatic off: nothing is compensated (the side-chain arrives as it comes)
    s.autoPdc = false;
    seen = pdc190hq::run (s);
    CHECK (seen.main >= 330 && seen.side >= 0 && seen.side < seen.main);
}

TEST (pdc190hq_fader_and_route_automation_follow_their_audio)
{
    using namespace lynx;
    // insert 1: a 4800-sample plugin (0.2 beat at 120 BPM / 48 kHz) under a fader ramp over beat 1
    auto s = pdc190hq::impulses (1);
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.mixer[1].effects.push_back (pdc190hq::plugin ("latent4800"));
    s.setRoute (1, 2, true);
    for (const auto& target : { AutomationTarget { AutomationTargetKind::mixerVolume, 1 }, AutomationTarget { AutomationTargetKind::mixerSend, 1, 2 },
                                AutomationTarget { AutomationTargetKind::mixerVolume, 0 } })
    {
        Automation a;
        a.target = target;
        a.points = { { 0, 0.0f }, { ppq, 1.0f } };
        s.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) s.automations.size() - 1;
        c.track = (int) s.automations.size();
        c.start = 0;
        c.length = 4 * ppq;
        s.playlist.push_back (c);
    }
    CHECK (Song {}.compensateMixerLevels); // (a new project: on)
    auto value = [&] (bool levels, bool automations, int automation, int track) {
        s.compensateMixerLevels = levels;
        s.compensateAutomations = automations;
        auto state = RackState::build (s, nullptr, 48000.0, pdc190hq::factories (std::make_shared<pdc190hq::Probe::Seen>()));
        RackRenderer r;
        r.prepare (48000.0, 256);
        std::vector<float> l (256), rr (256);
        r.process (state.get(), true, 0.5 * ppq, 256, l.data(), rr.data()); // half a beat in: the ramp is at 0.5
        return r.automatedLevel (*state, automation, 0.75f, track);
    };
    CHECK (std::abs (value (false, true, 0, 1) - 0.5f) < 0.01f); // off: as before (FL's scope; the block's last chunk)
    CHECK (std::abs (value (true, true, 0, 1) - 0.3f) < 0.02f);  // the fader of insert 1: 0.2 beat earlier
    CHECK (std::abs (value (true, true, 1, 1) - 0.3f) < 0.02f);  // its route to insert 2: the same
    CHECK (std::abs (value (true, false, 0, 1) - 0.5f) < 0.01f); // under FL's Compensate automations only
    // the Master's fader: as late as everything (insert 1's 4800 samples reach it)
    CHECK (std::abs (value (true, true, 2, 0) - 0.3f) < 0.02f);
    s.autoPdc = false;
    CHECK (std::abs (value (true, true, 0, 1) - 0.5f) < 0.01f);
}

TEST (pdc190hq_render_ahead_null_with_side_chains_and_level_automation)
{
    using namespace lynx;
    // LYNX's own latent effects (Emphasizer 2 ms, Transporter 5 ms): a side-chained compressor behind one of them, its
    // source behind the other, an automated fader and route, rendered real-time and rendered ahead
    Song song;
    song.bpm = 128.0;
    song.mode = PlayMode::song;
    song.mixer = Song::defaultMixer (4);
    song.mixer[1].effects.push_back (makeEffectSlot (EffectKind::emphasizer));
    song.mixer[2].effects.push_back (makeEffectSlot (EffectKind::flTransporter));
    auto comp = makeEffectSlot (EffectKind::compressor);
    comp.sidechain = 1;
    song.mixer[2].effects.push_back (comp);
    song.mixer[1].sends.push_back ({ 3, 0.5f, false });
    for (int c = 0; c < 3; ++c)
    {
        ChannelDef ch;
        ch.instrument = InstrumentKind::synth;
        ch.volume = 0.5f;
        ch.mixerTrack = c + 1;
        song.channels.push_back (ch);
        auto& p = song.patterns.emplace_back();
        for (int e = 0; e < 8; ++e)
        {
            Note n;
            n.channel = c;
            n.start = (std::int64_t) (e * 0.5 * ppq) + 30 * c;
            n.length = ppq / 4;
            n.key = 40 + 7 * c + e;
            n.velocity = 0.9f;
            p.notes.push_back (n);
        }
        Clip clip;
        clip.pattern = c;
        clip.track = c;
        clip.start = 0;
        clip.length = 4 * ppq;
        song.playlist.push_back (clip);
    }
    for (const auto& target : { AutomationTarget { AutomationTargetKind::mixerVolume, 2 }, AutomationTarget { AutomationTargetKind::mixerSend, 1, 3 } })
    {
        Automation a;
        a.target = target;
        a.points = { { 0, 0.2f }, { 2 * ppq, 0.9f }, { 4 * ppq, 0.1f } };
        song.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) song.automations.size() - 1;
        c.track = 3 + (int) song.automations.size();
        c.start = 0;
        c.length = 4 * ppq;
        song.playlist.push_back (c);
    }
    {
        auto check = RackState::build (song, nullptr, 44100.0);
        CHECK (check->pdc.total > 0 && check->pdc.sideDelay[2][1][0] > 0); // (the side-chain waits for the Transporter)
    }

    const int blocks = 700, block = 64;
    auto callback = [&] (auto&& render, std::vector<float>& left, std::vector<float>& right) {
        renderahead::TransportModel model;
        MarkerPlan markers;
        model.sampleRate = 44100.0;
        model.deviceBlock = block;
        model.songMode = false;
        model.markers = &markers;
        left.assign ((std::size_t) blocks * block, 0.0f);
        right.assign (left.size(), 0.0f);
        double position = 0.0;
        for (int k = 0; k < blocks; ++k)
            for (int done = 0; done < block;)
            {
                const auto tps = renderahead::transportTps (song.bpm, 44100.0);
                const auto sb = renderahead::planSubBlock (model, position, done, tps);
                model.offset = done;
                const auto at = (std::size_t) k * block + (std::size_t) done;
                render (model, sb.start, sb.samples, tps, left.data() + at, right.data() + at);
                position = sb.next;
                done += sb.samples;
            }
    };

    std::vector<float> l0, r0;
    {
        auto state = RackState::build (song, nullptr, 44100.0);
        RackRenderer renderer;
        renderer.prepare (44100.0, 2048);
        LiveEventQueue live;
        MeterBank meters;
        callback ([&] (renderahead::TransportModel&, double start, int n, double, float* a, float* b) {
            renderer.process (state.get(), true, start, n, a, b, &live, &meters, nullptr);
        }, l0, r0);
    }
    for (const auto& liveSet : std::vector<std::vector<int>> { {}, { 0 }, { 1 } }) // nothing live, the source live, the listener live
    {
        RackRenderer renderer;
        SnapshotExchange<RackState> exchange;
        renderahead::Targets targets;
        MeterBank meters;
        LiveEventQueue live, midi;
        RenderAhead ahead { renderer };
        renderer.prepare (44100.0, 2048);
        ahead.prepare (44100.0, 2048);
        ahead.setTargets (&targets);
        ahead.setAlwaysEngage (true);
        targets.publish (liveSet.data(), (int) liveSet.size(), nullptr, 0);
        exchange.publish (RackState::build (song, nullptr, 44100.0));
        std::vector<float> l1, r1;
        callback ([&] (renderahead::TransportModel& model, double start, int n, double tps, float* a, float* b) {
            renderahead::Block blk;
            blk.playing = true;
            blk.startTick = start;
            blk.samples = n;
            blk.tps = tps;
            blk.model = &model;
            ahead.process (exchange, blk, a, b, &live, &meters, &midi);
        }, l1, r1);
        const auto same = l0.size() == l1.size() && std::memcmp (l0.data(), l1.data(), l0.size() * sizeof (float)) == 0
                          && std::memcmp (r0.data(), r1.data(), r0.size() * sizeof (float)) == 0;
        CHECK (same);
        CHECK (ahead.stats().engagements.load() >= 1 && ahead.stats().splitMismatches.load() == 0);
        if (! same)
            std::printf ("  pdc190hq render-ahead null failed with %d live channel(s)\n", (int) liveSet.size());
    }
}
