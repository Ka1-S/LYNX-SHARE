// --- External audio input per mixer insert (core/LiveInput.h, RackRenderer) - round 88 ------------------------------

TEST (input_sources_pairs_then_monos)
{
    const auto four = lynx::inputSources ({ "In 1", "In 2", "In 3", "In 4" });
    CHECK (four.size() == 6);
    CHECK (four.size() == 6 && four[0].channel == 0 && four[0].stereo && four[0].name == "In 1 - In 2");
    CHECK (four.size() == 6 && four[1].channel == 2 && four[1].stereo && four[1].name == "In 3 - In 4");
    CHECK (four.size() == 6 && four[2].channel == 0 && ! four[2].stereo && four[2].name == "In 1");
    CHECK (four.size() == 6 && four[5].channel == 3 && ! four[5].stereo);

    const auto odd = lynx::inputSources ({ "L", "R", "Mic" }); // the last channel has no pair
    CHECK (odd.size() == 4 && odd[0].name == "L - R" && odd[3].name == "Mic");
    const auto mono = lynx::inputSources ({ "Microphone" });
    CHECK (mono.size() == 1 && mono[0].channel == 0 && ! mono[0].stereo);
    CHECK (lynx::inputSources ({}).empty());

    CHECK (lynx::inputSourceName (-1, true, { "a", "b" }).empty());
    CHECK (lynx::inputSourceName (0, true, { "a", "b" }) == "a - b");
    CHECK (lynx::inputSourceName (1, false, { "a", "b" }) == "b");
    CHECK (lynx::inputSourceName (4, true, { "a", "b" }) == "In 5 - In 6"); // a channel the open device doesn't have
}

TEST (input_monitor_modes)
{
    using lynx::InputMonitor;
    CHECK (! lynx::inputMonitored (InputMonitor::off, true, false, true));
    CHECK (lynx::inputMonitored (InputMonitor::on, false, true, false));
    CHECK (lynx::inputMonitored (InputMonitor::whenArmed, true, true, false) && ! lynx::inputMonitored (InputMonitor::whenArmed, false, false, true));
    // auto: armed and (recording or stopped); playback without recording plays the take, not the input
    CHECK (lynx::inputMonitored (InputMonitor::automatic, true, false, false));
    CHECK (lynx::inputMonitored (InputMonitor::automatic, true, true, true));
    CHECK (! lynx::inputMonitored (InputMonitor::automatic, true, true, false));
    CHECK (! lynx::inputMonitored (InputMonitor::automatic, false, false, false));
    CHECK (lynx::MixerTrack {}.monitor == InputMonitor::whenArmed && lynx::MixerTrack {}.inputChannel == -1); // FL's default (round 190ev)

    lynx::MixerTrack t;
    t.inputChannel = 1;
    t.inputStereo = true;
    CHECK (lynx::inputChannelsOf (t, 4) == std::make_pair (1, 2));
    CHECK (lynx::inputChannelsOf (t, 2) == std::make_pair (1, 1)); // the pair's second channel isn't there: mono
    CHECK (lynx::inputChannelsOf (t, 1).second == 0);
    t.inputStereo = false;
    CHECK (lynx::inputChannelsOf (t, 4) == std::make_pair (1, 1));
}

TEST (input_reaches_insert_and_meters)
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mixer = lynx::Song::defaultMixer (4);
    song.mixer[2].inputChannel = 0; // stereo pair 1-2
    song.mixer[3].inputChannel = 2; // mono channel 3, monitoring off
    song.mixer[3].inputStereo = false;
    song.mixer[3].monitor = lynx::InputMonitor::off;
    song.mixer[2].monitor = lynx::InputMonitor::on;
    auto state = lynx::RackState::build (song, nullptr, 48000.0);

    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput input;
    renderer.setLiveInput (&input);

    std::vector<float> a (256, 0.25f), b (256, -0.5f), c (256, 0.75f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = b.data();
    input.channels[2] = c.data();
    input.numChannels = 3;

    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data());
    // insert 2 at unity, routed to the Master at unity: the input comes out (left = ch 1, right = ch 2)
    CHECK (std::abs (l[100] - 0.25f) < 1e-3f && std::abs (r[100] + 0.5f) < 1e-3f);
    CHECK (std::abs (input.meters.takePeak (2, 0) - 0.25f) < 1e-6f && std::abs (input.meters.takePeak (2, 1) - 0.5f) < 1e-6f);
    CHECK (std::abs (input.meters.takePeak (3, 0) - 0.75f) < 1e-6f); // metered even though it isn't heard
    CHECK (input.meters.takePeak (1, 0) == 0.0f);

    // auto monitoring: heard while armed and stopped, not during playback without recording
    song.mixer[2].monitor = lynx::InputMonitor::automatic;
    auto autoState = lynx::RackState::build (song, state.get(), 48000.0);
    renderer.process (autoState.get(), false, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200]) < 1e-6f);
    input.armed[2].store (true);
    renderer.process (autoState.get(), false, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200] - 0.25f) < 1e-3f);
    renderer.process (autoState.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200]) < 1e-6f);
    input.recording.store (true);
    renderer.process (autoState.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200] - 0.25f) < 1e-3f);

    // the input goes through the insert's fader: muted insert, nothing out
    song.mixer[2].muted = true;
    auto muted = lynx::RackState::build (song, autoState.get(), 48000.0);
    // (the fader glides at most 0.001 per sample, REA_rodada85 §3: silent after 1000 samples)
    for (int block = 0; block < 5; ++block)
        renderer.process (muted.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200]) < 1e-6f);

    // no input open: nothing happens
    input.numChannels = 0;
    song.mixer[2].muted = false;
    auto none = lynx::RackState::build (song, muted.get(), 48000.0);
    renderer.process (none.get(), true, 0.0, 256, l.data(), r.data());
    renderer.process (none.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200]) < 1e-6f);
}
