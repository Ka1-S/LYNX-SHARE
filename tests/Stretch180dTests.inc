// --- Round 180d: FL's TIME stretches the region after its Trim, not the whole file (REA FUN_00d26890, FUN_00ff0db0) ----------

TEST (render180d_time_stretches_the_trimmed_region)
{
    using namespace lynx;
    // 1 s of file: 0.1 s of silence, 0.3 s of sound, 0.6 s of silence
    SampleData d;
    d.sampleRate = 1000.0;
    d.left.assign (1000, 0.0f);
    for (int i = 100; i < 400; ++i)
        d.left[(std::size_t) i] = 0.5f;
    SamplerSettings s;
    CHECK (precompute::stretchedFrames (d, s) == 1000);
    s.precompute.trim = 256; // 0.0078: the silence goes
    CHECK (precompute::stretchedFrames (d, s) == 300);
    s.precompute.trim = 0;
    s.regionEnd = 0.5f; // FL's Length: half the file
    CHECK (std::llabs (precompute::stretchedFrames (d, s) - 500) <= 1);
    s.precompute.trim = 256;
    CHECK (precompute::stretchedFrames (d, s) == 300);
    s.regionStart = 0.2f; // starts inside the sound
    s.regionEnd = 1.0f;
    CHECK (precompute::stretchedFrames (d, s) == 200);
}

TEST (render180d_a_trimmed_stretched_clip_plays_what_it_draws)
{
    using namespace lynx;
    // 1 s of file at 44.1 kHz: 0.1 s of silence, 0.3 s of a 440 Hz tone, 0.6 s of silence; Trim on; TIME 2 beats at 120 BPM
    constexpr double rate = 44100.0;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->left.assign (44100, 0.0f);
    for (int i = 4410; i < 17640; ++i)
        sample->left[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / rate);
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.mixer = Song::defaultMixer (1);
    ChannelDef c;
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = "x.wav";
    c.mixerTrack = 1;
    c.sampler.stretchBeats = 2.0f;
    c.sampler.keepPitch = true;
    c.sampler.precompute.trim = 256;
    s.channels.push_back (c);
    s.patterns.emplace_back();

    // drawn: the played span spread over TIME; the clip as long as the file shows
    const auto span = clipops::clipSpan (c, *sample);
    CHECK (std::llabs (span.first - 4410) <= 2 && std::llabs (span.frames - 13230) <= 2);
    const auto drawnTicks = clipops::fileTicks (c, (double) span.frames, rate, s.bpm);
    CHECK (drawnTicks == 2 * ppq);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.length = drawnTicks + ppq;
    s.playlist = { clip };

    RackState::Factories f;
    f.samples = [sample] (const std::string&) { return std::shared_ptr<const SampleData> (sample); };
    f.stretchedSamples = [sample] (const std::string&, double ratio, StretchMethod m, double) {
        return std::shared_ptr<const SampleData> (timeStretch (*sample, ratio, m));
    };
    auto state = RackState::build (s, nullptr, rate, f);
    RackRenderer r;
    r.prepare (rate, 441);
    std::vector<float> out, l (441), rr (441);
    const auto tps = ticksPerSample (s.bpm, rate);
    for (double tick = 0.0; tick < ppq * 3.0; tick += 441 * tps)
    {
        r.process (state.get(), true, tick, 441, l.data(), rr.data());
        out.insert (out.end(), l.begin(), l.end());
    }
    // played: the tone starts at once (the leading silence is trimmed) and lasts TIME (1 s), as drawn
    std::size_t firstLoud = out.size(), lastLoud = 0;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > 0.02f)
            firstLoud = std::min (firstLoud, i), lastLoud = i;
    CHECK (firstLoud < 441);
    const auto playedTicks = (double) lastLoud / rate * s.bpm / 60.0 * ppq;
    std::fprintf (stderr, "  [r180d] drawn %lld ticks, played %.0f ticks (first sound at %zu)\n", (long long) drawnTicks, playedTicks, firstLoud);
    CHECK (std::abs (playedTicks - (double) drawnTicks) < 0.03 * (double) drawnTicks);
}
