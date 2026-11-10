// Round 190aq: three audio-path extras, each off by default — pre-fader sends, "Low latency while recording" and the
// Browser's "Preview in time". Off, a song renders what it rendered before (the render-hash tests cover old projects).

namespace audiox190aq
{
// channel 0 plays a DC of 0.25 on insert 1, which sends to insert 2; insert 2 goes to the Master
inline std::vector<float> renderSend (const std::function<void (lynx::Song&)>& tweak, int blocks = 16)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (3);
    s.mixer[1].sends.push_back ({ 2, 1.0f });
    s.mixer[1].toMaster = false;
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = "dc25";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    tweak (s);
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (48000, 0.25f);
        return std::shared_ptr<const SampleData> (d);
    };
    auto st = RackState::build (s, nullptr, 48000.0, provider);
    RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    LiveEventQueue live;
    NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> out, l (256), r (256);
    for (int block = 0; block < blocks; ++block)
    {
        renderer.process (st.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

inline float peakOfTail (const std::vector<float>& x)
{
    float p = 0.0f;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}
} // namespace audiox190aq

TEST (audiox190aq_pre_fader_send_ignores_the_fader)
{
    using namespace lynx;
    const auto post = audiox190aq::renderSend ([] (Song&) {});
    CHECK (audiox190aq::peakOfTail (post) > 0.05f);

    // post-fader (FL, the default): the send follows the insert's fader
    const auto postDown = audiox190aq::renderSend ([] (Song& s) { s.mixer[1].volume = 0.0f; });
    CHECK (audiox190aq::peakOfTail (postDown) == 0.0f);

    // pre-fader: the same samples as the post-fader send at unity, whatever the fader, pan or separation
    const auto pre = audiox190aq::renderSend ([] (Song& s) { s.setSendPre (1, 2, true); });
    CHECK (pre == post);
    const auto preDown = audiox190aq::renderSend ([] (Song& s) {
        s.setSendPre (1, 2, true);
        s.mixer[1].volume = 0.0f;
        s.mixer[1].pan = -1.0f;
        s.mixer[1].stereo = 0.5f;
    });
    CHECK (preDown == post);

    // the send's own level still applies, and the insert's mute silences it
    const auto half = audiox190aq::renderSend ([] (Song& s) {
        s.setSendPre (1, 2, true);
        s.setRouteLevel (1, 2, 0.5f);
        s.mixer[1].volume = 0.0f;
    });
    CHECK (std::abs (audiox190aq::peakOfTail (half) - 0.5f * audiox190aq::peakOfTail (post)) < 1.0e-6f);
    const auto muted = audiox190aq::renderSend ([] (Song& s) {
        s.setSendPre (1, 2, true);
        s.mixer[1].muted = true;
    });
    CHECK (audiox190aq::peakOfTail (muted) == 0.0f);
    // a solo elsewhere silences it too
    const auto soloed = audiox190aq::renderSend ([] (Song& s) {
        s.setSendPre (1, 2, true);
        s.mixer.push_back (MixerTrack {});
        s.mixer.back().solo = true;
    });
    CHECK (audiox190aq::peakOfTail (soloed) == 0.0f);
}

TEST (audiox190aq_pre_fader_send_keeps_the_post_fader_route)
{
    using namespace lynx;
    // one insert with a pre-fader send and its post-fader route to Master: the Master route follows the fader, the
    // send does not; a second, post-fader send of the same insert follows it too
    auto build = [] (float fader, bool pre) {
        return audiox190aq::renderSend ([fader, pre] (Song& s) {
            s.mixer = Song::defaultMixer (4);
            s.mixer[1].sends = { { 2, 1.0f }, { 3, 1.0f } };
            s.mixer[1].sends[0].pre = pre;
            s.mixer[1].toMaster = true;
            s.mixer[1].volume = fader;
            s.mixer[2].masterLevel = 0.5f;  // the pre send's return: 0.5
            s.mixer[3].masterLevel = 0.25f; // the post send's return: 0.25
        });
    };
    const auto unity = audiox190aq::peakOfTail (build (unityFader, false)); // 1 + 0.5 + 0.25 of the signal
    const auto signal = unity / 1.75f;
    CHECK (signal > 0.05f);
    CHECK (std::abs (audiox190aq::peakOfTail (build (unityFader, true)) - unity) < 1.0e-6f);
    CHECK (std::abs (audiox190aq::peakOfTail (build (0.0f, true)) - 0.5f * signal) < 1.0e-6f);
    CHECK (audiox190aq::peakOfTail (build (0.0f, false)) == 0.0f);
}

TEST (audiox190aq_pre_fader_send_model)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (4);
    s.setRoute (1, 2, true);
    CHECK (! s.sendPre (1, 2)); // FL's post-fader send is the default
    s.setSendPre (1, 2, true);
    CHECK (s.sendPre (1, 2) && ! s.sendPre (1, 3) && ! s.sendPre (1, 0));
    s.setSendPre (1, 0, true); // the route to Master has no switch
    s.setSendPre (1, 3, true); // nor a send that does not exist
    CHECK (! s.sendPre (1, 0) && ! s.sendPre (1, 3) && s.mixer[1].sends.size() == 1);
    CHECK (! (s.mixer[1].sends[0] == MixerSend { 2, 1.0f })); // (undo and live sessions see the switch)
    s.setRouteLevel (1, 2, 0.5f);
    CHECK (s.sendPre (1, 2)); // the level knob keeps it

    // the snapshot: one tap per insert with pre-fader sends, shared by its sends
    s.setRoute (1, 3, true);
    s.setSendPre (1, 3, true);
    auto st = RackState::build (s, nullptr, 48000.0, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    CHECK (st->preTap.size() == 4 && st->preTap[1] == 0 && st->preTap[2] == -1 && st->sendTaps.size() == 1);
    CHECK (st->mixerInputs[2].size() == 1 && st->mixerInputs[2][0].pre && st->mixerInputs[2][0].tap == 0);
    CHECK (st->mixerInputs[3].size() == 1 && st->mixerInputs[3][0].pre && st->mixerInputs[3][0].tap == 0);
    // the PDC plan has the same edges as with post-fader sends
    Song post = s;
    post.setSendPre (1, 2, false);
    post.setSendPre (1, 3, false);
    auto plain = RackState::build (post, nullptr, 48000.0, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    CHECK (plain->sendTaps.empty() && plain->pdc.sendDelay == st->pdc.sendDelay);
}

TEST (audiox190aq_pre_fader_send_in_flp_and_als)
{
    using namespace lynx;
    Song s = Song::makeDefault();
    s.setRoute (1, 5, true);
    s.setRoute (2, 5, true);
    s.setSendPre (1, 5, true);
    s.setSendPre (2, 5, true);

    // .flp: FL has post-fader routes only; the send is written as one and the report says so
    flpexport::Report report;
    const auto flp = flpexport::exportFlp (s, {}, report);
    CHECK (! flp.empty());
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("Pre-fader sends") != std::string::npos; }));
    Song plain = s;
    plain.setSendPre (1, 5, false);
    plain.setSendPre (2, 5, false);
    flpexport::Report plainReport;
    CHECK (flpexport::exportFlp (plain, {}, plainReport) == flp); // the same bytes as the post-fader song
    CHECK (std::none_of (plainReport.notes.begin(), plainReport.notes.end(), [] (const std::string& n) { return n.find ("Pre-fader") != std::string::npos; }));

    // .als: Live's Pre / Post toggle is per return; every send into the return is pre-fader, so it is "Pre"
    const auto out = exportAls (s);
    CHECK (out.xml.find ("<SendPreBool Id=\"0\" Value=\"true\"") != std::string::npos);
    CHECK (exportAls (plain).xml.find ("<SendPreBool Id=\"0\" Value=\"true\"") == std::string::npos);
    const auto back = importAls (out.bytes, als181c::options());
    CHECK (back.has_value());
    if (back)
    {
        int pre = 0, post = 0;
        for (const auto& m : back->mixer)
            for (const auto& send : m.sends)
                (send.pre ? pre : post)++;
        CHECK (pre == 2); // Live's "Pre" comes back as LYNX's pre-fader sends
    }
    // a return fed both ways stays "Post" in Live, with a note
    Song mixed = s;
    mixed.setSendPre (2, 5, false);
    const auto mixedOut = exportAls (mixed);
    CHECK (mixedOut.xml.find ("<SendPreBool Id=\"0\" Value=\"true\"") == std::string::npos);
    CHECK (std::any_of (mixedOut.notes.begin(), mixedOut.notes.end(), [] (const std::string& n) { return n.find ("Pre-fader sends") != std::string::npos; }));
}

// --- "Low latency while recording" (core/LowLatency.h) ---------------------------------------------------------

namespace audiox190aq
{
// a plugin stand-in that delays by `latency` samples and reports it (a look-ahead effect)
class Latent final : public lynx::Effect
{
public:
    explicit Latent (int samples) : latency (samples), line (samples) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override
    {
        line.clear();
        ++resets;
    }
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override
    {
        line.process (l, r, n);
        ++blocks;
    }
    int latencySamples() const noexcept override { return latency; }
    int blocks = 0, resets = 0;

private:
    int latency;
    lynx::DelayLine line;
};

inline lynx::EffectSlot latentSlot (const char* id)
{
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = id;
    return fx;
}

// insert 1 (the singer: input 0, a 100-sample plugin and an EQ) → insert 2 (a bus with a 40-sample plugin) → Master
// (a 60-sample plugin); insert 3 (a 25-sample plugin, not on that path) → Master. One impulse channel on each of 1, 3.
inline lynx::Song lowLatencySong()
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (5);
    s.mixer[1].inputChannel = 0;
    s.mixer[1].recordSource = RecordSource::externalInput;
    s.mixer[1].effects.push_back (latentSlot ("lat100"));
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::eq));
    s.mixer[1].toMaster = false;
    s.mixer[1].sends.push_back ({ 2, 1.0f });
    s.mixer[2].effects.push_back (latentSlot ("lat40"));
    s.mixer[0].effects.push_back (latentSlot ("lat60"));
    s.mixer[3].effects.push_back (latentSlot ("lat25"));
    for (const auto insert : { 1, 3 })
    {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = "impulse";
        c.volume = 1.0f;
        c.mixerTrack = insert;
        s.channels.push_back (c);
    }
    s.patterns.emplace_back();
    return s;
}

inline lynx::RackState::Factories lowLatencyFactories (std::vector<char> tracks)
{
    lynx::RackState::Factories f;
    f.samples = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (64, 0.0f);
        d->left[0] = 1.0f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    f.pluginEffects = [] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Effect> {
        return std::make_shared<Latent> (std::atoi (ref.id.c_str() + 3));
    };
    f.lowLatencyTracks = std::move (tracks);
    return f;
}

// where channel `channel`'s impulse comes out of the Master (the first sample above 0.01), −1 = nowhere in 512
inline int impulseAt (const lynx::RackState& state, int channel)
{
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = channel;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> l (256), r (256);
    for (int block = 0; block < 2; ++block)
    {
        renderer.process (&state, false, 0.0, 256, l.data(), r.data(), &live);
        for (int i = 0; i < 256; ++i)
            if (std::abs (l[(std::size_t) i]) > 0.01f)
                return block * 256 + i;
    }
    return -1;
}
} // namespace audiox190aq

TEST (audiox190aq_low_latency_chooses_the_live_path)
{
    using namespace lynx;
    const auto s = audiox190aq::lowLatencySong();
    using Tracks = std::vector<char>;

    // nothing armed or monitored: nothing bypassed
    CHECK (lowlatency::bypassTracks (s, {}).empty());
    CHECK (lowlatency::bypassTracks (s, { 3 }).empty()); // armed without an audio input: not a live input
    CHECK (! lowlatency::isLive (s, 0, { 0 }) && ! lowlatency::isLive (s, 9, { 9 }));

    // insert 1 armed (it records "External input only"): itself, its bus and the Master; insert 3 and 4 are not on its path
    CHECK (lowlatency::bypassTracks (s, { 1 }) == (Tracks { 1, 1, 1, 0, 0 }));

    // monitoring "On" does the same without the arm switch; the other modes need the insert armed
    {
        auto m = s;
        m.mixer[1].monitor = InputMonitor::on;
        CHECK (lowlatency::bypassTracks (m, {}) == (Tracks { 1, 1, 1, 0, 0 }));
        m.mixer[1].monitor = InputMonitor::off;
        CHECK (lowlatency::bypassTracks (m, {}).empty());
        m.mixer[1].monitor = InputMonitor::whenArmed;
        CHECK (lowlatency::bypassTracks (m, {}).empty());
        CHECK (lowlatency::bypassTracks (m, { 1 }) == (Tracks { 1, 1, 1, 0, 0 }));
    }

    // what is recorded never changes: a take tapped after the slots keeps the insert's own slots (FL's default "Post EQ")
    for (const auto source : { RecordSource::postEffects, RecordSource::postEq, RecordSource::postLevel, RecordSource::postTrack })
    {
        auto m = s;
        m.mixer[1].recordSource = source;
        CHECK (lowlatency::bypassTracks (m, { 1 }) == (Tracks { 1, 0, 1, 0, 0 }));
    }
    {
        auto m = s;
        m.mixer[1].recordSource = RecordSource::externalAndMixer; // tapped before its slots
        CHECK (lowlatency::bypassTracks (m, { 1 }) == (Tracks { 1, 1, 1, 0, 0 }));
    }
    // ... and an armed bus that records what insert 1 sends it keeps insert 1's slots (and its own when tapped after them)
    {
        auto m = s;
        m.mixer[2].inputChannel = 1;
        m.mixer[2].recordSource = RecordSource::externalAndMixer;
        CHECK (lowlatency::bypassTracks (m, { 1, 2 }) == (Tracks { 1, 0, 1, 0, 0 }));
        m.mixer[2].recordSource = RecordSource::postEq;
        CHECK (lowlatency::bypassTracks (m, { 1, 2 }) == (Tracks { 1, 0, 0, 0, 0 }));
        m.mixer[2].recordSource = RecordSource::externalInput; // the bus's take is its device input alone
        CHECK (lowlatency::bypassTracks (m, { 1, 2 }) == (Tracks { 1, 1, 1, 0, 0 }));
    }
    // a level-0 send (a side-chain link) is not a path
    {
        auto m = s;
        m.mixer[1].sends.push_back ({ 4, 0.0f });
        CHECK (lowlatency::bypassTracks (m, { 1 }) == (Tracks { 1, 1, 1, 0, 0 }));
        m.mixer[1].sends.back().level = 0.5f;
        CHECK (lowlatency::bypassTracks (m, { 1 }) == (Tracks { 1, 1, 1, 0, 1 }));
    }
}

TEST (audiox190aq_low_latency_pdc_before_during_after)
{
    using namespace lynx;
    const auto s = audiox190aq::lowLatencySong();

    // before: the singer's path is 100 + 40 + 60 = 200 samples late, insert 3's path waits for it
    auto before = RackState::build (s, nullptr, 48000.0, audiox190aq::lowLatencyFactories ({}));
    CHECK (before->pdc.total == 200 && before->pdc.outLat[1] == 100 && before->pdc.outLat[2] == 140);
    CHECK (before->pdc.sendDelay[3][0] == 115); // insert 3 (25) lines up with the bus (140) at the Master
    CHECK (before->lowLatencySlots.empty());
    CHECK (audiox190aq::impulseAt (*before, 0) == 200 && audiox190aq::impulseAt (*before, 1) == 200);

    // during: insert 1 is armed — the latency-adding slots on its path (insert 1, the bus, the Master) are bypassed and
    // the plan is made without them; insert 3's own plugin stays, and nothing waits for it but the Master's input
    const auto tracks = lowlatency::bypassTracks (s, { 1 });
    auto during = RackState::build (s, before.get(), 48000.0, audiox190aq::lowLatencyFactories (tracks));
    CHECK (during->pdc.total == 25 && during->pdc.outLat[1] == 0 && during->pdc.outLat[2] == 0 && during->pdc.outLat[3] == 25);
    CHECK (during->pdc.sendDelay[2][0] == 25 && during->pdc.sendDelay[3][0] == 0);
    CHECK (during->lowLatencySlots.size() == 5 && during->lowLatencySlots[1] == 1 && during->lowLatencySlots[2] == 1
           && during->lowLatencySlots[0] == 1 && during->lowLatencySlots[3] == 0);
    CHECK (lowlatency::bypassed (during->lowLatencySlots, 1, 0) && ! lowlatency::bypassed (during->lowLatencySlots, 1, 1)); // the EQ (no latency) stays
    CHECK (during->lowLatencySaved[1][0] == 100 && during->lowLatencySaved[2][0] == 40 && during->lowLatencySaved[0][0] == 60);
    CHECK (during->currentLatencies() == during->latencies); // (no re-plan loop: the bypassed slots count as 0)
    // the same effect instances (nothing is rebuilt or unloaded), and the song in the snapshot is the song
    CHECK (during->effects[1][0] == before->effects[1][0] && during->effects[0][0] == before->effects[0][0]);
    CHECK (during->song.mixer == s.mixer && during->song.mixer[1].effects[0].enabled);
    // heard: the singer's path only waits for insert 3's alignment (25); the bypassed plugins did not run
    const auto* own = dynamic_cast<audiox190aq::Latent*> (during->effects[1][0].get());
    CHECK (own != nullptr);
    const auto ran = own != nullptr ? own->blocks : -1;
    CHECK (audiox190aq::impulseAt (*during, 0) == 25 && audiox190aq::impulseAt (*during, 1) == 25);
    CHECK (own != nullptr && own->blocks == ran);

    // after (disarmed): the plan and the sound are what they were
    auto after = RackState::build (s, during.get(), 48000.0, audiox190aq::lowLatencyFactories ({}));
    CHECK (after->pdc.total == 200 && after->pdc.sendDelay == before->pdc.sendDelay && after->pdc.channelDelay == before->pdc.channelDelay);
    CHECK (after->lowLatencySlots.empty() && after->latencies == before->latencies);
    CHECK (audiox190aq::impulseAt (*after, 0) == 200 && audiox190aq::impulseAt (*after, 1) == 200);

    // a slot switched off by the user, or one without latency, is never touched; an insert off the path neither
    {
        auto m = s;
        m.mixer[1].effects[0].enabled = false;
        auto st = RackState::build (m, nullptr, 48000.0, audiox190aq::lowLatencyFactories (lowlatency::bypassTracks (m, { 1 })));
        CHECK (st->lowLatencySlots[1] == 0 && st->lowLatencySlots[2] == 1);
    }
    // nothing with latency on the path: no bypass at all
    {
        auto m = s;
        m.mixer[1].effects.erase (m.mixer[1].effects.begin());
        m.mixer[2].effects.clear();
        m.mixer[0].effects.clear();
        auto st = RackState::build (m, nullptr, 48000.0, audiox190aq::lowLatencyFactories (lowlatency::bypassTracks (m, { 1 })));
        CHECK (st->lowLatencySlots.empty() && st->pdc.total == 25);
    }
}

TEST (audiox190aq_low_latency_slot_restarts_clean)
{
    using namespace lynx;
    const auto s = audiox190aq::lowLatencySong();
    auto before = RackState::build (s, nullptr, 48000.0, audiox190aq::lowLatencyFactories ({}));
    auto during = RackState::build (s, before.get(), 48000.0, audiox190aq::lowLatencyFactories (lowlatency::bypassTracks (s, { 1 })));
    auto after = RackState::build (s, during.get(), 48000.0, audiox190aq::lowLatencyFactories ({}));
    auto* own = dynamic_cast<audiox190aq::Latent*> (before->effects[1][0].get());
    CHECK (own != nullptr);
    if (own == nullptr)
        return;

    // one renderer through the three snapshots, as the engine's: a slot that was skipped is reset when it runs again
    RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    LiveEventQueue live;
    std::vector<float> l (256), r (256);
    auto hit = [&live] {
        NoteEvent on;
        on.channel = 0;
        on.key = 60;
        on.velocity = 1.0f;
        live.push (on);
    };
    hit();
    renderer.process (before.get(), false, 0.0, 256, l.data(), r.data(), &live);
    CHECK (own->resets == 0 && own->blocks > 0);
    hit();
    for (int i = 0; i < 4; ++i)
        renderer.process (during.get(), false, 0.0, 256, l.data(), r.data(), &live);
    const auto ran = own->blocks;
    CHECK (own->resets == 0);
    hit();
    for (int i = 0; i < 4; ++i)
        renderer.process (after.get(), false, 0.0, 256, l.data(), r.data(), &live);
    CHECK (own->resets == 1 && own->blocks > ran);
}
