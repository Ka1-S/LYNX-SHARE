// Round 190aq: three audio-path extras, each off by default — pre-fader sends, "Low latency while recording" and the
// Browser's "Preview in time". Off, a song renders what it rendered before (the render-hash tests cover old projects).

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/FlpExport.h"
#include "core/LowLatency.h"

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

// --- the Browser's "Preview in time" (core/PreviewTempo.h) -----------------------------------------------------

#include "core/PreviewTempo.h"

namespace audiox190aq
{
// a drum-like loop of `beats` beats at `bpm`: a kick on each beat, a noise hat between, over a quiet tone
inline std::shared_ptr<lynx::SampleData> drumLoop (double bpm, int beats, double rate = 44100.0, double tailSeconds = 0.0)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    const auto n = (std::size_t) std::llround (((double) beats * 60.0 / bpm + tailSeconds) * rate);
    d->left.assign (n, 0.0f);
    std::uint32_t seed = 7;
    const auto beat = 60.0 / bpm * rate;
    for (int b = 0; b < beats * 2; ++b)
    {
        const auto start = (std::size_t) std::llround ((double) b * beat * 0.5);
        const bool kick = b % 2 == 0;
        for (std::size_t i = 0; i < (std::size_t) (0.12 * rate) && start + i < n; ++i)
        {
            const auto t = (double) i / rate;
            seed = seed * 1664525u + 1013904223u;
            const auto noise = (float) (seed >> 8) / 8388608.0f - 1.0f;
            d->left[start + i] += kick ? (float) (0.8 * std::exp (-t * 30.0) * std::sin (6.283185307179586 * (50.0 + 80.0 * std::exp (-t * 40.0)) * t))
                                       : 0.25f * noise * (float) std::exp (-t * 90.0);
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        d->left[i] += 0.1f * (float) std::sin (6.283185307179586 * 110.0 * (double) i / rate);
    return d;
}
} // namespace audiox190aq

TEST (audiox190aq_preview_tempo_from_the_name)
{
    using namespace lynx;
    using previewtempo::nameTempo;
    // "bpm" beside a number, either side, any case; the file must be a beat long or more
    CHECK (nameTempo ("Break Scatty 174 bpm.wav", 4.0) == 174.0);
    CHECK (nameTempo ("loop_128BPM.wav", 7.5) == 128.0);
    CHECK (nameTempo ("BPM_98 groove.flac", 4.0) == 98.0);
    CHECK (nameTempo ("bpm140-top.wav", 3.0) == 140.0);
    CHECK (nameTempo ("Kick 120 bpm.wav", 0.3) == 0.0); // a hit cut from a 120 BPM kit
    CHECK (nameTempo ("Riser 500bpm.wav", 4.0) == 0.0); // out of range
    // a bare number: only when the file is that many whole beats (two or more)
    CHECK (nameTempo ("Loop_128_Am.wav", 7.5) == 128.0);   // 16 beats
    CHECK (nameTempo ("Drums 120 A.wav", 2.0) == 120.0);   // 4 beats
    CHECK (nameTempo ("Snare 100.wav", 0.35) == 0.0);      // a one-shot with a number
    CHECK (nameTempo ("Pad 100.wav", 3.1) == 0.0);         // 5.17 beats: not a loop at that tempo
    CHECK (nameTempo ("808 Sub.wav", 2.0) == 0.0 && nameTempo ("Dm7 chord 4bars.wav", 8.0) == 0.0);
    CHECK (nameTempo ("Vocal take 12.wav", 8.0) == 0.0);

    // ACID first, then the name; a file longer than a loop has none
    SampleData s;
    s.sampleRate = 48000.0;
    s.left.assign (96000, 0.0f); // 2 s
    s.acidBeats = 4.0f;
    auto found = previewtempo::quick (s, "Loop 90 bpm.wav");
    CHECK (found.source == previewtempo::Source::acid && std::abs (found.bpm - 120.0) < 1.0e-9);
    s.acidBeats = 0.0f;
    found = previewtempo::quick (s, "Loop 90 bpm.wav");
    CHECK (found.source == previewtempo::Source::name && found.bpm == 90.0);
    found = previewtempo::quick (s, "Loop.wav");
    CHECK (found.source == previewtempo::Source::none && found.bpm == 0.0);
    s.acidBeats = 0.25f; // 7.5 BPM: not a usable tempo
    CHECK (previewtempo::quick (s, "Loop.wav").bpm == 0.0);
    s.left.assign ((std::size_t) (48000 * 80), 0.0f); // 80 s: a song or a take
    s.acidBeats = 160.0f;
    CHECK (previewtempo::quick (s, "Song 120 bpm.wav").bpm == 0.0 && ! previewtempo::worthDetecting (s));
    s.left.assign (48000, 0.0f); // 1 s: too short to analyse
    CHECK (! previewtempo::worthDetecting (s));
}

TEST (audiox190aq_preview_tempo_detected_only_for_loops)
{
    using namespace lynx;
    // a 4-bar drum loop with no tempo in its name: LYNX's detection finds it
    const auto loop = audiox190aq::drumLoop (120.0, 16);
    const auto found = previewtempo::find (*loop, "Drums.wav");
    CHECK (found.source == previewtempo::Source::detected && std::abs (found.bpm - 120.0) < 0.3);
    const auto slow = audiox190aq::drumLoop (90.0, 16, 48000.0);
    CHECK (std::abs (previewtempo::find (*slow, "Groove.wav").bpm - 90.0) < 0.3);

    // not a loop of whole beats (a tail after it), a decaying tone, silence: no tempo, they play as they are
    const auto tail = audiox190aq::drumLoop (120.0, 8, 44100.0, 0.7);
    CHECK (previewtempo::find (*tail, "Tail.wav").bpm == 0.0);
    SampleData tone;
    tone.sampleRate = 44100.0;
    tone.left.resize (44100 * 3);
    for (std::size_t i = 0; i < tone.left.size(); ++i)
        tone.left[i] = (float) (0.7 * std::exp (-(double) i / 44100.0 * 1.5) * std::sin (6.283185307179586 * 220.0 * (double) i / 44100.0));
    CHECK (previewtempo::find (tone, "Bell.wav").bpm == 0.0);
    SampleData silence;
    silence.sampleRate = 44100.0;
    silence.left.assign (44100 * 4, 0.0f);
    CHECK (previewtempo::find (silence, "Nothing.wav").bpm == 0.0);
}

TEST (audiox190aq_preview_stretch_ratio_and_beat_start)
{
    using namespace lynx;
    using previewtempo::stretchRatio;
    CHECK (std::abs (stretchRatio (120.0, 140.0) - 120.0 / 140.0) < 1.0e-12); // faster: shorter
    CHECK (std::abs (stretchRatio (174.0, 140.0) - 174.0 / 140.0) < 1.0e-12);
    CHECK (std::abs (stretchRatio (90.0, 128.0) - 90.0 / 128.0) < 1.0e-12);   // up to 1.5 times: the project's tempo
    CHECK (stretchRatio (128.0, 128.0) == 1.0 && stretchRatio (128.05, 128.0) == 1.0); // within 0.1 %: untouched
    CHECK (stretchRatio (70.0, 140.0) == 1.0);                                  // half-time: as it is
    CHECK (std::abs (stretchRatio (87.0, 140.0) - 174.0 / 140.0) < 1.0e-12);  // read an octave up
    CHECK (std::abs (stretchRatio (170.0, 80.0) - 85.0 / 80.0) < 1.0e-12);    // read an octave down
    CHECK (stretchRatio (0.0, 140.0) == 1.0 && stretchRatio (120.0, 0.0) == 1.0);
    for (const auto file : { 40.0, 61.0, 87.0, 99.0, 133.0, 174.0, 300.0 })
        for (const auto project : { 60.0, 95.0, 128.0, 140.0, 175.0 })
        {
            const auto r = stretchRatio (file, project);
            CHECK (r >= 1.0 / 1.5 - 1.0e-9 && r <= 1.5 + 1.0e-9);
        }

    // the stretch itself (LYNX's own stretcher): the loop comes out at the project's tempo, the same pitch
    const auto loop = audiox190aq::drumLoop (120.0, 16);
    const auto ratio = stretchRatio (120.0, 140.0);
    const auto out = timeStretch (*loop, ratio, StretchMethod::general);
    CHECK (out != nullptr);
    if (out != nullptr)
    {
        CHECK (std::abs ((double) out->frames() - (double) loop->frames() * ratio) < 4.0);
        CHECK (out->sampleRate == loop->sampleRate);
        CHECK (std::abs (previewtempo::detect (*out).bpm - 140.0) < 0.5); // 16 beats in 6.857 s
    }

    // the start: how long until the next beat is heard (960 ticks per beat; 0.04 ticks per sample = 120 BPM at 48 kHz)
    using previewtempo::samplesToNextBeat;
    CHECK (samplesToNextBeat (0.0, 0.04, 0) == 0.0 && samplesToNextBeat (1920.0, 0.04, 0) == 0.0);
    CHECK (std::abs (samplesToNextBeat (480.0, 0.04, 0) - 12000.0) < 1.0e-6);
    CHECK (std::abs (samplesToNextBeat (959.0, 0.04, 0) - 25.0) < 1.0e-6);
    CHECK (std::abs (samplesToNextBeat (480.0, 0.04, 100) - 12100.0) < 1.0e-6); // what is heard is 100 samples behind
    CHECK (std::abs (samplesToNextBeat (0.0, 0.04, 100) - 100.0) < 1.0e-6);     // (before the song's first beat is heard)
    CHECK (samplesToNextBeat (480.0, 0.0, 0) == 0.0);                             // stopped tempo: now
}
