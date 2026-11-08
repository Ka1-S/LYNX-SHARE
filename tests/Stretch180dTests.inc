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
    s.precompute.trim = 256; // (the region's cut edges get FL's declick: two frames of sound fall under the trim)
    CHECK (std::llabs (precompute::stretchedFrames (d, s) - 300) <= 2);
    s.regionStart = 0.2f; // starts inside the sound
    s.regionEnd = 1.0f;
    CHECK (precompute::stretchedFrames (d, s) == 200);
}

TEST (render180d_a_trimmed_clip_plays_what_it_draws)
{
    using namespace lynx;
    // 1 s of file at 44.1 kHz: 0.1 s of silence, 0.3 s of a 440 Hz tone, 0.6 s of silence; Trim on
    constexpr double rate = 44100.0;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->left.assign (44100, 0.0f);
    for (int i = 4410; i < 17640; ++i)
        sample->left[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / rate);
    const std::shared_ptr<const SampleData> shared = sample;

    for (const auto stretched : { false, true })
    {
        Song s;
        s.bpm = 120.0;
        s.mode = PlayMode::song;
        s.mixer = Song::defaultMixer (1);
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "x.wav";
        c.mixerTrack = 1;
        c.sampler.precompute.trim = 256;
        if (stretched) // TIME: 2 beats at 120 BPM
            c.sampler.stretchBeats = 2.0f, c.sampler.keepPitch = true;
        s.channels.push_back (c);
        s.patterns.emplace_back();

        // drawn: the played span (the tone), at the file's speed or spread over TIME
        const auto span = clipops::clipSpan (c, shared);
        CHECK (std::llabs (span.first - 4410) <= 2 && std::llabs (span.frames - 13230) <= 2);
        const auto drawnTicks = clipops::fileTicks (c, (double) span.frames, rate, s.bpm);
        CHECK (stretched ? drawnTicks == 2 * ppq : std::llabs (drawnTicks - (std::int64_t) std::llround (0.3 * 2.0 * ppq)) <= 1);
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.length = drawnTicks + ppq;
        s.playlist = { clip };

        RackState::Factories f;
        f.samples = [shared] (const std::string&) { return shared; };
        f.stretchedSamples = [shared] (const std::string&, double ratio, StretchMethod m, double, const StretchMap&) {
            return std::shared_ptr<const SampleData> (timeStretch (*shared, ratio, m));
        };
        auto state = RackState::build (s, nullptr, rate, f);
        RackRenderer r;
        r.prepare (rate, 441);
        std::vector<float> out, l (441), rr (441);
        const auto tps = ticksPerSample (s.bpm, rate);
        for (double tick = 0.0; tick < (double) clip.length; tick += 441 * tps)
        {
            r.process (state.get(), true, tick, 441, l.data(), rr.data());
            out.insert (out.end(), l.begin(), l.end());
        }
        // played: the tone from the clip's start (the leading silence is trimmed), as long as drawn; the stretcher smears
        // the edges of a stretched one a little
        std::size_t firstLoud = out.size(), lastLoud = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > 0.02f)
                firstLoud = std::min (firstLoud, i), lastLoud = i;
        const auto playedTicks = (double) (lastLoud - firstLoud) / rate * s.bpm / 60.0 * ppq;
        std::fprintf (stderr, "  [r180d] %s: drawn %lld ticks, played %.0f ticks (first sound at %zu)\n", stretched ? "stretched" : "plain",
                      (long long) drawnTicks, playedTicks, firstLoud);
        CHECK (firstLoud < (stretched ? 4410u : 64u));
        CHECK (std::abs (playedTicks - (double) drawnTicks) < (stretched ? 0.25 : 0.01) * (double) drawnTicks);
    }
}

TEST (render180d_time_stretches_what_the_precomputed_effects_leave)
{
    // FL trims after its precomputed effects (FUN_00ff0db0): Thief's Importer Ride, faded out by Precomputed fade out 807,
    // trims to 0.38 s of its 1.30 s, and TIME stretches that (FL renders: 2x slower at TIME 367, round 180d)
    using namespace lynx;
    SampleData d;
    d.sampleRate = 1000.0;
    d.left.assign (1000, 0.5f);
    SamplerSettings s;
    s.precompute.trim = 256;
    CHECK (precompute::stretchedFrames (d, s) == 1000);
    s.precompute.fadeOut = 807;
    const auto processed = precompute::apply (d, s);
    CHECK (processed != nullptr);
    const auto faded = precompute::stretchedFrames (d, s);
    CHECK (processed != nullptr && faded == (std::int64_t) processed->frames());
    CHECK (faded < 1000);
    std::fprintf (stderr, "  [r180d] a constant second faded out by 807 trims to %lld frames\n", (long long) faded);
    auto shared = std::make_shared<const SampleData> (d);
    CHECK (precompute::stretchedFrames (shared, s) == faded);
    CHECK (precompute::stretchedFrames (shared, s) == faded); // the cached answer
    s.precompute.fadeOut = 0;
    CHECK (precompute::stretchedFrames (shared, s) == 1000); // another setting, another answer
}

TEST (render180d_a_stretched_trimmed_clip_starts_as_late_as_before_at_the_same_ratio)
{
    // The tone of a stretched trimmed clip starts some frames late: the stretcher smears the edge of the silence before it,
    // and the trim keeps that smear. That comes with the stretch ratio, not with this round's span: the old rule (ratio
    // from the whole file) gives the same start at the same ratio. Rendered both ways at ratio 1/0.3 s; the old rule's own
    // TIME for this clip (1 s for a 1 s file) did not stretch at all, and played 0.3 s where FL plays 1 s.
    using namespace lynx;
    constexpr double rate = 44100.0;
    auto sample = std::make_shared<SampleData>();
    sample->sampleRate = rate;
    sample->left.assign (44100, 0.0f);
    for (int i = 4410; i < 17640; ++i)
        sample->left[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * i / rate);
    const std::shared_ptr<const SampleData> shared = sample;

    auto onset = [&] (float stretchBeats, double forcedRatio) {
        Song s;
        s.bpm = 120.0;
        s.mode = PlayMode::song;
        s.mixer = Song::defaultMixer (1);
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "x.wav";
        c.mixerTrack = 1;
        c.sampler.precompute.trim = 256;
        c.sampler.stretchBeats = stretchBeats;
        c.sampler.keepPitch = true;
        s.channels.push_back (c);
        s.patterns.emplace_back();
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.length = 8 * ppq;
        s.playlist = { clip };
        RackState::Factories f;
        f.samples = [shared] (const std::string&) { return shared; };
        // forcedRatio > 0: the old rule's ratio, computed by hand (target / whole file)
        f.stretchedSamples = [shared, forcedRatio] (const std::string&, double ratio, StretchMethod m, double, const StretchMap&) {
            return std::shared_ptr<const SampleData> (timeStretch (*shared, forcedRatio > 0.0 ? forcedRatio : ratio, m));
        };
        auto state = RackState::build (s, nullptr, rate, f);
        RackRenderer r;
        r.prepare (rate, 441);
        std::vector<float> l (441), rr (441);
        const auto tps = ticksPerSample (s.bpm, rate);
        long frame = 0;
        for (double tick = 0.0; tick < 4.0 * ppq; tick += 441 * tps, frame += 441)
        {
            r.process (state.get(), true, tick, 441, l.data(), rr.data());
            for (int i = 0; i < 441; ++i)
                if (std::abs (l[(std::size_t) i]) > 0.02f)
                    return frame + i;
        }
        return -1L;
    };
    const auto ratio = 1.0 / 0.3;
    const auto now = onset (2.0f, 0.0);                       // this round: TIME 1 s over the 0.3 s tone
    const auto oldSameRatio = onset (2.0f, ratio);            // the old rule's code path at the same ratio
    const auto oldOwnTime = onset (2.0f, 1.0);                // the old rule at this TIME: the whole file, no stretch
    std::fprintf (stderr, "  [r180d] tone onset: now %ld, old rule at the same ratio %ld, old rule at its own TIME %ld frames\n", now,
                  oldSameRatio, oldOwnTime);
    CHECK (now >= 0 && oldSameRatio >= 0 && std::labs (now - oldSameRatio) <= 1);
}
