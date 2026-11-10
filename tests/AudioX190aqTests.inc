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
