// REA round 149: live-played notes through the channel's arpeggiator / echo delay / tracking

namespace rea149
{
// a synth channel; a live note played (stopped transport) for `holdSeconds`, rendered for `seconds`: the onsets
// heard (10 ms RMS windows rising past a quarter of the loudest)
int liveOnsets (const lynx::ChannelDef& channel, double seconds, double holdSeconds, int key = 60)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    s.bpm = 120.0;
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::RackRenderer r;
    r.prepare (48000.0, 480);
    lynx::LiveEventQueue live;
    lynx::NoteEvent e;
    e.channel = 0;
    e.key = key;
    e.velocity = 0.8f;
    live.push (e);
    std::vector<double> rms;
    std::vector<float> l (480), rr (480);
    const auto blocks = (int) (seconds * 100.0);
    for (int b = 0; b < blocks; ++b)
    {
        if (b == (int) (holdSeconds * 100.0))
        {
            e.type = lynx::NoteEvent::Type::off;
            live.push (e);
        }
        r.process (state.get(), false, 0.0, 480, l.data(), rr.data(), &live, nullptr);
        double sum = 0.0;
        for (int i = 0; i < 480; ++i)
            sum += (double) l[(std::size_t) i] * l[(std::size_t) i];
        rms.push_back (std::sqrt (sum / 480.0));
    }
    const auto peak = *std::max_element (rms.begin(), rms.end());
    int onsets = 0;
    bool above = false;
    for (auto v : rms)
    {
        const auto now = peak > 0.0 && v > peak * 0.25;
        if (now && ! above)
            ++onsets;
        above = now;
    }
    return onsets;
}
} // namespace rea149

TEST (rea149_live_notes_play_through_arp_and_echo)
{
    using namespace lynx;
    ChannelDef c;
    c.name = "Synth";
    c.instrument = InstrumentKind::synth;
    c.synth.release = 0.005f;
    CHECK (rea149::liveOnsets (c, 1.0, 1.0) == 1);
    // FL arpeggiates a held key while stopped: 1/4 beat at 120 BPM = 125 ms steps → 8 notes in 1 s
    c.arp.direction = 1;
    c.arp.stepBeats = 0.25f;
    c.arp.gate = 0.5f;
    const auto arpOns = rea149::liveOnsets (c, 1.0, 1.0);
    CHECK (arpOns >= 7 && arpOns <= 9);
    // the echo delay repeats a live note (the note and its 3 echoes, 3 steps = 375 ms apart, at 0.8^k of its level)
    c.arp.direction = 0;
    c.misc.echo.echoes = 3;
    c.misc.echo.timeBeats = 0.75f;
    c.misc.echo.feedback = 0.8f;
    CHECK (rea149::liveOnsets (c, 1.5, 0.1) == 4);
}

TEST (rea149_arpeggiator_repeat_zero_plays_the_chord)
{
    using namespace lynx;
    // FL's Repeat 0 [EXEC r146b]: each step plays the chord at once; range 2 alternates its octave
    ArpSettings a;
    a.direction = 1;
    a.stepBeats = 0.25f;
    a.gate = 0.5f;
    a.range = 2;
    a.octaveRepeat = 0;
    std::vector<double> times;
    const auto keys = arpKeys (a, { 60, 64, 67 }, 0, 1.0, &times);
    const int expected[] = { 60, 64, 67, 72, 76, 79, 60, 64, 67 };
    CHECK (keys.size() >= 9);
    for (std::size_t i = 0; i < 9 && i < keys.size(); ++i)
        CHECK (keys[i] == expected[i]);
    CHECK (times.size() >= 6 && std::abs (times[0] - times[2]) < 1.0 && std::abs (times[3] - ppq / 4.0) < 2.0);
}

TEST (rea149_wrapper_latency_and_midi_output_port)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (2);
    for (int k = 0; k < 3; ++k)
    {
        ChannelDef c;
        c.instrument = InstrumentKind::plugin;
        c.name = "P" + std::to_string (k);
        c.mixerTrack = 1;
        s.channels.push_back (c);
    }
    s.channels[0].plugin.midiOutPort = 3;
    s.channels[1].plugin.midiInPort = 3;
    s.channels[2].plugin.midiInPort = 4;
    s.channels[2].plugin.latencyOverrideMs = 10.0f;
    s.patterns.emplace_back();
    auto state = RackState::build (s, nullptr, 48000.0);
    // the plugin's MIDI output reaches the plugins whose input port is its output port
    CHECK (state->hasMidiOutputs && state->midiOutTargets[0] == std::vector<int> { 1 } && state->midiOutTargets[1].empty());
    // the latency set by hand replaces the reported one in the delay compensation
    const auto lat = state->currentLatencies();
    CHECK (lat.size() >= 3 && lat[0] == 0 && lat[2] == 480);
}

TEST (rea149_echo_fat_mode_like_fl)
{
    using namespace lynx;
    // FL's fat mode [DECOMP FUN_00d28c90]: a mirrored second series (pan / pitch steps negated), every echo at
    // feedback × the note's level
    EchoSettings s;
    s.feedback = 0.5f;
    s.echoes = 3;
    s.timeBeats = 0.5f;
    s.pan = -0.25f;
    s.pitch = 100.0f;
    s.fat = true;
    channelmisc::NoteEcho echo;
    std::vector<std::tuple<NoteEvent, float>> ons;
    int offs = 0;
    const auto tps = ticksPerSample (120.0, 48000.0);
    for (int block = 0; block < 100; ++block)
    {
        NoteEvent in[2];
        int count = 0;
        if (block == 0)
        {
            in[0].type = NoteEvent::Type::on;
            in[0].key = 60;
            in[0].velocity = 0.8f;
            in[1] = in[0];
            in[1].type = NoteEvent::Type::off;
            in[1].sampleOffset = 100;
            count = 2;
        }
        int n = 0;
        echo.process (0, in, count, block * 512 * tps, tps, 512, s, echo.output.data(), echo.outputGains.data(), n);
        for (int i = 0; i < n; ++i)
        {
            if (echo.output[(std::size_t) i].type == NoteEvent::Type::on)
                ons.emplace_back (echo.output[(std::size_t) i], echo.outputGains[(std::size_t) i]);
            else if (echo.output[(std::size_t) i].type == NoteEvent::Type::off)
                ++offs;
        }
    }
    CHECK (ons.size() == 7 && offs == 7);
    int up = 0, down = 0;
    for (std::size_t i = 1; i < ons.size(); ++i)
    {
        const auto& [e, gain] = ons[i];
        CHECK (std::abs (gain - 0.5f) < 1.0e-6f);
        if (e.key > 60 && e.pan < 0.0f)
            ++up;
        if (e.key < 60 && e.pan > 0.0f)
            ++down;
    }
    CHECK (up == 3 && down == 3);
}

TEST (rea149_add_to_key_follows_the_root_note)
{
    using namespace lynx;
    // FL's Add to key: an instrument that is not a sampler plays (C5 − root) semitones away (root C4 → an octave up)
    auto crossings = [] (bool addToKey) {
        Song s;
        s.mixer = Song::defaultMixer (2);
        ChannelDef c;
        c.instrument = InstrumentKind::synth;
        c.synth.osc[0].wave = Waveform::sine;
        c.synth.cutoff = 20000.0f;
        c.mixerTrack = 1;
        c.misc.rootNote = 48;
        c.misc.addToKey = addToKey;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        auto state = RackState::build (s, nullptr, 48000.0);
        RackRenderer r;
        r.prepare (48000.0, 480);
        LiveEventQueue live;
        NoteEvent e;
        e.channel = 0;
        e.key = 60;
        e.velocity = 0.8f;
        live.push (e);
        std::vector<float> l (480), rr (480);
        int count = 0;
        float last = 0.0f;
        for (int b = 0; b < 50; ++b)
        {
            r.process (state.get(), false, 0.0, 480, l.data(), rr.data(), &live, nullptr);
            for (auto v : l)
            {
                if (b >= 10 && last <= 0.0f && v > 0.0f)
                    ++count;
                last = v;
            }
        }
        return count; // over 0.4 s
    };
    const auto plain = crossings (false), shifted = crossings (true);
    CHECK (std::abs (plain - 105) <= 3);   // C5 = 261.6 Hz
    CHECK (std::abs (shifted - 209) <= 4); // C6 = 523.3 Hz
}

TEST (rea149_consolidate_keeps_the_tempo_map)
{
    using namespace lynx;
    // a song with a tempo change: the render of a track keeps the tempo clip (cut at the render's start)
    auto song = Song::makeDefault();
    song.playlist.clear();
    song.bpm = 120.0;
    song.mode = PlayMode::song;
    Automation tempo;
    tempo.target = { AutomationTargetKind::tempo, 0 };
    tempo.points = { { 0, valueFromTempo (100.0) }, { 4 * ppq * 4, valueFromTempo (100.0) }, { 4 * ppq * 4 + 1, valueFromTempo (140.0) },
                     { 16 * ppq * 4, valueFromTempo (140.0) } };
    song.automations.push_back (tempo);
    Clip a;
    a.kind = ClipKind::automation;
    a.automation = 0;
    a.track = 5;
    a.start = 0;
    a.length = 16 * ppq * 4;
    song.playlist.push_back (a);
    Clip c;
    c.pattern = 0;
    c.track = 1;
    c.start = 6 * ppq * 4;
    c.length = ppq * 4;
    song.playlist.push_back (c);
    // (round 168e: the consolidation core moved to core/QuickRender; the render plays the song's own range, so the
    // tempo there is the song's)
    quickrender::Plan plan;
    CHECK (quickrender::planTrack (song, 1, false, quickrender::Start::clipStart, plan) == quickrender::Result::ok);
    const auto render = quickrender::makePlanRenderSong (song, plan);
    const auto start = plan.start;
    CHECK (start == 6 * ppq * 4 && render.playlist.size() == 2);
    CHECK (render.loopStart == start && render.loopEnd == start + ppq * 4);
    CHECK (std::abs (render.tempoAt ((double) start) - 140.0) < 0.5 && std::abs (song.tempoAt ((double) start) - 140.0) < 0.5);
}

TEST (rea149_create_group_names_unnamed_tracks)
{
    using namespace lynx;
    // FL's Create group [DECOMP FUN_012aa200]: the selected tracks without a name take the current track's
    auto song = Song::makeDefault();
    song.mixer.resize (8);
    for (auto& t : song.mixer)
        t.name.clear();
    song.mixer[4].name = "Drums";
    song.mixer[5].name = "Snare";
    mixerops::createGroup (song, { 3, 4, 5 }, 4);
    CHECK (song.mixer[3].name == "Drums" && song.mixer[5].name == "Snare" && song.mixer[6].name.empty());
}
